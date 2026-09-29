#ifndef PXA_GUEST_SURFACE_GEOMETRY_H
#define PXA_GUEST_SURFACE_GEOMETRY_H

#include <stdint.h>

/* Largest exact nearest-neighbour scale supported by the presenter. */
static inline uint32_t pxa_surface_fit_scale(uint32_t surface_width,
                                             uint32_t surface_height,
                                             uint32_t display_width,
                                             uint32_t display_height) {
    uint32_t best = 0;
    if (surface_width == 0 || surface_height == 0 || display_width == 0 ||
        display_height == 0) return 0;
    for (uint32_t scale = 1; scale <= 4; scale *= 2)
        if (surface_width <= display_width / scale &&
            surface_height <= display_height / scale)
            best = scale;
    return best;
}

#endif
