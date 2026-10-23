#include "pxa/game_render.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static uint64_t clock_us(void *p) { return ++*(uint64_t *)p; }
static unsigned prepared_batches, arena_allocations, arena_releases;
static void *arena_allocate(void *p, size_t bytes) {
    (void)p; ++arena_allocations;
    return getenv("PXA_TEST_PREPARE_FAIL") ? NULL : malloc(bytes);
}
static void arena_release(void *p, void *memory) { (void)p; ++arena_releases; free(memory); }

static pxa_status_t execute(void *ctx, const pxa_raster_spans_t *b) {
    if (b->prepared) ++prepared_batches;
    pxa_runtime_t *r = ctx;
    uint8_t *m = NULL; size_t n = 0;
    assert(pxa_runtime_borrow_memory(r, PXA_COMPONENT_INVALID, &m, &n) == PXA_STATUS_BAD_STATE);
    assert(!m && !n);
    /* Reverse partition order still preserves all per-pixel ordering. */
    pxa_raster_spans_execute_range(b, 3, b->target_bytes);
    pxa_raster_spans_execute_range(b, 0, 3);
    return PXA_STATUS_OK;
}

/* End-to-end exact/legacy sampling, padded/unaligned rows, cursor clipping,
 * packet/source alias rejection, and reverse-order worker partitions. */
static void indexed_rect_cases(pxa_runtime_t *runtime,pxa_component_t component,uint64_t handle) {
    const size_t bytes=1000000;
    uint8_t *m=malloc(bytes),*expected=malloc(bytes);assert(m && expected);
    const unsigned widths[]={256,199,320,511,1};
    const unsigned heights[]={240,153,240,301,1};
    for(unsigned c=0;c<5;++c)for(unsigned exact=0;exact<2;++exact)
    for(unsigned cursor=0;cursor<2;++cursor) {
        memset(m,0x9d,bytes);
        for(unsigned y=0;y<240;++y)for(unsigned x=0;x<256;++x)m[8+y*272+x]=(x*13+y*7)&255;
        for(unsigned i=0;i<256;++i)pxa_write_u16(m+65536+i*2,(i*109)^0x55aa);
        for(unsigned i=0;i<64;++i)m[66048+i]=i%3?i:0;
        pxa_game_render_blit_wire_t b={0};
        b.source_offset=8;b.source_bytes=239*272+256;b.source_width=256;b.source_height=240;b.source_pitch=272;
        b.palette_offset=65536;b.palette_bytes=512;b.target_offset=66818;
        b.target_width=widths[c];b.target_height=heights[c];b.target_pitch=2*(widths[c]+7);
        b.target_bytes=(b.target_height-1)*b.target_pitch+b.target_width*2;
        if(cursor){b.cursor_offset=66048;b.cursor_bytes=64;b.cursor_width=b.cursor_height=b.cursor_pitch=8;b.cursor_x=-2;b.cursor_y=7;}
        uint8_t *packet=m+900000;pxa_game_render_blit_encode(packet,64,&b);
        memcpy(expected,m,bytes);
        for(unsigned y=0;y<b.target_height;++y)for(unsigned x=0;x<b.target_width;++x){
            unsigned sx=exact?x*256/b.target_width:(x*320/b.target_width)*256/320;
            unsigned sy=exact?y*240/b.target_height:(y*240/b.target_height)*240/240;
            unsigned index=m[8+sy*272+sx];
            int cx=(int)sx+2,cy=(int)sy-7;
            if(cursor && cx>=0 && cx<8 && cy>=0 && cy<8 && m[66048+cy*8+cx])index=m[66048+cy*8+cx];
            pxa_write_u16(expected+b.target_offset+y*b.target_pitch+x*2,pxa_read_u16(m+65536+index*2));
        }
        assert(pxa_runtime_io64_memory(runtime,component,handle,exact?0x109:0x108,packet,64,m,bytes)==64);
        assert(!memcmp(m,expected,bytes));
        pxa_raster_spans_t batch;
        assert(pxa_raster_blit_validate(packet,64,m,bytes,256,&batch)==0);
        batch.state.blit.exact_sampling=exact;
        memset(m+b.target_offset,0x9d,b.target_bytes);
        unsigned end=b.target_bytes;
        while(end){unsigned begin=end>67?end-67:0;pxa_raster_spans_execute_range(&batch,begin,end);end=begin;}
        assert(!memcmp(m,expected,bytes));
        b.target_offset=b.source_offset;pxa_game_render_blit_encode(packet,64,&b);
        memcpy(expected,m,bytes);
        assert(pxa_runtime_io64_memory(runtime,component,handle,0x109,packet,64,m,bytes)==PXA_STATUS_INVALID_ARGUMENT);
        assert(!memcmp(m,expected,bytes));
    }
    free(m);free(expected);
}

/* Exercise the actual borrowed-memory service, including atomic rejection
 * of a malformed second command after a valid first command. */
static void priority_tile_cases(pxa_runtime_t *runtime, pxa_component_t component, uint64_t handle) {
    uint8_t *m=malloc(8192), *before=malloc(8192); assert(m && before);
    memset(m,0xa5,8192); uint8_t *p=m+1024;
    pxa_game_render_tiles_header_wire_t h={PXA_GAME_RENDER_TILES_MAGIC,1,0,2,80,4096,6144,64,8};
    pxa_game_render_tiles_header_encode(p,32,&h);
    memset(m+6144,0,64);memset(p+128,1,64);p[128]=0;
    pxa_write_u16(p+194,0x07e0);pxa_write_u16(p+210,0xf800);
    pxa_game_render_tile_wire_t t={0};
    t.tile_offset=128;t.palette_offset=192;t.palette_entries=4;t.width=t.height=8;
    t.compare_depth=5;t.write_depth=3;
    pxa_game_render_tile_encode(p+32,24,&t);
    t.palette_offset=208;t.compare_depth=3;t.write_depth=200;
    pxa_game_render_tile_encode(p+56,24,&t);
    memcpy(before,m,8192);
    assert(pxa_runtime_io64_memory(runtime,component,handle,0x10a,p,1024,m,8192)==1024);
    assert(pxa_read_u16(m+4096)==0xa5a5 && m[6144]==0);
    for(unsigned i=1;i<64;++i)assert(pxa_read_u16(m+4096+i*2)==0x07e0 && m[6144+i]==3);
    assert(!memcmp(p,before+1024,1024));
    memcpy(m,before,8192);pxa_write_u32(p+56,UINT32_MAX);memcpy(before,m,8192);
    assert(pxa_runtime_io64_memory(runtime,component,handle,0x10a,p,1024,m,8192)<0);
    assert(!memcmp(m,before,8192));
    uint8_t *borrowed=NULL;size_t bytes=0;
    assert(pxa_runtime_borrow_memory(runtime,component,&borrowed,&bytes)==PXA_STATUS_BAD_STATE);
    free(m);free(before);
}

int main(void) {
    pxa_runtime_limits_t limits;
    pxa_runtime_t *runtime;
    pxa_component_t component;
    pxa_game_render_config_t cfg = {0};
    pxa_game_render_service_t *service;
    uint64_t clock = 0;
    uint8_t *memory = calloc(1, 8192), *moved = calloc(1, 8192);
    uint8_t packet[64], reply[64], *p;
    size_t reply_size;
    pxa_writer_t writer;
    pxa_event_view_t event;
    pxa_message_view_t decoded;
    pxa_runtime_limits_init(&limits);
    void *workspace = malloc(pxa_runtime_workspace_size(&limits));
    assert(workspace && memory && moved);
    assert(pxa_runtime_init(workspace, pxa_runtime_workspace_size(&limits), &limits, &runtime) == 0);
    cfg.struct_size = sizeof(cfg); cfg.max_contexts = cfg.max_contexts_per_component = 1;
    /* Compute contexts work with no display backend or surface buffers. */
    cfg.execute_spans = execute; cfg.execute_spans_context = runtime;
    cfg.now_us = clock_us; cfg.clock_context = &clock;
    pxa_memory_budget_t budget;
    pxa_memory_budget_config_t budget_config = {.limit = {0, 8192}};
    pxa_memory_owner_t owner;
    size_t arena_limits[2] = {0, 8192};
    assert(pxa_memory_budget_init(&budget, &budget_config) == 0);
    assert(pxa_memory_owner_open(&budget, arena_limits, &owner) == 0);
    pxa_memory_allocator_t allocator = {&budget, owner, PXA_MEMORY_EXTERNAL,
        PXA_MEMORY_RASTER, NULL, arena_allocate, arena_release};
    cfg.span_prepare_allocator = &allocator; cfg.span_prepare_bytes = 4096;
    void *sw = malloc(pxa_game_render_service_workspace_size(&cfg));
    assert(sw);
    assert(pxa_game_render_service_init(sw, pxa_game_render_service_workspace_size(&cfg), runtime, &cfg, &service) == 0);
    assert(pxa_game_render_service_register(service) == 0);
    assert(pxa_component_create(runtime, 1, &component) == 0);
    assert(pxa_component_set_wire_profile(runtime, component, PXA_WIRE_PROFILE_HANDLE64) == 0);
    assert(pxa_component_begin_start(runtime, component) == 0);
    uint8_t create[8] = {2, 0, 0, 0, 0, 0, 0, 0};
    pxa_writer_init(&writer, packet, sizeof(packet));
    assert(pxa_writer_message(&writer, 18, 3, 7, create, 8) == 0);
    assert(pxa_runtime_control(runtime, component, packet, writer.size) == 0);
    assert(pxa_component_finish_start(runtime, component, 0) == 0);
    assert(pxa_event_peek(runtime, component, &event) == 0);
    assert(pxa_event_read(runtime, event.token, 0, reply, sizeof(reply), &reply_size) == 0);
    assert(pxa_message_decode(reply, reply_size, 4096, &decoded) == 0);
    assert(decoded.payload.size == 20 && pxa_read_u32(decoded.payload.data) == 0);
    pxa_handle64_t handle = pxa_read_u64(decoded.payload.data + 4);
    assert(handle && pxa_read_u32(decoded.payload.data + 12) == 2);
    assert(pxa_event_consume(runtime, component, event.token) == 0);
    assert(pxa_component_begin_event(runtime, component) == 0);
    p = memory + 6000;
    pxa_game_render_spans_header_wire_t h = {0};
    h.magic = PXA_GAME_RENDER_SPANS_MAGIC; h.major = 1; h.count = 1;
    h.target_offset = 4096; h.target_bytes = 32;
    h.texture_offset = 1024; h.texture_bytes = 16; h.texture_width = h.texture_height = 4;
    h.light_offset = 2048; h.light_bytes = 16; h.light_width = h.light_height = 2;
    h.colormap_offset = 3072; h.colormap_bytes = 256; h.colormap_levels = 1;
    h.light_shift = 4; h.flags = PXA_GAME_RENDER_SPANS_POW2;
    pxa_game_render_span_wire_t span = {0, 8, 0, 0, 0, 0};
    pxa_game_render_spans_header_encode(p, 72, &h);
    pxa_game_render_span_encode(p + 72, 24, &span);
    memset(memory + 1024, 37, 16);
    /* Unused packed bottom-row words may hold arbitrary engine scratch. */
    memset(memory + 2048 + 8, 0xff, 8);
    for (unsigned i = 0; i < 256; ++i) memory[3072 + i] = i;
    /* Only written bytes are exclusive: packed engine arenas may place
     * sources and the packet in the unwritten tail of a coarse target view. */
    {
        pxa_raster_spans_t batch;
        h.target_offset = 900; h.target_bytes = 5300;
        pxa_game_render_spans_header_encode(p, 72, &h);
        assert(pxa_raster_spans_validate(p, 96, memory, 8192, 2, &batch) == 0);
        pxa_raster_spans_execute_range(&batch, 0, h.target_bytes);
        assert(memory[900] == 37);
        const uint32_t source_offsets[] = {1024, 2048, 3072, 6000};
        for (unsigned i=0; i<4; ++i) {
            span.destination = source_offsets[i] - h.target_offset;
            pxa_game_render_span_encode(p + 72, 24, &span);
            assert(pxa_raster_spans_validate(p, 96, memory, 8192, 2, &batch) ==
                PXA_STATUS_INVALID_ARGUMENT);
        }
        h.target_offset = 4096; h.target_bytes = 32; span.destination = 0;
        pxa_game_render_spans_header_encode(p, 72, &h);
        pxa_game_render_span_encode(p + 72, 24, &span);
    }
    assert(pxa_runtime_io64(runtime, component, handle, 0x104, p, 96) == PXA_STATUS_BAD_STATE);
    assert(pxa_runtime_io64_memory(runtime, component, handle, 0x104, p, 96, memory, 8192) == 96);
    for (unsigned i = 0; i < 8; ++i) assert(memory[4096 + i] == 37);
    memcpy(moved, memory, 8192); memset(moved + 4096, 0, 32); memset(moved + 1024, 89, 16);
    assert(pxa_runtime_io64_memory(runtime, component, handle, 0x104,
        moved + 6000, 96, moved, 8192) == 96);
    assert(moved[4096] == 89 && memory[4096] == 37);
    uint8_t *borrowed; size_t borrowed_size;
    assert(pxa_runtime_borrow_memory(runtime, component, &borrowed, &borrowed_size) == PXA_STATUS_BAD_STATE);
    h.count = 2; pxa_game_render_spans_header_encode(p, 72, &h);
    span.destination = 32; pxa_game_render_span_encode(p + 96, 24, &span);
    memset(memory + 4096, 0xa5, 32);
    assert(pxa_runtime_io64_memory(runtime, component, handle, 0x104, p, 120, memory, 8192) == PXA_STATUS_INVALID_ARGUMENT);
    for (unsigned i = 0; i < 32; ++i) assert(memory[4096 + i] == 0xa5);
    assert(pxa_runtime_io64_memory(runtime, component, handle, 0x104, p, 119, memory, 8192) < 0);
    assert(pxa_runtime_io64(runtime, component, handle, 0x105, p, 64) == 64);
    assert(pxa_read_u64(p) == 2 && pxa_read_u64(p + 8) == 3 && pxa_read_u64(p + 24) == 16);
    assert(pxa_read_u32(p + 56) == 2 && pxa_read_u32(p + 60) == 0);
    assert(pxa_runtime_io64(runtime, component, handle, 0x101, p, 64) == PXA_STATUS_UNSUPPORTED);
    /* Projected execution borrows the fresh memory view, validates every row
     * before either colour or depth writes, and shares the original context. */
    h.magic = PXA_GAME_RENDER_PROJECTED_MAGIC;
    pxa_game_render_projection_wire_t projection = {0};
    projection.z_origin_bits = 0x3d800000; /* 1 / 16 */
    projection.s_max = projection.t_max = 65535;
    projection.pitch = 8; projection.flags = PXA_GAME_RENDER_PROJECTED_WRITE_DEPTH;
    projection.depth_offset = 4200; projection.depth_bytes = 64; projection.depth_pitch = 16;
    pxa_game_render_row_wire_t rows[2] = {{1, 1, 4, 0}, {0, 0, 8, 0}};
    pxa_game_render_spans_header_encode(p, 72, &h);
    pxa_game_render_projection_encode(p + 72, 80, &projection);
    for (unsigned i = 0; i < 2; ++i) pxa_game_render_row_encode(p + 152 + i*8, 8, &rows[i]);
    memset(memory + 4096, 0xa5, 32); memset(memory + 4200, 0x5a, 64);
    assert(pxa_runtime_io64(runtime, component, handle, 0x106, p, 168) == PXA_STATUS_BAD_STATE);
    assert(pxa_runtime_io64_memory(runtime, component, handle, 0x106, p, 168, memory, 8192) == 168);
    assert(memory[4096] == 37 && memory[4105] == 37 && memory[4104] == 0xa5);
    assert(pxa_read_u16(memory + 4200) == 0x0800 && pxa_read_u16(memory + 4218) == 0x0800);
    assert(pxa_read_u16(memory + 4216) == 0x5a5a);
    memcpy(moved, memory, 8192); memset(moved + 1024, 89, 16);
    assert(pxa_runtime_io64_memory(runtime, component, handle, 0x106, moved + 6000,
        168, moved, 8192) == 168);
    assert(moved[4096] == 89 && memory[4096] == 37);
    const uint32_t invalid_coefficients[] = {0x7fc00000, 0x7f800000, 0xff800000,
        0xbf800000, 0x00000000, 0x7f7fffff};
    for (unsigned i = 0; i < sizeof(invalid_coefficients)/sizeof(*invalid_coefficients); ++i) {
        pxa_write_u32(p + 72 + 24, invalid_coefficients[i]);
        memset(memory + 4096, 0xa5, 32); memset(memory + 4200, 0x5a, 64);
        assert(pxa_runtime_io64_memory(runtime, component, handle, 0x106, p, 168,
            memory, 8192) == PXA_STATUS_INVALID_ARGUMENT);
        for (unsigned j = 0; j < 32; ++j) assert(memory[4096 + j] == 0xa5);
        for (unsigned j = 0; j < 64; ++j) assert(memory[4200 + j] == 0x5a);
    }
    pxa_game_render_projection_encode(p + 72, 80, &projection);
    /* A bad final row cannot leave the first row partially executed. */
    pxa_write_u16(p + 160 + 2, 65535);
    assert(pxa_runtime_io64_memory(runtime, component, handle, 0x106, p, 168,
        memory, 8192) == PXA_STATUS_INVALID_ARGUMENT);
    for (unsigned j = 0; j < 32; ++j) assert(memory[4096 + j] == 0xa5);
    for (unsigned j = 0; j < 64; ++j) assert(memory[4200 + j] == 0x5a);
    pxa_game_render_row_encode(p + 160, 8, &rows[1]);
    const uint32_t invalid_depth[] = {4096, 1024, 2048, 3072, 6000, 8190, 4201};
    for (unsigned i = 0; i < sizeof(invalid_depth)/sizeof(*invalid_depth); ++i) {
        pxa_write_u32(p + 72 + 64, invalid_depth[i]);
        assert(pxa_runtime_io64_memory(runtime, component, handle, 0x106, p, 168,
            memory, 8192) == PXA_STATUS_INVALID_ARGUMENT);
    }
    pxa_game_render_projection_encode(p + 72, 80, &projection);
    pxa_write_u16(p + 72 + 76, 0); /* Depth addresses require the explicit flag. */
    assert(pxa_runtime_io64_memory(runtime, component, handle, 0x106, p, 168,
        memory, 8192) == PXA_STATUS_INVALID_ARGUMENT);
    projection.depth_offset = projection.depth_bytes = projection.depth_pitch = projection.flags = 0;
    pxa_game_render_projection_encode(p + 72, 80, &projection);
    assert(pxa_runtime_io64_memory(runtime, component, handle, 0x106, p, 168, memory, 8192) == 168);
    for (unsigned j = 0; j < 64; ++j) assert(memory[4200 + j] == 0x5a);
    /* Cross-material batches snapshot every source relative to the parent.
     * Moving the packet itself must not require rewriting source addresses. */
    memset(p, 0, 816);
    pxa_game_render_frame_header_wire_t frame = {PXA_GAME_RENDER_FRAME_MAGIC, 1, 0, 2, 336};
    pxa_game_render_frame_header_encode(p, 16, &frame);
    h.magic = PXA_GAME_RENDER_FRAME_MATERIAL_MAGIC; h.count = 1;
    h.texture_offset = 512; h.light_offset = 528; h.colormap_offset = 544;
    projection.flags = PXA_GAME_RENDER_PROJECTED_WRITE_DEPTH;
    projection.depth_offset = 4200; projection.depth_bytes = 64; projection.depth_pitch = 16;
    rows[0] = (pxa_game_render_row_wire_t){0, 0, 8, 0};
    pxa_game_render_spans_header_encode(p + 16, 72, &h);
    pxa_game_render_projection_encode(p + 88, 80, &projection);
    pxa_game_render_row_encode(p + 168, 8, rows);
    h.texture_offset = 800; rows[0].x = 4; rows[0].count = 4;
    projection.z_origin_bits = 0x3e000000; /* 1 / 8 */
    pxa_game_render_spans_header_encode(p + 176, 72, &h);
    pxa_game_render_projection_encode(p + 248, 80, &projection);
    pxa_game_render_row_encode(p + 328, 8, rows);
    memset(p + 512, 37, 16); memcpy(p + 528, memory + 2048, 16);
    memcpy(p + 544, memory + 3072, 256); memset(p + 800, 89, 16);
    memset(memory + 4096, 0xa5, 32); memset(memory + 4200, 0x5a, 64);
    assert(pxa_runtime_io64_memory(runtime, component, handle, 0x107, p, 816, memory, 8192) == 816);
    for (unsigned i = 0; i < 8; ++i) {
        assert(memory[4096 + i] == (i < 4 ? 37 : 89));
        assert(pxa_read_u16(memory + 4200 + i*2) == (i < 4 ? 0x0800 : 0x1000));
    }
    assert(memory[4104] == 0xa5 && pxa_read_u16(memory + 4216) == 0x5a5a);
    for (size_t skew = 0; skew < 16; ++skew)
    for (size_t capacity = 0; capacity <= 4096; capacity += 16) {
        uint8_t *storage = malloc(4096 + 16); assert(storage);
        void *arena = storage + skew;
        memset(storage, 0xc7, 4096 + 16);
        memset(memory + 4096, 0xa5, 32); memset(memory + 4200, 0x5a, 64);
        pxa_raster_spans_t batch;
        /* Later materials may widen the target; coarsen prior work buckets. */
        h.target_bytes = capacity & 32 ? 64 : 32;
        pxa_game_render_spans_header_encode(p + 176, 72, &h);
        assert(pxa_raster_frame_spans_prepare(p, 816, memory, 8192, 2,
            arena, capacity, &batch) == 0);
        if (capacity == 4096) assert(batch.prepared);
        uint32_t split = capacity & 16 ? 3 : pxa_raster_spans_split_hint(&batch, 128);
        assert(split <= batch.target_bytes);
        pxa_raster_spans_execute_range(&batch, split, batch.target_bytes);
        pxa_raster_spans_execute_range(&batch, 0, split);
        for (unsigned i = 0; i < 8; ++i) {
            assert(memory[4096 + i] == (i < 4 ? 37 : 89));
            assert(pxa_read_u16(memory + 4200 + i*2) == (i < 4 ? 0x0800 : 0x1000));
        }
        for (size_t i = capacity; i < 4096; ++i) assert(((uint8_t *)arena)[i] == 0xc7);
        for (size_t i = 0; i < skew; ++i) assert(storage[i] == 0xc7);
        free(storage);
    }
    memcpy(moved + 7000, p, 816); memset(moved + 4096, 0xa5, 32);
    assert(pxa_runtime_io64_memory(runtime, component, handle, 0x107,
        moved + 7000, 816, moved, 8192) == 816);
    assert(moved[4096] == 37 && moved[4100] == 89);
    const unsigned bad_offsets[] = {12, 176 + 16, 176 + 24, 248 + 64, 328 + 6};
    const uint32_t bad_values[] = {335, 4098, 0xfffffff0u, 4202, 1};
    for (unsigned i = 0; i < sizeof(bad_offsets)/sizeof(*bad_offsets); ++i) {
        uint32_t saved = pxa_read_u32(p + bad_offsets[i]);
        pxa_write_u32(p + bad_offsets[i], bad_values[i]);
        memset(memory + 4096, 0xa5, 32); memset(memory + 4200, 0x5a, 64);
        assert(pxa_runtime_io64_memory(runtime, component, handle, 0x107,
            p, 816, memory, 8192) < 0);
        for (unsigned j = 0; j < 32; ++j) assert(memory[4096 + j] == 0xa5);
        for (unsigned j = 0; j < 64; ++j) assert(memory[4200 + j] == 0x5a);
        pxa_write_u32(p + bad_offsets[i], saved);
    }
    /* No child writes this parent's header, even though it is outside each
     * child's own command interval and outside every texture/light source. */
    pxa_write_u32(p + 16 + 16, 6000); pxa_write_u32(p + 176 + 16, 6000);
    uint8_t saved_header[16]; memcpy(saved_header, p, 16);
    assert(pxa_runtime_io64_memory(runtime, component, handle, 0x107,
        p, 816, memory, 8192) == PXA_STATUS_INVALID_ARGUMENT);
    assert(!memcmp(saved_header, p, 16));
    /* A small packet can repeatedly reference one large light grid. Bound
     * aggregate preflight work independently of pixels and unique bytes. */
    uint8_t *large = calloc(1, 75000);
    assert(large);
    h.target_offset = 4096; h.target_bytes = 512;
    h.texture_offset = 74240; h.colormap_offset = 74500; h.light_offset = 8192;
    h.light_width = h.light_height = 128; h.light_bytes = 65536;
    projection.flags = projection.depth_offset = projection.depth_bytes = projection.depth_pitch = 0;
    rows[0] = (pxa_game_render_row_wire_t){0, 0, 8, 0};
    for (unsigned i = 0; i < 33; ++i) {
        uint8_t *record = large + 16 + i*160;
        pxa_game_render_spans_header_encode(record, 72, &h);
        pxa_game_render_projection_encode(record + 72, 80, &projection);
        pxa_game_render_row_encode(record + 152, 8, rows);
    }
    frame.count = 32; frame.command_bytes = 16 + 32*160;
    pxa_game_render_frame_header_encode(large, 16, &frame);
    pxa_raster_spans_t bounded;
    assert(pxa_raster_frame_spans_validate(large, 75000, memory, 8192, 2, &bounded) == 0);
    frame.count = 33; frame.command_bytes += 160;
    pxa_game_render_frame_header_encode(large, 16, &frame);
    assert(pxa_raster_frame_spans_validate(large, 75000, memory, 8192, 2, &bounded) == PXA_STATUS_LIMIT_EXCEEDED);
    free(large);
    pxa_game_render_blit_wire_t blit = {0};
    blit.source_offset = 1024; blit.source_bytes = 16;
    blit.source_width = blit.source_height = blit.source_pitch = 4;
    blit.palette_offset = 3072; blit.palette_bytes = 512;
    blit.target_offset = 4096; blit.target_bytes = 32;
    blit.target_width = blit.target_height = 4; blit.target_pitch = 8;
    pxa_game_render_blit_encode(p, 64, &blit);
    assert(pxa_runtime_io64_memory(runtime, component, handle, 0x108, p, 64, memory, 8192) == 64);
    for (unsigned i = 0; i < 16; ++i)
        assert(pxa_read_u16(memory + 4096 + i*2) == pxa_read_u16(memory + 3072 + 37*2));
    indexed_rect_cases(runtime,component,handle);
    priority_tile_cases(runtime,component,handle);
    assert(pxa_handle_close64(runtime, component, handle) == 0);
    assert(arena_allocations == 1);
    if (getenv("PXA_TEST_PREPARE_FAIL")) assert(!prepared_batches && !arena_releases);
    else assert(prepared_batches && arena_releases == 1);
    assert(pxa_memory_owner_close(&budget, owner) == 0);
    assert(pxa_runtime_io64(runtime, component, handle, 0x105, p, 64) < 0);
    assert(pxa_component_finish_event(runtime, component, 1) == 0);
    pxa_runtime_deinit(runtime);
    free(sw); free(workspace); free(memory); free(moved);
    return 0;
}
