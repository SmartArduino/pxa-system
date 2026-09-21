#ifndef PXA_RASTER_GUEST_H
#define PXA_RASTER_GUEST_H

#include <stddef.h>
#include <stdint.h>

#include "pxa_game_render.h"

#define PXA_RASTER_ABI_MAJOR UINT16_C(1)
#define PXA_RASTER_ABI_MINOR UINT16_C(3)
#define PXA_RASTER_DRAW_MAGIC UINT32_C(0x4c525850)
#define PXA_RASTER_UPLOAD_MAGIC UINT32_C(0x52555850)
#define PXA_RASTER_MAX_TEXTURES UINT8_C(48)
#define PXA_RASTER_PALETTE_COLORS UINT16_C(256)
#define PXA_RASTER_MAX_DRAW_BYTES UINT32_C(49152)
#define PXA_RASTER_MAX_COMMANDS UINT32_C(768)
#define PXA_RASTER_MAX_COORDINATE_SHIFT UINT8_C(3)
#define PXA_RASTER_CAP_FLAT_QUAD UINT32_C(1)
#define PXA_RASTER_CAP_TEXTURED_QUAD UINT32_C(2)
#define PXA_RASTER_CAP_ADDITIVE_SPRITE UINT32_C(4)
#define PXA_RASTER_CAP_SPRITE_BATCH UINT32_C(8)
#define PXA_RASTER_CAP_TRIANGLE_BATCH UINT32_C(16)
#define PXA_RASTER_CAP_AFFINE_UV UINT32_C(32)
/* The Host accepts texture slots up to PXA_RASTER_MAX_TEXTURES. Only address
 * slots >= 16 when this is advertised so older Hosts keep working. */
#define PXA_RASTER_CAP_TEXTURE_SLOTS_48 UINT32_C(64)
#define PXA_RASTER_CAP_PAINTER_POLYGON UINT32_C(128)
#define PXA_RASTER_UPLOAD_PALETTE_RGB565 UINT8_C(1)
#define PXA_RASTER_UPLOAD_TEXTURE_INDEX8 UINT8_C(2)
#define PXA_RASTER_UPLOAD_LIT_PALETTE_RGB565 UINT8_C(3)
#define PXA_RASTER_UPLOAD_HEADER_BYTES UINT32_C(20)
#define PXA_RASTER_DRAW_HEADER_BYTES UINT32_C(32)
#define PXA_RASTER_RECORD_CLEAR_RGB565 UINT8_C(1)
#define PXA_RASTER_RECORD_FLAT_QUAD UINT8_C(2)
#define PXA_RASTER_RECORD_TEXTURED_QUAD UINT8_C(3)
#define PXA_RASTER_RECORD_SPRITE UINT8_C(4)
#define PXA_RASTER_RECORD_SPRITE_BATCH UINT8_C(5)
#define PXA_RASTER_RECORD_TRIANGLE_BATCH UINT8_C(6)
#define PXA_RASTER_CLEAR_BYTES UINT16_C(8)
#define PXA_RASTER_FLAT_QUAD_BYTES UINT16_C(24)
#define PXA_RASTER_TEXTURED_QUAD_BYTES UINT16_C(56)
#define PXA_RASTER_SPRITE_BYTES UINT16_C(24)
#define PXA_RASTER_VERTEX_BYTES UINT16_C(12)
#define PXA_RASTER_SPRITE_BATCH_HEADER_BYTES UINT16_C(12)
#define PXA_RASTER_SPRITE_INSTANCE_BYTES UINT16_C(16)
#define PXA_RASTER_TRIANGLE_BATCH_HEADER_BYTES UINT16_C(12)
#define PXA_RASTER_QUAD_SOLID_COLOR UINT8_C(1)
/* Textured quads only. Only emit when the Host advertised
 * PXA_RASTER_CAP_AFFINE_UV; an older Host rejects unknown flags. */
#define PXA_RASTER_QUAD_AFFINE_UV UINT8_C(2)
#define PXA_RASTER_QUAD_PAINTER UINT8_C(4)
#define PXA_RASTER_QUAD_TRANSPARENT_INDEX0 UINT8_C(8)
#define PXA_RASTER_SPRITE_TRANSPARENT_INDEX0 UINT8_C(1)
#define PXA_RASTER_SPRITE_SOLID_COLOR UINT8_C(2)
#define PXA_RASTER_SPRITE_ADDITIVE UINT8_C(4)
#define PXA_RASTER_TELEMETRY_BYTES UINT32_C(104)

typedef struct {
    int16_t x_q4;
    int16_t y_q4;
    int16_t u_q4;
    int16_t v_q4;
    uint8_t light;
    uint16_t depth_q8;
} pxa_raster_vertex_t;

typedef struct {
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t source_x;
    uint16_t source_y;
    uint16_t source_width;
    uint16_t source_height;
} pxa_raster_sprite_instance_t;

typedef struct {
    uint8_t *bytes;
    uint32_t capacity;
    uint32_t length;
    uint32_t command_count;
    uint32_t required_capabilities;
    uint64_t frame_id;
    int32_t status;
    uint8_t coordinate_shift;
} pxa_raster_draw_list_t;

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
} pxa_raster_telemetry_t;

static inline void pxa_raster_zero_bytes(void *memory, size_t size) {
    uint8_t *bytes = (uint8_t *)memory;
    size_t index;
    for (index = 0; index < size; ++index) bytes[index] = 0;
}

static inline void pxa_raster_copy_bytes(uint8_t *destination,
                                         const uint8_t *source,
                                         size_t size) {
    size_t index;
    for (index = 0; index < size; ++index) destination[index] = source[index];
}

static inline int32_t pxa_raster_upload(
    uint32_t context_handle, uint8_t kind, uint8_t slot, uint16_t width,
    uint16_t height, const uint8_t *payload, uint32_t payload_bytes,
    uint8_t *scratch, uint32_t scratch_capacity) {
    uint32_t total = PXA_RASTER_UPLOAD_HEADER_BYTES + payload_bytes;
    if (context_handle == 0 || payload == NULL || scratch == NULL ||
        total < payload_bytes || total > scratch_capacity)
        return PXA_STATUS_INVALID_ARGUMENT;
    pxa_game_render_store_u32(scratch, PXA_RASTER_UPLOAD_MAGIC);
    pxa_game_render_store_u16(scratch + 4, PXA_RASTER_ABI_MAJOR);
    pxa_game_render_store_u16(scratch + 6, PXA_RASTER_ABI_MINOR);
    scratch[8] = kind;
    scratch[9] = slot;
    pxa_game_render_store_u16(scratch + 10, 0);
    pxa_game_render_store_u16(scratch + 12, width);
    pxa_game_render_store_u16(scratch + 14, height);
    pxa_game_render_store_u32(scratch + 16, payload_bytes);
    if (payload != scratch + PXA_RASTER_UPLOAD_HEADER_BYTES)
        pxa_raster_copy_bytes(scratch + PXA_RASTER_UPLOAD_HEADER_BYTES,
                              payload, payload_bytes);
    return pxa_io(context_handle, PXA_GAME_RENDER_IO_UPLOAD, scratch, total);
}

static inline int32_t pxa_raster_upload_palette_rgb565(
    uint32_t context_handle, const uint16_t palette[256], uint8_t *scratch,
    uint32_t scratch_capacity) {
    uint32_t index;
    if (palette == NULL || scratch == NULL || scratch_capacity < 532u)
        return PXA_STATUS_INVALID_ARGUMENT;
    for (index = 0; index < 256u; ++index)
        pxa_game_render_store_u16(scratch + PXA_RASTER_UPLOAD_HEADER_BYTES +
                                           index * 2u,
                              palette[index]);
    return pxa_raster_upload(
        context_handle, PXA_RASTER_UPLOAD_PALETTE_RGB565, 0, 256, 1,
        scratch + PXA_RASTER_UPLOAD_HEADER_BYTES, 512, scratch,
        scratch_capacity);
}

static inline int32_t pxa_raster_upload_lit_palette_rgb565(
    uint32_t context_handle, uint16_t light_levels, const uint16_t *palette,
    uint8_t *scratch, uint32_t scratch_capacity) {
    uint32_t entries = (uint32_t)light_levels * PXA_RASTER_PALETTE_COLORS;
    uint32_t payload_bytes = entries * 2u;
    uint32_t index;
    if (light_levels == 0 || light_levels > 256u || palette == NULL ||
        scratch == NULL || payload_bytes > UINT32_MAX - PXA_RASTER_UPLOAD_HEADER_BYTES ||
        scratch_capacity < PXA_RASTER_UPLOAD_HEADER_BYTES + payload_bytes)
        return PXA_STATUS_INVALID_ARGUMENT;
    for (index = 0; index < entries; ++index)
        pxa_game_render_store_u16(scratch + PXA_RASTER_UPLOAD_HEADER_BYTES +
                                               index * 2u,
                                  palette[index]);
    return pxa_raster_upload(
        context_handle, PXA_RASTER_UPLOAD_LIT_PALETTE_RGB565, 0, 256,
        light_levels, scratch + PXA_RASTER_UPLOAD_HEADER_BYTES, payload_bytes,
        scratch, scratch_capacity);
}

static inline int32_t pxa_raster_upload_texture_index8(
    uint32_t context_handle, uint8_t slot, uint16_t width, uint16_t height,
    const uint8_t *pixels, uint8_t *scratch, uint32_t scratch_capacity) {
    uint64_t bytes = (uint64_t)width * height;
    if (bytes == 0 || bytes > UINT32_MAX) return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_raster_upload(context_handle, PXA_RASTER_UPLOAD_TEXTURE_INDEX8,
                             slot, width, height, pixels, (uint32_t)bytes,
                             scratch, scratch_capacity);
}

static inline int16_t pxa_raster_scale_coordinate(int16_t coordinate,
                                                   uint8_t shift) {
    int32_t value = coordinate;
    if (shift == 0) return coordinate;
    if (shift > PXA_RASTER_MAX_COORDINATE_SHIFT) return 0;
    if (value >= 0) return (int16_t)(value >> shift);
    return (int16_t)-(((-value) + ((INT32_C(1) << shift) - 1)) >> shift);
}

static inline uint16_t pxa_raster_scale_extent(uint16_t extent,
                                               uint8_t shift) {
    if (shift == 0 || extent == 0) return extent;
    if (shift > PXA_RASTER_MAX_COORDINATE_SHIFT) return 0;
    return (uint16_t)(((uint32_t)extent +
                       ((UINT32_C(1) << shift) - 1u)) >> shift);
}

static inline void pxa_raster_draw_list_begin_scaled(
    pxa_raster_draw_list_t *list, uint8_t *bytes, uint32_t capacity,
    uint64_t frame_id, uint8_t coordinate_shift) {
    if (list == NULL) return;
    pxa_raster_zero_bytes(list, sizeof(*list));
    list->bytes = bytes;
    list->capacity = capacity;
    list->frame_id = frame_id;
    list->coordinate_shift = coordinate_shift;
    if (bytes == NULL || capacity < PXA_RASTER_DRAW_HEADER_BYTES ||
        frame_id == 0 || coordinate_shift > PXA_RASTER_MAX_COORDINATE_SHIFT) {
        list->status = PXA_STATUS_INVALID_ARGUMENT;
        return;
    }
    pxa_raster_zero_bytes(bytes, PXA_RASTER_DRAW_HEADER_BYTES);
    list->length = PXA_RASTER_DRAW_HEADER_BYTES;
}

static inline void pxa_raster_draw_list_begin(
    pxa_raster_draw_list_t *list, uint8_t *bytes, uint32_t capacity,
    uint64_t frame_id) {
    pxa_raster_draw_list_begin_scaled(list, bytes, capacity, frame_id, 0);
}

static inline uint8_t *pxa_raster_append(pxa_raster_draw_list_t *list,
                                         uint8_t type, uint16_t size) {
    uint8_t *record;
    if (list == NULL || list->status != PXA_STATUS_OK ||
        list->length > list->capacity ||
        list->command_count >= PXA_RASTER_MAX_COMMANDS ||
        size > list->capacity - list->length) {
        if (list != NULL) list->status = PXA_STATUS_LIMIT_EXCEEDED;
        return NULL;
    }
    record = list->bytes + list->length;
    pxa_raster_zero_bytes(record, size);
    record[0] = type;
    pxa_game_render_store_u16(record + 2, size);
    list->length += size;
    ++list->command_count;
    return record;
}

static inline int pxa_raster_clear(pxa_raster_draw_list_t *list,
                                   uint16_t color) {
    uint8_t *record = pxa_raster_append(list, PXA_RASTER_RECORD_CLEAR_RGB565,
                                        PXA_RASTER_CLEAR_BYTES);
    if (record == NULL) return 0;
    pxa_game_render_store_u16(record + 4, color);
    return 1;
}

static inline int pxa_raster_flat_quad(pxa_raster_draw_list_t *list,
                                       const int16_t xy_q4[8],
                                       uint16_t color) {
    uint8_t *record;
    uint8_t index;
    if (xy_q4 == NULL) return 0;
    record = pxa_raster_append(list, PXA_RASTER_RECORD_FLAT_QUAD,
                               PXA_RASTER_FLAT_QUAD_BYTES);
    if (record == NULL) return 0;
    pxa_game_render_store_u16(record + 4, color);
    for (index = 0; index < 8; ++index)
        pxa_game_render_store_u16(
            record + 8 + index * 2u,
            (uint16_t)pxa_raster_scale_coordinate(
                xy_q4[index], list->coordinate_shift));
    list->required_capabilities |= PXA_RASTER_CAP_FLAT_QUAD;
    return 1;
}

static inline int pxa_raster_textured_quad_flags(
    pxa_raster_draw_list_t *list, const pxa_raster_vertex_t vertices[4],
    uint8_t texture_slot, uint8_t flags) {
    uint8_t *record;
    uint8_t index;
    if (vertices == NULL || texture_slot >= PXA_RASTER_MAX_TEXTURES ||
        (flags & ~(PXA_RASTER_QUAD_AFFINE_UV |
                   PXA_RASTER_QUAD_PAINTER |
                   PXA_RASTER_QUAD_TRANSPARENT_INDEX0)) != 0 ||
        ((flags & PXA_RASTER_QUAD_TRANSPARENT_INDEX0) != 0 &&
         (flags & PXA_RASTER_QUAD_PAINTER) == 0))
        return 0;
    record = pxa_raster_append(list, PXA_RASTER_RECORD_TEXTURED_QUAD,
                               PXA_RASTER_TEXTURED_QUAD_BYTES);
    if (record == NULL) return 0;
    record[1] = flags;
    record[4] = texture_slot;
    for (index = 0; index < 4; ++index) {
        uint8_t *wire = record + 8 + index * PXA_RASTER_VERTEX_BYTES;
        pxa_game_render_store_u16(
            wire, (uint16_t)pxa_raster_scale_coordinate(
                      vertices[index].x_q4, list->coordinate_shift));
        pxa_game_render_store_u16(
            wire + 2, (uint16_t)pxa_raster_scale_coordinate(
                          vertices[index].y_q4, list->coordinate_shift));
        pxa_game_render_store_u16(wire + 4, (uint16_t)vertices[index].u_q4);
        pxa_game_render_store_u16(wire + 6, (uint16_t)vertices[index].v_q4);
        wire[8] = vertices[index].light;
        pxa_game_render_store_u16(wire + 10, vertices[index].depth_q8);
    }
    list->required_capabilities |= PXA_RASTER_CAP_TEXTURED_QUAD;
    if ((flags & PXA_RASTER_QUAD_AFFINE_UV) != 0)
        list->required_capabilities |= PXA_RASTER_CAP_AFFINE_UV;
    if ((flags & PXA_RASTER_QUAD_PAINTER) != 0)
        list->required_capabilities |= PXA_RASTER_CAP_PAINTER_POLYGON;
    return 1;
}

static inline int pxa_raster_textured_quad(
    pxa_raster_draw_list_t *list, const pxa_raster_vertex_t vertices[4],
    uint8_t texture_slot) {
    return pxa_raster_textured_quad_flags(list, vertices, texture_slot, 0);
}

static inline int pxa_raster_solid_depth_quad(
    pxa_raster_draw_list_t *list, const pxa_raster_vertex_t vertices[4],
    uint16_t color) {
    uint8_t *record;
    uint8_t index;
    if (vertices == NULL) return 0;
    record = pxa_raster_append(list, PXA_RASTER_RECORD_TEXTURED_QUAD,
                               PXA_RASTER_TEXTURED_QUAD_BYTES);
    if (record == NULL) return 0;
    record[1] = PXA_RASTER_QUAD_SOLID_COLOR;
    pxa_game_render_store_u16(record + 6, color);
    for (index = 0; index < 4; ++index) {
        uint8_t *wire = record + 8 + index * PXA_RASTER_VERTEX_BYTES;
        pxa_game_render_store_u16(
            wire, (uint16_t)pxa_raster_scale_coordinate(
                      vertices[index].x_q4, list->coordinate_shift));
        pxa_game_render_store_u16(
            wire + 2, (uint16_t)pxa_raster_scale_coordinate(
                          vertices[index].y_q4, list->coordinate_shift));
        pxa_game_render_store_u16(wire + 4, (uint16_t)vertices[index].u_q4);
        pxa_game_render_store_u16(wire + 6, (uint16_t)vertices[index].v_q4);
        wire[8] = vertices[index].light;
        pxa_game_render_store_u16(wire + 10, vertices[index].depth_q8);
    }
    list->required_capabilities |= PXA_RASTER_CAP_TEXTURED_QUAD;
    return 1;
}

static inline int pxa_raster_solid_painter_quad(
    pxa_raster_draw_list_t *list, const pxa_raster_vertex_t vertices[4],
    uint8_t palette_index) {
    uint8_t *record;
    uint8_t index;
    if (vertices == NULL) return 0;
    record = pxa_raster_append(list, PXA_RASTER_RECORD_TEXTURED_QUAD,
                               PXA_RASTER_TEXTURED_QUAD_BYTES);
    if (record == NULL) return 0;
    record[1] = PXA_RASTER_QUAD_SOLID_COLOR | PXA_RASTER_QUAD_PAINTER;
    pxa_game_render_store_u16(record + 6, palette_index);
    for (index = 0; index < 4; ++index) {
        uint8_t *wire = record + 8 + index * PXA_RASTER_VERTEX_BYTES;
        pxa_game_render_store_u16(
            wire, (uint16_t)pxa_raster_scale_coordinate(
                      vertices[index].x_q4, list->coordinate_shift));
        pxa_game_render_store_u16(
            wire + 2, (uint16_t)pxa_raster_scale_coordinate(
                          vertices[index].y_q4, list->coordinate_shift));
        pxa_game_render_store_u16(wire + 4, (uint16_t)vertices[index].u_q4);
        pxa_game_render_store_u16(wire + 6, (uint16_t)vertices[index].v_q4);
        wire[8] = vertices[index].light;
        pxa_game_render_store_u16(wire + 10, 0);
    }
    list->required_capabilities |= PXA_RASTER_CAP_TEXTURED_QUAD |
                                   PXA_RASTER_CAP_PAINTER_POLYGON;
    return 1;
}

/* Additive blending is optional. When unavailable the same sprite is emitted
 * with ordinary replacement blending, so a new Guest remains valid on an old
 * Host and never sends an unknown/unsupported flag. */
static inline int pxa_raster_sprite(
    pxa_raster_draw_list_t *list, uint8_t texture_slot, uint8_t flags,
    uint32_t available_capabilities, int16_t x, int16_t y, uint16_t width,
    uint16_t height, uint16_t source_x, uint16_t source_y,
    uint16_t source_width, uint16_t source_height, uint16_t solid_color) {
    const uint8_t known = PXA_RASTER_SPRITE_TRANSPARENT_INDEX0 |
                          PXA_RASTER_SPRITE_SOLID_COLOR |
                          PXA_RASTER_SPRITE_ADDITIVE;
    uint8_t *record;
    if (list == NULL || texture_slot >= PXA_RASTER_MAX_TEXTURES ||
        (flags & ~known) != 0 || width == 0 || height == 0 ||
        source_width == 0 || source_height == 0)
        return 0;
    if ((available_capabilities & PXA_RASTER_CAP_ADDITIVE_SPRITE) == 0)
        flags &= (uint8_t)~PXA_RASTER_SPRITE_ADDITIVE;
    record = pxa_raster_append(list, PXA_RASTER_RECORD_SPRITE,
                               PXA_RASTER_SPRITE_BYTES);
    if (record == NULL) return 0;
    record[1] = flags;
    record[4] = texture_slot;
    pxa_game_render_store_u16(record + 6, solid_color);
    pxa_game_render_store_u16(
        record + 8,
        (uint16_t)pxa_raster_scale_coordinate(x, list->coordinate_shift));
    pxa_game_render_store_u16(
        record + 10,
        (uint16_t)pxa_raster_scale_coordinate(y, list->coordinate_shift));
    pxa_game_render_store_u16(
        record + 12, pxa_raster_scale_extent(width, list->coordinate_shift));
    pxa_game_render_store_u16(
        record + 14, pxa_raster_scale_extent(height, list->coordinate_shift));
    pxa_game_render_store_u16(record + 16, source_x);
    pxa_game_render_store_u16(record + 18, source_y);
    pxa_game_render_store_u16(record + 20, source_width);
    pxa_game_render_store_u16(record + 22, source_height);
    if ((flags & PXA_RASTER_SPRITE_ADDITIVE) != 0)
        list->required_capabilities |= PXA_RASTER_CAP_ADDITIVE_SPRITE;
    return 1;
}

static inline int pxa_raster_sprite_batch(
    pxa_raster_draw_list_t *list, uint8_t texture_slot, uint8_t flags,
    uint32_t available_capabilities,
    const pxa_raster_sprite_instance_t *instances, uint16_t count,
    uint16_t solid_color) {
    const uint8_t known = PXA_RASTER_SPRITE_TRANSPARENT_INDEX0 |
                          PXA_RASTER_SPRITE_SOLID_COLOR |
                          PXA_RASTER_SPRITE_ADDITIVE;
    uint32_t size = PXA_RASTER_SPRITE_BATCH_HEADER_BYTES +
                    (uint32_t)count * PXA_RASTER_SPRITE_INSTANCE_BYTES;
    uint8_t *record;
    uint16_t index;
    if (list == NULL || texture_slot >= PXA_RASTER_MAX_TEXTURES ||
        instances == NULL || count == 0 || size > UINT16_MAX ||
        (flags & ~known) != 0 ||
        (available_capabilities & PXA_RASTER_CAP_SPRITE_BATCH) == 0)
        return 0;
    if ((available_capabilities & PXA_RASTER_CAP_ADDITIVE_SPRITE) == 0)
        flags &= (uint8_t)~PXA_RASTER_SPRITE_ADDITIVE;
    record = pxa_raster_append(list, PXA_RASTER_RECORD_SPRITE_BATCH,
                               (uint16_t)size);
    if (record == NULL) return 0;
    record[1] = flags;
    record[4] = texture_slot;
    pxa_game_render_store_u16(record + 6, solid_color);
    pxa_game_render_store_u16(record + 8, count);
    for (index = 0; index < count; ++index) {
        const pxa_raster_sprite_instance_t *instance = &instances[index];
        uint8_t *wire = record + PXA_RASTER_SPRITE_BATCH_HEADER_BYTES +
                        (uint32_t)index * PXA_RASTER_SPRITE_INSTANCE_BYTES;
        if (instance->width == 0 || instance->height == 0 ||
            instance->source_width == 0 || instance->source_height == 0) {
            list->status = PXA_STATUS_INVALID_ARGUMENT;
            return 0;
        }
        pxa_game_render_store_u16(
            wire, (uint16_t)pxa_raster_scale_coordinate(
                      instance->x, list->coordinate_shift));
        pxa_game_render_store_u16(
            wire + 2, (uint16_t)pxa_raster_scale_coordinate(
                          instance->y, list->coordinate_shift));
        pxa_game_render_store_u16(
            wire + 4, pxa_raster_scale_extent(instance->width,
                                               list->coordinate_shift));
        pxa_game_render_store_u16(
            wire + 6, pxa_raster_scale_extent(instance->height,
                                               list->coordinate_shift));
        pxa_game_render_store_u16(wire + 8, instance->source_x);
        pxa_game_render_store_u16(wire + 10, instance->source_y);
        pxa_game_render_store_u16(wire + 12, instance->source_width);
        pxa_game_render_store_u16(wire + 14, instance->source_height);
    }
    list->required_capabilities |= PXA_RASTER_CAP_SPRITE_BATCH;
    if ((flags & PXA_RASTER_SPRITE_ADDITIVE) != 0)
        list->required_capabilities |= PXA_RASTER_CAP_ADDITIVE_SPRITE;
    return 1;
}

static inline int pxa_raster_triangle_batch_flags(
    pxa_raster_draw_list_t *list, const pxa_raster_vertex_t *vertices,
    uint16_t triangle_count, uint8_t texture_slot, uint8_t flags,
    uint16_t solid_color) {
    uint32_t vertex_count = (uint32_t)triangle_count * 3u;
    uint32_t size = PXA_RASTER_TRIANGLE_BATCH_HEADER_BYTES +
                    vertex_count * PXA_RASTER_VERTEX_BYTES;
    uint8_t *record;
    uint32_t index;
    if (list == NULL || vertices == NULL || triangle_count == 0 ||
        texture_slot >= PXA_RASTER_MAX_TEXTURES || size > UINT16_MAX ||
        (flags & ~(PXA_RASTER_QUAD_SOLID_COLOR |
                   PXA_RASTER_QUAD_AFFINE_UV |
                   PXA_RASTER_QUAD_PAINTER |
                   PXA_RASTER_QUAD_TRANSPARENT_INDEX0)) != 0 ||
        ((flags & PXA_RASTER_QUAD_TRANSPARENT_INDEX0) != 0 &&
         (flags & PXA_RASTER_QUAD_PAINTER) == 0))
        return 0;
    record = pxa_raster_append(list, PXA_RASTER_RECORD_TRIANGLE_BATCH,
                               (uint16_t)size);
    if (record == NULL) return 0;
    record[1] = flags;
    record[4] = texture_slot;
    pxa_game_render_store_u16(record + 6, solid_color);
    pxa_game_render_store_u16(record + 8, triangle_count);
    for (index = 0; index < vertex_count; ++index) {
        uint8_t *wire = record + PXA_RASTER_TRIANGLE_BATCH_HEADER_BYTES +
                        index * PXA_RASTER_VERTEX_BYTES;
        pxa_game_render_store_u16(
            wire, (uint16_t)pxa_raster_scale_coordinate(
                      vertices[index].x_q4, list->coordinate_shift));
        pxa_game_render_store_u16(
            wire + 2, (uint16_t)pxa_raster_scale_coordinate(
                          vertices[index].y_q4, list->coordinate_shift));
        pxa_game_render_store_u16(wire + 4, (uint16_t)vertices[index].u_q4);
        pxa_game_render_store_u16(wire + 6, (uint16_t)vertices[index].v_q4);
        wire[8] = vertices[index].light;
        pxa_game_render_store_u16(wire + 10, vertices[index].depth_q8);
    }
    list->required_capabilities |= PXA_RASTER_CAP_TRIANGLE_BATCH;
    if ((flags & PXA_RASTER_QUAD_AFFINE_UV) != 0)
        list->required_capabilities |= PXA_RASTER_CAP_AFFINE_UV;
    if ((flags & PXA_RASTER_QUAD_PAINTER) != 0)
        list->required_capabilities |= PXA_RASTER_CAP_PAINTER_POLYGON;
    return 1;
}


static inline int pxa_raster_triangle_batch(
    pxa_raster_draw_list_t *list, const pxa_raster_vertex_t *vertices,
    uint16_t triangle_count, uint8_t texture_slot, uint8_t solid,
    uint16_t solid_color) {
    return pxa_raster_triangle_batch_flags(
        list, vertices, triangle_count, texture_slot,
        solid ? PXA_RASTER_QUAD_SOLID_COLOR : 0, solid_color);
}

static inline int32_t pxa_raster_submit(uint32_t context_handle,
                                        pxa_raster_draw_list_t *list) {
    if (context_handle == 0 || list == NULL || list->status != PXA_STATUS_OK ||
        list->command_count == 0)
        return list != NULL && list->status != PXA_STATUS_OK
                   ? list->status
                   : PXA_STATUS_INVALID_ARGUMENT;
    pxa_game_render_store_u32(list->bytes, PXA_RASTER_DRAW_MAGIC);
    pxa_game_render_store_u16(list->bytes + 4, PXA_RASTER_ABI_MAJOR);
    pxa_game_render_store_u16(list->bytes + 6, PXA_RASTER_ABI_MINOR);
    pxa_game_render_store_u32(list->bytes + 8, list->length);
    pxa_game_render_store_u32(list->bytes + 12, list->required_capabilities);
    pxa_game_render_store_u32(list->bytes + 16, list->command_count);
    pxa_game_render_store_u64(list->bytes + 20, list->frame_id);
    pxa_game_render_store_u32(list->bytes + 28, 0);
    return pxa_io(context_handle, PXA_GAME_RENDER_IO_SUBMIT, list->bytes,
                  list->length);
}

static inline int32_t pxa_raster_query_telemetry(
    uint32_t context_handle, pxa_raster_telemetry_t *telemetry) {
    uint8_t bytes[PXA_RASTER_TELEMETRY_BYTES];
    int32_t result;
    if (context_handle == 0 || telemetry == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    result = pxa_io(context_handle, PXA_GAME_RENDER_IO_TELEMETRY, bytes,
                    sizeof(bytes));
    if (result != (int32_t)sizeof(bytes)) return result;
    telemetry->submitted_frames = pxa_read_u64(bytes);
    telemetry->draw_list_bytes = pxa_read_u64(bytes + 8);
    telemetry->covered_pixels = pxa_read_u64(bytes + 16);
    telemetry->host_raster_us = pxa_read_u64(bytes + 24);
    telemetry->queue_wait_us = pxa_read_u64(bytes + 32);
    telemetry->present_us = pxa_read_u64(bytes + 40);
    telemetry->dropped_frames = pxa_read_u64(bytes + 48);
    telemetry->clear_commands = pxa_read_u32(bytes + 56);
    telemetry->flat_quad_commands = pxa_read_u32(bytes + 60);
    telemetry->textured_quad_commands = pxa_read_u32(bytes + 64);
    telemetry->sprite_commands = pxa_read_u32(bytes + 68);
    telemetry->rejected_lists = pxa_read_u32(bytes + 72);
    telemetry->last_draw_list_bytes = pxa_read_u32(bytes + 76);
    telemetry->last_covered_pixels = pxa_read_u32(bytes + 80);
    telemetry->last_host_raster_us = pxa_read_u32(bytes + 84);
    telemetry->rendered_frames = pxa_read_u64(bytes + 88);
    telemetry->visible_frames = pxa_read_u64(bytes + 96);
    return result;
}

#endif
