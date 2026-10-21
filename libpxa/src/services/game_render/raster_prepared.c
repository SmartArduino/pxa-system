/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bounded Host-private projection plans. No borrowed pointer survives the
 * synchronous batch. Exhausting the arena discards the entire plan, while
 * normal preflight continues to validate every remaining command. */
#include "raster_spans_internal.h"
#include <string.h>
#include <stddef.h>

typedef struct {
    pxa_raster_spans_t material;
    uint32_t count, next, depth_step;
} prepared_material_t;

typedef struct {
    uint32_t destination, count, depth_offset, inverse_depth;
} prepared_row_t;

struct pxa_raster_prepared {
    size_t capacity, used;
    uint32_t first, active, count;
    uint32_t work[32], total_work;
    uint8_t band_shift;
    uint8_t overflow;
};

static void *reserve(pxa_raster_prepared_t *p, size_t bytes, size_t alignment) {
    if (!p || p->overflow) return NULL;
    size_t at = (p->used + alignment - 1u) & ~(alignment - 1u);
    if (at > p->capacity || bytes > p->capacity - at) {
        p->overflow = 1;
        return NULL;
    }
    p->used = at + bytes;
    return (uint8_t *)p + at;
}

pxa_raster_prepared_t *pxa_raster_prepare_begin(void *scratch, size_t bytes) {
    if (!scratch || bytes > 1024u * 1024u ||
        (uintptr_t)scratch > UINTPTR_MAX - bytes) return NULL;
    /* Capability allocators can provide a weaker alignment than this Host
     * ABI's uint64_t. Consume padding inside the supplied bound instead of
     * silently disabling preparation for a valid allocation. */
    size_t padding = (-(uintptr_t)scratch) & (_Alignof(prepared_material_t) - 1u);
    if (padding > bytes || bytes - padding < sizeof(pxa_raster_prepared_t)) return NULL;
    bytes -= padding;
    pxa_raster_prepared_t *p = (void *)((uint8_t *)scratch + padding);
    memset(p, 0, sizeof(*p));
    p->capacity = bytes; p->used = sizeof(*p);
    return p;
}

void pxa_raster_prepare_material(pxa_raster_prepared_t *p, const pxa_raster_spans_t *b) {
    prepared_material_t *m = reserve(p, sizeof(*m), _Alignof(prepared_material_t));
    if (!m) return;
    uint32_t offset = (uint32_t)((uint8_t *)m - (uint8_t *)p);
    if (p->active) {
        prepared_material_t *previous = (void *)((uint8_t *)p + p->active);
        previous->next = offset;
    } else p->first = offset;
    m->material = *b;
    m->material.first_pixel = UINT32_MAX; m->material.end_pixel = 0;
    m->count = m->next = m->depth_step = 0;
    p->active = offset;
    ++p->count;
    uint8_t shift = p->band_shift;
    while (((uint64_t)32u << shift) < b->target_bytes) ++shift;
    if (shift != p->band_shift) {
        unsigned grow = shift - p->band_shift;
        /* Coarsen in place: every unread source bucket is at or above its
         * destination. This also handles differing validated target extents. */
        for (unsigned i = 0; p->count > 1 && i < 32; ++i) {
            uint32_t sum = 0;
            for (uint64_t j = (uint64_t)i << grow;
                 j < ((uint64_t)(i + 1u) << grow) && j < 32; ++j) sum += p->work[j];
            p->work[i] = sum;
        }
        p->band_shift = shift;
    }
}

pxa_raster_prepared_step_t *pxa_raster_prepare_row(pxa_raster_prepared_t *p,
    uint32_t destination, uint32_t count, uint32_t depth_offset,
    uint32_t inverse_depth, uint32_t depth_step) {
    size_t bytes = sizeof(prepared_row_t) + ((count + 31u) / 32u) * sizeof(pxa_raster_prepared_step_t);
    prepared_row_t *s = reserve(p, bytes, _Alignof(prepared_row_t));
    if (!s) return NULL;
    s->destination = destination; s->count = count; s->depth_offset = depth_offset;
    s->inverse_depth = inverse_depth;
    prepared_material_t *m = (void *)((uint8_t *)p + p->active);
    m->depth_step = depth_step;
    if (destination < m->material.first_pixel) m->material.first_pixel = destination;
    if (destination + count > m->material.end_pixel) m->material.end_pixel = destination + count;
    ++m->count;
    /* Accumulate the split estimate while each row is already hot. No
     * second traversal of the PSRAM plan is needed after validation. */
    uint32_t band_bytes = 1u << p->band_shift;
    uint32_t weight = m->material.flags & PXA_GAME_RENDER_SPANS_FULLBRIGHT ? 1u : 2u;
    uint32_t at = destination, left = count;
    while (left) {
        uint32_t n = band_bytes - (at & (band_bytes - 1u));
        if (n > left) n = left;
        uint32_t cost = n * weight + ((n + 31u) / 32u) * 8u;
        p->work[at >> p->band_shift] += cost; p->total_work += cost;
        at += n; left -= n;
    }
    return (void *)(s + 1);
}

void pxa_raster_prepare_finish(pxa_raster_prepared_t *p, pxa_raster_spans_t *b) {
    if (!p || p->overflow || !p->count) return;
    b->prepared = p;
}

uint32_t pxa_raster_spans_split_hint(const pxa_raster_spans_t *b, uint8_t primary_share) {
    const pxa_raster_prepared_t *p = b->prepared;
    if (!p || !p->total_work)
        return b->first_pixel + (uint32_t)(((uint64_t)(b->end_pixel - b->first_pixel) * primary_share) >> 8);
    uint32_t remaining = (uint32_t)(((uint64_t)p->total_work * primary_share) >> 8);
    for (uint32_t i = 0; i < 32; ++i) {
        if (p->work[i] && remaining < p->work[i])
            return (i << p->band_shift) +
                (uint32_t)(((uint64_t)remaining << p->band_shift) / p->work[i]);
        remaining -= p->work[i];
    }
    return b->end_pixel;
}

void pxa_raster_prepared_execute_range(const pxa_raster_spans_t *b, uint32_t begin, uint32_t end) {
    const pxa_raster_prepared_t *p = b->prepared;
    if (end > b->target_bytes) end = b->target_bytes;
    for (uint32_t offset = p->first; offset;) {
        const prepared_material_t *m = (const void *)((const uint8_t *)p + offset);
        const pxa_raster_spans_t *material = &m->material;
        offset = m->next;
        if (material->first_pixel >= end || material->end_pixel <= begin) continue;
        pxa_span_kernel_fn kernel = pxa_raster_span_kernel(material);
        const prepared_row_t *row = (const void *)(m + 1);
        for (uint32_t i = 0; i < m->count; ++i) {
            const pxa_raster_prepared_step_t *steps = (const void *)(row + 1);
            uint32_t at = row->destination, limit = at + row->count;
            uint32_t first = at > begin ? at : begin;
            uint32_t last = limit < end ? limit : end;
            if (first < last) {
                uint32_t segment = (first - at) / 32u;
                uint32_t destination = at + segment * 32u;
                while (destination < last) {
                    uint32_t lo = destination > first ? destination : first;
                    uint32_t hi = last - destination < 32u ? last : destination + 32u;
                    uint32_t skip = lo - destination;
                    const pxa_raster_prepared_step_t *step = steps + segment;
                    uint32_t ds = (uint32_t)(int32_t)step->ds, dt = (uint32_t)(int32_t)step->dt;
                    kernel(material, material->target + lo, hi - lo,
                        (uint32_t)(int32_t)step->s + skip * ds,
                        (uint32_t)(int32_t)step->t + skip * dt, ds, dt);
                    ++segment;
                    if (last - destination <= 32u) break;
                    destination += 32u;
                }
            }
            if (first < last && material->state.projection.depth) {
                uint32_t skip = first - at;
                uint32_t value = row->inverse_depth + skip * m->depth_step;
                uint8_t *depth = material->state.projection.depth + row->depth_offset + 2u * skip;
                for (uint32_t pixel = first; pixel < last; ++pixel) {
                    depth[0] = (uint8_t)(value >> 16); depth[1] = (uint8_t)(value >> 24);
                    depth += 2; value += m->depth_step;
                }
            }
            row = (const void *)(steps + (row->count + 31u) / 32u);
        }
    }
}
