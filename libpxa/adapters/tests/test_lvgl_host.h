#ifndef PXA_TEST_LVGL_HOST_H
#define PXA_TEST_LVGL_HOST_H

#include "lvgl.h"

/* Minimal LVGL host environment for adapter tests: one virtual display with
 * a counting flush callback and a tick/refresh helper. */

static lv_display_t *g_test_display;
static uint32_t g_test_flush_count;
static uint64_t g_test_flushed_pixels;
static int32_t g_test_rgb565_x = -1;
static int32_t g_test_rgb565_y = -1;
static uint8_t g_test_rgb565_pixels;

static void test_flush_cb(lv_display_t *display, const lv_area_t *area,
                          uint8_t *pixels) {
    static const lv_color32_t expected[4] = {
        {0, 0, 255, 0}, {0, 255, 0, 0},
        {255, 0, 0, 0}, {255, 255, 255, 0},
    };
    unsigned index;
    g_test_flushed_pixels += (uint64_t)lv_area_get_width(area) * lv_area_get_height(area);
    if (g_test_rgb565_x >= 0 && g_test_rgb565_y >= 0 &&
        lv_display_get_color_format(display) == LV_COLOR_FORMAT_XRGB8888) {
        int32_t width = lv_area_get_width(area);
        for (index = 0; index < 4; ++index) {
            int32_t x = g_test_rgb565_x + (int32_t)(index & 1u);
            int32_t y = g_test_rgb565_y + (int32_t)(index >> 1u);
            if (x >= area->x1 && x <= area->x2 &&
                y >= area->y1 && y <= area->y2) {
                lv_color32_t actual;
                size_t offset = ((size_t)(y - area->y1) * (size_t)width +
                                 (size_t)(x - area->x1)) * sizeof(actual);
                memcpy(&actual, pixels + offset, sizeof(actual));
                if (actual.red == expected[index].red &&
                    actual.green == expected[index].green &&
                    actual.blue == expected[index].blue)
                    g_test_rgb565_pixels |= (uint8_t)(1u << index);
            }
        }
    }
    ++g_test_flush_count;
    lv_display_flush_ready(display);
}

static void test_lvgl_init(void) {
    static uint8_t draw_buffer[320 * 240 * 4];
    lv_init();
    g_test_display = lv_display_create(320, 240);
    lv_display_set_flush_cb(g_test_display, test_flush_cb);
    lv_display_set_buffers(g_test_display, draw_buffer, NULL,
                           sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_FULL);
}

static void test_lvgl_tick(void) {
    lv_tick_inc(32);
    lv_timer_handler();
    lv_refr_now(g_test_display);
}

#endif
