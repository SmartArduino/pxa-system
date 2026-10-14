/* Include the implementation to check role and timer state as well as public
 * lifecycle behavior. The linked archive still supplies the IME implementation. */
#undef NDEBUG
#if PXSYS_TEST_NO_SNAPSHOT
#include "lvgl.h"
#undef LV_USE_SNAPSHOT
#define LV_USE_SNAPSHOT 0
#endif
#if defined(PXSYS_TEST_PINYIN)
#include "lvgl.h"
#undef LV_USE_IME_PINYIN
#define LV_USE_IME_PINYIN PXSYS_TEST_PINYIN
#undef LV_IME_PINYIN_USE_K9_MODE
#define LV_IME_PINYIN_USE_K9_MODE 0
#endif
#include "../lvgl/src/reference_lvgl.c"
#include <assert.h>
#include <stdio.h>

static void* test_allocate(void* context, size_t bytes) {
    (void)context;
    return malloc(bytes);
}

static void test_release(void* context, void* memory) {
    (void)context;
    free(memory);
}

static void flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels) {
    (void)area;
    (void)pixels;
    lv_display_flush_ready(display);
}

static void check_role(pxsys_standard_system_t* system, const char* role,
                       bool enabled) {
    const pxsys_app_descriptor_t* app = NULL;
    pxsys_status_t status = pxsys_role_resolve(
        pxsys_standard_system_roles(system), pxsys_string_from_cstr(role), &app);
    assert(status == (enabled ? PXSYS_STATUS_OK : PXSYS_STATUS_NOT_FOUND));
}

static void check_features(uint32_t features) {
    pxsys_standard_system_config_t system_config;
    pxsys_standard_system_t* system = NULL;
    pxsys_reference_lvgl_config_t ui_config;
    pxsys_reference_lvgl_t* ui = NULL;
    pxsys_standard_system_config_init(&system_config);
    system_config.allocator.allocate = test_allocate;
    system_config.allocator.release = test_release;
    pxsys_display_profile_init(&system_config.initial_display, 320, 240);
    system_config.initial_system_status.battery_valid = 1;
    system_config.initial_system_status.battery_percent = 42;
    system_config.initial_system_status.wifi_supported = 1;
    system_config.initial_system_status.wifi_enabled = 1;
    assert(pxsys_standard_system_create(&system_config, &system) == PXSYS_STATUS_OK);
    pxsys_reference_lvgl_config_init(&ui_config);
    ui_config.system = system;
    ui_config.allocator = system_config.allocator;
    ui_config.parent = lv_layer_top();
    ui_config.features = features;
    assert(pxsys_reference_lvgl_create(&ui_config, &ui) == PXSYS_STATUS_OK);
    check_role(system, PXSYS_ROLE_HOME, (features & PXSYS_REFERENCE_UI_HOME) != 0);
    check_role(system, PXSYS_ROLE_SETTINGS, (features & PXSYS_REFERENCE_UI_SETTINGS) != 0);
    check_role(system, PXSYS_ROLE_STATUS_BAR, (features & PXSYS_REFERENCE_UI_STATUS_BAR) != 0);
    check_role(system, PXSYS_ROLE_NAVIGATION_BAR, (features & PXSYS_REFERENCE_UI_NAVIGATION_BAR) != 0);
    check_role(system, PXSYS_ROLE_SOUND_SETTINGS,
        PXSYS_REFERENCE_UI_BUILTIN_SETTINGS && (features & PXSYS_REFERENCE_UI_SOUND_SETTINGS));
    assert((ui->app_ime_timer != NULL) == !!PXSYS_REFERENCE_UI_BUILTIN_SETTINGS);
    assert(pxsys_reference_lvgl_start(ui) == PXSYS_STATUS_OK);
    assert((ui->status_bar != NULL) == !!(features & PXSYS_REFERENCE_UI_STATUS_BAR));
    assert((ui->navigation_bar != NULL) == !!(features & PXSYS_REFERENCE_UI_NAVIGATION_BAR));
    assert(!!ui->content_active == !!(features & PXSYS_REFERENCE_UI_HOME));
    if (!(features & PXSYS_REFERENCE_UI_HOME))
        assert(pxsys_reference_lvgl_home(ui) == PXSYS_STATUS_NOT_FOUND);
    if (features & PXSYS_REFERENCE_UI_SETTINGS) {
        assert(open_role(ui, PXSYS_ROLE_SETTINGS) == PXSYS_STATUS_OK);
        assert(ui->active_page == REFERENCE_PAGE_SETTINGS && ui->content != NULL);
    }
    pxsys_theme_snapshot_t theme;
    pxsys_theme_snapshot_init(&theme, PXSYS_COLOR_SCHEME_LIGHT);
    assert(pxsys_theme_service_update(pxsys_standard_system_theme(system), &theme) == PXSYS_STATUS_OK);
    pxsys_reference_lvgl_refresh_apps(ui);
    build_task_switcher(ui);
    assert(ui->task_switcher != NULL);
    close_task_switcher(ui);
    lv_tick_inc(250);
    (void)lv_timer_handler();
#if PXSYS_REFERENCE_UI_BUILTIN_SETTINGS
    lv_obj_t* input = lv_textarea_create(lv_screen_active());
    app_input_method_open(ui, input);
    assert(ui->app_ime != NULL);
#if !LV_USE_IME_PINYIN || !LV_IME_PINYIN_USE_K9_MODE
    assert(ui->app_ime_keyboard != NULL);
#endif
#if !LV_USE_IME_PINYIN
    assert(ui->app_ime_pinyin == NULL);
#endif
    app_input_method_close(ui);
    lv_obj_delete(input);
#endif
    assert(pxsys_reference_lvgl_set_animations_enabled(ui, false) == PXSYS_STATUS_OK);
    pxsys_toast_message_t toast;
    pxsys_toast_message_init(&toast, pxsys_string_from_cstr("Immediate toast"));
    assert(pxsys_toast_service_post(pxsys_standard_system_toasts(system), &toast) == PXSYS_STATUS_OK);
    assert(ui->toast_visible && lv_obj_get_style_opa(ui->toast, 0) == LV_OPA_COVER);
    assert(pxsys_task_manager_finish_all(pxsys_standard_system_tasks(system), PXSYS_STOP_SHUTDOWN) == PXSYS_STATUS_OK);
    assert(pxsys_reference_lvgl_destroy(ui) == PXSYS_STATUS_OK);
    assert(pxsys_standard_system_destroy(system) == PXSYS_STATUS_OK);
}

int main(void) {
    static uint8_t buffer[320 * 10 * 4];
    lv_init();
    lv_display_t* display = lv_display_create(320, 240);
    lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    /* All combinations of Home, Settings and the two independently owned bars. */
    for (uint32_t features = 0; features < 16; ++features)
        check_features(features | PXSYS_REFERENCE_UI_SOUND_SETTINGS);
    check_features(PXSYS_REFERENCE_UI_ALL);
    lv_deinit();
    puts("Reference feature masks: role registration, chrome, startup, settings, theme, recents and repeated teardown passed");
    return 0;
}
