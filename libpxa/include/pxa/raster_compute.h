/* SPDX-License-Identifier: MIT */
#ifndef PXA_RASTER_COMPUTE_H
#define PXA_RASTER_COMPUTE_H
#include "pxa/game_render_compute_wire_generated.h"
#include "pxa/status.h"

enum {
    PXA_RASTER_COMPUTE_BLIT = 1,
    PXA_RASTER_COMPUTE_PRIORITY_TILES = 2
};

/* Borrowed, validated state for one synchronous call. No retained Guest
 * pointers, allocation or engine-specific projection state. */
typedef struct {
    const uint8_t *records;
    uint8_t *target;
    uint32_t count, target_bytes, first_pixel, end_pixel;
    uint64_t pixels;
    uint8_t kind;
    union {
        struct {
            const uint8_t *pixels, *palette, *cursor;
            uint32_t source_width, source_height, source_pitch;
            uint32_t target_width, target_height, target_pitch;
            uint32_t cursor_width, cursor_height, cursor_pitch;
            int32_t cursor_x, cursor_y;
            uint8_t exact_sampling;
        } blit;
        struct {
            const uint8_t *packet;
            uint8_t *color;
            uint32_t pitch;
        } tiles;
    } state;
} pxa_raster_compute_t;

pxa_status_t pxa_raster_blit_validate(const uint8_t *, size_t, uint8_t *, size_t,
    uint32_t, pxa_raster_compute_t *);
pxa_status_t pxa_raster_priority_tiles_validate(const uint8_t *, size_t, uint8_t *, size_t,
    uint32_t, pxa_raster_compute_t *);
/* Ranges own target bytes; priority bytes also own their corresponding RGB565
 * pixels. Commands within each range execute in their original order. */
void pxa_raster_compute_execute_range(const pxa_raster_compute_t *, uint32_t, uint32_t);
int pxa_raster_compute_partition_safe(const pxa_raster_compute_t *, uint32_t, uint32_t);
uint32_t pxa_raster_compute_split_hint(const pxa_raster_compute_t *, uint8_t);
/* A backend must reject before writing, or join all workers and return OK. */
typedef pxa_status_t (*pxa_raster_compute_execute_fn)(void *, const pxa_raster_compute_t *);
#endif
