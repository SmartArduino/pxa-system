#ifndef PXA_LVGL_BACK_INDICATOR_H
#define PXA_LVGL_BACK_INDICATOR_H

#include "lvgl.h"

static inline lv_obj_t *pxa_lvgl_back_chevron_create(lv_obj_t *parent) {
    static const lv_point_precise_t points[] = {{9, 2}, {1, 9}, {9, 16}};
    lv_obj_t *chevron = lv_line_create(parent);
    lv_line_set_points(chevron, points, 3);
    lv_obj_set_style_line_width(chevron, 2, 0);
    lv_obj_set_style_line_rounded(chevron, true, 0);
    lv_obj_set_size(chevron, 11, 19);
    lv_obj_center(chevron);
    return chevron;
}

static inline int32_t pxa_lvgl_back_edge_inset(int32_t radius, int32_t row) {
    if (row >= radius) return 0;
    const int32_t delta = radius - row;
    return radius - lv_sqrt32((uint32_t)(radius * radius - delta * delta));
}

static inline void pxa_lvgl_back_stem_draw(lv_event_t *event) {
    lv_obj_t *indicator = lv_event_get_target(event);
    lv_area_t stem;
    lv_obj_get_coords(indicator, &stem);
    const int32_t radius = lv_obj_get_style_radius(indicator, 0);
    stem.x2 -= radius;
    lv_draw_rect_dsc_t fill;
    lv_draw_rect_dsc_init(&fill);
    fill.bg_color = lv_obj_get_style_bg_color(indicator, 0);
    fill.bg_opa = LV_OPA_COVER;
    lv_draw_rect(lv_event_get_layer(event), &fill, &stem);
}

static inline lv_obj_t *pxa_lvgl_back_indicator_create(
    lv_obj_t *parent, int32_t start_y, int32_t viewport_height, int32_t height,
    int32_t top_radius, int32_t bottom_radius) {
    lv_area_t viewport;
    lv_obj_get_coords(parent, &viewport);
    height = LV_MIN(height, viewport_height);
    const int32_t y = LV_CLAMP(0, start_y - viewport.y1 - height / 2,
                               viewport_height - height);
    const int32_t bottom = viewport_height - y - height;
    const int32_t top_end = pxa_lvgl_back_edge_inset(top_radius, y);
    const int32_t bottom_end = pxa_lvgl_back_edge_inset(bottom_radius, bottom);
    const int32_t top_start = pxa_lvgl_back_edge_inset(top_radius, y + height - 1);
    const int32_t bottom_start = pxa_lvgl_back_edge_inset(bottom_radius, bottom + height - 1);
    const int32_t x = LV_MAX(top_start, bottom_start);

    lv_obj_t *indicator = lv_obj_create(parent);
    lv_obj_remove_style_all(indicator);
    lv_obj_set_scrollable(indicator, false);
    lv_obj_set_clickable(indicator, false);
    lv_obj_set_pos(indicator, x, y);
    lv_obj_set_height(indicator, height);
    /* The display clips the stem to its outline. Leave enough solid fill to
     * reach the edge at every row, including the ends of a curved segment. */
    lv_obj_set_style_pad_left(indicator, LV_MAX(top_end, bottom_end) - x, 0);
    lv_obj_set_style_bg_color(indicator, lv_color_hex(0x101114), 0);
    lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(indicator, pxa_lvgl_back_stem_draw, LV_EVENT_DRAW_MAIN, NULL);

    pxa_lvgl_back_chevron_create(indicator);
    return indicator;
}

static inline bool pxa_lvgl_back_indicator_move(lv_obj_t *indicator,
                                               int32_t distance, bool ready) {
    const int32_t body_width = LV_MIN(distance, 46);
    const int32_t max_stem = lv_obj_get_style_pad_left(indicator, 0);
    const int32_t stem = max_stem * LV_MIN(body_width, 32) / 32;
    const int32_t total_width = body_width + stem;
    lv_obj_t *chevron = lv_obj_get_child(indicator, 0);
    const lv_color_t color = ready ? lv_color_white() : lv_color_hex(0xc5c7cc);
    if (lv_obj_get_style_width(indicator, 0) == total_width &&
        lv_color_eq(lv_obj_get_style_line_color(chevron, 0), color))
        return false;

    lv_obj_set_width(indicator, total_width);
    lv_obj_set_style_radius(indicator,
                           LV_MIN(body_width, lv_obj_get_style_height(indicator, 0)) / 2, 0);
    lv_obj_align(chevron, LV_ALIGN_LEFT_MID, stem - max_stem + (body_width - 11) / 2, 0);
    lv_obj_set_style_line_opa(chevron, LV_CLAMP(0, (body_width - 12) * 32, 255), 0);
    lv_obj_set_style_line_color(chevron, color, 0);
    return true;
}

#endif
