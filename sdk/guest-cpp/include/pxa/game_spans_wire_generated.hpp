/* Generated from pxa-game-render.json. Do not edit. */
#ifndef PXA_CPP_GAME_RENDER_SPANS_WIRE_GENERATED_HPP
#define PXA_CPP_GAME_RENDER_SPANS_WIRE_GENERATED_HPP
#include "core.hpp"
#include <stdint.h>
namespace pxa::game::wire_spans {
#define PXA_GAME_RENDER_SPANS_MAGIC UINT32_C(1347639376)
#define PXA_GAME_RENDER_SPANS_HEADER_BYTES 72u
#define PXA_GAME_RENDER_SPAN_BYTES 24u
#define PXA_GAME_RENDER_SPANS_POW2 1u
#define PXA_GAME_RENDER_SPANS_FULLBRIGHT 2u
#define PXA_GAME_RENDER_SPANS_KNOWN_FLAGS 3u
#ifndef PXA_CPP_GAME_RENDER_SPANS_WIRE_SHARED_H
#define PXA_CPP_GAME_RENDER_SPANS_WIRE_SHARED_H
struct pxa_game_render_spans_header_wire_t {
    uint32_t magic = {};
    uint16_t major = {};
    uint16_t minor = {};
    uint32_t count = {};
    uint32_t reserved = {};
    uint32_t target_offset = {};
    uint32_t target_bytes = {};
    uint32_t texture_offset = {};
    uint32_t texture_bytes = {};
    uint16_t texture_width = {};
    uint16_t texture_height = {};
    uint32_t light_offset = {};
    uint32_t light_bytes = {};
    uint16_t light_width = {};
    uint16_t light_height = {};
    int16_t light_origin_s = {};
    int16_t light_origin_t = {};
    uint32_t colormap_offset = {};
    uint32_t colormap_bytes = {};
    uint16_t colormap_levels = {};
    uint8_t light_shift = {};
    uint8_t flags = {};
    uint32_t wrap_s = {};
    uint32_t wrap_t = {};
};
static inline int pxa_game_render_spans_header_decode(const std::byte *p, size_t n,
    pxa_game_render_spans_header_wire_t *v) {
    if (!p || !v || n < 72u) return 0;
    v->magic = (uint32_t)wire::get32(p + 0);
    v->major = (uint16_t)wire::get16(p + 4);
    v->minor = (uint16_t)wire::get16(p + 6);
    v->count = (uint32_t)wire::get32(p + 8);
    v->reserved = (uint32_t)wire::get32(p + 12);
    v->target_offset = (uint32_t)wire::get32(p + 16);
    v->target_bytes = (uint32_t)wire::get32(p + 20);
    v->texture_offset = (uint32_t)wire::get32(p + 24);
    v->texture_bytes = (uint32_t)wire::get32(p + 28);
    v->texture_width = (uint16_t)wire::get16(p + 32);
    v->texture_height = (uint16_t)wire::get16(p + 34);
    v->light_offset = (uint32_t)wire::get32(p + 36);
    v->light_bytes = (uint32_t)wire::get32(p + 40);
    v->light_width = (uint16_t)wire::get16(p + 44);
    v->light_height = (uint16_t)wire::get16(p + 46);
    v->light_origin_s = (int16_t)wire::get16(p + 48);
    v->light_origin_t = (int16_t)wire::get16(p + 50);
    v->colormap_offset = (uint32_t)wire::get32(p + 52);
    v->colormap_bytes = (uint32_t)wire::get32(p + 56);
    v->colormap_levels = (uint16_t)wire::get16(p + 60);
    v->light_shift = std::to_integer<uint8_t>(p[62]);
    v->flags = std::to_integer<uint8_t>(p[63]);
    v->wrap_s = (uint32_t)wire::get32(p + 64);
    v->wrap_t = (uint32_t)wire::get32(p + 68);
    return 1;
}
static inline int pxa_game_render_spans_header_encode(std::byte *p, size_t n,
    const pxa_game_render_spans_header_wire_t *v) {
    if (!p || !v || n < 72u) return 0;
    wire::put32(p + 0, (uint32_t)v->magic);
    wire::put16(p + 4, (uint16_t)v->major);
    wire::put16(p + 6, (uint16_t)v->minor);
    wire::put32(p + 8, (uint32_t)v->count);
    wire::put32(p + 12, (uint32_t)v->reserved);
    wire::put32(p + 16, (uint32_t)v->target_offset);
    wire::put32(p + 20, (uint32_t)v->target_bytes);
    wire::put32(p + 24, (uint32_t)v->texture_offset);
    wire::put32(p + 28, (uint32_t)v->texture_bytes);
    wire::put16(p + 32, (uint16_t)v->texture_width);
    wire::put16(p + 34, (uint16_t)v->texture_height);
    wire::put32(p + 36, (uint32_t)v->light_offset);
    wire::put32(p + 40, (uint32_t)v->light_bytes);
    wire::put16(p + 44, (uint16_t)v->light_width);
    wire::put16(p + 46, (uint16_t)v->light_height);
    wire::put16(p + 48, (uint16_t)v->light_origin_s);
    wire::put16(p + 50, (uint16_t)v->light_origin_t);
    wire::put32(p + 52, (uint32_t)v->colormap_offset);
    wire::put32(p + 56, (uint32_t)v->colormap_bytes);
    wire::put16(p + 60, (uint16_t)v->colormap_levels);
    p[62] = std::byte(v->light_shift);
    p[63] = std::byte(v->flags);
    wire::put32(p + 64, (uint32_t)v->wrap_s);
    wire::put32(p + 68, (uint32_t)v->wrap_t);
    return 1;
}
struct pxa_game_render_span_wire_t {
    uint32_t destination = {};
    uint32_t count = {};
    int32_t s = {};
    int32_t t = {};
    int32_t ds = {};
    int32_t dt = {};
};
static inline int pxa_game_render_span_decode(const std::byte *p, size_t n,
    pxa_game_render_span_wire_t *v) {
    if (!p || !v || n < 24u) return 0;
    v->destination = (uint32_t)wire::get32(p + 0);
    v->count = (uint32_t)wire::get32(p + 4);
    v->s = (int32_t)wire::get32(p + 8);
    v->t = (int32_t)wire::get32(p + 12);
    v->ds = (int32_t)wire::get32(p + 16);
    v->dt = (int32_t)wire::get32(p + 20);
    return 1;
}
static inline int pxa_game_render_span_encode(std::byte *p, size_t n,
    const pxa_game_render_span_wire_t *v) {
    if (!p || !v || n < 24u) return 0;
    wire::put32(p + 0, (uint32_t)v->destination);
    wire::put32(p + 4, (uint32_t)v->count);
    wire::put32(p + 8, (uint32_t)v->s);
    wire::put32(p + 12, (uint32_t)v->t);
    wire::put32(p + 16, (uint32_t)v->ds);
    wire::put32(p + 20, (uint32_t)v->dt);
    return 1;
}
#endif /* shared Host/Guest definitions */
} // namespace pxa::game::wire_spans
#endif
