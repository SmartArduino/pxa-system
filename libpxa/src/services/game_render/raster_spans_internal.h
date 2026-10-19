#ifndef PXA_RASTER_SPANS_INTERNAL_H
#define PXA_RASTER_SPANS_INTERNAL_H
#include "pxa/raster_spans.h"
typedef void (*pxa_span_kernel_fn)(const pxa_raster_spans_t *, uint8_t *,
    uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
pxa_status_t pxa_raster_material_validate(const uint8_t *, size_t, uint8_t *, size_t,
    uint32_t, uint32_t, uint32_t, uint32_t, pxa_raster_spans_t *);
pxa_status_t pxa_raster_material_validate_sources(const uint8_t *, size_t, uint8_t *, size_t,
    uint32_t, uint32_t, uint32_t, uint32_t, const uint8_t *, size_t, pxa_raster_spans_t *);
void pxa_raster_material_load(const pxa_game_render_spans_header_wire_t *,
    const uint8_t *, uint8_t *, const uint8_t *, pxa_raster_spans_t *);
pxa_status_t pxa_raster_projected_validate_sources(const uint8_t *, size_t, uint8_t *, size_t,
    uint32_t, const uint8_t *, size_t, uint32_t, pxa_raster_spans_t *);
void pxa_raster_projected_load(const uint8_t *, uint8_t *, const uint8_t *, pxa_raster_spans_t *);
void pxa_raster_frame_execute_range(const pxa_raster_spans_t *, uint32_t, uint32_t);
void pxa_raster_blit_execute_range(const pxa_raster_spans_t *, uint32_t, uint32_t);
int pxa_raster_write_disjoint(const pxa_raster_spans_t *, const uint8_t *, uint32_t,
    const uint8_t *, size_t);
pxa_status_t pxa_raster_span_validate(pxa_raster_spans_t *,
    const pxa_game_render_span_wire_t *, const uint8_t *, size_t);
pxa_span_kernel_fn pxa_raster_span_kernel(const pxa_raster_spans_t *);
void pxa_raster_span_execute_clipped(const pxa_raster_spans_t *, pxa_span_kernel_fn,
    const pxa_game_render_span_wire_t *, uint32_t, uint32_t);
void pxa_raster_projected_execute_range(const pxa_raster_spans_t *, uint32_t, uint32_t);
#endif
