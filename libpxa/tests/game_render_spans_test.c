#include "pxa/game_render.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static uint64_t clock_us(void *p) { return ++*(uint64_t *)p; }

static pxa_status_t execute(void *ctx, const pxa_raster_spans_t *b) {
    pxa_runtime_t *r = ctx;
    uint8_t *m = NULL; size_t n = 0;
    assert(pxa_runtime_borrow_memory(r, PXA_COMPONENT_INVALID, &m, &n) == PXA_STATUS_BAD_STATE);
    assert(!m && !n);
    /* Reverse partition order still preserves all per-pixel ordering. */
    pxa_raster_spans_execute_range(b, 3, b->target_bytes);
    pxa_raster_spans_execute_range(b, 0, 3);
    return PXA_STATUS_OK;
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
    assert(pxa_handle_close64(runtime, component, handle) == 0);
    assert(pxa_runtime_io64(runtime, component, handle, 0x105, p, 64) < 0);
    assert(pxa_component_finish_event(runtime, component, 1) == 0);
    pxa_runtime_deinit(runtime);
    free(sw); free(workspace); free(memory); free(moved);
    return 0;
}
