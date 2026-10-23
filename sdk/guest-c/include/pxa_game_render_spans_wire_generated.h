/* Generated from pxa-game-render.json. Do not edit. */
#ifndef PXA_GUEST_GAME_RENDER_SPANS_WIRE_GENERATED_H
#define PXA_GUEST_GAME_RENDER_SPANS_WIRE_GENERATED_H
#include "pxa_core.h"
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
#define PXA_GAME_RENDER_TILES_MAGIC UINT32_C(1280596048)
#define PXA_GAME_RENDER_TILES_HEADER_BYTES UINT32_C(32)
#define PXA_GAME_RENDER_TILES_COMMAND_BYTES UINT32_C(24)
#define PXA_GAME_RENDER_TILES_MAX_PACKET_BYTES UINT32_C(262144)
#define PXA_GAME_RENDER_TILES_MAX_TILES UINT32_C(4096)
#define PXA_GAME_RENDER_TILES_FLIP_X 1u
#define PXA_GAME_RENDER_TILES_FLIP_Y 2u
#if defined(PXA_GAME_RENDER_WASM_PACKED_CODEC) && PXA_GAME_RENDER_WASM_PACKED_CODEC && defined(__wasm__) && defined(__clang__) && (defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L))
/* Only the WASM little-endian object layout is eligible. Each
 * encoder proves every field offset and the complete size. */
__attribute__((target("bulk-memory"), noinline))
static void pxa_game_render_wire_copy(void *to, const void *from, size_t bytes) {
    __builtin_memmove(to, from, bytes);
}
#endif
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
#if defined(PXA_GAME_RENDER_WASM_PACKED_CODEC) && PXA_GAME_RENDER_WASM_PACKED_CODEC && defined(__wasm__) && defined(__clang__) && (defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L))
    _Static_assert(sizeof(pxa_game_render_tiles_header_wire_t) == 32u, "wire size");
    _Static_assert(offsetof(pxa_game_render_tiles_header_wire_t, magic) == 0u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_tiles_header_wire_t, major) == 4u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_tiles_header_wire_t, minor) == 6u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_tiles_header_wire_t, count) == 8u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_tiles_header_wire_t, command_bytes) == 12u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_tiles_header_wire_t, color_offset) == 16u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_tiles_header_wire_t, priority_offset) == 20u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_tiles_header_wire_t, pixel_count) == 24u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_tiles_header_wire_t, pitch) == 28u, "wire offset");
    pxa_game_render_wire_copy(p, v, 32u);
    return 1;
#endif
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
#if defined(PXA_GAME_RENDER_WASM_PACKED_CODEC) && PXA_GAME_RENDER_WASM_PACKED_CODEC && defined(__wasm__) && defined(__clang__) && (defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L))
    _Static_assert(sizeof(pxa_game_render_tile_wire_t) == 24u, "wire size");
    _Static_assert(offsetof(pxa_game_render_tile_wire_t, destination) == 0u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_tile_wire_t, tile_offset) == 4u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_tile_wire_t, palette_offset) == 8u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_tile_wire_t, palette_entries) == 12u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_tile_wire_t, source_x) == 14u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_tile_wire_t, width) == 15u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_tile_wire_t, source_y) == 16u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_tile_wire_t, height) == 17u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_tile_wire_t, flags) == 18u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_tile_wire_t, compare_depth) == 19u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_tile_wire_t, write_depth) == 20u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_tile_wire_t, reserved0) == 21u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_tile_wire_t, reserved1) == 22u, "wire offset");
    pxa_game_render_wire_copy(p, v, 24u);
    return 1;
#endif
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
#if defined(PXA_GAME_RENDER_WASM_PACKED_CODEC) && PXA_GAME_RENDER_WASM_PACKED_CODEC && defined(__wasm__) && defined(__clang__) && (defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L))
    _Static_assert(sizeof(pxa_game_render_blit_wire_t) == 64u, "wire size");
    _Static_assert(offsetof(pxa_game_render_blit_wire_t, source_offset) == 0u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_blit_wire_t, source_bytes) == 4u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_blit_wire_t, palette_offset) == 8u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_blit_wire_t, palette_bytes) == 12u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_blit_wire_t, target_offset) == 16u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_blit_wire_t, target_bytes) == 20u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_blit_wire_t, source_width) == 24u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_blit_wire_t, source_height) == 26u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_blit_wire_t, source_pitch) == 28u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_blit_wire_t, target_width) == 32u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_blit_wire_t, target_height) == 34u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_blit_wire_t, target_pitch) == 36u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_blit_wire_t, cursor_offset) == 40u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_blit_wire_t, cursor_bytes) == 44u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_blit_wire_t, cursor_x) == 48u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_blit_wire_t, cursor_y) == 52u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_blit_wire_t, cursor_width) == 56u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_blit_wire_t, cursor_height) == 58u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_blit_wire_t, cursor_pitch) == 60u, "wire offset");
    pxa_game_render_wire_copy(p, v, 64u);
    return 1;
#endif
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
#if defined(PXA_GAME_RENDER_WASM_PACKED_CODEC) && PXA_GAME_RENDER_WASM_PACKED_CODEC && defined(__wasm__) && defined(__clang__) && (defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L))
    _Static_assert(sizeof(pxa_game_render_frame_header_wire_t) == 16u, "wire size");
    _Static_assert(offsetof(pxa_game_render_frame_header_wire_t, magic) == 0u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_frame_header_wire_t, major) == 4u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_frame_header_wire_t, minor) == 6u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_frame_header_wire_t, count) == 8u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_frame_header_wire_t, command_bytes) == 12u, "wire offset");
    pxa_game_render_wire_copy(p, v, 16u);
    return 1;
#endif
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
#if defined(PXA_GAME_RENDER_WASM_PACKED_CODEC) && PXA_GAME_RENDER_WASM_PACKED_CODEC && defined(__wasm__) && defined(__clang__) && (defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L))
    _Static_assert(sizeof(pxa_game_render_spans_header_wire_t) == 72u, "wire size");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, magic) == 0u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, major) == 4u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, minor) == 6u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, count) == 8u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, reserved) == 12u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, target_offset) == 16u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, target_bytes) == 20u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, texture_offset) == 24u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, texture_bytes) == 28u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, texture_width) == 32u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, texture_height) == 34u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, light_offset) == 36u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, light_bytes) == 40u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, light_width) == 44u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, light_height) == 46u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, light_origin_s) == 48u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, light_origin_t) == 50u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, colormap_offset) == 52u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, colormap_bytes) == 56u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, colormap_levels) == 60u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, light_shift) == 62u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, flags) == 63u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, wrap_s) == 64u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_spans_header_wire_t, wrap_t) == 68u, "wire offset");
    pxa_game_render_wire_copy(p, v, 72u);
    return 1;
#endif
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
#if defined(PXA_GAME_RENDER_WASM_PACKED_CODEC) && PXA_GAME_RENDER_WASM_PACKED_CODEC && defined(__wasm__) && defined(__clang__) && (defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L))
    _Static_assert(sizeof(pxa_game_render_span_wire_t) == 24u, "wire size");
    _Static_assert(offsetof(pxa_game_render_span_wire_t, destination) == 0u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_span_wire_t, count) == 4u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_span_wire_t, s) == 8u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_span_wire_t, t) == 12u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_span_wire_t, ds) == 16u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_span_wire_t, dt) == 20u, "wire offset");
    pxa_game_render_wire_copy(p, v, 24u);
    return 1;
#endif
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
#if defined(PXA_GAME_RENDER_WASM_PACKED_CODEC) && PXA_GAME_RENDER_WASM_PACKED_CODEC && defined(__wasm__) && defined(__clang__) && (defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L))
    _Static_assert(sizeof(pxa_game_render_projection_wire_t) == 80u, "wire size");
    _Static_assert(offsetof(pxa_game_render_projection_wire_t, s_origin_bits) == 0u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_projection_wire_t, s_du_bits) == 4u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_projection_wire_t, s_dv_bits) == 8u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_projection_wire_t, t_origin_bits) == 12u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_projection_wire_t, t_du_bits) == 16u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_projection_wire_t, t_dv_bits) == 20u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_projection_wire_t, z_origin_bits) == 24u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_projection_wire_t, z_du_bits) == 28u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_projection_wire_t, z_dv_bits) == 32u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_projection_wire_t, s_adjust) == 36u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_projection_wire_t, t_adjust) == 40u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_projection_wire_t, s_max) == 44u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_projection_wire_t, t_max) == 48u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_projection_wire_t, s_offset) == 52u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_projection_wire_t, t_offset) == 56u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_projection_wire_t, pitch) == 60u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_projection_wire_t, depth_offset) == 64u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_projection_wire_t, depth_bytes) == 68u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_projection_wire_t, depth_pitch) == 72u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_projection_wire_t, flags) == 76u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_projection_wire_t, reserved) == 78u, "wire offset");
    pxa_game_render_wire_copy(p, v, 80u);
    return 1;
#endif
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
static inline int pxa_game_render_rows_encode(uint8_t *p, size_t n,
    const pxa_game_render_row_wire_t *rows, uint32_t count) {
    if ((uint64_t)count * PXA_GAME_RENDER_ROW_BYTES > n || (count && (!p || !rows))) return 0;
    if (!count) return 1;
#if defined(PXA_GAME_RENDER_WASM_PACKED_CODEC) && PXA_GAME_RENDER_WASM_PACKED_CODEC && defined(__wasm__) && defined(__clang__) && (defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L))
    _Static_assert(sizeof(pxa_game_render_row_wire_t) == 8u, "wire size");
    _Static_assert(offsetof(pxa_game_render_row_wire_t, x) == 0u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_row_wire_t, y) == 2u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_row_wire_t, count) == 4u, "wire offset");
    _Static_assert(offsetof(pxa_game_render_row_wire_t, reserved) == 6u, "wire offset");
    pxa_game_render_wire_copy(p, rows, (size_t)count * PXA_GAME_RENDER_ROW_BYTES);
    return 1;
#endif
    for (uint32_t i = 0; i < count; ++i)
        pxa_game_render_row_encode(p + (size_t)i * PXA_GAME_RENDER_ROW_BYTES, PXA_GAME_RENDER_ROW_BYTES, rows + i);
    return 1;
}
#endif /* shared Host/Guest definitions */
#endif
