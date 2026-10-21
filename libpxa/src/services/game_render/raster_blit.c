/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 PXA System contributors.
 * Indexed image/cursor resolve into borrowed RGB565LE memory. Preserve the
 * 320x240 logical sampling grid used by existing PXA game adapters. */
#include "raster_spans_internal.h"
#include <string.h>

static int range(uint32_t at, uint32_t bytes, size_t size) {
    return bytes && at <= size && bytes <= size - at;
}
static int disjoint(const uint8_t *a, size_t an, const uint8_t *b, size_t bn) {
    return (uintptr_t)a >= (uintptr_t)b + bn || (uintptr_t)b >= (uintptr_t)a + an;
}
pxa_status_t pxa_raster_blit_validate(const uint8_t *packet, size_t size,
    uint8_t *memory, size_t memory_size, uint32_t max_spans, pxa_raster_spans_t *b) {
    pxa_game_render_blit_wire_t h = {0};
    (void)max_spans;
    if (!b) return PXA_STATUS_INVALID_ARGUMENT;
    memset(b, 0, sizeof(*b));
    if (!packet || !memory || (uintptr_t)packet > UINTPTR_MAX - size ||
        (uintptr_t)memory > UINTPTR_MAX - memory_size || size != PXA_GAME_RENDER_BLIT_BYTES ||
        !pxa_game_render_blit_decode(packet, size, &h) ||
        !h.source_width || !h.source_height || !h.target_width || !h.target_height ||
        h.source_pitch < h.source_width || h.target_pitch < (uint32_t)h.target_width * 2u ||
        (h.target_pitch & 1u) || h.palette_bytes != 512 ||
        !range(h.source_offset, h.source_bytes, memory_size) ||
        !range(h.palette_offset, h.palette_bytes, memory_size) ||
        !range(h.target_offset, h.target_bytes, memory_size) ||
        ((uintptr_t)(memory + h.palette_offset) & 1u) ||
        ((uintptr_t)(memory + h.target_offset) & 1u) ||
        (uint64_t)(h.source_height - 1u) * h.source_pitch + h.source_width > h.source_bytes ||
        (uint64_t)(h.target_height - 1u) * h.target_pitch + (uint32_t)h.target_width * 2u > h.target_bytes)
        return PXA_STATUS_INVALID_ARGUMENT;
    if ((uint64_t)h.target_width * h.target_height > PXA_GAME_RENDER_BLIT_MAX_PIXELS)
        return PXA_STATUS_LIMIT_EXCEEDED;
    if (h.cursor_bytes) {
        if (!h.cursor_width || !h.cursor_height || h.cursor_pitch < h.cursor_width ||
            !range(h.cursor_offset, h.cursor_bytes, memory_size) ||
            (uint64_t)(h.cursor_height - 1u) * h.cursor_pitch + h.cursor_width > h.cursor_bytes)
            return PXA_STATUS_INVALID_ARGUMENT;
        b->state.blit.cursor = memory + h.cursor_offset;
    } else if (h.cursor_offset || h.cursor_x || h.cursor_y || h.cursor_width || h.cursor_height || h.cursor_pitch) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    b->target = memory + h.target_offset;
    b->target_bytes = h.target_bytes;
    b->state.blit.pixels = memory + h.source_offset; b->state.blit.palette = memory + h.palette_offset;
    b->state.blit.source_width = h.source_width; b->state.blit.source_height = h.source_height;
    b->state.blit.source_pitch = h.source_pitch; b->state.blit.target_width = h.target_width;
    b->state.blit.target_height = h.target_height; b->state.blit.target_pitch = h.target_pitch;
    b->state.blit.cursor_width = h.cursor_width; b->state.blit.cursor_height = h.cursor_height;
    b->state.blit.cursor_pitch = h.cursor_pitch; b->state.blit.cursor_x = h.cursor_x; b->state.blit.cursor_y = h.cursor_y;
    for (uint32_t y = 0; y < h.target_height; ++y) {
        uint8_t *row = b->target + (size_t)y * h.target_pitch;
        uint32_t bytes = (uint32_t)h.target_width * 2u;
        if (!disjoint(row, bytes, b->state.blit.pixels, h.source_bytes) ||
            !disjoint(row, bytes, b->state.blit.palette, h.palette_bytes) ||
            (b->state.blit.cursor && !disjoint(row, bytes, b->state.blit.cursor, h.cursor_bytes)) ||
            !disjoint(row, bytes, packet, size))
            return PXA_STATUS_INVALID_ARGUMENT;
    }
    b->pixels = (uint64_t)h.target_width * h.target_height;
    b->count = h.target_height;
    b->first_pixel = 0;
    b->end_pixel = (uint32_t)((uint64_t)(h.target_height - 1u) * h.target_pitch + h.target_width * 2u);
    b->kind = PXA_RASTER_SPANS_BLIT;
    return PXA_STATUS_OK;
}

static inline uint16_t palette_color(const pxa_raster_spans_t *b, uint8_t index) {
    const uint8_t *at = b->state.blit.palette + (uint32_t)index * 2u;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint16_t value;
    memcpy(&value, __builtin_assume_aligned(at, 2), 2);
    return value;
#else
    return pxa_wire_generated_load_u16(at);
#endif
}
static inline uint16_t color(const pxa_raster_spans_t *b, const uint8_t *row,
    uint32_t x, uint32_t sy) {
    uint32_t sx = b->state.blit.exact_sampling ?
        x * b->state.blit.source_width / b->state.blit.target_width :
        (x * 320u / b->state.blit.target_width) * b->state.blit.source_width / 320u;
    uint8_t index = row[sx];
    if (b->state.blit.cursor) {
        int64_t cx = (int64_t)sx - b->state.blit.cursor_x, cy = (int64_t)sy - b->state.blit.cursor_y;
        if (cx >= 0 && cx < b->state.blit.cursor_width && cy >= 0 && cy < b->state.blit.cursor_height) {
            uint8_t overlay = b->state.blit.cursor[(size_t)cy * b->state.blit.cursor_pitch + (size_t)cx];
            if (overlay) index = overlay;
        }
    }
    return palette_color(b, index);
}
static inline void store16(uint8_t *at, uint16_t value) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    memcpy(__builtin_assume_aligned(at, 2), &value, 2);
#else
    at[0] = (uint8_t)value; at[1] = (uint8_t)(value >> 8);
#endif
}
void pxa_raster_blit_execute_range(const pxa_raster_spans_t *b, uint32_t begin, uint32_t end) {
    if (end > b->target_bytes) end = b->target_bytes;
    for (uint32_t y = 0; y < b->state.blit.target_height; ++y) {
        uint32_t at = y * b->state.blit.target_pitch;
        uint32_t row_end = at + b->state.blit.target_width * 2u;
        uint32_t first = begin > at ? begin : at, last = end < row_end ? end : row_end;
        if (last <= first) continue;
        uint32_t sy = b->state.blit.exact_sampling ?
            y * b->state.blit.source_height / b->state.blit.target_height :
            (y * 240u / b->state.blit.target_height) * b->state.blit.source_height / 240u;
        const uint8_t *row = b->state.blit.pixels + (size_t)sy * b->state.blit.source_pitch;
        /* Arbitrary byte partitions may split a RGB565 pixel. Each partition
         * writes only its own byte; normal aligned worker splits use full words. */
        if (first & 1u) b->target[first] = (uint8_t)(color(b, row, (first - at)/2u, sy) >> 8);
        uint32_t x = (first - at + 1u)/2u, limit = (last - at)/2u;
        if (b->state.blit.target_width == b->state.blit.source_width &&
            (b->state.blit.exact_sampling || b->state.blit.target_width == 320) &&
            !b->state.blit.cursor) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            if (x < limit && ((uintptr_t)(b->target + at + x*2u) & 3u)) {
                store16(b->target + at + x*2u, palette_color(b, row[x])); ++x;
            }
            for (; x + 1 < limit; x += 2) {
                uint32_t pair = palette_color(b, row[x]) |
                    (uint32_t)palette_color(b, row[x+1]) << 16;
                memcpy(__builtin_assume_aligned(b->target + at + x*2u, 4), &pair, 4);
            }
#endif
            for (; x < limit; ++x) store16(b->target + at + x*2u,
                palette_color(b, row[x]));
        } else for (; x < limit; ++x) store16(b->target + at + x*2u, color(b, row, x, sy));
        if (last & 1u) b->target[last - 1u] = (uint8_t)color(b, row, (last - at)/2u, sy);
    }
}
