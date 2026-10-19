/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 1996-1997 id Software, Inc.
 * Copyright (C) 2023-2024 Nicola Wrachien and contributors.
 * Copyright (C) 2026 PXA System contributors.
 * Projection and inverse depth adapted from recrof/Quake-ESP32S3 d_scan.c.
 * Preserve its 32-pixel subdivision, reciprocal rounding and negative bias.
 * Compile with -ffp-contract=off: Guest f32 expressions do not fuse.
 */
#include "raster_spans_internal.h"
#include <limits.h>
#include <float.h>
_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 &&
    FLT_MAX_EXP == 128, "Projected spans require IEEE binary32 float");
#include <string.h>

static float float_bits(uint32_t bits) {
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int to_int(float value, int32_t *out) {
    /* These comparisons reject NaN, infinities and every undefined C cast. */
    if (!(value >= -2147483648.0f && value < 2147483648.0f)) return 0;
    *out = (int32_t)value;
    return 1;
}

static int coordinate(float numerator, float scale, int32_t adjust,
    int32_t maximum, int32_t minimum, int32_t *out) {
    int32_t integer;
    if (!to_int(numerator * scale, &integer)) return 0;
    int64_t sum = (int64_t)integer + adjust;
    if (sum > maximum) *out = maximum;
    else if (sum < minimum) *out = minimum;
    else *out = (int32_t)sum;
    return 1;
}

static int32_t signed_bits(uint32_t bits) {
    int32_t value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

/* Shared by preflight and execution, so accepted arithmetic is identical.
 * No projected span array, texture copy or depth allocation is necessary. */
static pxa_status_t process_row(pxa_raster_spans_t *b,
    const pxa_game_render_row_wire_t *row, const uint8_t *packet, size_t size,
    pxa_span_kernel_fn kernel, uint32_t begin, uint32_t end) {
    const int validate = packet != NULL;
    const float x = (float)row->x, y = (float)row->y;
    float sd = b->state.projection.s_origin + y * b->state.projection.s_dv + x * b->state.projection.s_du;
    float td = b->state.projection.t_origin + y * b->state.projection.t_dv + x * b->state.projection.t_du;
    float zi = b->state.projection.z_origin + y * b->state.projection.z_dv + x * b->state.projection.z_du;
    float scale;
    int32_t s, t, izi = 0, izistep = 0;
    uint32_t destination = (uint32_t)row->y * b->state.projection.pitch + row->x;
    uint32_t remaining = row->count;
    if (!(zi > 0.0f)) return PXA_STATUS_INVALID_ARGUMENT;
    scale = 65536.0f * (1.0f / zi);
    if (!coordinate(sd, scale, b->state.projection.s_adjust, b->state.projection.s_max, 0, &s) ||
        !coordinate(td, scale, b->state.projection.t_adjust, b->state.projection.t_max, 0, &t))
        return PXA_STATUS_INVALID_ARGUMENT;
    if (b->state.projection.depth) {
        if (!to_int(zi * 32768.0f * 65536.0f, &izi) ||
            !to_int(b->state.projection.z_du * 32768.0f * 65536.0f, &izistep))
            return PXA_STATUS_INVALID_ARGUMENT;
    }
    do {
        uint32_t count = remaining < 32u ? remaining : 32u;
        int32_t snext, tnext, ds = 0, dt = 0;
        remaining -= count;
        float advance = remaining ? 32.0f : (float)(count - 1u);
        sd += b->state.projection.s_du * advance;
        td += b->state.projection.t_du * advance;
        zi += b->state.projection.z_du * advance;
        if (!(zi > 0.0f)) return PXA_STATUS_INVALID_ARGUMENT;
        scale = 65536.0f * (1.0f / zi);
        if (!coordinate(sd, scale, b->state.projection.s_adjust, b->state.projection.s_max, 32, &snext) ||
            !coordinate(td, scale, b->state.projection.t_adjust, b->state.projection.t_max, 32, &tnext))
            return PXA_STATUS_INVALID_ARGUMENT;
        if (remaining) {
            ds = (snext - s) / 32; dt = (tnext - t) / 32;
        } else if (count > 1u) {
            int32_t reciprocal = (int32_t)(65536u / (count - 1u));
            ds = (int32_t)(((int64_t)(snext - s) * reciprocal) >> 16);
            dt = (int32_t)(((int64_t)(tnext - t) * reciprocal) >> 16);
        }
        pxa_game_render_span_wire_t span = {
            destination, count,
            signed_bits((uint32_t)s + (uint32_t)b->state.projection.s_offset * 65536u),
            signed_bits((uint32_t)t + (uint32_t)b->state.projection.t_offset * 65536u),
            ds < 0 ? ds + 1024 : ds, dt < 0 ? dt + 1024 : dt};
        if (validate) {
            pxa_status_t status = pxa_raster_span_validate(b, &span, packet, size);
            if (status) return status;
        } else {
            pxa_raster_span_execute_clipped(b, kernel, &span, begin, end);
        }
        destination += count;
        s = snext; t = tnext;
    } while (remaining);
    if (!validate && b->state.projection.depth) {
        uint32_t at = (uint32_t)row->y * b->state.projection.pitch + row->x;
        uint32_t first = at > begin ? at : begin;
        uint32_t last = at + row->count < end ? at + row->count : end;
        if (last > first) {
            uint32_t skip = first - at;
            uint32_t value = (uint32_t)izi + skip * (uint32_t)izistep;
            uint8_t *depth = b->state.projection.depth + (size_t)row->y * b->state.projection.depth_pitch +
                2u * (row->x + skip);
            for (uint32_t i = first; i < last; ++i) {
                depth[0] = (uint8_t)(value >> 16); depth[1] = (uint8_t)(value >> 24);
                depth += 2; value += (uint32_t)izistep;
            }
        }
    }
    return PXA_STATUS_OK;
}

static void projection_load(const pxa_game_render_projection_wire_t *p,
    uint8_t *memory, pxa_raster_spans_t *b) {
#define COEFFICIENT(name) b->state.projection.name = float_bits(p->name##_bits)
    COEFFICIENT(s_origin); COEFFICIENT(s_du); COEFFICIENT(s_dv);
    COEFFICIENT(t_origin); COEFFICIENT(t_du); COEFFICIENT(t_dv);
    COEFFICIENT(z_origin); COEFFICIENT(z_du); COEFFICIENT(z_dv);
#undef COEFFICIENT
    b->state.projection.s_adjust = p->s_adjust; b->state.projection.t_adjust = p->t_adjust;
    b->state.projection.s_max = p->s_max; b->state.projection.t_max = p->t_max;
    b->state.projection.s_offset = p->s_offset; b->state.projection.t_offset = p->t_offset;
    b->state.projection.pitch = p->pitch;
    b->kind = PXA_RASTER_SPANS_PROJECTED;
    if (p->flags & PXA_GAME_RENDER_PROJECTED_WRITE_DEPTH) {
        b->state.projection.depth = memory + p->depth_offset;
        b->state.projection.depth_bytes = p->depth_bytes;
        b->state.projection.depth_pitch = p->depth_pitch;
    }
}

void pxa_raster_projected_load(const uint8_t *packet, uint8_t *memory,
    const uint8_t *sources, pxa_raster_spans_t *b) {
    pxa_game_render_spans_header_wire_t h = {0};
    pxa_game_render_projection_wire_t p = {0};
    memset(b, 0, sizeof(*b));
    pxa_game_render_spans_header_decode(packet, PXA_GAME_RENDER_SPANS_HEADER_BYTES, &h);
    pxa_game_render_projection_decode(packet + PXA_GAME_RENDER_SPANS_HEADER_BYTES,
        PXA_GAME_RENDER_PROJECTION_BYTES, &p);
    pxa_raster_material_load(&h, packet + PXA_GAME_RENDER_PROJECTED_PREFIX_BYTES,
        memory, sources, b);
    projection_load(&p, memory, b);
}

pxa_status_t pxa_raster_projected_spans_validate(const uint8_t *packet, size_t size,
    uint8_t *memory, size_t memory_size, uint32_t max_spans, pxa_raster_spans_t *b) {
    return pxa_raster_projected_validate_sources(packet, size, memory, memory_size,
        max_spans, memory, memory_size, PXA_GAME_RENDER_PROJECTED_MAGIC, b);
}

pxa_status_t pxa_raster_projected_validate_sources(const uint8_t *packet, size_t size,
    uint8_t *memory, size_t memory_size, uint32_t max_spans, const uint8_t *sources, size_t source_size,
    uint32_t magic, pxa_raster_spans_t *b) {
    pxa_status_t status = pxa_raster_material_validate_sources(packet, size, memory, memory_size,
        max_spans, magic, PXA_GAME_RENDER_PROJECTED_PREFIX_BYTES,
        PXA_GAME_RENDER_ROW_BYTES, sources, source_size, b);
    if (status) return status;
    pxa_game_render_projection_wire_t p = {0};
    pxa_game_render_projection_decode(packet + PXA_GAME_RENDER_SPANS_HEADER_BYTES,
        PXA_GAME_RENDER_PROJECTION_BYTES, &p);
    /* IEEE-754 binary32 coefficients are explicit bits on the wire. */
    for (uint32_t i = 0; i < 9; ++i)
        if ((pxa_wire_generated_load_u32(packet + 72u + i * 4u) & 0x7f800000u) == 0x7f800000u)
            return PXA_STATUS_INVALID_ARGUMENT;
    if (p.reserved || (p.flags & ~PXA_GAME_RENDER_PROJECTED_WRITE_DEPTH) ||
        !p.pitch || p.pitch > 65535u || p.s_max < 32 || p.t_max < 32)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (p.flags & PXA_GAME_RENDER_PROJECTED_WRITE_DEPTH) {
        if (!p.depth_bytes || p.depth_offset > memory_size ||
            p.depth_bytes > memory_size - p.depth_offset ||
            ((uintptr_t)(memory + p.depth_offset) & 1u) ||
            (p.depth_pitch & 1u) || p.depth_pitch < 2u * p.pitch)
            return PXA_STATUS_INVALID_ARGUMENT;
    } else if (p.depth_offset || p.depth_bytes || p.depth_pitch) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    projection_load(&p, memory, b);
    uint64_t depth_first = UINT64_MAX, depth_end = 0;
    for (uint32_t i = 0; i < b->count; ++i) {
        pxa_game_render_row_wire_t row = {0};
        pxa_game_render_row_decode(b->records + (size_t)i * PXA_GAME_RENDER_ROW_BYTES,
            PXA_GAME_RENDER_ROW_BYTES, &row);
        uint64_t destination = (uint64_t)row.y * p.pitch + row.x;
        if (row.reserved || !row.count || (uint32_t)row.x + row.count > p.pitch ||
            destination + row.count > b->target_bytes)
            return PXA_STATUS_INVALID_ARGUMENT;
        if (b->state.projection.depth) {
            uint64_t at = (uint64_t)row.y * p.depth_pitch + 2u * row.x;
            uint32_t bytes = 2u * row.count;
            if (at + bytes > p.depth_bytes ||
                !pxa_raster_write_disjoint(b, b->state.projection.depth + at, bytes, packet, size))
                return PXA_STATUS_INVALID_ARGUMENT;
            if (at < depth_first) depth_first = at;
            if (at + bytes > depth_end) depth_end = at + bytes;
        }
        status = process_row(b, &row, packet, size, NULL, 0, 0);
        if (status) return status;
    }
    if (b->state.projection.depth) {
        uintptr_t color_start = (uintptr_t)b->target + b->first_pixel;
        uintptr_t color_end = (uintptr_t)b->target + b->end_pixel;
        if (color_start < (uintptr_t)b->state.projection.depth + depth_end &&
            (uintptr_t)b->state.projection.depth + depth_first < color_end)
            return PXA_STATUS_INVALID_ARGUMENT;
    }
    return PXA_STATUS_OK;
}

void pxa_raster_projected_execute_range(const pxa_raster_spans_t *b, uint32_t begin, uint32_t end) {
    pxa_span_kernel_fn kernel = pxa_raster_span_kernel(b);
    if (end > b->target_bytes) end = b->target_bytes;
    for (uint32_t i = 0; i < b->count; ++i) {
        pxa_game_render_row_wire_t row = {0};
        pxa_game_render_row_decode(b->records + (size_t)i * PXA_GAME_RENDER_ROW_BYTES,
            PXA_GAME_RENDER_ROW_BYTES, &row);
        uint32_t at = (uint32_t)row.y * b->state.projection.pitch + row.x;
        if (at < end && at + row.count > begin)
            (void)process_row((pxa_raster_spans_t *)b, &row, NULL, 0, kernel, begin, end);
    }
}
