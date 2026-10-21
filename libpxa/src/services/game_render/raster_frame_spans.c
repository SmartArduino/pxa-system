/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 PXA System contributors.
 * Multi-material execution of the GPL projected-span kernel. Sources are
 * immutable snapshots inside one borrowed packet, not persistent Host copies.
 */
#include "raster_spans_internal.h"
#include <limits.h>
#include <string.h>

static int disjoint(const uint8_t *a, size_t an, const uint8_t *b, size_t bn) {
    return (uintptr_t)a >= (uintptr_t)b + bn || (uintptr_t)b >= (uintptr_t)a + an;
}

pxa_status_t pxa_raster_frame_spans_validate(const uint8_t *packet, size_t size,
    uint8_t *memory, size_t memory_size, uint32_t max_spans, pxa_raster_spans_t *b) {
    return pxa_raster_frame_spans_prepare(packet, size, memory, memory_size,
        max_spans, NULL, 0, b);
}

pxa_status_t pxa_raster_frame_spans_prepare(const uint8_t *packet, size_t size,
    uint8_t *memory, size_t memory_size, uint32_t max_spans,
    void *scratch, size_t scratch_bytes, pxa_raster_spans_t *b) {
    pxa_raster_prepared_t *prepared = pxa_raster_prepare_begin(scratch, scratch_bytes);
    pxa_game_render_frame_header_wire_t h = {0};
    uint8_t *depth = NULL;
    uint32_t depth_bytes = 0, depth_pitch = 0, pitch = 0;
    if (!b) return PXA_STATUS_INVALID_ARGUMENT;
    memset(b, 0, sizeof(*b));
    if (!packet || !memory || (uintptr_t)packet > UINTPTR_MAX - size ||
        (uintptr_t)memory > UINTPTR_MAX - memory_size ||
        !pxa_game_render_frame_header_decode(packet, size, &h))
        return PXA_STATUS_INVALID_ARGUMENT;
    if (h.magic != PXA_GAME_RENDER_FRAME_MAGIC || h.major != 1 || h.minor)
        return PXA_STATUS_UNSUPPORTED;
    if (!h.count || h.count > PXA_GAME_RENDER_FRAME_MAX_MATERIALS ||
        size > PXA_GAME_RENDER_FRAME_MAX_PACKET_BYTES ||
        h.command_bytes > size || h.command_bytes < PXA_GAME_RENDER_FRAME_HEADER_BYTES)
        return PXA_STATUS_INVALID_ARGUMENT;
    uint64_t light_work = 0;
    size_t position = PXA_GAME_RENDER_FRAME_HEADER_BYTES;
    b->first_pixel = UINT32_MAX;
    for (uint32_t i = 0; i < h.count; ++i) {
        pxa_game_render_spans_header_wire_t material = {0};
        pxa_raster_spans_t child;
        if (position > h.command_bytes || h.command_bytes - position < PXA_GAME_RENDER_PROJECTED_PREFIX_BYTES)
            return PXA_STATUS_INVALID_ARGUMENT;
        pxa_game_render_spans_header_decode(packet + position, h.command_bytes - position, &material);
        uint64_t bytes = PXA_GAME_RENDER_PROJECTED_PREFIX_BYTES +
            (uint64_t)material.count * PXA_GAME_RENDER_ROW_BYTES;
        if (bytes > h.command_bytes - position ||
            material.texture_offset < h.command_bytes || material.light_offset < h.command_bytes ||
            material.colormap_offset < h.command_bytes)
            return PXA_STATUS_INVALID_ARGUMENT;
        if (!(material.flags & PXA_GAME_RENDER_SPANS_FULLBRIGHT)) {
            light_work += material.light_bytes;
            if (light_work > PXA_GAME_RENDER_FRAME_MAX_LIGHT_VALIDATION_BYTES)
                return PXA_STATUS_LIMIT_EXCEEDED;
        }
        pxa_status_t status = pxa_raster_projected_prepare_sources(packet + position, (size_t)bytes,
            memory, memory_size, max_spans, packet, size, PXA_GAME_RENDER_FRAME_MATERIAL_MAGIC, prepared, &child);
        if (status) return status;
        if (!i) { b->target = child.target; pitch = child.state.projection.pitch; }
        if (child.target != b->target || child.state.projection.pitch != pitch)
            return PXA_STATUS_INVALID_ARGUMENT;
        if (child.target_bytes > b->target_bytes) b->target_bytes = child.target_bytes;
        if (child.state.projection.depth) {
            if (depth && (child.state.projection.depth != depth || child.state.projection.depth_pitch != depth_pitch))
                return PXA_STATUS_INVALID_ARGUMENT;
            depth = child.state.projection.depth; depth_pitch = child.state.projection.depth_pitch;
            if (child.state.projection.depth_bytes > depth_bytes) depth_bytes = child.state.projection.depth_bytes;
        }
        b->pixels += child.pixels;
        b->state.frame.rows += child.count;
        if (b->pixels > 262144u) return PXA_STATUS_LIMIT_EXCEEDED;
        if (child.first_pixel < b->first_pixel) b->first_pixel = child.first_pixel;
        if (child.end_pixel > b->end_pixel) b->end_pixel = child.end_pixel;
        position += (size_t)bytes;
    }
    if (position != h.command_bytes) return PXA_STATUS_INVALID_ARGUMENT;
    if (b->pixels > (uint64_t)b->target_bytes * 4u) return PXA_STATUS_LIMIT_EXCEEDED;
    /* Every source is in the parent packet. This global proof covers sources
     * belonging to later materials as well as the current material. */
    if (!disjoint(b->target, b->target_bytes, packet, size) ||
        (depth && (!disjoint(depth, depth_bytes, packet, size) ||
        !disjoint(depth, depth_bytes, b->target, b->target_bytes))))
        return PXA_STATUS_INVALID_ARGUMENT;
    b->count = h.count;
    b->records = packet + PXA_GAME_RENDER_FRAME_HEADER_BYTES;
    b->state.frame.memory = memory; b->state.frame.packet = packet; b->state.frame.command_bytes = h.command_bytes;
    b->kind = PXA_RASTER_SPANS_FRAME;
    pxa_raster_prepare_finish(prepared, b);
    return PXA_STATUS_OK;
}

void pxa_raster_frame_execute_range(const pxa_raster_spans_t *b, uint32_t begin, uint32_t end) {
    const uint8_t *record = b->records;
    for (uint32_t i = 0; i < b->count; ++i) {
        pxa_raster_spans_t child;
        /* Preflight already proved every source/row. Reconstruct only the
         * trusted view, with one bounded stack object per execution worker. */
        pxa_raster_projected_load(record, b->state.frame.memory, b->state.frame.packet, &child);
        pxa_raster_projected_execute_range(&child, begin, end);
        record += PXA_GAME_RENDER_PROJECTED_PREFIX_BYTES + (size_t)child.count * PXA_GAME_RENDER_ROW_BYTES;
    }
}
