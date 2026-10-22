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

#if PXSYS_REFERENCE_UI_BUILTIN_SETTINGS
static bool test_managed_input(void *context,void *textarea) {
    return textarea == context;
}
static size_t empty_file_list(void* context, const char* path,
    pxsys_reference_file_entry_t* entries, size_t capacity) {
    (void)context; (void)path; (void)entries; (void)capacity;
    return 0;
}
#endif

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

static void check_gamepad_lock(pxsys_reference_lvgl_t* ui) {
    lv_style_t keypad_outline;
    lv_style_init(&keypad_outline);
    lv_style_set_outline_width(&keypad_outline, 2);
    assert(!pxsys_reference_lvgl_handle_gamepad(NULL, 1));
    for (unsigned bit = 0; bit < 14; ++bit) {
        const uint32_t button = UINT32_C(1) << bit;
        assert(pxsys_reference_lvgl_set_locked(ui, true) == PXSYS_STATUS_OK);
        assert(pxsys_reference_lvgl_handle_gamepad(ui, 0) && ui->locked);
        lv_obj_add_style(ui->lock_screen, &keypad_outline,
                         LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
        lv_obj_add_state(ui->lock_screen, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
        assert(lv_obj_get_style_outline_width(ui->lock_screen, LV_PART_MAIN) == 0);
        /* Every direction, face, shoulder and system button unlocks on press.
         * Holding or joining another button must not reach the resumed UI. */
        assert(pxsys_reference_lvgl_handle_gamepad(ui, button) && !ui->locked);
        assert(pxsys_reference_lvgl_handle_gamepad(ui, button));
        assert(pxsys_reference_lvgl_handle_gamepad(ui, button | 1u));
        assert(pxsys_reference_lvgl_handle_gamepad(ui, 0));
        assert(!pxsys_reference_lvgl_handle_gamepad(ui, 0));
    }
    lv_style_reset(&keypad_outline);
    /* Locking while playing must not immediately unlock on a held button. */
    assert(!pxsys_reference_lvgl_handle_gamepad(ui, 1));
    assert(pxsys_reference_lvgl_set_locked(ui, true) == PXSYS_STATUS_OK);
    assert(pxsys_reference_lvgl_handle_gamepad(ui, 1) && ui->locked);
    assert(pxsys_reference_lvgl_handle_gamepad(ui, 3) && ui->locked);
    assert(pxsys_reference_lvgl_handle_gamepad(ui, 0) && ui->locked);
    assert(pxsys_reference_lvgl_handle_gamepad(ui, 2) && !ui->locked);
    assert(pxsys_reference_lvgl_handle_gamepad(ui, 0));
    assert(!pxsys_reference_lvgl_handle_gamepad(ui, 0));
    /* A touch/external unlock with an idle gamepad needs no extra key press. */
    assert(pxsys_reference_lvgl_set_locked(ui, true) == PXSYS_STATUS_OK);
    assert(pxsys_reference_lvgl_handle_gamepad(ui, 0));
    assert(pxsys_reference_lvgl_set_locked(ui, false) == PXSYS_STATUS_OK);
    assert(!pxsys_reference_lvgl_handle_gamepad(ui, 0));
}

static void check_settings_safe_area(pxsys_standard_system_t* system,
                                      pxsys_reference_lvgl_t* ui) {
    const pxsys_display_profile_t original = ui->display;
    const pxsys_insets_t insets[] = {{0, 0, 0, 0}, {4, 6, 8, 10}, {12, 16, 24, 20}};
    for (unsigned mode = 0; mode < 2; ++mode) {
        assert(pxsys_reference_lvgl_set_navigation_mode(ui,
            mode ? PXSYS_NAVIGATION_GESTURES : PXSYS_NAVIGATION_BUTTONS) == PXSYS_STATUS_OK);
        for (size_t i = 0; i < sizeof(insets) / sizeof(insets[0]); ++i) {
            pxsys_display_profile_t display = original;
            display.safe_insets = insets[i];
            assert(pxsys_display_service_update(pxsys_standard_system_display(system),
                                                &display) == PXSYS_STATUS_OK);
            lv_obj_update_layout(ui->root);
            lv_area_t content;
            lv_obj_get_coords(ui->content, &content);
            /* No navigation role means the viewport ends exactly at the
             * system safe edge, in both button and gesture modes. */
            assert(content.x1 == display.safe_insets.left);
            assert(content.x2 == (int32_t)display.width - display.safe_insets.right - 1);
            assert(content.y2 == (int32_t)display.height - display.safe_insets.bottom - 1);
        }
    }
    assert(pxsys_display_service_update(pxsys_standard_system_display(system),
                                        &original) == PXSYS_STATUS_OK);
    assert(pxsys_reference_lvgl_set_navigation_mode(ui,
        PXSYS_NAVIGATION_BUTTONS) == PXSYS_STATUS_OK);
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
#if PXSYS_REFERENCE_UI_BUILTIN_SETTINGS
    ui_config.file_list = empty_file_list;
#endif
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
        if (!(features & PXSYS_REFERENCE_UI_NAVIGATION_BAR))
            check_settings_safe_area(system, ui);
    }
    pxsys_theme_snapshot_t theme;
    pxsys_theme_snapshot_init(&theme, PXSYS_COLOR_SCHEME_LIGHT);
    assert(pxsys_theme_service_update(pxsys_standard_system_theme(system), &theme) == PXSYS_STATUS_OK);
    pxsys_reference_lvgl_refresh_apps(ui);
    build_task_switcher(ui);
    assert(ui->task_switcher != NULL);
    assert(pxsys_reference_lvgl_key_navigation_scope(ui) == ui->task_switcher);
    assert(pxsys_reference_lvgl_back(ui) == PXSYS_STATUS_OK);
    assert(!task_switcher_is_open(ui));
    close_task_switcher(ui);
    assert(pxsys_reference_lvgl_set_locked(ui, true) == PXSYS_STATUS_OK);
    assert(pxsys_reference_lvgl_key_navigation_scope(ui) == ui->lock_screen);
    assert(pxsys_reference_lvgl_back(ui) == PXSYS_STATUS_OK && ui->locked);
    uint32_t unlock_key = LV_KEY_ENTER;
    lv_obj_send_event(ui->lock_screen, LV_EVENT_KEY, &unlock_key);
    assert(!ui->locked);
    check_gamepad_lock(ui);
    if (ui->status_bar && (features & PXSYS_REFERENCE_UI_NOTIFICATION_SHADE)) {
        lv_obj_send_event(ui->status_bar, LV_EVENT_KEY, &unlock_key);
        assert(ui->notification_shade_open);
        assert(pxsys_reference_lvgl_key_navigation_scope(ui) == ui->notification_panel);
        assert(pxsys_reference_lvgl_back(ui) == PXSYS_STATUS_OK);
        assert(!ui->notification_shade_open);
    }
#if PXSYS_REFERENCE_UI_BUILTIN_SETTINGS
    if (features & PXSYS_REFERENCE_UI_FILE_MANAGER) {
        assert(open_role(ui, PXSYS_ROLE_FILE_MANAGER) == PXSYS_STATUS_OK);
        strcpy(ui->file_path, "/sdcard/roms");
        assert(pxsys_reference_lvgl_back(ui) == PXSYS_STATUS_OK);
        assert(strcmp(ui->file_path, "/sdcard") == 0);
        assert(pxsys_reference_lvgl_back(ui) == PXSYS_STATUS_OK);
        assert(ui->file_path[0] == '\0');
        show_page_confirm(ui, "Confirm test");
        assert(ui->confirm_dialog != NULL);
        assert(pxsys_reference_lvgl_key_navigation_scope(ui) == ui->confirm_dialog);
        assert(pxsys_reference_lvgl_back(ui) == PXSYS_STATUS_OK);
        assert(ui->confirm_dialog == NULL && ui->active_page == REFERENCE_PAGE_FILE_MANAGER);
    }
#endif
    lv_tick_inc(250);
    (void)lv_timer_handler();
#if PXSYS_REFERENCE_UI_BUILTIN_SETTINGS
    lv_obj_t* input = lv_textarea_create(lv_screen_active());
    app_input_method_open(ui, input);
    assert(ui->app_ime != NULL);
    assert(ui->app_ime_keypad != NULL);
    assert(pxsys_reference_ime_is_visible(ui->app_ime_keypad));
    /* Native and external keyboards share an ownership marker. A later
     * system poll must keep its own native keyboard open. */
    lv_obj_add_state(input,LV_STATE_FOCUSED);
    ui->input_method_managed=test_managed_input;
    ui->input_method_context=input;
    pxsys_reference_ime_t *opened=ui->app_ime_keypad;
    app_input_method_poll(ui->app_ime_timer);
    assert(ui->app_ime_keypad == opened);
    assert(pxsys_reference_ime_is_visible(opened));
    ui->input_method_managed=NULL;
    ui->input_method_context=NULL;
    app_input_method_close(ui);
    assert(ui->app_ime == NULL && ui->app_ime_keypad == NULL);
    lv_group_t* key_group = lv_group_create();
    lv_indev_t* keys = lv_indev_create();
    lv_indev_set_type(keys, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_group(keys, key_group);
    lv_group_add_obj(key_group, input);
    app_input_method_poll(ui->app_ime_timer);
    assert(ui->app_ime == NULL); /* Focus alone must not open a keyboard. */
    assert(pxsys_reference_lvgl_key_activate(ui, input) == PXSYS_STATUS_OK);
    assert(ui->app_ime != NULL);
    assert(pxsys_reference_lvgl_key_navigation_scope(ui) == ui->app_ime);
    lv_group_remove_obj(input);
    /* A keyboard key takes focus away from its text editor. Polling must
     * preserve the session while keypad focus is inside that keyboard. */
    lv_obj_t* key = lv_button_create(ui->app_ime);
    lv_group_add_obj(key_group, key);
    app_input_method_poll(ui->app_ime_timer);
    assert(ui->app_ime != NULL);
    assert(pxsys_reference_lvgl_back(ui) == PXSYS_STATUS_OK);
    assert(ui->app_ime == NULL);
    lv_group_add_obj(key_group, input);
    app_input_method_poll(ui->app_ime_timer);
    assert(ui->app_ime == NULL); /* Back does not immediately reopen it. */
    lv_indev_delete(keys);
    lv_group_delete(key_group);
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
