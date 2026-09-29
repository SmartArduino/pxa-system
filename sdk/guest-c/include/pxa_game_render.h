#ifndef PXA_GUEST_GAME_RENDER_H
#define PXA_GUEST_GAME_RENDER_H

#include "pxa_core.h"
#include "pxa_game_render_wire.h"

#define PXA_GAME_RENDER_SERVICE 18u
#define PXA_GAME_RENDER_CREATE 1u
#define PXA_GAME_RENDER_CREATE_AUTO 2u
#define PXA_GAME_RENDER_IO_BIND_ASSETS UINT32_C(0x103)
#ifndef PXA_GAME_RENDER_SCRATCH_DEPTH16
#define PXA_GAME_RENDER_SCRATCH_DEPTH16 0u
#define PXA_GAME_RENDER_SCRATCH_NONE 1u
#define PXA_GAME_RENDER_SCRATCH_COVERAGE_2BIT 2u
#endif

typedef struct {
    uint64_t handle; /* Zero explicitly unbinds the slot. */
    uint8_t kind; /* 1 = texture, 2 = palette. */
    uint8_t slot; /* Texture 0..47; palette must use zero. */
} pxa_game_render_binding_t;

static inline int pxa_game_render_build_bindings(uint8_t *packet, size_t capacity,
    const pxa_game_render_binding_t *bindings, size_t count, uint32_t *size) {
    uint64_t seen = 0;
    uint8_t palette = 0;
    if (size) *size = 0;
    if (!packet || !bindings || !size || !count || count > 49 || capacity < 4 + count * 12) return 0;
    pxa_store_u16(packet, (uint16_t)count);
    pxa_store_u16(packet + 2, 0);
    for (size_t i = 0; i < count; ++i) {
        const pxa_game_render_binding_t *b = &bindings[i];
        uint8_t *p = packet + 4 + i * 12;
        if (b->kind == 1) {
            if (b->slot >= 48 || (seen & (UINT64_C(1) << b->slot))) return 0;
            seen |= UINT64_C(1) << b->slot;
        } else if (b->kind == 2 && !b->slot && !palette) palette = 1;
        else return 0;
        p[0] = b->kind; p[1] = b->slot;
        pxa_store_u16(p + 2, 0);
        pxa_store_u64(p + 4, b->handle);
    }
    *size = (uint32_t)(4 + count * 12);
    return 1;
}

/* Atomic batch: failure preserves every old binding. Uses at most 592 bytes
 * of stack; repeated callers can reuse a buffer with the builder above. */
static inline int32_t pxa_game_render_bind_assets(uint64_t context,
    const pxa_game_render_binding_t *bindings, size_t count) {
    uint8_t packet[4 + 49 * 12];
    uint32_t size;
    if (!context || !pxa_game_render_build_bindings(packet, sizeof(packet), bindings, count, &size)) return -1;
    return pxa_io(context, PXA_GAME_RENDER_IO_BIND_ASSETS, packet, size);
}

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t buffer_count;
    uint8_t prefer_direct_scanout;
    uint8_t requested_scale;
    uint8_t scratch_mode;
    uint32_t max_draw_bytes;
} pxa_game_render_options_t;

/* Map a display coordinate to the render buffer with nearest-pixel rounding. */
static inline int32_t pxa_game_render_map_coord(
    int32_t value, uint16_t source_extent, uint16_t destination_extent) {
    int64_t product;
    int64_t rounding;
    if (source_extent == 0 || destination_extent == 0) return 0;
    product = (int64_t)value * (int64_t)destination_extent;
    rounding = (int64_t)source_extent / 2;
    return (int32_t)((product >= 0 ? product + rounding : product - rounding) /
                     (int64_t)source_extent);
}

typedef struct {
    int32_t status;
    uint64_t handle;
    uint32_t capabilities;
    uint32_t max_draw_bytes;
    uint16_t max_texture_dimension;
    uint8_t max_textures;
    uint16_t display_width;
    uint16_t display_height;
    uint16_t render_width;
    uint16_t render_height;
    uint8_t render_scale;
    uint8_t supported_scale_mask;
} pxa_game_render_create_result_t;

typedef struct {
    uint64_t submitted_frames;
    uint64_t draw_list_bytes;
    uint64_t covered_pixels;
    uint64_t host_raster_us;
    uint64_t queue_wait_us;
    uint64_t present_us;
    uint64_t dropped_frames;
    uint32_t clear_commands;
    uint32_t flat_quad_commands;
    uint32_t textured_quad_commands;
    uint32_t sprite_commands;
    uint32_t rejected_lists;
    uint32_t last_draw_list_bytes;
    uint32_t last_covered_pixels;
    uint32_t last_host_raster_us;
    uint64_t rendered_frames;
    uint64_t visible_frames;
} pxa_game_render_telemetry_t;

/* Builder only: the caller decides when to submit the exact-size packet. */
static inline int pxa_game_render_build_create(
    uint8_t *packet, size_t capacity, uint64_t token,
    const pxa_game_render_options_t *options, uint32_t *packet_size) {
    uint8_t payload[12] = {0};
    uint16_t opcode;
    if (token == 0 || options == NULL || options->buffer_count < 2 ||
        options->buffer_count > 3 ||
        options->scratch_mode > PXA_GAME_RENDER_SCRATCH_COVERAGE_2BIT ||
        options->prefer_direct_scanout > 1 ||
        (options->max_draw_bytes != 0 &&
         (options->max_draw_bytes < 32 ||
          options->max_draw_bytes > 49152)))
        return 0;
    if (options->width == 0 && options->height == 0 &&
        options->requested_scale <= 4)
        opcode = PXA_GAME_RENDER_CREATE_AUTO;
    else if (options->width != 0 && options->height != 0 &&
             options->requested_scale == 0)
        opcode = PXA_GAME_RENDER_CREATE;
    else
        return 0;
    pxa_store_u16(payload, options->width);
    pxa_store_u16(payload + 2, options->height);
    payload[4] = options->buffer_count;
    payload[5] = options->prefer_direct_scanout;
    payload[6] = options->requested_scale;
    payload[7] = options->scratch_mode;
    if (options->max_draw_bytes != 0)
        pxa_store_u32(payload + 8, options->max_draw_bytes);
    return pxa_build_message(packet, capacity,
                                PXA_GAME_RENDER_SERVICE, opcode, token,
                                payload,
                                options->max_draw_bytes != 0 ? 12u : 8u,
                                packet_size);
}

static inline int pxa_game_render_parse_create(
    const pxa_event_t *event, uint64_t expected_token,
    pxa_game_render_create_result_t *output) {
    const uint8_t *p;
    int auto_target;
    if (output == NULL) return 0;
    pxa_zero(output, sizeof(*output));
    if (event == NULL || event->service != PXA_GAME_RENDER_SERVICE ||
        (event->opcode != PXA_GAME_RENDER_CREATE &&
         event->opcode != PXA_GAME_RENDER_CREATE_AUTO) ||
        expected_token == 0 || event->token != expected_token ||
        event->payload == NULL || event->payload_size < 4)
        return 0;
    p = event->payload;
    output->status = (int32_t)pxa_load_u32(p);
    if (output->status != 0) return event->payload_size == 4;
    auto_target = event->opcode == PXA_GAME_RENDER_CREATE_AUTO;
    if (event->payload_size != (auto_target ? 36u : 24u)) return 0;
    output->handle = pxa_load_u64(p + 4);
    output->capabilities = pxa_load_u32(p + 12);
    output->max_draw_bytes = pxa_load_u32(p + 16);
    output->max_texture_dimension = pxa_load_u16(p + 20);
    output->max_textures = p[22];
    if (output->handle == 0 || p[23] != 0) return 0;
    if (auto_target) {
        output->display_width = pxa_load_u16(p + 24);
        output->display_height = pxa_load_u16(p + 26);
        output->render_width = pxa_load_u16(p + 28);
        output->render_height = pxa_load_u16(p + 30);
        output->render_scale = p[32];
        output->supported_scale_mask = p[33];
        if (p[34] != 0 || p[35] != 0) return 0;
    }
    return 1;
}

static inline int32_t pxa_game_render_close(uint64_t handle) {
    return pxa_close_handle(handle);
}

/* DrawList and upload packets stay in caller-owned Guest memory. */
static inline int32_t pxa_game_render_upload(
    uint64_t handle, uint8_t *packet, uint32_t size) {
    if (handle == 0 || packet == NULL || size == 0) return -1;
    return pxa_io(handle, PXA_GAME_RENDER_IO_UPLOAD, packet, size);
}

static inline int32_t pxa_game_render_submit(
    uint64_t handle, uint8_t *draw_list, uint32_t size) {
    if (handle == 0 || draw_list == NULL || size == 0) return -1;
    return pxa_io(handle, PXA_GAME_RENDER_IO_SUBMIT, draw_list, size);
}

/* One bounded Handle I/O call. A negative Host status is returned unchanged. */
static inline int32_t pxa_game_render_query_telemetry(
    uint64_t handle, pxa_game_render_telemetry_t *output) {
    uint8_t bytes[PXA_GAME_RENDER_TELEMETRY_BYTES];
    int32_t result;
    if (handle == 0 || output == NULL) return -1;
    pxa_zero(output, sizeof(*output));
    result = pxa_io(handle, PXA_GAME_RENDER_IO_TELEMETRY,
                       bytes, sizeof(bytes));
    if (result < 0) return result;
    if (result != (int32_t)sizeof(bytes)) return -15;
    output->submitted_frames = pxa_load_u64(bytes);
    output->draw_list_bytes = pxa_load_u64(bytes + 8);
    output->covered_pixels = pxa_load_u64(bytes + 16);
    output->host_raster_us = pxa_load_u64(bytes + 24);
    output->queue_wait_us = pxa_load_u64(bytes + 32);
    output->present_us = pxa_load_u64(bytes + 40);
    output->dropped_frames = pxa_load_u64(bytes + 48);
    output->clear_commands = pxa_load_u32(bytes + 56);
    output->flat_quad_commands = pxa_load_u32(bytes + 60);
    output->textured_quad_commands = pxa_load_u32(bytes + 64);
    output->sprite_commands = pxa_load_u32(bytes + 68);
    output->rejected_lists = pxa_load_u32(bytes + 72);
    output->last_draw_list_bytes = pxa_load_u32(bytes + 76);
    output->last_covered_pixels = pxa_load_u32(bytes + 80);
    output->last_host_raster_us = pxa_load_u32(bytes + 84);
    output->rendered_frames = pxa_load_u64(bytes + 88);
    output->visible_frames = pxa_load_u64(bytes + 96);
    return 0;
}

#endif
