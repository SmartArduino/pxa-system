#include "pxa/game_render.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static uint64_t clock_us(void *p) { return ++*(uint64_t *)p; }
static pxa_status_t execute(void *ctx, const pxa_raster_compute_t *b) {
    pxa_runtime_t *r = ctx;
    uint8_t *m = NULL; size_t n = 0;
    assert(pxa_runtime_borrow_memory(r, PXA_COMPONENT_INVALID, &m, &n) == PXA_STATUS_BAD_STATE);
    assert(!m && !n);
    /* Reverse partition order still preserves all per-pixel ordering. */
    pxa_raster_compute_execute_range(b, 3, b->target_bytes);
    pxa_raster_compute_execute_range(b, 0, 3);
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
        pxa_raster_compute_t batch;
        assert(pxa_raster_blit_validate(packet,64,m,bytes,256,&batch)==0);
        batch.state.blit.exact_sampling=exact;
        memset(m+b.target_offset,0x9d,b.target_bytes);
        unsigned end=b.target_bytes;
        while(end){unsigned begin=end>67?end-67:0;pxa_raster_compute_execute_range(&batch,begin,end);end=begin;}
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
    uint8_t *memory = calloc(1, 8192);
    uint8_t packet[64], reply[64];
    size_t reply_size;
    pxa_writer_t writer;
    pxa_event_view_t event;
    pxa_message_view_t decoded;
    pxa_runtime_limits_init(&limits);
    void *workspace = malloc(pxa_runtime_workspace_size(&limits));
    assert(workspace && memory);
    assert(pxa_runtime_init(workspace, pxa_runtime_workspace_size(&limits), &limits, &runtime) == 0);
    cfg.struct_size = sizeof(cfg); cfg.max_contexts = cfg.max_contexts_per_component = 1;
    /* Compute contexts work with no display backend or surface buffers. */
    cfg.execute_compute = execute; cfg.execute_compute_context = runtime;
    cfg.now_us = clock_us; cfg.clock_context = &clock;
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
    indexed_rect_cases(runtime,component,handle);
    priority_tile_cases(runtime,component,handle);
    /* Retired numeric operations are unrecognized, with no compatibility dispatch. */
    for (unsigned op=0x104;op<=0x107;++op)
        assert(pxa_runtime_io64_memory(runtime,component,handle,op,memory,64,memory,8192)==PXA_STATUS_UNSUPPORTED);
    assert(pxa_runtime_io64_memory(runtime,component,handle,0x10b,memory,64,memory,8192)==PXA_STATUS_UNSUPPORTED);
    assert(pxa_runtime_io64(runtime,component,handle,0x10c,reply,64)==64);
    assert(pxa_read_u64(reply)>0 && pxa_read_u64(reply+16)>0 && pxa_read_u32(reply+56)==2);
    assert(pxa_handle_close64(runtime, component, handle) == 0);
    assert(pxa_component_finish_event(runtime, component, 1) == 0);
    pxa_runtime_deinit(runtime);
    free(sw); free(workspace); free(memory);
    return 0;
}
