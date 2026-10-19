/* Generated from pxa-game-render.json. Do not edit. */
#ifndef PXA_GAME_RENDER_SPANS_WIRE_GENERATED_H
#define PXA_GAME_RENDER_SPANS_WIRE_GENERATED_H
#include "pxa/wire.h"
#define PXA_GAME_RENDER_SPANS_MAGIC UINT32_C(1347639376)
#define PXA_GAME_RENDER_SPANS_HEADER_BYTES 72u
#define PXA_GAME_RENDER_SPAN_BYTES 24u
#define PXA_GAME_RENDER_SPANS_POW2 1u
#define PXA_GAME_RENDER_SPANS_FULLBRIGHT 2u
#define PXA_GAME_RENDER_SPANS_KNOWN_FLAGS 3u
#ifndef PXA_GAME_RENDER_SPANS_WIRE_SHARED_H
#define PXA_GAME_RENDER_SPANS_WIRE_SHARED_H
#define PXA_GAME_RENDER_PROJECTED_MAGIC UINT32_C(1347442768)
#define PXA_GAME_RENDER_PROJECTION_BYTES 80u
#define PXA_GAME_RENDER_ROW_BYTES 8u
#define PXA_GAME_RENDER_PROJECTED_PREFIX_BYTES 152u
#define PXA_GAME_RENDER_PROJECTED_WRITE_DEPTH 1u
#define PXA_GAME_RENDER_PROJECTED_SEGMENT_PIXELS 32u
#define PXA_GAME_RENDER_FRAME_MAGIC UINT32_C(1179211856)
#define PXA_GAME_RENDER_FRAME_MATERIAL_MAGIC UINT32_C(1296652368)
#define PXA_GAME_RENDER_FRAME_HEADER_BYTES UINT32_C(16)
#define PXA_GAME_RENDER_FRAME_MAX_PACKET_BYTES UINT32_C(262144)
#define PXA_GAME_RENDER_FRAME_MAX_MATERIALS UINT32_C(1024)
#define PXA_GAME_RENDER_FRAME_MAX_LIGHT_VALIDATION_BYTES UINT32_C(2097152)
#define PXA_GAME_RENDER_BLIT_BYTES 64u
#define PXA_GAME_RENDER_BLIT_MAX_PIXELS UINT32_C(262144)
typedef struct {
    uint32_t source_offset;
    uint32_t source_bytes;
    uint32_t palette_offset;
    uint32_t palette_bytes;
    uint32_t target_offset;
    uint32_t target_bytes;
    uint16_t source_width;
    uint16_t source_height;
    uint32_t source_pitch;
    uint16_t target_width;
    uint16_t target_height;
    uint32_t target_pitch;
    uint32_t cursor_offset;
    uint32_t cursor_bytes;
    int32_t cursor_x;
    int32_t cursor_y;
    uint16_t cursor_width;
    uint16_t cursor_height;
    uint32_t cursor_pitch;
} pxa_game_render_blit_wire_t;
static inline int pxa_game_render_blit_decode(const uint8_t *p, size_t n,
    pxa_game_render_blit_wire_t *v) {
    if (!p || !v || n < 64u) return 0;
    v->source_offset = (uint32_t)pxa_wire_generated_load_u32(p + 0);
    v->source_bytes = (uint32_t)pxa_wire_generated_load_u32(p + 4);
    v->palette_offset = (uint32_t)pxa_wire_generated_load_u32(p + 8);
    v->palette_bytes = (uint32_t)pxa_wire_generated_load_u32(p + 12);
    v->target_offset = (uint32_t)pxa_wire_generated_load_u32(p + 16);
    v->target_bytes = (uint32_t)pxa_wire_generated_load_u32(p + 20);
    v->source_width = (uint16_t)pxa_wire_generated_load_u16(p + 24);
    v->source_height = (uint16_t)pxa_wire_generated_load_u16(p + 26);
    v->source_pitch = (uint32_t)pxa_wire_generated_load_u32(p + 28);
    v->target_width = (uint16_t)pxa_wire_generated_load_u16(p + 32);
    v->target_height = (uint16_t)pxa_wire_generated_load_u16(p + 34);
    v->target_pitch = (uint32_t)pxa_wire_generated_load_u32(p + 36);
    v->cursor_offset = (uint32_t)pxa_wire_generated_load_u32(p + 40);
    v->cursor_bytes = (uint32_t)pxa_wire_generated_load_u32(p + 44);
    v->cursor_x = (int32_t)pxa_wire_generated_load_u32(p + 48);
    v->cursor_y = (int32_t)pxa_wire_generated_load_u32(p + 52);
    v->cursor_width = (uint16_t)pxa_wire_generated_load_u16(p + 56);
    v->cursor_height = (uint16_t)pxa_wire_generated_load_u16(p + 58);
    v->cursor_pitch = (uint32_t)pxa_wire_generated_load_u32(p + 60);
    return 1;
}
static inline int pxa_game_render_blit_encode(uint8_t *p, size_t n,
    const pxa_game_render_blit_wire_t *v) {
    if (!p || !v || n < 64u) return 0;
    pxa_wire_generated_store_u32(p + 0, (uint32_t)v->source_offset);
    pxa_wire_generated_store_u32(p + 4, (uint32_t)v->source_bytes);
    pxa_wire_generated_store_u32(p + 8, (uint32_t)v->palette_offset);
    pxa_wire_generated_store_u32(p + 12, (uint32_t)v->palette_bytes);
    pxa_wire_generated_store_u32(p + 16, (uint32_t)v->target_offset);
    pxa_wire_generated_store_u32(p + 20, (uint32_t)v->target_bytes);
    pxa_wire_generated_store_u16(p + 24, (uint16_t)v->source_width);
    pxa_wire_generated_store_u16(p + 26, (uint16_t)v->source_height);
    pxa_wire_generated_store_u32(p + 28, (uint32_t)v->source_pitch);
    pxa_wire_generated_store_u16(p + 32, (uint16_t)v->target_width);
    pxa_wire_generated_store_u16(p + 34, (uint16_t)v->target_height);
    pxa_wire_generated_store_u32(p + 36, (uint32_t)v->target_pitch);
    pxa_wire_generated_store_u32(p + 40, (uint32_t)v->cursor_offset);
    pxa_wire_generated_store_u32(p + 44, (uint32_t)v->cursor_bytes);
    pxa_wire_generated_store_u32(p + 48, (uint32_t)v->cursor_x);
    pxa_wire_generated_store_u32(p + 52, (uint32_t)v->cursor_y);
    pxa_wire_generated_store_u16(p + 56, (uint16_t)v->cursor_width);
    pxa_wire_generated_store_u16(p + 58, (uint16_t)v->cursor_height);
    pxa_wire_generated_store_u32(p + 60, (uint32_t)v->cursor_pitch);
    return 1;
}
typedef struct {
    uint32_t magic;
    uint16_t major;
    uint16_t minor;
    uint32_t count;
    uint32_t command_bytes;
} pxa_game_render_frame_header_wire_t;
static inline int pxa_game_render_frame_header_decode(const uint8_t *p, size_t n,
    pxa_game_render_frame_header_wire_t *v) {
    if (!p || !v || n < 16u) return 0;
    v->magic = (uint32_t)pxa_wire_generated_load_u32(p + 0);
    v->major = (uint16_t)pxa_wire_generated_load_u16(p + 4);
    v->minor = (uint16_t)pxa_wire_generated_load_u16(p + 6);
    v->count = (uint32_t)pxa_wire_generated_load_u32(p + 8);
    v->command_bytes = (uint32_t)pxa_wire_generated_load_u32(p + 12);
    return 1;
}
static inline int pxa_game_render_frame_header_encode(uint8_t *p, size_t n,
    const pxa_game_render_frame_header_wire_t *v) {
    if (!p || !v || n < 16u) return 0;
    pxa_wire_generated_store_u32(p + 0, (uint32_t)v->magic);
    pxa_wire_generated_store_u16(p + 4, (uint16_t)v->major);
    pxa_wire_generated_store_u16(p + 6, (uint16_t)v->minor);
    pxa_wire_generated_store_u32(p + 8, (uint32_t)v->count);
    pxa_wire_generated_store_u32(p + 12, (uint32_t)v->command_bytes);
    return 1;
}
typedef struct {
    uint32_t magic;
    uint16_t major;
    uint16_t minor;
    uint32_t count;
    uint32_t reserved;
    uint32_t target_offset;
    uint32_t target_bytes;
    uint32_t texture_offset;
    uint32_t texture_bytes;
    uint16_t texture_width;
    uint16_t texture_height;
    uint32_t light_offset;
    uint32_t light_bytes;
    uint16_t light_width;
    uint16_t light_height;
    int16_t light_origin_s;
    int16_t light_origin_t;
    uint32_t colormap_offset;
    uint32_t colormap_bytes;
    uint16_t colormap_levels;
    uint8_t light_shift;
    uint8_t flags;
    uint32_t wrap_s;
    uint32_t wrap_t;
} pxa_game_render_spans_header_wire_t;
static inline int pxa_game_render_spans_header_decode(const uint8_t *p, size_t n,
    pxa_game_render_spans_header_wire_t *v) {
    if (!p || !v || n < 72u) return 0;
    v->magic = (uint32_t)pxa_wire_generated_load_u32(p + 0);
    v->major = (uint16_t)pxa_wire_generated_load_u16(p + 4);
    v->minor = (uint16_t)pxa_wire_generated_load_u16(p + 6);
    v->count = (uint32_t)pxa_wire_generated_load_u32(p + 8);
    v->reserved = (uint32_t)pxa_wire_generated_load_u32(p + 12);
    v->target_offset = (uint32_t)pxa_wire_generated_load_u32(p + 16);
    v->target_bytes = (uint32_t)pxa_wire_generated_load_u32(p + 20);
    v->texture_offset = (uint32_t)pxa_wire_generated_load_u32(p + 24);
    v->texture_bytes = (uint32_t)pxa_wire_generated_load_u32(p + 28);
    v->texture_width = (uint16_t)pxa_wire_generated_load_u16(p + 32);
    v->texture_height = (uint16_t)pxa_wire_generated_load_u16(p + 34);
    v->light_offset = (uint32_t)pxa_wire_generated_load_u32(p + 36);
    v->light_bytes = (uint32_t)pxa_wire_generated_load_u32(p + 40);
    v->light_width = (uint16_t)pxa_wire_generated_load_u16(p + 44);
    v->light_height = (uint16_t)pxa_wire_generated_load_u16(p + 46);
    v->light_origin_s = (int16_t)pxa_wire_generated_load_u16(p + 48);
    v->light_origin_t = (int16_t)pxa_wire_generated_load_u16(p + 50);
    v->colormap_offset = (uint32_t)pxa_wire_generated_load_u32(p + 52);
    v->colormap_bytes = (uint32_t)pxa_wire_generated_load_u32(p + 56);
    v->colormap_levels = (uint16_t)pxa_wire_generated_load_u16(p + 60);
    v->light_shift = (uint8_t)p[62];
    v->flags = (uint8_t)p[63];
    v->wrap_s = (uint32_t)pxa_wire_generated_load_u32(p + 64);
    v->wrap_t = (uint32_t)pxa_wire_generated_load_u32(p + 68);
    return 1;
}
static inline int pxa_game_render_spans_header_encode(uint8_t *p, size_t n,
    const pxa_game_render_spans_header_wire_t *v) {
    if (!p || !v || n < 72u) return 0;
    pxa_wire_generated_store_u32(p + 0, (uint32_t)v->magic);
    pxa_wire_generated_store_u16(p + 4, (uint16_t)v->major);
    pxa_wire_generated_store_u16(p + 6, (uint16_t)v->minor);
    pxa_wire_generated_store_u32(p + 8, (uint32_t)v->count);
    pxa_wire_generated_store_u32(p + 12, (uint32_t)v->reserved);
    pxa_wire_generated_store_u32(p + 16, (uint32_t)v->target_offset);
    pxa_wire_generated_store_u32(p + 20, (uint32_t)v->target_bytes);
    pxa_wire_generated_store_u32(p + 24, (uint32_t)v->texture_offset);
    pxa_wire_generated_store_u32(p + 28, (uint32_t)v->texture_bytes);
    pxa_wire_generated_store_u16(p + 32, (uint16_t)v->texture_width);
    pxa_wire_generated_store_u16(p + 34, (uint16_t)v->texture_height);
    pxa_wire_generated_store_u32(p + 36, (uint32_t)v->light_offset);
    pxa_wire_generated_store_u32(p + 40, (uint32_t)v->light_bytes);
    pxa_wire_generated_store_u16(p + 44, (uint16_t)v->light_width);
    pxa_wire_generated_store_u16(p + 46, (uint16_t)v->light_height);
    pxa_wire_generated_store_u16(p + 48, (uint16_t)v->light_origin_s);
    pxa_wire_generated_store_u16(p + 50, (uint16_t)v->light_origin_t);
    pxa_wire_generated_store_u32(p + 52, (uint32_t)v->colormap_offset);
    pxa_wire_generated_store_u32(p + 56, (uint32_t)v->colormap_bytes);
    pxa_wire_generated_store_u16(p + 60, (uint16_t)v->colormap_levels);
    p[62] = v->light_shift;
    p[63] = v->flags;
    pxa_wire_generated_store_u32(p + 64, (uint32_t)v->wrap_s);
    pxa_wire_generated_store_u32(p + 68, (uint32_t)v->wrap_t);
    return 1;
}
typedef struct {
    uint32_t destination;
    uint32_t count;
    int32_t s;
    int32_t t;
    int32_t ds;
    int32_t dt;
} pxa_game_render_span_wire_t;
static inline int pxa_game_render_span_decode(const uint8_t *p, size_t n,
    pxa_game_render_span_wire_t *v) {
    if (!p || !v || n < 24u) return 0;
    v->destination = (uint32_t)pxa_wire_generated_load_u32(p + 0);
    v->count = (uint32_t)pxa_wire_generated_load_u32(p + 4);
    v->s = (int32_t)pxa_wire_generated_load_u32(p + 8);
    v->t = (int32_t)pxa_wire_generated_load_u32(p + 12);
    v->ds = (int32_t)pxa_wire_generated_load_u32(p + 16);
    v->dt = (int32_t)pxa_wire_generated_load_u32(p + 20);
    return 1;
}
static inline int pxa_game_render_span_encode(uint8_t *p, size_t n,
    const pxa_game_render_span_wire_t *v) {
    if (!p || !v || n < 24u) return 0;
    pxa_wire_generated_store_u32(p + 0, (uint32_t)v->destination);
    pxa_wire_generated_store_u32(p + 4, (uint32_t)v->count);
    pxa_wire_generated_store_u32(p + 8, (uint32_t)v->s);
    pxa_wire_generated_store_u32(p + 12, (uint32_t)v->t);
    pxa_wire_generated_store_u32(p + 16, (uint32_t)v->ds);
    pxa_wire_generated_store_u32(p + 20, (uint32_t)v->dt);
    return 1;
}
typedef struct {
    uint32_t s_origin_bits;
    uint32_t s_du_bits;
    uint32_t s_dv_bits;
    uint32_t t_origin_bits;
    uint32_t t_du_bits;
    uint32_t t_dv_bits;
    uint32_t z_origin_bits;
    uint32_t z_du_bits;
    uint32_t z_dv_bits;
    int32_t s_adjust;
    int32_t t_adjust;
    int32_t s_max;
    int32_t t_max;
    int32_t s_offset;
    int32_t t_offset;
    uint32_t pitch;
    uint32_t depth_offset;
    uint32_t depth_bytes;
    uint32_t depth_pitch;
    uint16_t flags;
    uint16_t reserved;
} pxa_game_render_projection_wire_t;
static inline int pxa_game_render_projection_decode(const uint8_t *p, size_t n,
    pxa_game_render_projection_wire_t *v) {
    if (!p || !v || n < 80u) return 0;
    v->s_origin_bits = (uint32_t)pxa_wire_generated_load_u32(p + 0);
    v->s_du_bits = (uint32_t)pxa_wire_generated_load_u32(p + 4);
    v->s_dv_bits = (uint32_t)pxa_wire_generated_load_u32(p + 8);
    v->t_origin_bits = (uint32_t)pxa_wire_generated_load_u32(p + 12);
    v->t_du_bits = (uint32_t)pxa_wire_generated_load_u32(p + 16);
    v->t_dv_bits = (uint32_t)pxa_wire_generated_load_u32(p + 20);
    v->z_origin_bits = (uint32_t)pxa_wire_generated_load_u32(p + 24);
    v->z_du_bits = (uint32_t)pxa_wire_generated_load_u32(p + 28);
    v->z_dv_bits = (uint32_t)pxa_wire_generated_load_u32(p + 32);
    v->s_adjust = (int32_t)pxa_wire_generated_load_u32(p + 36);
    v->t_adjust = (int32_t)pxa_wire_generated_load_u32(p + 40);
    v->s_max = (int32_t)pxa_wire_generated_load_u32(p + 44);
    v->t_max = (int32_t)pxa_wire_generated_load_u32(p + 48);
    v->s_offset = (int32_t)pxa_wire_generated_load_u32(p + 52);
    v->t_offset = (int32_t)pxa_wire_generated_load_u32(p + 56);
    v->pitch = (uint32_t)pxa_wire_generated_load_u32(p + 60);
    v->depth_offset = (uint32_t)pxa_wire_generated_load_u32(p + 64);
    v->depth_bytes = (uint32_t)pxa_wire_generated_load_u32(p + 68);
    v->depth_pitch = (uint32_t)pxa_wire_generated_load_u32(p + 72);
    v->flags = (uint16_t)pxa_wire_generated_load_u16(p + 76);
    v->reserved = (uint16_t)pxa_wire_generated_load_u16(p + 78);
    return 1;
}
static inline int pxa_game_render_projection_encode(uint8_t *p, size_t n,
    const pxa_game_render_projection_wire_t *v) {
    if (!p || !v || n < 80u) return 0;
    pxa_wire_generated_store_u32(p + 0, (uint32_t)v->s_origin_bits);
    pxa_wire_generated_store_u32(p + 4, (uint32_t)v->s_du_bits);
    pxa_wire_generated_store_u32(p + 8, (uint32_t)v->s_dv_bits);
    pxa_wire_generated_store_u32(p + 12, (uint32_t)v->t_origin_bits);
    pxa_wire_generated_store_u32(p + 16, (uint32_t)v->t_du_bits);
    pxa_wire_generated_store_u32(p + 20, (uint32_t)v->t_dv_bits);
    pxa_wire_generated_store_u32(p + 24, (uint32_t)v->z_origin_bits);
    pxa_wire_generated_store_u32(p + 28, (uint32_t)v->z_du_bits);
    pxa_wire_generated_store_u32(p + 32, (uint32_t)v->z_dv_bits);
    pxa_wire_generated_store_u32(p + 36, (uint32_t)v->s_adjust);
    pxa_wire_generated_store_u32(p + 40, (uint32_t)v->t_adjust);
    pxa_wire_generated_store_u32(p + 44, (uint32_t)v->s_max);
    pxa_wire_generated_store_u32(p + 48, (uint32_t)v->t_max);
    pxa_wire_generated_store_u32(p + 52, (uint32_t)v->s_offset);
    pxa_wire_generated_store_u32(p + 56, (uint32_t)v->t_offset);
    pxa_wire_generated_store_u32(p + 60, (uint32_t)v->pitch);
    pxa_wire_generated_store_u32(p + 64, (uint32_t)v->depth_offset);
    pxa_wire_generated_store_u32(p + 68, (uint32_t)v->depth_bytes);
    pxa_wire_generated_store_u32(p + 72, (uint32_t)v->depth_pitch);
    pxa_wire_generated_store_u16(p + 76, (uint16_t)v->flags);
    pxa_wire_generated_store_u16(p + 78, (uint16_t)v->reserved);
    return 1;
}
typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t count;
    uint16_t reserved;
} pxa_game_render_row_wire_t;
static inline int pxa_game_render_row_decode(const uint8_t *p, size_t n,
    pxa_game_render_row_wire_t *v) {
    if (!p || !v || n < 8u) return 0;
    v->x = (uint16_t)pxa_wire_generated_load_u16(p + 0);
    v->y = (uint16_t)pxa_wire_generated_load_u16(p + 2);
    v->count = (uint16_t)pxa_wire_generated_load_u16(p + 4);
    v->reserved = (uint16_t)pxa_wire_generated_load_u16(p + 6);
    return 1;
}
static inline int pxa_game_render_row_encode(uint8_t *p, size_t n,
    const pxa_game_render_row_wire_t *v) {
    if (!p || !v || n < 8u) return 0;
    pxa_wire_generated_store_u16(p + 0, (uint16_t)v->x);
    pxa_wire_generated_store_u16(p + 2, (uint16_t)v->y);
    pxa_wire_generated_store_u16(p + 4, (uint16_t)v->count);
    pxa_wire_generated_store_u16(p + 6, (uint16_t)v->reserved);
    return 1;
}
#endif /* shared Host/Guest definitions */
#endif
