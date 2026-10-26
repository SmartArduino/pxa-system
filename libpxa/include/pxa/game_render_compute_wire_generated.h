/* Generated from pxa-game-render.json. Do not edit. */
#ifndef PXA_GAME_RENDER_COMPUTE_WIRE_GENERATED_H
#define PXA_GAME_RENDER_COMPUTE_WIRE_GENERATED_H
#include "pxa/wire.h"
#ifndef PXA_GAME_RENDER_COMPUTE_WIRE_SHARED_H
#define PXA_GAME_RENDER_COMPUTE_WIRE_SHARED_H
#define PXA_GAME_RENDER_BLIT_BYTES 64u
#define PXA_GAME_RENDER_BLIT_MAX_PIXELS UINT32_C(262144)
#define PXA_GAME_RENDER_TILES_MAGIC UINT32_C(1280596048)
#define PXA_GAME_RENDER_TILES_HEADER_BYTES UINT32_C(32)
#define PXA_GAME_RENDER_TILES_COMMAND_BYTES UINT32_C(24)
#define PXA_GAME_RENDER_TILES_MAX_PACKET_BYTES UINT32_C(262144)
#define PXA_GAME_RENDER_TILES_MAX_TILES UINT32_C(4096)
#define PXA_GAME_RENDER_TILES_FLIP_X 1u
#define PXA_GAME_RENDER_TILES_FLIP_Y 2u
typedef struct {
    uint32_t magic;
    uint16_t major;
    uint16_t minor;
    uint32_t count;
    uint32_t command_bytes;
    uint32_t color_offset;
    uint32_t priority_offset;
    uint32_t pixel_count;
    uint32_t pitch;
} pxa_game_render_tiles_header_wire_t;
static inline int pxa_game_render_tiles_header_decode(const uint8_t *p, size_t n,
    pxa_game_render_tiles_header_wire_t *v) {
    if (!p || !v || n < 32u) return 0;
    v->magic = (uint32_t)pxa_wire_generated_load_u32(p + 0);
    v->major = (uint16_t)pxa_wire_generated_load_u16(p + 4);
    v->minor = (uint16_t)pxa_wire_generated_load_u16(p + 6);
    v->count = (uint32_t)pxa_wire_generated_load_u32(p + 8);
    v->command_bytes = (uint32_t)pxa_wire_generated_load_u32(p + 12);
    v->color_offset = (uint32_t)pxa_wire_generated_load_u32(p + 16);
    v->priority_offset = (uint32_t)pxa_wire_generated_load_u32(p + 20);
    v->pixel_count = (uint32_t)pxa_wire_generated_load_u32(p + 24);
    v->pitch = (uint32_t)pxa_wire_generated_load_u32(p + 28);
    return 1;
}
static inline int pxa_game_render_tiles_header_encode(uint8_t *p, size_t n,
    const pxa_game_render_tiles_header_wire_t *v) {
    if (!p || !v || n < 32u) return 0;
    pxa_wire_generated_store_u32(p + 0, (uint32_t)v->magic);
    pxa_wire_generated_store_u16(p + 4, (uint16_t)v->major);
    pxa_wire_generated_store_u16(p + 6, (uint16_t)v->minor);
    pxa_wire_generated_store_u32(p + 8, (uint32_t)v->count);
    pxa_wire_generated_store_u32(p + 12, (uint32_t)v->command_bytes);
    pxa_wire_generated_store_u32(p + 16, (uint32_t)v->color_offset);
    pxa_wire_generated_store_u32(p + 20, (uint32_t)v->priority_offset);
    pxa_wire_generated_store_u32(p + 24, (uint32_t)v->pixel_count);
    pxa_wire_generated_store_u32(p + 28, (uint32_t)v->pitch);
    return 1;
}
typedef struct {
    uint32_t destination;
    uint32_t tile_offset;
    uint32_t palette_offset;
    uint16_t palette_entries;
    uint8_t source_x;
    uint8_t width;
    uint8_t source_y;
    uint8_t height;
    uint8_t flags;
    uint8_t compare_depth;
    uint8_t write_depth;
    uint8_t reserved0;
    uint16_t reserved1;
} pxa_game_render_tile_wire_t;
static inline int pxa_game_render_tile_decode(const uint8_t *p, size_t n,
    pxa_game_render_tile_wire_t *v) {
    if (!p || !v || n < 24u) return 0;
    v->destination = (uint32_t)pxa_wire_generated_load_u32(p + 0);
    v->tile_offset = (uint32_t)pxa_wire_generated_load_u32(p + 4);
    v->palette_offset = (uint32_t)pxa_wire_generated_load_u32(p + 8);
    v->palette_entries = (uint16_t)pxa_wire_generated_load_u16(p + 12);
    v->source_x = (uint8_t)p[14];
    v->width = (uint8_t)p[15];
    v->source_y = (uint8_t)p[16];
    v->height = (uint8_t)p[17];
    v->flags = (uint8_t)p[18];
    v->compare_depth = (uint8_t)p[19];
    v->write_depth = (uint8_t)p[20];
    v->reserved0 = (uint8_t)p[21];
    v->reserved1 = (uint16_t)pxa_wire_generated_load_u16(p + 22);
    return 1;
}
static inline int pxa_game_render_tile_encode(uint8_t *p, size_t n,
    const pxa_game_render_tile_wire_t *v) {
    if (!p || !v || n < 24u) return 0;
    pxa_wire_generated_store_u32(p + 0, (uint32_t)v->destination);
    pxa_wire_generated_store_u32(p + 4, (uint32_t)v->tile_offset);
    pxa_wire_generated_store_u32(p + 8, (uint32_t)v->palette_offset);
    pxa_wire_generated_store_u16(p + 12, (uint16_t)v->palette_entries);
    p[14] = v->source_x;
    p[15] = v->width;
    p[16] = v->source_y;
    p[17] = v->height;
    p[18] = v->flags;
    p[19] = v->compare_depth;
    p[20] = v->write_depth;
    p[21] = v->reserved0;
    pxa_wire_generated_store_u16(p + 22, (uint16_t)v->reserved1);
    return 1;
}
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
#endif /* shared Host/Guest definitions */
#endif
