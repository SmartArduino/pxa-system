/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 PXA System contributors.
 * Dispatch and cache boundaries for the system's indexed image/tile paths.
 * Engine-specific lighting and projection are implemented by applications.
 */
#include "raster_compute_internal.h"

void pxa_raster_compute_execute_range(const pxa_raster_compute_t *batch,
                                   uint32_t begin, uint32_t end) {
    if (!batch || begin >= end) return;
    switch (batch->kind) {
    case PXA_RASTER_COMPUTE_BLIT:
        pxa_raster_blit_execute_range(batch, begin, end);
        break;
    case PXA_RASTER_COMPUTE_PRIORITY_TILES:
        pxa_raster_priority_tiles_execute_range(batch, begin, end);
        break;
    default:
        break;
    }
}

uint32_t pxa_raster_compute_split_hint(const pxa_raster_compute_t *batch,
                                    uint8_t primary_share) {
    if (!batch || batch->first_pixel >= batch->end_pixel ||
        batch->end_pixel > batch->target_bytes) return 0;
    return batch->first_pixel + (uint32_t)(
        (uint64_t)(batch->end_pixel - batch->first_pixel) * primary_share / 256u);
}

int pxa_raster_compute_partition_safe(const pxa_raster_compute_t *batch,
                                   uint32_t split, uint32_t cache_line_bytes) {
    if (!batch || !batch->target || split > batch->target_bytes ||
        !cache_line_bytes || (cache_line_bytes & (cache_line_bytes - 1u))) return 0;
    uintptr_t mask = cache_line_bytes - 1u;
    if (((uintptr_t)batch->target + split) & mask) return 0;
    if (batch->kind == PXA_RASTER_COMPUTE_BLIT) return 1;
    if (batch->kind == PXA_RASTER_COMPUTE_PRIORITY_TILES && batch->state.tiles.color)
        return (((uintptr_t)batch->state.tiles.color + (uintptr_t)split * 2u) & mask) == 0;
    return 0;
}
