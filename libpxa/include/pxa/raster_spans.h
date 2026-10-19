#ifndef PXA_RASTER_SPANS_H
#define PXA_RASTER_SPANS_H
#include "pxa/game_render_spans_wire_generated.h"
#include "pxa/status.h"

enum {
    PXA_RASTER_SPANS_LINEAR = 0,
    PXA_RASTER_SPANS_PROJECTED = 1,
    PXA_RASTER_SPANS_FRAME = 2,
    PXA_RASTER_SPANS_BLIT = 3
};

/* All pointers are borrowed for one synchronous execution. The validated
 * batch is Host-private and must never be serialized or retained. */
typedef struct {
    const uint8_t *records;
    uint8_t *target;
    const uint8_t *texture;
    const uint32_t *lights;
    const uint8_t *colormap;
    uint32_t count, target_bytes;
    uint32_t first_pixel, end_pixel;
    uint32_t texture_width, texture_height, light_width, light_height;
    uint32_t texture_bytes, light_bytes, colormap_bytes;
    int32_t light_origin_s, light_origin_t;
    uint32_t wrap_s, wrap_t;
    uint64_t pixels;
    uint8_t light_shift, flags, kind;
    union {
        struct {
            float s_origin, s_du, s_dv, t_origin, t_du, t_dv;
            float z_origin, z_du, z_dv;
            int32_t s_adjust, t_adjust, s_max, t_max, s_offset, t_offset;
            uint32_t pitch, depth_bytes, depth_pitch;
            uint8_t *depth;
        } projection;
        struct {
            uint8_t *memory;
            const uint8_t *packet;
            uint32_t command_bytes, rows;
        } frame;
        struct {
            const uint8_t *pixels, *palette, *cursor;
            uint32_t source_width, source_height, source_pitch;
            uint32_t target_width, target_height, target_pitch;
            uint32_t cursor_width, cursor_height, cursor_pitch;
            int32_t cursor_x, cursor_y;
        } blit;
    } state;
} pxa_raster_spans_t;

pxa_status_t pxa_raster_spans_validate(const uint8_t *packet, size_t size,
    uint8_t *memory, size_t memory_size, uint32_t max_spans,
    pxa_raster_spans_t *output);
pxa_status_t pxa_raster_projected_spans_validate(const uint8_t *packet, size_t size,
    uint8_t *memory, size_t memory_size, uint32_t max_spans,
    pxa_raster_spans_t *output);
pxa_status_t pxa_raster_frame_spans_validate(const uint8_t *packet, size_t size,
    uint8_t *memory, size_t memory_size, uint32_t max_spans,
    pxa_raster_spans_t *output);
pxa_status_t pxa_raster_blit_validate(const uint8_t *packet, size_t size,
    uint8_t *memory, size_t memory_size, uint32_t max_spans,
    pxa_raster_spans_t *output);
/* Disjoint byte ranges may execute concurrently; ordering inside each range
 * is unchanged. No allocation, locks, runtime calls or retained pointers. */
void pxa_raster_spans_execute_range(const pxa_raster_spans_t *batch,
    uint32_t begin, uint32_t end);
typedef pxa_status_t (*pxa_raster_spans_execute_fn)(void *context,
    const pxa_raster_spans_t *batch);
#endif
