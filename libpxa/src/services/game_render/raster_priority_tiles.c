/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 PXA System contributors.
 * Ordered palette/priority tile batches over borrowed Guest memory. */
#include "raster_spans_internal.h"
#include <string.h>

static int range(uint32_t at, uint64_t bytes, size_t size) {
    return bytes && at <= size && bytes <= size - at;
}
static int disjoint(const uint8_t *a, size_t an, const uint8_t *b, size_t bn) {
    return (uintptr_t)a >= (uintptr_t)b + bn || (uintptr_t)b >= (uintptr_t)a + an;
}
pxa_status_t pxa_raster_priority_tiles_validate(const uint8_t *packet, size_t size,
    uint8_t *memory, size_t memory_size, uint32_t max_spans, pxa_raster_spans_t *out) {
    pxa_game_render_tiles_header_wire_t h = {0};
    pxa_raster_spans_t b = {0};
    if (!out) return PXA_STATUS_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (!packet || !memory || (uintptr_t)packet > UINTPTR_MAX - size ||
        (uintptr_t)memory > UINTPTR_MAX - memory_size ||
        !pxa_game_render_tiles_header_decode(packet, size, &h) ||
        h.magic != PXA_GAME_RENDER_TILES_MAGIC || h.major != 1 || h.minor ||
        !h.count || !h.pixel_count || !h.pitch || h.pitch > h.pixel_count)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (size > PXA_GAME_RENDER_TILES_MAX_PACKET_BYTES || h.count > max_spans ||
        h.count > PXA_GAME_RENDER_TILES_MAX_TILES) return PXA_STATUS_LIMIT_EXCEEDED;
    uint64_t command_bytes = PXA_GAME_RENDER_TILES_HEADER_BYTES +
        (uint64_t)h.count * PXA_GAME_RENDER_TILES_COMMAND_BYTES;
    uint64_t color_bytes = (uint64_t)h.pixel_count * 2u;
    if (h.command_bytes != command_bytes || command_bytes > size ||
        !range(h.color_offset, color_bytes, memory_size) ||
        !range(h.priority_offset, h.pixel_count, memory_size) ||
        ((uintptr_t)(memory + h.color_offset) & 1u))
        return PXA_STATUS_INVALID_ARGUMENT;
    b.target = memory + h.priority_offset;
    b.target_bytes = h.pixel_count;
    b.state.tiles.color = memory + h.color_offset;
    if (!disjoint(b.target, h.pixel_count, b.state.tiles.color, (size_t)color_bytes) ||
        !disjoint(b.target, h.pixel_count, packet, size) ||
        !disjoint(b.state.tiles.color, (size_t)color_bytes, packet, size))
        return PXA_STATUS_INVALID_ARGUMENT;
    b.first_pixel = h.pixel_count;
    b.records = packet + PXA_GAME_RENDER_TILES_HEADER_BYTES;
    for (uint32_t i = 0; i < h.count; ++i) {
        pxa_game_render_tile_wire_t t = {0};
        pxa_game_render_tile_decode(b.records + (size_t)i * PXA_GAME_RENDER_TILES_COMMAND_BYTES,
            PXA_GAME_RENDER_TILES_COMMAND_BYTES, &t);
        if (!t.width || !t.height || t.source_x + t.width > 8 ||
            t.source_y + t.height > 8 || t.reserved0 || t.reserved1 ||
            (t.flags & ~(PXA_GAME_RENDER_TILES_FLIP_X | PXA_GAME_RENDER_TILES_FLIP_Y)) ||
            (t.palette_entries != 4 && t.palette_entries != 16 && t.palette_entries != 256) ||
            t.tile_offset < command_bytes || t.palette_offset < command_bytes ||
            !range(t.tile_offset, 64, size) ||
            !range(t.palette_offset, (uint32_t)t.palette_entries * 2u, size) ||
            ((uintptr_t)(packet + t.palette_offset) & 1u) ||
            (uint64_t)t.destination % h.pitch + t.width > h.pitch)
            return PXA_STATUS_INVALID_ARGUMENT;
        uint64_t end = (uint64_t)t.destination + (uint64_t)(t.height - 1u) * h.pitch + t.width;
        if (end > h.pixel_count) return PXA_STATUS_INVALID_ARGUMENT;
        if (t.palette_entries < 256) {
            const uint8_t *pixels = packet + t.tile_offset;
            for (unsigned j = 0; j < 64; ++j)
                if (pixels[j] >= t.palette_entries) return PXA_STATUS_INVALID_ARGUMENT;
        }
        if (t.destination < b.first_pixel) b.first_pixel = t.destination;
        if (end > b.end_pixel) b.end_pixel = (uint32_t)end;
        b.pixels += (uint32_t)t.width * t.height;
    }
    b.state.tiles.packet = packet;
    b.state.tiles.pitch = h.pitch;
    b.count = h.count;
    b.kind = PXA_RASTER_SPANS_PRIORITY_TILES;
    *out = b;
    return PXA_STATUS_OK;
}

static inline uint16_t load_color(const uint8_t *p) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint16_t value;
    memcpy(&value, __builtin_assume_aligned(p, 2), 2);
    return value;
#else
    return pxa_wire_generated_load_u16(p);
#endif
}
static inline void store_color(uint8_t *p, uint16_t value) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    memcpy(__builtin_assume_aligned(p, 2), &value, 2);
#else
    pxa_wire_generated_store_u16(p, value);
#endif
}
void pxa_raster_priority_tiles_execute_range(const pxa_raster_spans_t *b,
    uint32_t begin, uint32_t end) {
    if (end > b->target_bytes) end = b->target_bytes;
    if (begin >= end) return;
    for (uint32_t i = 0; i < b->count; ++i) {
        pxa_game_render_tile_wire_t t = {0};
        pxa_game_render_tile_decode(b->records + (size_t)i * PXA_GAME_RENDER_TILES_COMMAND_BYTES,
            PXA_GAME_RENDER_TILES_COMMAND_BYTES, &t);
        const uint8_t *source = b->state.tiles.packet + t.tile_offset;
        const uint8_t *palette = b->state.tiles.packet + t.palette_offset;
        for (uint32_t y = 0; y < t.height; ++y) {
            uint32_t at = t.destination + y * b->state.tiles.pitch;
            uint32_t first = at > begin ? at : begin, last = at + t.width;
            if (last > end) last = end;
            if (first >= last) continue;
            unsigned sy = t.source_y + y;
            if (t.flags & PXA_GAME_RENDER_TILES_FLIP_Y) sy = 7u - sy;
            const uint8_t *row = source + sy * 8u;
            for (uint32_t pixel = first; pixel < last; ++pixel) {
                unsigned sx = t.source_x + pixel - at;
                if (t.flags & PXA_GAME_RENDER_TILES_FLIP_X) sx = 7u - sx;
                unsigned index = row[sx];
                if (index && t.compare_depth > b->target[pixel]) {
                    store_color(b->state.tiles.color + (size_t)pixel * 2u,
                        load_color(palette + index * 2u));
                    b->target[pixel] = t.write_depth;
                }
            }
        }
    }
}
