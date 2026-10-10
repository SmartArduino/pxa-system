/* Exercise actual LVGL hit testing, hidden bars and task-manager navigation. */
#undef NDEBUG
#include "../../../ui/reference/lvgl/src/reference_lvgl.c"
#include "pxsys/lvgl_renderer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static lv_point_t pointer;
static lv_indev_state_t pointer_state;
static unsigned back_calls, background_calls;
static pxsys_standard_system_t *system_under_test;

static void *test_allocate(void *context, size_t bytes) {
    (void)context;
    return malloc(bytes);
}
static void test_release(void *context, void *memory) {
    (void)context;
    free(memory);
}
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    (void)area; (void)pixels;
    lv_display_flush_ready(display);
}
static void read_pointer(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    data->point = pointer;
    data->state = pointer_state;
}
static void advance(unsigned milliseconds) {
    for (unsigned i = 0; i < milliseconds; i += 10) {
        lv_tick_inc(10);
        (void)lv_timer_handler();
    }
}
static void touch(lv_indev_t *indev, int x, int y, lv_indev_state_t state) {
    pointer = (lv_point_t){x, y};
    pointer_state = state;
    lv_indev_read(indev);
    advance(20);
}
static void swipe(lv_indev_t *indev, int x1, int y1, int x2, int y2) {
    touch(indev, x1, y1, LV_INDEV_STATE_PRESSED);
    for (int i = 1; i <= 10; ++i)
        touch(indev, x1 + (x2 - x1) * i / 10, y1 + (y2 - y1) * i / 10,
              LV_INDEV_STATE_PRESSED);
    touch(indev, x2, y2, LV_INDEV_STATE_RELEASED);
    advance(350);
}

static pxsys_status_t game_create(void *context, const pxsys_app_descriptor_t *app,
                                 uint64_t id, void **instance) {
    (void)app; (void)id;
    *instance = context;
    return PXSYS_STATUS_OK;
}
static pxsys_status_t game_start(void *context, void *instance,
                                const pxsys_message_t *message) {
    (void)context; (void)instance; (void)message;
    return PXSYS_STATUS_OK;
}
static pxsys_status_t game_foreground(void *context, void *instance) {
    (void)context; (void)instance;
    pxsys_window_snapshot_t window;
    pxsys_window_snapshot_init(&window);
    window.edge_to_edge = 1;
    window.status_bar_mode = PXSYS_WINDOW_BAR_HIDDEN;
    window.navigation_bar_mode = PXSYS_WINDOW_BAR_HIDDEN;
    return pxsys_window_service_update(
        pxsys_standard_system_window(system_under_test), &window);
}
static pxsys_status_t game_background(void *context, void *instance) {
    (void)context; (void)instance;
    ++background_calls;
    return PXSYS_STATUS_OK;
}
static pxsys_back_result_t game_back(void *context, void *instance) {
    (void)context; (void)instance;
    ++back_calls;
    return PXSYS_BACK_HANDLED;
}
static pxsys_status_t game_event(void *context, void *instance,
                                const pxsys_message_t *message) {
    (void)context; (void)instance; (void)message;
    return PXSYS_STATUS_OK;
}
static void game_stop(void *context, void *instance, pxsys_stop_reason_t reason) {
    (void)context; (void)instance; (void)reason;
}
static void game_destroy(void *context, void *instance) {
    (void)context; (void)instance;
}

int main(void) {
    lv_init();
    lv_display_t *display = lv_display_create(480, 480);
    static uint8_t pixels[480 * 16 * 2];
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels, NULL, sizeof(pixels), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, display);
    lv_indev_set_read_cb(indev, read_pointer);

    pxsys_allocator_t allocator = {.struct_size = sizeof(allocator),
        .allocate = test_allocate, .release = test_release};
    pxsys_lvgl_renderer_config_t renderer_config;
    pxsys_lvgl_renderer_config_init(&renderer_config);
    renderer_config.allocator = allocator;
    renderer_config.parent = lv_screen_active();
    pxsys_lvgl_renderer_t *renderer;
    pxsys_renderer_provider_t provider;
    assert(!pxsys_lvgl_renderer_create(&renderer_config, &renderer));
    assert(!pxsys_lvgl_renderer_provider(renderer, &provider));
    pxsys_standard_system_config_t system_config;
    pxsys_standard_system_config_init(&system_config);
    system_config.allocator = allocator;
    system_config.initial_renderer = &provider;
    pxsys_display_profile_init(&system_config.initial_display, 480, 480);
    system_config.initial_display.density_dpi = 305;
    system_config.initial_display.safe_insets = (pxsys_insets_t){12, 12, 12, 12};
    system_config.initial_display.shape = PXSYS_DISPLAY_SHAPE_ROUNDED_RECTANGLE;
    system_config.initial_display.corner_radii = (pxsys_corner_radii_t){58, 58, 58, 58};
    assert(!pxsys_standard_system_create(&system_config, &system_under_test));

    pxsys_reference_lvgl_config_t ui_config;
    pxsys_reference_lvgl_config_init(&ui_config);
    ui_config.system = system_under_test;
    ui_config.allocator = allocator;
    ui_config.parent = lv_layer_top();
    ui_config.navigation_mode = PXSYS_NAVIGATION_GESTURES;
    pxsys_reference_lvgl_t *ui;
    assert(!pxsys_reference_lvgl_create(&ui_config, &ui));
    assert(!pxsys_reference_lvgl_start(ui));
    advance(50);

    pxsys_app_descriptor_t app = {.struct_size = sizeof(app)};
    app.identity.app_id = pxsys_string_from_cstr("gesture-game");
    app.display_name = pxsys_string_from_cstr("Game");
    app.version = pxsys_string_from_cstr("1.0.0");
    app.runtime_id = pxsys_string_from_cstr(PXSYS_NATIVE_RUNTIME_ID);
    app.flags = PXSYS_APP_FLAG_ENABLED | PXSYS_APP_FLAG_LAUNCHER;
    pxsys_native_app_t implementation = {.struct_size = sizeof(implementation),
        .identity = app.identity, .context = ui, .create = game_create,
        .start = game_start, .foreground = game_foreground,
        .background = game_background, .back = game_back,
        .event = game_event, .stop = game_stop, .destroy = game_destroy};
    assert(!pxsys_app_registry_register(pxsys_standard_system_apps(system_under_test), &app));
    assert(!pxsys_native_runtime_register_app(
        pxsys_standard_system_native_runtime(system_under_test), &implementation));
    pxsys_intent_t intent = {.struct_size = sizeof(intent), .target = &app.identity,
                            .action = pxsys_string_from_cstr("system.intent.main")};
    pxsys_instance_ref_t instance;
    assert(!pxsys_task_manager_start(pxsys_standard_system_tasks(system_under_test), &intent, &instance));
    advance(50);
    assert(!ui->content_active);
    assert(ui->status_bar && ui->navigation_bar && !ui->navigation_handle);
    assert(lv_obj_get_style_bg_opa(ui->status_bar, 0) == LV_OPA_TRANSP);
    assert(lv_obj_get_style_bg_opa(ui->navigation_bar, 0) == LV_OPA_TRANSP);
    /* Fullscreen target edges must remain touchable at the physical x=0. */
    swipe(indev, 0, 240, 9, 240);
    assert(!back_calls);
    swipe(indev, 0, 240, 110, 240);
    assert(back_calls == 1 && !background_calls && !ui->content_active);
    touch(indev, 240, 3, LV_INDEV_STATE_PRESSED);
    touch(indev, 240, 3, LV_INDEV_STATE_RELEASED);
    assert(!ui->notification_shade_open);
    assert(!status_bar_is_visible(ui));
    swipe(indev, 240, 3, 240, 9);
    assert(!status_bar_is_visible(ui) && !ui->notification_shade_open);
    /* First swipe reveals only status chrome, and owns the whole drag even
     * after crossing the shade's opening threshold. No fullscreen resize. */
    touch(indev, 240, 3, LV_INDEV_STATE_PRESSED);
    touch(indev, 240, 280, LV_INDEV_STATE_PRESSED);
    assert(!status_bar_is_visible(ui) && !ui->notification_shade_open);
    touch(indev, 240, 280, LV_INDEV_STATE_RELEASED);
    advance(50);
    assert(status_bar_is_visible(ui) && !ui->notification_shade_open);
    assert(ui->transient_revealed == CHROME_REVEALED_HIDDEN_STATUS);
    assert(lv_obj_get_style_bg_opa(ui->status_bar, 0) != LV_OPA_TRANSP);
    assert(!bar_is_visible(ui, navigation_bar_mode(ui)));
    assert(!ui->navigation_handle);
    assert(ui->content_inset_top == 0 && ui->content_inset_bottom == 0);
    assert(!ui->content_active && !background_calls);
    /* Status samples must update the temporarily shown bar as well. */
    ui->system_status.time_valid = 1;
    ui->system_status.hour = 12;
    ui->system_status.minute = 34;
    ui->status_refresh_pending = 1;
    status_refresh_poll(ui->status_refresh_timer);
    assert(strcmp(lv_label_get_text(lv_obj_get_child(ui->status_bar, 0)),
                  "12:34") == 0);
    swipe(indev, 240, 3, 240, 280);
    assert(ui->notification_shade_open && ui->notification_progress == 256);
    advance(TRANSIENT_BAR_TIMEOUT_MS + 100);
    assert(ui->notification_shade_open && status_bar_is_visible(ui));
    swipe(indev, 0, 240, 110, 240);
    assert(!ui->notification_shade_open && back_calls == 1);
    assert(ui->window.status_bar_mode == PXSYS_WINDOW_BAR_HIDDEN);
    assert(ui->window.navigation_bar_mode == PXSYS_WINDOW_BAR_HIDDEN);
    advance(TRANSIENT_BAR_TIMEOUT_MS + 100);
    assert(!status_bar_is_visible(ui) && !ui->transient_revealed);
    assert(lv_obj_get_style_bg_opa(ui->status_bar, 0) == LV_OPA_TRANSP);
    lv_timer_t *reused_timer = ui->transient_timer;
    swipe(indev, 240, 3, 240, 280);
    assert(status_bar_is_visible(ui) && !ui->notification_shade_open);
    assert(ui->transient_timer == reused_timer);
    /* A held second gesture cannot lose its target at the reveal timeout. */
    touch(indev, 240, 3, LV_INDEV_STATE_PRESSED);
    advance(TRANSIENT_BAR_TIMEOUT_MS + 100);
    assert(status_bar_is_visible(ui));
    touch(indev, 240, 280, LV_INDEV_STATE_PRESSED);
    touch(indev, 240, 280, LV_INDEV_STATE_RELEASED);
    advance(350);
    assert(ui->notification_shade_open);
    close_notification_shade(ui);
    /* Cancelling a first gesture cannot reveal chrome or open the shade. */
    window_changed(ui, &ui->window);
    advance(50);
    touch(indev, 240, 3, LV_INDEV_STATE_PRESSED);
    touch(indev, 240, 100, LV_INDEV_STATE_PRESSED);
    lv_obj_send_event(ui->status_bar, LV_EVENT_PRESS_LOST, indev);
    lv_indev_wait_release(indev);
    touch(indev, 240, 100, LV_INDEV_STATE_RELEASED);
    assert(!status_bar_is_visible(ui) && !ui->notification_shade_open);
    /* Temporarily revealing hidden status must not also reveal an unrelated
     * transient navigation bar. The two states share the existing byte. */
    pxsys_window_snapshot_t mixed = ui->window;
    mixed.navigation_bar_mode = PXSYS_WINDOW_BAR_TRANSIENT;
    window_changed(ui, &mixed);
    advance(50);
    swipe(indev, 240, 3, 240, 280);
    assert(status_bar_is_visible(ui) && !ui->notification_shade_open);
    assert(!bar_is_visible(ui, navigation_bar_mode(ui)));
    reveal_transient_chrome(ui);
    advance(50);
    assert(status_bar_is_visible(ui) && bar_is_visible(ui, navigation_bar_mode(ui)));
    /* Normal transient status keeps its established reveal-first behavior. */
    mixed.status_bar_mode = PXSYS_WINDOW_BAR_TRANSIENT;
    mixed.navigation_bar_mode = PXSYS_WINDOW_BAR_HIDDEN;
    window_changed(ui, &mixed);
    advance(50);
    swipe(indev, 240, 3, 240, 280);
    assert(status_bar_is_visible(ui) && !ui->notification_shade_open);
    swipe(indev, 240, 3, 240, 280);
    assert(ui->notification_shade_open);
    mixed.status_bar_mode = PXSYS_WINDOW_BAR_HIDDEN;
    window_changed(ui, &mixed);
    advance(50);
    assert(!status_bar_is_visible(ui) && !ui->notification_shade_open);
    swipe(indev, 240, 478, 240, 280);
    assert(ui->content_active && ui->active_page == REFERENCE_PAGE_HOME);
    assert(background_calls == 1);

    /* Tapping a running game's launcher icon must keep Home in the stack,
     * reuse the game, then restore Home when the Guest exits independently. */
    size_t retained_tasks = pxsys_task_manager_count(
        pxsys_standard_system_tasks(system_under_test));
    assert(retained_tasks == 2);
    size_t game_tile = 0;
    while (game_tile < ui->launcher_count &&
           !pxsys_app_identity_equal(&ui->launcher_items[game_tile].identity, &app.identity))
        ++game_tile;
    assert(game_tile < ui->launcher_count);
    lv_area_t tile;
    lv_obj_get_coords(ui->launcher_items[game_tile].tile, &tile);
    int x = (tile.x1 + tile.x2) / 2, y = (tile.y1 + tile.y2) / 2;
    touch(indev, x, y, LV_INDEV_STATE_PRESSED);
    touch(indev, x, y, LV_INDEV_STATE_RELEASED);
    advance(50);
    assert(!ui->content_active);
    assert(pxsys_task_manager_count(pxsys_standard_system_tasks(system_under_test)) == retained_tasks);
    pxsys_instance_ref_t resumed;
    assert(!pxsys_task_manager_current(pxsys_standard_system_tasks(system_under_test), &resumed));
    assert(resumed.slot == instance.slot && resumed.generation == instance.generation);
    assert(!pxsys_task_manager_report_stopped(
        pxsys_standard_system_tasks(system_under_test), resumed, PXSYS_STOP_NORMAL));
    advance(50);
    assert(ui->content_active && ui->active_page == REFERENCE_PAGE_HOME);
    assert(pxsys_task_manager_count(pxsys_standard_system_tasks(system_under_test)) == 1);

    assert(!pxsys_task_manager_finish_all(
        pxsys_standard_system_tasks(system_under_test), PXSYS_STOP_SHUTDOWN));
    assert(!pxsys_reference_lvgl_destroy(ui));
    assert(!pxsys_standard_system_destroy(system_under_test));
    assert(!pxsys_lvgl_renderer_destroy(renderer));
    lv_deinit();
    puts("Fullscreen system gestures: Back, reveal-first status/shade, timeout, cancellation, mixed bar modes, Home, resume and Guest exit passed");
    return 0;
}
