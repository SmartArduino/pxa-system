/* Generated from pxa-game-render.json. Do not edit. */
#ifndef PXA_CPP_GAME_RENDER_COMPUTE_WIRE_GENERATED_HPP
#define PXA_CPP_GAME_RENDER_COMPUTE_WIRE_GENERATED_HPP
#include "core.hpp"
#include <stdint.h>
namespace pxa::game::wire_compute {
#ifndef PXA_CPP_GAME_RENDER_COMPUTE_WIRE_SHARED_H
#define PXA_CPP_GAME_RENDER_COMPUTE_WIRE_SHARED_H
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
struct pxa_game_render_tiles_header_wire_t {
    uint32_t magic = {};
    uint16_t major = {};
    uint16_t minor = {};
    uint32_t count = {};
    uint32_t command_bytes = {};
    uint32_t color_offset = {};
    uint32_t priority_offset = {};
    uint32_t pixel_count = {};
    uint32_t pitch = {};
};
static inline int pxa_game_render_tiles_header_decode(const std::byte *p, size_t n,
    pxa_game_render_tiles_header_wire_t *v) {
    if (!p || !v || n < 32u) return 0;
    v->magic = (uint32_t)wire::get32(p + 0);
    v->major = (uint16_t)wire::get16(p + 4);
    v->minor = (uint16_t)wire::get16(p + 6);
    v->count = (uint32_t)wire::get32(p + 8);
    v->command_bytes = (uint32_t)wire::get32(p + 12);
    v->color_offset = (uint32_t)wire::get32(p + 16);
    v->priority_offset = (uint32_t)wire::get32(p + 20);
    v->pixel_count = (uint32_t)wire::get32(p + 24);
    v->pitch = (uint32_t)wire::get32(p + 28);
    return 1;
}
static inline int pxa_game_render_tiles_header_encode(std::byte *p, size_t n,
    const pxa_game_render_tiles_header_wire_t *v) {
    if (!p || !v || n < 32u) return 0;
#if defined(PXA_GAME_RENDER_WASM_PACKED_CODEC) && PXA_GAME_RENDER_WASM_PACKED_CODEC && defined(__wasm__) && defined(__clang__) && (defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L))
    static_assert(sizeof(pxa_game_render_tiles_header_wire_t) == 32u, "wire size");
    static_assert(offsetof(pxa_game_render_tiles_header_wire_t, magic) == 0u, "wire offset");
    static_assert(offsetof(pxa_game_render_tiles_header_wire_t, major) == 4u, "wire offset");
    static_assert(offsetof(pxa_game_render_tiles_header_wire_t, minor) == 6u, "wire offset");
    static_assert(offsetof(pxa_game_render_tiles_header_wire_t, count) == 8u, "wire offset");
    static_assert(offsetof(pxa_game_render_tiles_header_wire_t, command_bytes) == 12u, "wire offset");
    static_assert(offsetof(pxa_game_render_tiles_header_wire_t, color_offset) == 16u, "wire offset");
    static_assert(offsetof(pxa_game_render_tiles_header_wire_t, priority_offset) == 20u, "wire offset");
    static_assert(offsetof(pxa_game_render_tiles_header_wire_t, pixel_count) == 24u, "wire offset");
    static_assert(offsetof(pxa_game_render_tiles_header_wire_t, pitch) == 28u, "wire offset");
    pxa_game_render_wire_copy(p, v, 32u);
    return 1;
#endif
    wire::put32(p + 0, (uint32_t)v->magic);
    wire::put16(p + 4, (uint16_t)v->major);
    wire::put16(p + 6, (uint16_t)v->minor);
    wire::put32(p + 8, (uint32_t)v->count);
    wire::put32(p + 12, (uint32_t)v->command_bytes);
    wire::put32(p + 16, (uint32_t)v->color_offset);
    wire::put32(p + 20, (uint32_t)v->priority_offset);
    wire::put32(p + 24, (uint32_t)v->pixel_count);
    wire::put32(p + 28, (uint32_t)v->pitch);
    return 1;
}
struct pxa_game_render_tile_wire_t {
    uint32_t destination = {};
    uint32_t tile_offset = {};
    uint32_t palette_offset = {};
    uint16_t palette_entries = {};
    uint8_t source_x = {};
    uint8_t width = {};
    uint8_t source_y = {};
    uint8_t height = {};
    uint8_t flags = {};
    uint8_t compare_depth = {};
    uint8_t write_depth = {};
    uint8_t reserved0 = {};
    uint16_t reserved1 = {};
};
static inline int pxa_game_render_tile_decode(const std::byte *p, size_t n,
    pxa_game_render_tile_wire_t *v) {
    if (!p || !v || n < 24u) return 0;
    v->destination = (uint32_t)wire::get32(p + 0);
    v->tile_offset = (uint32_t)wire::get32(p + 4);
    v->palette_offset = (uint32_t)wire::get32(p + 8);
    v->palette_entries = (uint16_t)wire::get16(p + 12);
    v->source_x = std::to_integer<uint8_t>(p[14]);
    v->width = std::to_integer<uint8_t>(p[15]);
    v->source_y = std::to_integer<uint8_t>(p[16]);
    v->height = std::to_integer<uint8_t>(p[17]);
    v->flags = std::to_integer<uint8_t>(p[18]);
    v->compare_depth = std::to_integer<uint8_t>(p[19]);
    v->write_depth = std::to_integer<uint8_t>(p[20]);
    v->reserved0 = std::to_integer<uint8_t>(p[21]);
    v->reserved1 = (uint16_t)wire::get16(p + 22);
    return 1;
}
static inline int pxa_game_render_tile_encode(std::byte *p, size_t n,
    const pxa_game_render_tile_wire_t *v) {
    if (!p || !v || n < 24u) return 0;
#if defined(PXA_GAME_RENDER_WASM_PACKED_CODEC) && PXA_GAME_RENDER_WASM_PACKED_CODEC && defined(__wasm__) && defined(__clang__) && (defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L))
    static_assert(sizeof(pxa_game_render_tile_wire_t) == 24u, "wire size");
    static_assert(offsetof(pxa_game_render_tile_wire_t, destination) == 0u, "wire offset");
    static_assert(offsetof(pxa_game_render_tile_wire_t, tile_offset) == 4u, "wire offset");
    static_assert(offsetof(pxa_game_render_tile_wire_t, palette_offset) == 8u, "wire offset");
    static_assert(offsetof(pxa_game_render_tile_wire_t, palette_entries) == 12u, "wire offset");
    static_assert(offsetof(pxa_game_render_tile_wire_t, source_x) == 14u, "wire offset");
    static_assert(offsetof(pxa_game_render_tile_wire_t, width) == 15u, "wire offset");
    static_assert(offsetof(pxa_game_render_tile_wire_t, source_y) == 16u, "wire offset");
    static_assert(offsetof(pxa_game_render_tile_wire_t, height) == 17u, "wire offset");
    static_assert(offsetof(pxa_game_render_tile_wire_t, flags) == 18u, "wire offset");
    static_assert(offsetof(pxa_game_render_tile_wire_t, compare_depth) == 19u, "wire offset");
    static_assert(offsetof(pxa_game_render_tile_wire_t, write_depth) == 20u, "wire offset");
    static_assert(offsetof(pxa_game_render_tile_wire_t, reserved0) == 21u, "wire offset");
    static_assert(offsetof(pxa_game_render_tile_wire_t, reserved1) == 22u, "wire offset");
    pxa_game_render_wire_copy(p, v, 24u);
    return 1;
#endif
    wire::put32(p + 0, (uint32_t)v->destination);
    wire::put32(p + 4, (uint32_t)v->tile_offset);
    wire::put32(p + 8, (uint32_t)v->palette_offset);
    wire::put16(p + 12, (uint16_t)v->palette_entries);
    p[14] = std::byte(v->source_x);
    p[15] = std::byte(v->width);
    p[16] = std::byte(v->source_y);
    p[17] = std::byte(v->height);
    p[18] = std::byte(v->flags);
    p[19] = std::byte(v->compare_depth);
    p[20] = std::byte(v->write_depth);
    p[21] = std::byte(v->reserved0);
    wire::put16(p + 22, (uint16_t)v->reserved1);
    return 1;
}
struct pxa_game_render_blit_wire_t {
    uint32_t source_offset = {};
    uint32_t source_bytes = {};
    uint32_t palette_offset = {};
    uint32_t palette_bytes = {};
    uint32_t target_offset = {};
    uint32_t target_bytes = {};
    uint16_t source_width = {};
    uint16_t source_height = {};
    uint32_t source_pitch = {};
    uint16_t target_width = {};
    uint16_t target_height = {};
    uint32_t target_pitch = {};
    uint32_t cursor_offset = {};
    uint32_t cursor_bytes = {};
    int32_t cursor_x = {};
    int32_t cursor_y = {};
    uint16_t cursor_width = {};
    uint16_t cursor_height = {};
    uint32_t cursor_pitch = {};
};
static inline int pxa_game_render_blit_decode(const std::byte *p, size_t n,
    pxa_game_render_blit_wire_t *v) {
    if (!p || !v || n < 64u) return 0;
    v->source_offset = (uint32_t)wire::get32(p + 0);
    v->source_bytes = (uint32_t)wire::get32(p + 4);
    v->palette_offset = (uint32_t)wire::get32(p + 8);
    v->palette_bytes = (uint32_t)wire::get32(p + 12);
    v->target_offset = (uint32_t)wire::get32(p + 16);
    v->target_bytes = (uint32_t)wire::get32(p + 20);
    v->source_width = (uint16_t)wire::get16(p + 24);
    v->source_height = (uint16_t)wire::get16(p + 26);
    v->source_pitch = (uint32_t)wire::get32(p + 28);
    v->target_width = (uint16_t)wire::get16(p + 32);
    v->target_height = (uint16_t)wire::get16(p + 34);
    v->target_pitch = (uint32_t)wire::get32(p + 36);
    v->cursor_offset = (uint32_t)wire::get32(p + 40);
    v->cursor_bytes = (uint32_t)wire::get32(p + 44);
    v->cursor_x = (int32_t)wire::get32(p + 48);
    v->cursor_y = (int32_t)wire::get32(p + 52);
    v->cursor_width = (uint16_t)wire::get16(p + 56);
    v->cursor_height = (uint16_t)wire::get16(p + 58);
    v->cursor_pitch = (uint32_t)wire::get32(p + 60);
    return 1;
}
static inline int pxa_game_render_blit_encode(std::byte *p, size_t n,
    const pxa_game_render_blit_wire_t *v) {
    if (!p || !v || n < 64u) return 0;
#if defined(PXA_GAME_RENDER_WASM_PACKED_CODEC) && PXA_GAME_RENDER_WASM_PACKED_CODEC && defined(__wasm__) && defined(__clang__) && (defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L))
    static_assert(sizeof(pxa_game_render_blit_wire_t) == 64u, "wire size");
    static_assert(offsetof(pxa_game_render_blit_wire_t, source_offset) == 0u, "wire offset");
    static_assert(offsetof(pxa_game_render_blit_wire_t, source_bytes) == 4u, "wire offset");
    static_assert(offsetof(pxa_game_render_blit_wire_t, palette_offset) == 8u, "wire offset");
    static_assert(offsetof(pxa_game_render_blit_wire_t, palette_bytes) == 12u, "wire offset");
    static_assert(offsetof(pxa_game_render_blit_wire_t, target_offset) == 16u, "wire offset");
    static_assert(offsetof(pxa_game_render_blit_wire_t, target_bytes) == 20u, "wire offset");
    static_assert(offsetof(pxa_game_render_blit_wire_t, source_width) == 24u, "wire offset");
    static_assert(offsetof(pxa_game_render_blit_wire_t, source_height) == 26u, "wire offset");
    static_assert(offsetof(pxa_game_render_blit_wire_t, source_pitch) == 28u, "wire offset");
    static_assert(offsetof(pxa_game_render_blit_wire_t, target_width) == 32u, "wire offset");
    static_assert(offsetof(pxa_game_render_blit_wire_t, target_height) == 34u, "wire offset");
    static_assert(offsetof(pxa_game_render_blit_wire_t, target_pitch) == 36u, "wire offset");
    static_assert(offsetof(pxa_game_render_blit_wire_t, cursor_offset) == 40u, "wire offset");
    static_assert(offsetof(pxa_game_render_blit_wire_t, cursor_bytes) == 44u, "wire offset");
    static_assert(offsetof(pxa_game_render_blit_wire_t, cursor_x) == 48u, "wire offset");
    static_assert(offsetof(pxa_game_render_blit_wire_t, cursor_y) == 52u, "wire offset");
    static_assert(offsetof(pxa_game_render_blit_wire_t, cursor_width) == 56u, "wire offset");
    static_assert(offsetof(pxa_game_render_blit_wire_t, cursor_height) == 58u, "wire offset");
    static_assert(offsetof(pxa_game_render_blit_wire_t, cursor_pitch) == 60u, "wire offset");
    pxa_game_render_wire_copy(p, v, 64u);
    return 1;
#endif
    wire::put32(p + 0, (uint32_t)v->source_offset);
    wire::put32(p + 4, (uint32_t)v->source_bytes);
    wire::put32(p + 8, (uint32_t)v->palette_offset);
    wire::put32(p + 12, (uint32_t)v->palette_bytes);
    wire::put32(p + 16, (uint32_t)v->target_offset);
    wire::put32(p + 20, (uint32_t)v->target_bytes);
    wire::put16(p + 24, (uint16_t)v->source_width);
    wire::put16(p + 26, (uint16_t)v->source_height);
    wire::put32(p + 28, (uint32_t)v->source_pitch);
    wire::put16(p + 32, (uint16_t)v->target_width);
    wire::put16(p + 34, (uint16_t)v->target_height);
    wire::put32(p + 36, (uint32_t)v->target_pitch);
    wire::put32(p + 40, (uint32_t)v->cursor_offset);
    wire::put32(p + 44, (uint32_t)v->cursor_bytes);
    wire::put32(p + 48, (uint32_t)v->cursor_x);
    wire::put32(p + 52, (uint32_t)v->cursor_y);
    wire::put16(p + 56, (uint16_t)v->cursor_width);
    wire::put16(p + 58, (uint16_t)v->cursor_height);
    wire::put32(p + 60, (uint32_t)v->cursor_pitch);
    return 1;
}
#endif /* shared Host/Guest definitions */
} // namespace pxa::game::wire_compute
#endif
