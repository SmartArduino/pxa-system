/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 1996-1997 id Software, Inc.
 * Copyright (C) 2023-2024 Nicola Wrachien and contributors.
 * Copyright (C) 2026 PXA System contributors.
 * Indexed light-grid spans, adapted from recrof/Quake-ESP32S3's portable
 * d_scan.c lit-span kernel (MG24Quake / Nicola Wrachien and contributors).
 * Keep the 10.6 wrapping, packed vertical light pair and colormap semantics.
 */
#include "pxa/raster_spans.h"
#include <limits.h>
#include <string.h>

static int valid_range(uint32_t offset, uint32_t bytes, size_t size) {
    return bytes && offset <= size && bytes <= size - offset;
}

static int overlap(uint64_t a, uint32_t an, uint64_t b, uint32_t bn) {
    return a < b + bn && b < a + an;
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

pxa_status_t pxa_raster_spans_validate(const uint8_t *packet, size_t size,
    uint8_t *memory, size_t memory_size, uint32_t max_spans,
    pxa_raster_spans_t *output) {
    pxa_game_render_spans_header_wire_t h;
    uint32_t i, light_limit;
    uint64_t pixels = 0;
    uint32_t first_pixel = UINT32_MAX, end_pixel = 0;
    if (!output) return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    if (!packet || !memory || (uintptr_t)packet > UINTPTR_MAX - size ||
        (uintptr_t)memory > UINTPTR_MAX - memory_size ||
        !pxa_game_render_spans_header_decode(packet, size, &h))
        return PXA_STATUS_INVALID_ARGUMENT;
    if (h.magic != PXA_GAME_RENDER_SPANS_MAGIC || h.major != 1 || h.minor)
        return PXA_STATUS_UNSUPPORTED;
    if (h.reserved || !h.count || h.count > max_spans ||
        (uint64_t)size != PXA_GAME_RENDER_SPANS_HEADER_BYTES +
                    (uint64_t)h.count * PXA_GAME_RENDER_SPAN_BYTES ||
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
        !valid_range(h.texture_offset, h.texture_bytes, memory_size) ||
        !valid_range(h.light_offset, h.light_bytes, memory_size) ||
        !valid_range(h.colormap_offset, h.colormap_bytes, memory_size) ||
        ((uintptr_t)(memory + h.light_offset) & 3u))
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
            uint32_t l = pxa_wire_generated_load_u32(memory + h.light_offset + i);
            if ((l & 65535u) > light_limit || (l >> 16) > light_limit)
                return PXA_STATUS_INVALID_ARGUMENT;
        }
    }
    for (i = 0; i < h.count; ++i) {
        pxa_game_render_span_wire_t s;
        int32_t ss, tt, ds, dt;
        int64_t se, te, lo_s, hi_s, lo_t, hi_t;
        pxa_game_render_span_decode(packet + PXA_GAME_RENDER_SPANS_HEADER_BYTES +
            (size_t)i * PXA_GAME_RENDER_SPAN_BYTES, PXA_GAME_RENDER_SPAN_BYTES, &s);
        if (!s.count || s.destination > h.target_bytes ||
            s.count > h.target_bytes - s.destination)
            return PXA_STATUS_INVALID_ARGUMENT;
        /* The target is a bounds view, not an allocation. Packed engines may
         * include read-only subregions in its unused tail. Validate the exact
         * write set against every source and the borrowed command bytes. */
        {
            uint64_t at = (uint64_t)h.target_offset + s.destination;
            if (overlap(at, s.count, h.texture_offset, h.texture_bytes) ||
                overlap(at, s.count, h.light_offset, h.light_bytes) ||
                overlap(at, s.count, h.colormap_offset, h.colormap_bytes) ||
                ((uintptr_t)packet < (uintptr_t)memory + at + s.count &&
                 (uintptr_t)memory + at < (uintptr_t)packet + size))
                return PXA_STATUS_INVALID_ARGUMENT;
        }
        ss = (int16_t)asr(s.s, 10); tt = (int16_t)asr(s.t, 10);
        ds = asr(s.ds, 10); dt = asr(s.dt, 10);
        se = ss + (int64_t)ds * (s.count - 1u);
        te = tt + (int64_t)dt * (s.count - 1u);
        /* A batch that crosses signed 10.6 wrap is left to the reference
         * renderer. This makes the bounds proof constant work per span. */
        if (se < INT16_MIN || se > INT16_MAX ||
            te < INT16_MIN || te > INT16_MAX)
            return PXA_STATUS_INVALID_ARGUMENT;
        lo_s = asr(asr(ss < se ? ss : (int32_t)se, 6), h.light_shift) - h.light_origin_s;
        hi_s = asr(asr(ss > se ? ss : (int32_t)se, 6), h.light_shift) - h.light_origin_s;
        lo_t = asr(asr(tt < te ? tt : (int32_t)te, 6), h.light_shift) - h.light_origin_t;
        hi_t = asr(asr(tt > te ? tt : (int32_t)te, 6), h.light_shift) - h.light_origin_t;
        if (lo_s < 0 || hi_s + 1 >= h.light_width ||
            lo_t < 0 || hi_t + 1 >= h.light_height)
            return PXA_STATUS_INVALID_ARGUMENT;
        pixels += s.count;
        if (s.destination < first_pixel) first_pixel = s.destination;
        if (s.destination + s.count > end_pixel) end_pixel = s.destination + s.count;
        /* Bound total work, even when an untrusted batch redraws one pixel. */
        if (pixels > 262144u || pixels > (uint64_t)h.target_bytes * 4u)
            return PXA_STATUS_LIMIT_EXCEEDED;
    }
    output->records = packet + PXA_GAME_RENDER_SPANS_HEADER_BYTES;
    output->target = memory + h.target_offset;
    output->texture = memory + h.texture_offset;
    output->lights = (const uint32_t *)(const void *)(memory + h.light_offset);
    output->colormap = memory + h.colormap_offset;
    output->count = h.count; output->target_bytes = h.target_bytes;
    output->first_pixel = first_pixel; output->end_pixel = end_pixel;
    output->texture_width = h.texture_width; output->texture_height = h.texture_height;
    output->light_width = h.light_width;
    output->light_origin_s = h.light_origin_s; output->light_origin_t = h.light_origin_t;
    output->wrap_s = h.wrap_s; output->wrap_t = h.wrap_t;
    output->light_shift = h.light_shift; output->flags = h.flags;
    output->pixels = pixels;
    return PXA_STATUS_OK;
}

/* Specialize the two texture modes and four light-grid sizes outside the
 * pixel loop. Unsigned arithmetic defines negative-coordinate wrapping. */
#define SPAN_KERNEL(shift, pow2) \
static void span_##shift##_##pow2(const pxa_raster_spans_t *restrict b, uint8_t *restrict out, \
    uint32_t n, uint32_t s, uint32_t t, uint32_t ds, uint32_t dt) { \
    const uint32_t mask = (1u << shift) - 1u; \
    const int32_t origin = b->light_origin_s + b->light_origin_t * (int32_t)b->light_width; \
    const uint32_t light_mask = (b->flags & PXA_GAME_RENDER_SPANS_FULLBRIGHT) ? 0 : 0xff00u; \
    while (n--) { \
        int32_t ss = (int16_t)s, tt = (int16_t)t; \
        int32_t si = asr(ss, 6), ti = asr(tt, 6); \
        const uint32_t *p = b->lights + asr(si, shift) + \
            (int32_t)b->light_width * asr(ti, shift) - origin; \
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
        *out++ = b->colormap[((light >> shift) & light_mask) + \
            b->texture[y * b->texture_width + x]]; \
        s += ds; t += dt; \
    } \
}
SPAN_KERNEL(1, 0) SPAN_KERNEL(2, 0) SPAN_KERNEL(3, 0) SPAN_KERNEL(4, 0)
SPAN_KERNEL(1, 1) SPAN_KERNEL(2, 1) SPAN_KERNEL(3, 1) SPAN_KERNEL(4, 1)
#undef SPAN_KERNEL

void pxa_raster_spans_execute_range(const pxa_raster_spans_t *b,
    uint32_t begin, uint32_t end) {
    typedef void (*kernel_fn)(const pxa_raster_spans_t *, uint8_t *,
        uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    static const kernel_fn kernels[2][4] = {
        {span_1_0, span_2_0, span_3_0, span_4_0},
        {span_1_1, span_2_1, span_3_1, span_4_1}};
    kernel_fn fn = kernels[!!(b->flags & PXA_GAME_RENDER_SPANS_POW2)][b->light_shift - 1u];
    uint32_t i;
    if (end > b->target_bytes) end = b->target_bytes;
    for (i = 0; i < b->count; ++i) {
        pxa_game_render_span_wire_t s = {0};
        uint32_t first, last, skip, ds, dt;
        pxa_game_render_span_decode(b->records + (size_t)i * PXA_GAME_RENDER_SPAN_BYTES,
            PXA_GAME_RENDER_SPAN_BYTES, &s);
        first = s.destination > begin ? s.destination : begin;
        last = s.destination + s.count < end ? s.destination + s.count : end;
        if (last <= first) continue;
        skip = first - s.destination;
        ds = (uint32_t)asr(s.ds, 10); dt = (uint32_t)asr(s.dt, 10);
        fn(b, b->target + first, last - first,
            (uint32_t)asr(s.s, 10) + skip * ds,
            (uint32_t)asr(s.t, 10) + skip * dt, ds, dt);
    }
}
