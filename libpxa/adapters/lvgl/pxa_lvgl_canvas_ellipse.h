/* SPDX-License-Identifier: MIT */
/* Private to pxa_lvgl_ui.c. */
#ifndef PXA_LVGL_CANVAS_ELLIPSE_H
#define PXA_LVGL_CANVAS_ELLIPSE_H

enum { ELLIPSE_SUBPIXELS = 256 };
typedef struct { int64_t left, right; } ellipse_span_t;

static ellipse_span_t ellipse_span(int32_t width, int32_t height,
                                   int32_t row, double sample, int32_t inset) {
    const int64_t center = (int64_t)width * (ELLIPSE_SUBPIXELS / 2);
    double inner_height = (double)height - 2.0 * inset;
    double inner_width = (double)width - 2.0 * inset;
    if (inner_width <= 0 || inner_height <= 0) return (ellipse_span_t){center, center};
    double y = (2.0 * (row + sample) - height) / inner_height;
    if (y <= -1.0 || y >= 1.0) return (ellipse_span_t){center, center};
    int64_t half = (int64_t)(inner_width * (ELLIPSE_SUBPIXELS / 2) *
                             sqrt(1.0 - y * y) + 0.5);
    return (ellipse_span_t){center - half, center + half};
}

static unsigned ellipse_coverage(const ellipse_span_t spans[2], int32_t x) {
    int64_t left = (int64_t)x * ELLIPSE_SUBPIXELS;
    int64_t right = left + ELLIPSE_SUBPIXELS;
    unsigned sum = 0;
    for (unsigned sample = 0; sample < 2; ++sample) {
        int64_t overlap = LV_MIN(right, spans[sample].right) -
                          LV_MAX(left, spans[sample].left);
        if (overlap > 0) sum += (unsigned)overlap;
    }
    return (sum + 1) / 2;
}

static void ellipse_add_edge(int32_t cuts[18], unsigned *count, int32_t edge) {
    unsigned at = 0;
    while (at < *count && cuts[at] < edge) ++at;
    if (at == *count || cuts[at] != edge) {
        memmove(cuts + at + 1, cuts + at, (*count - at) * sizeof(*cuts));
        cuts[at] = edge;
        ++*count;
    }
}

static void ellipse_cut_span(int32_t cuts[18], unsigned *count, ellipse_span_t span,
                             int32_t left, int32_t right) {
    if (span.left == span.right) return;
    const int64_t edges[] = {span.left, span.right};
    for (unsigned i = 0; i < 2; ++i) {
        int32_t pixel = (int32_t)(edges[i] / ELLIPSE_SUBPIXELS);
        if (left < pixel && pixel < right) ellipse_add_edge(cuts, count, pixel);
        if (edges[i] % ELLIPSE_SUBPIXELS && left <= pixel && pixel + 1 < right)
            ellipse_add_edge(cuts, count, pixel + 1);
    }
}

/* Resolve border-over-fill on the part covered by the stroke and apply edge
 * coverage once. Sending two overlapping antialiased draw tasks would make
 * the silhouette darker or more opaque over a translucent background. */
static uint32_t ellipse_color(uint32_t fill, uint32_t border,
                              unsigned outer, unsigned stroke) {
    uint32_t border_weight = stroke * (border & 255) * 255;
    uint32_t fill_weight = (fill & 255) * (outer * 255 - stroke * (border & 255));
    uint32_t alpha = border_weight + fill_weight;
    if (!alpha) return 0;
    uint32_t rgba = (alpha + ELLIPSE_SUBPIXELS * 255 / 2) / (ELLIPSE_SUBPIXELS * 255);
    if (!border_weight) return (fill & UINT32_C(0xffffff00)) | rgba;
    if (!fill_weight) return (border & UINT32_C(0xffffff00)) | rgba;
    for (unsigned shift = 8; shift <= 24; shift += 8) {
        uint32_t channel = (((fill >> shift) & 255) * fill_weight +
            ((border >> shift) & 255) * border_weight + alpha / 2) / alpha;
        rgba |= channel << shift;
    }
    return rgba;
}

static void ellipse_draw_run(lv_layer_t *layer, lv_draw_fill_dsc_t *fill,
                             int32_t x, int32_t y, int32_t end, uint32_t rgba) {
    fill->opa = rgba_opa(rgba);
    if (fill->opa <= LV_OPA_MIN) return;
    fill->color = rgba_color(rgba);
    const lv_area_t area = {x, y, end - 1, y};
    lv_draw_fill(layer, fill, &area);
}

static void draw_canvas_ellipse(lv_layer_t *layer, const lv_area_t *area,
    uint32_t fill_rgba, int32_t border_width, uint32_t border_rgba) {
    lv_area_t visible = *area;
    if (!canvas_intersect(&visible, &layer->_clip_area)) return;
    int32_t width = lv_area_get_width(area), height = lv_area_get_height(area);
    bool has_border = border_width > 0 && (border_rgba & 255) != 0;
    if (!has_border && !(fill_rgba & 255)) return;
    int32_t left = visible.x1 - area->x1, right = visible.x2 - area->x1 + 1;
    lv_draw_fill_dsc_t fill;
    lv_draw_fill_dsc_init(&fill);
    /* Two vertical samples with horizontal coverage. Split only where a
     * boundary crosses a pixel. Clipped interior runs take one LVGL task. */
    for (int32_t y = visible.y1; y <= visible.y2; ++y) {
        int32_t row = y - area->y1;
        ellipse_span_t outer[] = {
            ellipse_span(width, height, row, 0.25, 0),
            ellipse_span(width, height, row, 0.75, 0)
        };
        ellipse_span_t inner[2];
        int32_t cuts[18] = {left, right}; /* 2 ends + 4 spans * 4 cuts */
        unsigned count = 2;
        for (unsigned i = 0; i < 2; ++i) {
            ellipse_cut_span(cuts, &count, outer[i], left, right);
            if (has_border) {
                inner[i] = ellipse_span(width, height, row, i ? 0.75 : 0.25, border_width);
                ellipse_cut_span(cuts, &count, inner[i], left, right);
            }
        }
        int32_t start = left;
        uint32_t previous = 0;
        for (unsigned i = 0; i + 1 < count; ++i) {
            unsigned coverage = ellipse_coverage(outer, cuts[i]);
            unsigned stroke = has_border ? coverage -
                LV_MIN(coverage, ellipse_coverage(inner, cuts[i])) : 0;
            uint32_t rgba = ellipse_color(fill_rgba, border_rgba, coverage, stroke);
            if (rgba != previous) {
                if (start < cuts[i])
                    ellipse_draw_run(layer, &fill, area->x1 + start, y, area->x1 + cuts[i], previous);
                start = cuts[i];
                previous = rgba;
            }
        }
        ellipse_draw_run(layer, &fill, area->x1 + start, y, area->x1 + right, previous);
    }
}
#endif
