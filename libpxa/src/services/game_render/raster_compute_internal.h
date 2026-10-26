/* SPDX-License-Identifier: MIT */
#ifndef PXA_RASTER_COMPUTE_INTERNAL_H
#define PXA_RASTER_COMPUTE_INTERNAL_H
#include "pxa/raster_compute.h"
void pxa_raster_blit_execute_range(const pxa_raster_compute_t *, uint32_t, uint32_t);
void pxa_raster_priority_tiles_execute_range(const pxa_raster_compute_t *, uint32_t, uint32_t);
#endif
