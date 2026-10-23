/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 1996-1997 id Software, Inc.
 * Copyright (C) 2023-2024 Nicola Wrachien and contributors.
 * Copyright (C) 2026 PXA System contributors.
 * Indexed light-grid spans, adapted from recrof/Quake-ESP32S3's portable
 * d_scan.c lit-span kernel (MG24Quake / Nicola Wrachien and contributors).
 * Keep the 10.6 wrapping, packed vertical light pair and colormap semantics.
 */
#include "raster_spans_internal.h"
#include <limits.h>
#include <string.h>

static int valid_range(uint32_t offset, uint32_t bytes, size_t size) {
    return bytes && offset <= size && bytes <= size - offset;
}

static inline int32_t asr(int32_t value, unsigned bits) {
    /* All supported C backends use arithmetic right shift. Fail at compile
     * time on a backend that cannot implement the wire's floor semantics. */
#if (-1 >> 1) != -1
#error "GameRender requires arithmetic signed right shift"
#endif
    return value >> bits;
}

static inline uint32_t light_word(const uint32_t *p) {
    uint32_t value;
    /* Guest memory is opaque byte storage, not a native uint32_t object. */
    memcpy(&value, p, sizeof(value));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return ((value & 255u) << 24) | ((value & 0xff00u) << 8) |
           ((value >> 8) & 0xff00u) | (value >> 24);
#else
    return value;
#endif
}

pxa_status_t pxa_raster_material_validate(const uint8_t *packet, size_t size,
    uint8_t *memory, size_t memory_size, uint32_t max_spans, uint32_t magic,
    uint32_t prefix, uint32_t record_bytes, pxa_raster_spans_t *output) {
    return pxa_raster_material_validate_sources(packet, size, memory, memory_size,
        max_spans, magic, prefix, record_bytes, memory, memory_size, output);
}

pxa_status_t pxa_raster_material_validate_sources(const uint8_t *packet, size_t size,
    uint8_t *memory, size_t memory_size, uint32_t max_spans, uint32_t magic, uint32_t prefix, uint32_t record_bytes,
    const uint8_t *sources, size_t source_size, pxa_raster_spans_t *output) {
    pxa_game_render_spans_header_wire_t h = {0};
    uint32_t i, light_limit;
    if (!output) return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    if (!packet || !memory || !sources || (uintptr_t)sources > UINTPTR_MAX - source_size ||
        (uintptr_t)packet > UINTPTR_MAX - size ||
        (uintptr_t)memory > UINTPTR_MAX - memory_size ||
        !pxa_game_render_spans_header_decode(packet, size, &h))
        return PXA_STATUS_INVALID_ARGUMENT;
    if (h.magic != magic || h.major != 1 || h.minor)
        return PXA_STATUS_UNSUPPORTED;
    if (h.reserved || !h.count || h.count > max_spans ||
        (uint64_t)size != prefix + (uint64_t)h.count * record_bytes ||
        h.flags & ~PXA_GAME_RENDER_SPANS_KNOWN_FLAGS ||
        !h.texture_width || !h.texture_height ||
        (uint64_t)h.texture_width * h.texture_height != h.texture_bytes ||
        h.light_shift < 1 || h.light_shift > 4 ||
        h.light_width < 2 || h.light_height < 2 ||
        h.light_bytes > 262144u ||
        (uint64_t)h.light_width * h.light_height * 4 != h.light_bytes ||
        !h.colormap_levels || h.colormap_levels > 256 ||
        h.colormap_bytes != (uint32_t)h.colormap_levels * 256 ||
        !valid_range(h.target_offset, h.target_bytes, memory_size) ||
        !valid_range(h.texture_offset, h.texture_bytes, source_size) ||
        !valid_range(h.light_offset, h.light_bytes, source_size) ||
        !valid_range(h.colormap_offset, h.colormap_bytes, source_size) ||
        ((uintptr_t)(sources + h.light_offset) & 3u))
        return PXA_STATUS_INVALID_ARGUMENT;
    if ((h.flags & PXA_GAME_RENDER_SPANS_POW2) &&
        ((h.texture_width & (h.texture_width - 1u)) ||
         (h.texture_height & (h.texture_height - 1u))))
        return PXA_STATUS_INVALID_ARGUMENT;
    light_limit = (((uint32_t)h.colormap_levels << 8) - 1u) >> h.light_shift;
    if (!(h.flags & PXA_GAME_RENDER_SPANS_FULLBRIGHT)) {
        /* The final row's values are already packed into the preceding
         * row's high halves. Its own high halves are never sampled. */
        uint32_t sampled_bytes = (uint32_t)(h.light_height - 1u) * h.light_width * 4u;
        for (i = 0; i < sampled_bytes; i += 4) {
            uint32_t l = pxa_wire_generated_load_u32(sources + h.light_offset + i);
            if ((l & 65535u) > light_limit || (l >> 16) > light_limit)
                return PXA_STATUS_INVALID_ARGUMENT;
        }
    }
    pxa_raster_material_load(&h, packet + prefix, memory, sources, output);
    return PXA_STATUS_OK;
}

void pxa_raster_material_load(const pxa_game_render_spans_header_wire_t *h,
    const uint8_t *records, uint8_t *memory, const uint8_t *sources,
    pxa_raster_spans_t *output) {
    output->records = records;
    output->target = memory + h->target_offset;
    output->texture = sources + h->texture_offset;
    output->lights = (const uint32_t *)(const void *)(sources + h->light_offset);
    output->colormap = sources + h->colormap_offset;
    output->count = h->count; output->target_bytes = h->target_bytes;
    output->first_pixel = UINT32_MAX;
    output->texture_width = h->texture_width; output->texture_height = h->texture_height;
    output->light_width = h->light_width; output->light_height = h->light_height;
    output->texture_bytes = h->texture_bytes; output->light_bytes = h->light_bytes;
    output->colormap_bytes = h->colormap_bytes;
    output->light_origin_s = h->light_origin_s; output->light_origin_t = h->light_origin_t;
    output->wrap_s = h->wrap_s; output->wrap_t = h->wrap_t;
    output->light_shift = h->light_shift; output->flags = h->flags;
}


int pxa_raster_write_disjoint(const pxa_raster_spans_t *b, const uint8_t *destination,
    uint32_t bytes, const uint8_t *packet, size_t size) {
    uintptr_t at = (uintptr_t)destination;
#define DISJOINT(p, n) (at >= (uintptr_t)(p) + (n) || (uintptr_t)(p) >= at + bytes)
    return DISJOINT(b->texture, b->texture_bytes) &&
        DISJOINT(b->lights, b->light_bytes) &&
        DISJOINT(b->colormap, b->colormap_bytes) && DISJOINT(packet, size);
#undef DISJOINT
}

pxa_status_t pxa_raster_span_validate(pxa_raster_spans_t *b,
    const pxa_game_render_span_wire_t *s, const uint8_t *packet, size_t size) {
    int32_t ss, tt, ds, dt;
    int64_t se, te, lo_s, hi_s, lo_t, hi_t;
    if (!s->count || s->destination > b->target_bytes ||
        s->count > b->target_bytes - s->destination ||
        !pxa_raster_write_disjoint(b, b->target + s->destination, s->count, packet, size))
        return PXA_STATUS_INVALID_ARGUMENT;
    ss = (int16_t)asr(s->s, 10); tt = (int16_t)asr(s->t, 10);
    ds = asr(s->ds, 10); dt = asr(s->dt, 10);
    se = ss + (int64_t)ds * (s->count - 1u);
    te = tt + (int64_t)dt * (s->count - 1u);
    /* Crossing signed 10.6 wrap is left to the reference renderer. */
    if (se < INT16_MIN || se > INT16_MAX || te < INT16_MIN || te > INT16_MAX)
        return PXA_STATUS_INVALID_ARGUMENT;
    lo_s = asr(asr(ss < se ? ss : (int32_t)se, 6), b->light_shift) - b->light_origin_s;
    hi_s = asr(asr(ss > se ? ss : (int32_t)se, 6), b->light_shift) - b->light_origin_s;
    lo_t = asr(asr(tt < te ? tt : (int32_t)te, 6), b->light_shift) - b->light_origin_t;
    hi_t = asr(asr(tt > te ? tt : (int32_t)te, 6), b->light_shift) - b->light_origin_t;
    if (lo_s < 0 || hi_s + 1 >= b->light_width ||
        lo_t < 0 || hi_t + 1 >= b->light_height)
        return PXA_STATUS_INVALID_ARGUMENT;
    b->pixels += s->count;
    if (b->pixels > 262144u || b->pixels > (uint64_t)b->target_bytes * 4u)
        return PXA_STATUS_LIMIT_EXCEEDED;
    if (s->destination < b->first_pixel) b->first_pixel = s->destination;
    if (s->destination + s->count > b->end_pixel) b->end_pixel = s->destination + s->count;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_raster_spans_validate(const uint8_t *packet, size_t size,
    uint8_t *memory, size_t memory_size, uint32_t max_spans, pxa_raster_spans_t *output) {
    pxa_status_t status = pxa_raster_material_validate(packet, size, memory, memory_size,
        max_spans, PXA_GAME_RENDER_SPANS_MAGIC, PXA_GAME_RENDER_SPANS_HEADER_BYTES,
        PXA_GAME_RENDER_SPAN_BYTES, output);
    if (status) return status;
    for (uint32_t i = 0; i < output->count; ++i) {
        pxa_game_render_span_wire_t s = {0};
        pxa_game_render_span_decode(output->records + (size_t)i * PXA_GAME_RENDER_SPAN_BYTES,
            PXA_GAME_RENDER_SPAN_BYTES, &s);
        status = pxa_raster_span_validate(output, &s, packet, size);
        if (status) return status;
    }
    return PXA_STATUS_OK;
}

/* Specialize the two texture modes and four light-grid sizes outside the
 * pixel loop. Unsigned arithmetic defines negative-coordinate wrapping. */
/* Horizontal light interpolation reuses the same vertical pair until the
 * texture row or light cell changes. Keep only coefficients on the stack;
 * no texture/light cache or shared state is needed by either raster core. */
#define CACHED_SPAN_KERNEL(shift, pow2) \
static void cached_span_##shift##_##pow2(const pxa_raster_spans_t *restrict b, uint8_t *restrict out, \
    uint32_t n, uint32_t s, uint32_t t, uint32_t ds, uint32_t dt) { \
    const uint32_t mask = (1u << shift) - 1u; \
    const int32_t origin = b->light_origin_s + b->light_origin_t * (int32_t)b->light_width; \
    int32_t last_t = asr((int16_t)t, 6) ^ 1, last_cell = 0; \
    int32_t base = 0, step = 0; \
    while (n--) { \
        int32_t ss = (int16_t)s, tt = (int16_t)t; \
        int32_t si = asr(ss, 6), ti = asr(tt, 6), cell = asr(si, shift); \
        if (ti != last_t || cell != last_cell) { \
            const uint32_t *p = b->lights + cell + (int32_t)b->light_width * asr(ti, shift) - origin; \
            uint32_t l0 = light_word(p), l1 = light_word(p + 1), dy = (uint32_t)ti & mask; \
            int32_t a0 = (int16_t)l0, a1 = (int16_t)l1; \
            int32_t left = a0 * (1 << shift) + ((int32_t)(l0 >> 16) - a0) * (int32_t)dy; \
            int32_t right = a1 * (1 << shift) + ((int32_t)(l1 >> 16) - a1) * (int32_t)dy; \
            base = left * (1 << shift); step = right - left; \
            last_t = ti; last_cell = cell; \
        } \
        uint32_t light = (uint32_t)(base + step * (int32_t)((uint32_t)si & mask)); \
        uint32_t x, y; \
        if (pow2) { \
            x = (uint32_t)si & (b->texture_width - 1u); \
            y = (uint32_t)ti & (b->texture_height - 1u); \
        } else { \
            x = (uint32_t)(((uint64_t)(b->wrap_s * ((uint32_t)ss << 10)) * b->texture_width) >> 32); \
            y = (uint32_t)(((uint64_t)(b->wrap_t * ((uint32_t)tt << 10)) * b->texture_height) >> 32); \
        } \
        *out++ = b->colormap[((light >> shift) & 0xff00u) + b->texture[y * b->texture_width + x]]; \
        s += ds; t += dt; \
    } \
}
CACHED_SPAN_KERNEL(1, 0) CACHED_SPAN_KERNEL(2, 0) CACHED_SPAN_KERNEL(3, 0) CACHED_SPAN_KERNEL(4, 0)
CACHED_SPAN_KERNEL(1, 1) CACHED_SPAN_KERNEL(2, 1) CACHED_SPAN_KERNEL(3, 1) CACHED_SPAN_KERNEL(4, 1)
#undef CACHED_SPAN_KERNEL

/* Fullbright still applies the original colormap row zero. It needs no
 * light reads or interpolation; validation/ownership rules remain unchanged. */
#define FULLBRIGHT_KERNEL(pow2) \
static void fullbright_##pow2(const pxa_raster_spans_t *restrict b, uint8_t *restrict out, \
    uint32_t n, uint32_t s, uint32_t t, uint32_t ds, uint32_t dt) { \
    while (n--) { \
        uint32_t x, y; \
        if (pow2) { \
            x = (uint32_t)asr((int16_t)s, 6) & (b->texture_width - 1u); \
            y = (uint32_t)asr((int16_t)t, 6) & (b->texture_height - 1u); \
        } else { \
            x = (uint32_t)(((uint64_t)(b->wrap_s * ((uint32_t)(int16_t)s << 10)) * b->texture_width) >> 32); \
            y = (uint32_t)(((uint64_t)(b->wrap_t * ((uint32_t)(int16_t)t << 10)) * b->texture_height) >> 32); \
        } \
        *out++ = b->colormap[b->texture[y * b->texture_width + x]]; \
        s += ds; t += dt; \
    } \
}
FULLBRIGHT_KERNEL(0) FULLBRIGHT_KERNEL(1)
#undef FULLBRIGHT_KERNEL

#define SPAN_KERNEL(shift, pow2) \
static void span_##shift##_##pow2(const pxa_raster_spans_t *restrict b, uint8_t *restrict out, \
    uint32_t n, uint32_t s, uint32_t t, uint32_t ds, uint32_t dt) { \
    const uint32_t mask = (1u << shift) - 1u; \
    const int32_t origin = b->light_origin_s + b->light_origin_t * (int32_t)b->light_width; \
    const uint32_t ads = (int32_t)ds < 0 ? 0u - ds : ds; \
    const uint32_t adt = (int32_t)dt < 0 ? 0u - dt : dt; \
    if (n >= 8 && (uint64_t)adt + (ads >> shift) <= 16u) { \
        cached_span_##shift##_##pow2(b, out, n, s, t, ds, dt); return; \
    } \
    while (n--) { \
        int32_t ss = (int16_t)s, tt = (int16_t)t; \
        int32_t si = asr(ss, 6), ti = asr(tt, 6); \
        const uint32_t *p = b->lights + (asr(si, shift) + \
            (int32_t)b->light_width * asr(ti, shift) - origin); \
        uint32_t l0 = light_word(p), l1 = light_word(p + 1), dy = (uint32_t)ti & mask; \
        int32_t a0 = (int16_t)l0, a1 = (int16_t)l1; \
        int32_t left = a0 * (1 << shift) + ((int32_t)(l0 >> 16) - a0) * (int32_t)dy; \
        int32_t right = a1 * (1 << shift) + ((int32_t)(l1 >> 16) - a1) * (int32_t)dy; \
        uint32_t light = (uint32_t)(left * (1 << shift) + \
            (right - left) * (int32_t)((uint32_t)si & mask)); \
        uint32_t x, y; \
        if (pow2) { \
            x = (uint32_t)si & (b->texture_width - 1u); \
            y = (uint32_t)ti & (b->texture_height - 1u); \
        } else { \
            x = (uint32_t)(((uint64_t)(b->wrap_s * ((uint32_t)ss << 10)) * b->texture_width) >> 32); \
            y = (uint32_t)(((uint64_t)(b->wrap_t * ((uint32_t)tt << 10)) * b->texture_height) >> 32); \
        } \
        *out++ = b->colormap[((light >> shift) & 0xff00u) + \
            b->texture[y * b->texture_width + x]]; \
        s += ds; t += dt; \
    } \
}
SPAN_KERNEL(1, 0) SPAN_KERNEL(2, 0) SPAN_KERNEL(3, 0) SPAN_KERNEL(4, 0)
SPAN_KERNEL(1, 1) SPAN_KERNEL(2, 1) SPAN_KERNEL(3, 1) SPAN_KERNEL(4, 1)
#undef SPAN_KERNEL

pxa_span_kernel_fn pxa_raster_span_kernel(const pxa_raster_spans_t *b) {
    if (b->flags & PXA_GAME_RENDER_SPANS_FULLBRIGHT)
        return (b->flags & PXA_GAME_RENDER_SPANS_POW2) ? fullbright_1 : fullbright_0;
    static const pxa_span_kernel_fn kernels[2][4] = {
        {span_1_0, span_2_0, span_3_0, span_4_0},
        {span_1_1, span_2_1, span_3_1, span_4_1}};
    return kernels[!!(b->flags & PXA_GAME_RENDER_SPANS_POW2)][b->light_shift - 1u];
}

void pxa_raster_span_execute_clipped(const pxa_raster_spans_t *b, pxa_span_kernel_fn fn,
    const pxa_game_render_span_wire_t *s, uint32_t begin, uint32_t end) {
    uint32_t first = s->destination > begin ? s->destination : begin;
    uint32_t last = s->destination + s->count < end ? s->destination + s->count : end;
    if (last <= first) return;
    uint32_t skip = first - s->destination;
    uint32_t ds = (uint32_t)asr(s->ds, 10), dt = (uint32_t)asr(s->dt, 10);
    fn(b, b->target + first, last - first,
        (uint32_t)asr(s->s, 10) + skip * ds,
        (uint32_t)asr(s->t, 10) + skip * dt, ds, dt);
}

void pxa_raster_spans_execute_range(const pxa_raster_spans_t *b,
    uint32_t begin, uint32_t end) {
    if (b->prepared) { pxa_raster_prepared_execute_range(b, begin, end); return; }
    if (b->kind == PXA_RASTER_SPANS_PRIORITY_TILES) { pxa_raster_priority_tiles_execute_range(b, begin, end); return; }
    if (b->kind == PXA_RASTER_SPANS_BLIT) { pxa_raster_blit_execute_range(b, begin, end); return; }
    if (b->kind == PXA_RASTER_SPANS_FRAME) { pxa_raster_frame_execute_range(b, begin, end); return; }
    if (b->kind == PXA_RASTER_SPANS_PROJECTED) { pxa_raster_projected_execute_range(b, begin, end); return; }
    pxa_span_kernel_fn fn = pxa_raster_span_kernel(b);
    if (end > b->target_bytes) end = b->target_bytes;
    for (uint32_t i = 0; i < b->count; ++i) {
        pxa_game_render_span_wire_t s = {0};
        pxa_game_render_span_decode(b->records + (size_t)i * PXA_GAME_RENDER_SPAN_BYTES,
            PXA_GAME_RENDER_SPAN_BYTES, &s);
        pxa_raster_span_execute_clipped(b, fn, &s, begin, end);
    }
}

int pxa_raster_spans_partition_safe(const pxa_raster_spans_t *b,
    uint32_t split, uint32_t cache_line_bytes) {
    if (!b || !cache_line_bytes || (cache_line_bytes & (cache_line_bytes - 1u)))
        return 0;
    if (b->kind != PXA_RASTER_SPANS_PRIORITY_TILES) return 1;
    if (split > b->target_bytes) return 0;
    return !(((uintptr_t)(b->target + split) |
              (uintptr_t)(b->state.tiles.color + (size_t)split * 2u)) &
             (cache_line_bytes - 1u));
}
