/* SPDX-License-Identifier: MIT
 * Ordered, synchronous RGB565LE rectangles in borrowed application memory.
 * All descriptors and source ranges are validated before any output write.
 */
#include "raster_compute_internal.h"
#include <string.h>

#ifdef PXA_RASTER_FILL_RGB565
extern void PXA_RASTER_FILL_RGB565(uint16_t *, uint32_t, uint16_t);
#endif

static int range(uint32_t at, uint32_t bytes, size_t total) {
    return bytes && at <= total && bytes <= total - at;
}
static int disjoint(const uint8_t *a, size_t an, const uint8_t *b, size_t bn) {
    return (uintptr_t)a >= (uintptr_t)b + bn || (uintptr_t)b >= (uintptr_t)a + an;
}
static int32_t maximum(int32_t a, int32_t b) { return a > b ? a : b; }
static int32_t minimum(int32_t a, int32_t b) { return a < b ? a : b; }

pxa_status_t pxa_raster_rgb565_validate(const uint8_t *packet, size_t size,
    uint8_t *memory, size_t memory_size, uint32_t max_commands, pxa_raster_compute_t *b) {
    pxa_game_render_rgb565_header_wire_t h;
    if (!b) return PXA_STATUS_INVALID_ARGUMENT;
    memset(b, 0, sizeof(*b));
    if (!packet || !memory || (uintptr_t)packet > UINTPTR_MAX - size ||
        (uintptr_t)memory > UINTPTR_MAX - memory_size ||
        !pxa_game_render_rgb565_header_decode(packet, size, &h) ||
        h.magic != PXA_GAME_RENDER_RGB565_MAGIC || h.version != 1 || h.reserved || h.reserved2 ||
        !h.count || h.count > max_commands || h.count > PXA_GAME_RENDER_RGB565_MAX_COMMANDS ||
        size != PXA_GAME_RENDER_RGB565_HEADER_BYTES + (size_t)h.count * PXA_GAME_RENDER_RGB565_COMMAND_BYTES ||
        !h.target_width || !h.target_height || h.target_pitch < (uint32_t)h.target_width * 2 ||
        (h.target_pitch & 1) || !range(h.target_offset, h.target_bytes, memory_size) ||
        ((uintptr_t)(memory + h.target_offset) & 1) ||
        (uint64_t)(h.target_height - 1) * h.target_pitch + (uint32_t)h.target_width * 2 > h.target_bytes ||
        !disjoint(memory + h.target_offset, h.target_bytes, packet, size)) return PXA_STATUS_INVALID_ARGUMENT;
    if ((uint64_t)h.target_width * h.target_height > PXA_GAME_RENDER_BLIT_MAX_PIXELS)
        return PXA_STATUS_LIMIT_EXCEEDED;
    uint64_t pixels = 0;
    for (uint32_t i = 0; i < h.count; ++i) {
        pxa_game_render_rgb565_command_wire_t c = {0};
        if (!pxa_game_render_rgb565_command_decode(packet + 32 + i * 32, 32, &c))
            return PXA_STATUS_INVALID_ARGUMENT;
        if (c.reserved || c.reserved2 || c.kind > 1 || !c.width || !c.height ||
            (c.flags & ~(PXA_GAME_RENDER_RGB565_TRANSPARENT_ZERO | PXA_GAME_RENDER_RGB565_FLIP_X)))
            return PXA_STATUS_INVALID_ARGUMENT;
        if (c.kind == 0) {
            if (c.flags || c.light != 255 || c.source_offset || c.source_bytes ||
                c.source_width || c.source_height || c.source_pitch) return PXA_STATUS_INVALID_ARGUMENT;
        } else if (c.color || !c.source_width || !c.source_height ||
            c.source_pitch < (uint32_t)c.source_width * 2 || (c.source_pitch & 1) ||
            !range(c.source_offset, c.source_bytes, memory_size) ||
            ((uintptr_t)(memory + c.source_offset) & 1) ||
            (uint64_t)(c.source_height - 1) * c.source_pitch + (uint32_t)c.source_width * 2 > c.source_bytes ||
            !disjoint(memory + h.target_offset, h.target_bytes, memory + c.source_offset, c.source_bytes))
            return PXA_STATUS_INVALID_ARGUMENT;
        int32_t x0 = maximum(c.x, 0), x1 = minimum((int32_t)c.x + c.width, h.target_width);
        int32_t y0 = maximum(c.y, 0), y1 = minimum((int32_t)c.y + c.height, h.target_height);
        if (x1 > x0 && y1 > y0) pixels += (uint64_t)(x1 - x0) * (y1 - y0);
        if (pixels > PXA_GAME_RENDER_RGB565_MAX_PIXELS) return PXA_STATUS_LIMIT_EXCEEDED;
    }
    b->kind = PXA_RASTER_COMPUTE_RGB565; b->records = packet + 32;
    b->target = memory + h.target_offset; b->target_bytes = h.target_bytes;
    b->count = h.count; b->pixels = pixels;
    b->end_pixel = (h.target_height - 1u) * h.target_pitch + (uint32_t)h.target_width * 2;
    b->state.rgb565.memory = memory; b->state.rgb565.width = h.target_width;
    b->state.rgb565.height = h.target_height; b->state.rgb565.pitch = h.target_pitch;
    return PXA_STATUS_OK;
}

static inline uint16_t load16(const uint8_t *p) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint16_t v; memcpy(&v, __builtin_assume_aligned(p, 2), 2); return v;
#else
    return pxa_wire_generated_load_u16(p);
#endif
}
static inline void store16(uint8_t *p, uint16_t v) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    memcpy(__builtin_assume_aligned(p, 2), &v, 2);
#else
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
#endif
}
static inline unsigned div255(unsigned n) { return (n + 1 + (n >> 8)) >> 8; }
static inline uint16_t dim(uint16_t c, unsigned light) {
    return (uint16_t)((div255((c >> 11) * light) << 11) |
        (div255(((c >> 5) & 63) * light) << 5) | div255((c & 31) * light));
}

void pxa_raster_rgb565_execute_range(const pxa_raster_compute_t *b, uint32_t begin, uint32_t end) {
    if (end > b->end_pixel) end = b->end_pixel;
    if (begin >= end) return;
    const uint32_t pitch = b->state.rgb565.pitch;
    for (uint32_t i = 0; i < b->count; ++i) {
        pxa_game_render_rgb565_command_wire_t c = {0};
        pxa_game_render_rgb565_command_decode(b->records + i * 32, 32, &c);
        int32_t x0 = maximum(c.x, 0), x1 = minimum((int32_t)c.x + c.width, b->state.rgb565.width);
        int32_t y0 = maximum(maximum(c.y, 0), begin / pitch);
        int32_t y1 = minimum(minimum((int32_t)c.y + c.height, b->state.rgb565.height), (end - 1) / pitch + 1);
        if (x0 >= x1 || y0 >= y1) continue;
        const uint8_t *source = b->state.rgb565.memory + c.source_offset;
        for (int32_t y = y0; y < y1; ++y) {
            uint32_t row = (uint32_t)y * pitch;
            uint32_t first = row + (uint32_t)x0 * 2, last = row + (uint32_t)x1 * 2;
            if (first < begin) first = begin;
            if (last > end) last = end;
            if (first >= last) continue;
            /* Byte partitions preserve ownership even when a split cuts a
             * pixel. Normal workers use cache-line-aligned full pixels. */
            uint32_t first_x = (first - row) / 2, last_x = (last - row + 1) / 2;
            uint32_t sy = c.kind ? (uint32_t)(y - c.y) * c.source_height / c.height : 0;
            const uint8_t *src = source + (size_t)sy * c.source_pitch;
            if (!((first | last) & 1)) {
                if (c.kind == 0) {
#ifdef PXA_RASTER_FILL_RGB565
                    PXA_RASTER_FILL_RGB565((uint16_t *)(b->target + first), (last - first) / 2, c.color);
#else
                    for (uint32_t at = first; at < last; at += 2) store16(b->target + at, c.color);
#endif
                    continue;
                }
                if (!c.flags && c.light == 255 && c.source_width == c.width) {
                    memcpy(b->target + first, src + (first_x - c.x) * 2, last - first); continue;
                }
                /* Nearest-neighbour repeated rows are identical. Only read
                 * rows already written by this same command and worker. */
                if (!c.flags && c.light == 255 && y > y0 && first - pitch >= begin &&
                    (uint32_t)(y - c.y - 1) * c.source_height / c.height == sy) {
                    memcpy(b->target + first, b->target + first - pitch, last - first); continue;
                }
                /* Common 288 -> 480 viewport. Three source pixels produce
                 * exactly a,a,b,b,c. Preserve clipped and odd worker edges;
                 * avoid a sampling branch and division for every pixel. */
                if (!c.flags && c.light == 255 && (uint32_t)c.source_width * 5 == (uint32_t)c.width * 3) {
                    uint32_t x = first_x, dx = first_x - c.x;
                    for (; x < last_x && dx % 5; ++x, ++dx)
                        store16(b->target + row + x * 2, load16(src + (dx * 3 / 5) * 2));
                    const uint8_t *s = src + (dx / 5) * 6;
                    for (; x + 5 <= last_x; x += 5, dx += 5, s += 6) {
                        uint16_t a = load16(s), d = load16(s + 2), e = load16(s + 4);
                        uint8_t *out = b->target + row + x * 2;
                        store16(out, a); store16(out + 2, a);
                        store16(out + 4, d); store16(out + 6, d); store16(out + 8, e);
                    }
                    for (; x < last_x; ++x, ++dx)
                        store16(b->target + row + x * 2, load16(src + (dx * 3 / 5) * 2));
                    continue;
                }
            }
            uint32_t numerator = (first_x - c.x) * c.source_width;
            uint32_t sx = c.kind ? numerator / c.width : 0;
            uint32_t error = c.kind ? numerator % c.width : 0;
            const uint32_t advance = c.source_width / c.width, remainder = c.source_width % c.width;
            for (uint32_t x = first_x; x < last_x; ++x) {
                uint16_t color = c.color;
                if (c.kind) {
                    uint32_t tx = c.flags & PXA_GAME_RENDER_RGB565_FLIP_X ? c.source_width - 1 - sx : sx;
                    color = load16(src + tx * 2);
                }
                if (!(c.flags & PXA_GAME_RENDER_RGB565_TRANSPARENT_ZERO) || color) {
                    if (c.kind && c.light != 255) color = dim(color, c.light);
                    uint32_t at = row + x * 2;
                    if (at < first) b->target[at + 1] = (uint8_t)(color >> 8);
                    else if (at + 2 > last) b->target[at] = (uint8_t)color;
                    else store16(b->target + at, color);
                }
                sx += advance; error += remainder;
                if (error >= c.width) { error -= c.width; ++sx; }
            }
        }
    }
}
