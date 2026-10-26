/* SPDX-License-Identifier: MIT
 * Browser platform adapter. Guest execution stays in the browser's Wasm VM;
 * this module owns the existing Core services and LVGL renderer. */
#include <emscripten.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "pxa/runtime.h"
#include "pxa/service.h"
#include "pxa/clock.h"
#include "pxa/window.h"
#include "pxa/lvgl/pxa_lvgl_ui.h"

static pxa_runtime_t *runtime;
static pxa_component_t component;
static pxa_ui_service_t *ui;
static lv_display_t *display;
static lv_indev_t *pointer;
static uint8_t *frame;
static uint32_t width, height, revision, clock_period;
static uint64_t current_us, next_tick;
static int pointer_x, pointer_y, pointer_down;
static void *allocate(void *c, size_t n) { (void)c; return malloc(n); }
static void release(void *c, void *p) { (void)c; free(p); }
static uint64_t now_us(void *c) { (void)c; return current_us; }
static pxa_status_t execute(pxa_lvgl_ui_execute_callback_fn fn, void *data, void *c) {
    (void)c; fn(data); return PXA_STATUS_OK;
}
static void flush(lv_display_t *d, const lv_area_t *area, uint8_t *pixels) {
    (void)area; (void)pixels; ++revision; lv_display_flush_ready(d);
}
static void read_pointer(lv_indev_t *i, lv_indev_data_t *data) {
    (void)i; data->point.x = pointer_x; data->point.y = pointer_y;
    data->state = pointer_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
static void ui_event(uint32_t surface, uint32_t node, pxa_ui_event_kind_t kind,
                     uint16_t flags, const void *value, size_t size, void *c) {
    (void)c;
    /* Events are drained by JS after returning from an import; never reenter. */
    pxa_ui_queue_event(ui, component, surface, node, kind, flags, current_us, value, size);
}
static pxa_status_t configure_clock(void *c, pxa_component_t id, uint32_t period) {
    (void)c; (void)id; clock_period = period; next_tick = current_us + period;
    return PXA_STATUS_OK;
}
static pxa_status_t clock_call(void *c, pxa_runtime_t *rt, pxa_component_t id,
                               const pxa_message_view_t *m) {
    (void)c;
    if (m->opcode == PXA_CLOCK_CONFIGURE || m->opcode == PXA_CLOCK_LIMITS) {
        pxa_clock_timing_backend_t backend = {
            {1000, 60000000, 1, 1000, PXA_CLOCK_TIMING_COALESCED | PXA_CLOCK_TIMING_FOREGROUND},
            NULL, configure_clock};
        return pxa_clock_timing_control(rt, id, m, &backend);
    }
    if (m->opcode == PXA_CLOCK_SET_PERIOD && m->payload.size == 2 && !m->request_token) {
        uint32_t ms = pxa_read_u16(m->payload.data);
        if (ms && (ms < 16 || ms > 1000)) return PXA_STATUS_INVALID_ARGUMENT;
        return configure_clock(NULL, id, ms * 1000);
    }
    if (m->opcode == PXA_CLOCK_NOW && !m->payload.size && m->request_token) {
        uint8_t bytes[8]; pxa_write_u64(bytes, current_us);
        pxa_status_t s = pxa_request_begin_reserved(rt, id, m->request_token, 4, m->opcode, 0, 8);
        if (s) return s;
        s = pxa_request_commit(rt, id, m->request_token);
        return s ? s : pxa_request_complete(rt, id, m->request_token, PXA_STATUS_OK, bytes, 8);
    }
    return PXA_STATUS_UNSUPPORTED;
}
static pxa_status_t window_apply(void *c, const pxa_window_configuration_t *v) {
    (void)c; (void)v; return PXA_STATUS_OK;
}
#define CHECK(call) do { int s = (call); if (s) return s; } while (0)
#define WORKSPACE(type) calloc(1, pxa_##type##_workspace_size())

EMSCRIPTEN_KEEPALIVE int browser_init(uint32_t w, uint32_t h) {
    if (runtime || w < 64 || h < 64 || w > 1024 || h > 1024) return PXA_STATUS_INVALID_ARGUMENT;
    width = w; height = h;
    pxa_runtime_limits_t limits; pxa_runtime_limits_init(&limits);
    limits.max_components = 1;
    size_t size = pxa_runtime_workspace_size(&limits);
    void *workspace = calloc(1, size);
    CHECK(pxa_runtime_init(workspace, size, &limits, &runtime));
    CHECK(pxa_component_create(runtime, 1, &component));
    CHECK(pxa_component_set_wire_profile(runtime, component, PXA_WIRE_PROFILE_HANDLE64));
    lv_init();
    display = lv_display_create(w, h);
    frame = calloc(w * h, 4);
    if (!display || !frame) return PXA_STATUS_RESOURCE_LIMIT;
    lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display, frame, NULL, w * h * 4, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(pointer, display);
    lv_indev_set_read_cb(pointer, read_pointer);
    lv_indev_set_mode(pointer, LV_INDEV_MODE_EVENT);
    pxa_ui_config_t cfg; pxa_ui_config_init(&cfg);
    cfg.allocate = allocate; cfg.release = release; cfg.now_us = now_us;
    cfg.primary_width = w; cfg.primary_height = h;
    cfg.features = PXA_UI_FEATURE_CANVAS | PXA_UI_FEATURE_GRID |
        PXA_UI_FEATURE_CONTROLLER_INPUT | PXA_UI_FEATURE_RGB565_BITMAP |
        PXA_UI_FEATURE_CANVAS_STREAM_IO | PXA_UI_FEATURE_DYNAMIC_TEXT;
    CHECK(pxa_ui_service_init(WORKSPACE(ui_service), pxa_ui_service_workspace_size(), runtime, &cfg, &ui));
    CHECK(pxa_ui_service_register(ui));
    pxa_lvgl_ui_config_t lc = {0};
    lc.struct_size = sizeof(lc); lc.allocate = allocate; lc.release = release;
    lc.execute = execute; lc.event_callback = ui_event; lc.now_us = now_us;
    lc.parent_object = lv_screen_active();
    lc.primary_environment.surface = PXA_UI_PRIMARY_SURFACE;
    lc.primary_environment.width = w; lc.primary_environment.height = h;
    lc.primary_environment.density_q16 = lc.primary_environment.font_scale_q16 = 65536;
    pxa_lvgl_ui_theme_init(&lc.theme);
    pxa_lvgl_ui_t *adapter; pxa_ui_backend_t backend;
    CHECK(pxa_lvgl_ui_init(WORKSPACE(lvgl_ui), pxa_lvgl_ui_workspace_size(), &lc, &adapter, &backend));
    CHECK(pxa_ui_bind(ui, component, &backend));
    pxa_window_service_t *window;
    size = pxa_window_service_workspace_size(1);
    CHECK(pxa_window_service_init(calloc(1, size), size, runtime, 1, &window));
    CHECK(pxa_window_service_register(window));
    pxa_window_backend_t wb = {.struct_size=sizeof(wb), .apply=window_apply};
    CHECK(pxa_window_bind(window, component, &wb));
    pxa_window_snapshot_t snapshot = {.logical_width=w,.logical_height=h,
        .pixel_width=w,.pixel_height=h,.density_numerator=1,.density_denominator=1,.focused=1};
    CHECK(pxa_window_update_snapshot(window, component, &snapshot));
    pxa_service_ops_t clock = {.struct_size=sizeof(clock),.service_id=4,
        .major=PXA_CLOCK_SERVICE_MAJOR,.minor=PXA_CLOCK_SERVICE_MINOR,
        .features=PXA_CLOCK_FEATURE_TIMING,.control=clock_call};
    CHECK(pxa_service_register(runtime, &clock));
    return 0;
}
EMSCRIPTEN_KEEPALIVE int browser_begin(int event) {
    return event ? pxa_component_begin_event(runtime, component) : pxa_component_begin_start(runtime, component);
}
EMSCRIPTEN_KEEPALIVE int browser_configuration(uint8_t *data, uint32_t capacity) {
    pxa_ui_environment_t environment;
    uint8_t encoded[128]; size_t size;
    CHECK(pxa_ui_get_environment(ui, component, PXA_UI_PRIMARY_SURFACE, &environment));
    CHECK(pxa_ui_encode_environment(&environment, encoded, sizeof(encoded), &size));
    pxa_writer_t writer; pxa_writer_init(&writer, data, capacity);
    CHECK(pxa_writer_record(&writer, PXA_UI_CONFIG_ENVIRONMENT, encoded, size));
    /* Locale reflects the Latin font bundle provided by this first port. */
    const uint8_t locale[] = {1,0,5,0,'e','n','-','U','S',2,128,1,0,0};
    CHECK(pxa_writer_record(&writer, 12, locale, sizeof(locale)));
    return (int)writer.size;
}
EMSCRIPTEN_KEEPALIVE int browser_finish(int event, int result) {
    return event ? pxa_component_finish_event(runtime, component, result) : pxa_component_finish_start(runtime, component, result);
}
EMSCRIPTEN_KEEPALIVE int browser_call(uint32_t route, uint64_t token, uint8_t *data, uint32_t size) {
    pxa_message_view_t m = {(uint16_t)route, (uint16_t)(route >> 16), token, {data, size}};
    return pxa_runtime_control_view(runtime, component, &m);
}
EMSCRIPTEN_KEEPALIVE int browser_io(uint64_t handle, uint32_t op, uint8_t *data, uint32_t size) {
    return pxa_runtime_io64(runtime, component, handle, op, data, size);
}
EMSCRIPTEN_KEEPALIVE int browser_event(uint8_t *data, uint32_t capacity) {
    pxa_event_view_t view;
    if (pxa_event_peek(runtime, component, &view) != PXA_STATUS_OK) return 0;
    size_t size = 0;
    int status = pxa_event_pop(runtime, component, data, capacity, &size);
    return status ? status : (int)size;
}
EMSCRIPTEN_KEEPALIVE void browser_tick(double milliseconds) {
    uint64_t us = (uint64_t)(milliseconds * 1000);
    if (us < current_us) return;
    lv_tick_inc((uint32_t)(us / 1000 - current_us / 1000)); current_us = us;
    if (clock_period && current_us >= next_tick) {
        next_tick += ((current_us - next_tick) / clock_period + 1) * clock_period;
        uint8_t bytes[8]; pxa_write_u64(bytes, current_us);
        pxa_event_post_message(runtime, component, 4, PXA_CLOCK_TICK, 0,
            (pxa_bytes_t){bytes, 8}, 0, UINT64_C(0x000400008001));
    }
}
EMSCRIPTEN_KEEPALIVE uint32_t browser_render(void) { lv_timer_handler(); return revision; }
EMSCRIPTEN_KEEPALIVE uint8_t *browser_pixels(void) { return frame; }
EMSCRIPTEN_KEEPALIVE void browser_pointer(int x, int y, int down) {
    pointer_x = x < 0 ? 0 : x >= (int)width ? (int)width - 1 : x;
    pointer_y = y < 0 ? 0 : y >= (int)height ? (int)height - 1 : y;
    pointer_down = !!down; lv_indev_read(pointer);
}
EMSCRIPTEN_KEEPALIVE int browser_controller(uint32_t buttons) {
    uint8_t data[8] = {0, 1}; pxa_write_u32(data + 4, buttons & 4095);
    return pxa_ui_queue_event(ui, component, 1, 1, PXA_UI_EVENT_CONTROLLER_STATE,
        0, current_us, data, sizeof(data));
}
