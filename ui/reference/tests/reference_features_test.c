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

static void check_bar_background(lv_obj_t* bar, uint32_t rgba) {
    assert(bar != NULL);
    assert(lv_color_eq(lv_obj_get_style_bg_color(bar, 0), lv_color_hex(rgba >> 8)));
    assert(lv_obj_get_style_bg_opa(bar, 0) == (rgba & 255u));
#if LV_USE_SNAPSHOT
    lv_draw_buf_t* image = lv_snapshot_take(bar, LV_COLOR_FORMAT_ARGB8888);
    assert(image != NULL);
    lv_color32_t corner;
    memcpy(&corner, image->data, sizeof(corner));
    /* The first pixel is outside every glyph. Inspect rendered alpha as well
     * as styles; a set color on an invisible object would otherwise pass. */
    assert(abs((int)corner.alpha - (int)(rgba & 255u)) <= 1);
    if ((rgba & 255u) == 255u) {
        assert(corner.red == (rgba >> 24));
        assert(corner.green == ((rgba >> 16) & 255u));
        assert(corner.blue == ((rgba >> 8) & 255u));
    }
    lv_draw_buf_destroy(image);
#endif
}

static void check_status_foreground(pxsys_reference_lvgl_t* ui, uint32_t rgb) {
    lv_obj_t* time_label = lv_obj_get_child(ui->status_bar, 0);
    assert(lv_color_eq(lv_obj_get_style_text_color(time_label, 0), lv_color_hex(rgb)));
    int battery_found = 0;
    for (uint32_t i = 0; i < lv_obj_get_child_count(ui->status_bar); ++i) {
        lv_obj_t* child = lv_obj_get_child(ui->status_bar, (int32_t)i);
        if (lv_obj_get_style_border_width(child, 0) == 1) {
            battery_found = 1;
            assert(lv_color_eq(lv_obj_get_style_border_color(child, 0), lv_color_hex(rgb)));
        }
    }
    assert(battery_found);
}

static void check_window_chrome(pxsys_reference_lvgl_t* ui) {
    const pxsys_window_snapshot_t saved_window = ui->window;
    const pxsys_theme_snapshot_t saved_theme = ui->theme;
    const uint8_t saved_content = ui->content_active;
    const pxsys_navigation_mode_t saved_mode = ui->navigation_mode;
    pxsys_window_snapshot_t next = saved_window;
    ui->content_active = 0; /* The foreground task is a Guest. */
    next.status_bar_mode = next.navigation_bar_mode = PXSYS_WINDOW_BAR_VISIBLE;
    assert(pxsys_reference_lvgl_set_navigation_mode(ui, PXSYS_NAVIGATION_BUTTONS) == PXSYS_STATUS_OK);
    for (unsigned scheme = 0; scheme < 2; ++scheme) {
        pxsys_theme_snapshot_t theme;
        pxsys_theme_snapshot_init(&theme, (pxsys_color_scheme_t)scheme);
        assert(pxsys_theme_service_update(pxsys_standard_system_theme(ui->system), &theme) == PXSYS_STATUS_OK);
        next.status_bar_color = UINT32_C(0xeeddbbff);
        next.navigation_bar_color = UINT32_C(0x123456ff);
        next.status_bar_icons = PXSYS_WINDOW_ICON_DARK;
        next.navigation_bar_icons = PXSYS_WINDOW_ICON_LIGHT;
        window_changed(ui, &next);
        check_bar_background(ui->status_bar, next.status_bar_color);
        check_bar_background(ui->navigation_bar, next.navigation_bar_color);
        check_status_foreground(ui, 0);
        for (int i = 0; i < 3; ++i) {
            lv_obj_t* glyph = lv_obj_get_child(lv_obj_get_child(ui->navigation_bar, i), 0);
            assert(lv_color_eq(i == 0 ? lv_obj_get_style_line_color(glyph, 0) :
                lv_obj_get_style_border_color(glyph, 0), lv_color_hex(0xffffff)));
        }
        next.status_bar_icons = next.navigation_bar_icons = PXSYS_WINDOW_ICON_AUTO;
        window_changed(ui, &next);
        check_status_foreground(ui, 0); /* Light background, dark AUTO icons. */
        next.status_bar_color = UINT32_C(0x123456ff);
        window_changed(ui, &next);
        check_status_foreground(ui, 0xffffff);
        next.status_bar_color = UINT32_C(0x12345680);
        next.navigation_bar_color = UINT32_C(0xeeddbb80);
        window_changed(ui, &next);
        check_bar_background(ui->status_bar, next.status_bar_color);
        check_bar_background(ui->navigation_bar, next.navigation_bar_color);
        next.status_bar_color = next.navigation_bar_color = 0;
        next.status_bar_icons = PXSYS_WINDOW_ICON_LIGHT;
        next.navigation_bar_icons = PXSYS_WINDOW_ICON_DARK;
        window_changed(ui, &next);
        check_bar_background(ui->status_bar, 0);
        check_bar_background(ui->navigation_bar, 0);
        check_status_foreground(ui, 0xffffff);
        assert(pxsys_reference_lvgl_set_navigation_mode(ui, PXSYS_NAVIGATION_GESTURES) == PXSYS_STATUS_OK);
#if PXSYS_REFERENCE_UI_GESTURE_HANDLE
        assert(ui->navigation_handle != NULL);
        assert(lv_obj_get_style_bg_opa(ui->navigation_handle, 0) == LV_OPA_COVER);
        assert(lv_color_eq(lv_obj_get_style_bg_color(ui->navigation_handle, 0), lv_color_hex(0)));
#if LV_USE_SNAPSHOT
        lv_draw_buf_t* handle = lv_snapshot_take(ui->navigation_handle, LV_COLOR_FORMAT_ARGB8888);
        assert(handle != NULL);
        lv_color32_t center;
        memcpy(&center, handle->data + (handle->header.h / 2) * handle->header.stride +
            (handle->header.w / 2) * sizeof(center), sizeof(center));
        assert(center.alpha == 255 && center.red == 0 && center.green == 0 && center.blue == 0);
        lv_draw_buf_destroy(handle);
#endif
        next.navigation_bar_icons = PXSYS_WINDOW_ICON_LIGHT;
        window_changed(ui, &next);
        assert(lv_color_eq(lv_obj_get_style_bg_color(ui->navigation_handle, 0), lv_color_hex(0xffffff)));
#endif
        next.status_bar_mode = next.navigation_bar_mode = PXSYS_WINDOW_BAR_TRANSIENT;
        next.status_bar_color = UINT32_C(0x12345680);
        next.navigation_bar_color = UINT32_C(0xeeddbb80);
        window_changed(ui, &next);
        assert(!status_bar_is_visible(ui) && ui->navigation_handle == NULL);
        if (ui->status_bar != NULL) {
            assert(lv_obj_get_child_count(ui->status_bar) == 0); /* Input catcher only. */
            assert(lv_obj_get_style_bg_opa(ui->status_bar, 0) == LV_OPA_TRANSP);
        }
        assert(lv_obj_get_style_bg_opa(ui->navigation_bar, 0) == LV_OPA_TRANSP);
        reveal_transient_chrome(ui);
        (void)lv_timer_handler();
        check_bar_background(ui->status_bar, next.status_bar_color);
        check_bar_background(ui->navigation_bar, next.navigation_bar_color);
        /* Native Home must use the theme, not the previous Guest's style. */
        ui->content_active = 1;
        rebuild(ui);
        assert(lv_color_eq(lv_obj_get_style_text_color(lv_obj_get_child(ui->status_bar, 0), 0),
            color_token(ui, PXSYS_COLOR_TEXT_PRIMARY)));
        assert(lv_obj_get_style_bg_opa(ui->status_bar, 0) == LV_OPA_TRANSP);
        ui->content_active = 0;
        next.status_bar_mode = next.navigation_bar_mode = PXSYS_WINDOW_BAR_VISIBLE;
        assert(pxsys_reference_lvgl_set_navigation_mode(ui, PXSYS_NAVIGATION_BUTTONS) == PXSYS_STATUS_OK);
    }
    ui->content_active = saved_content;
    window_changed(ui, &saved_window);
    assert(pxsys_reference_lvgl_set_navigation_mode(ui, saved_mode) == PXSYS_STATUS_OK);
    assert(pxsys_theme_service_update(pxsys_standard_system_theme(ui->system), &saved_theme) == PXSYS_STATUS_OK);
}

#if PXSYS_REFERENCE_UI_WIFI && LV_USE_SNAPSHOT
static lv_obj_t* status_wifi_icon(pxsys_reference_lvgl_t* ui) {
    for (uint32_t i = 0; i < lv_obj_get_child_count(ui->status_bar); ++i) {
        lv_obj_t* child = lv_obj_get_child(ui->status_bar, (int32_t)i);
        for (uint32_t e = 0; e < lv_obj_get_event_count(child); ++e)
            if (lv_event_dsc_get_cb(lv_obj_get_event_dsc(child, e)) == wifi_icon_draw)
                return child;
    }
    return NULL;
}

static uint64_t wifi_pixels(pxsys_reference_lvgl_t* ui, uint8_t* pixels) {
    lv_obj_t* icon = status_wifi_icon(ui);
    assert(icon != NULL);
    lv_draw_buf_t* image = lv_snapshot_take(icon, LV_COLOR_FORMAT_RGB888);
    assert(image && image->header.w == 16 && image->header.h == 16);
    uint64_t hash = UINT64_C(14695981039346656037);
    for (uint32_t y = 0; y < image->header.h; ++y)
        for (uint32_t x = 0; x < image->header.w * 3; ++x) {
            if (pixels) pixels[y * image->header.w * 3 + x] = image->data[y * image->header.stride + x];
            hash ^= image->data[y * image->header.stride + x];
            hash *= UINT64_C(1099511628211);
        }
    lv_draw_buf_destroy(image);
    return hash;
}

static void check_wifi_signal(pxsys_standard_system_t* system, pxsys_reference_lvgl_t* ui) {
    pxsys_system_status_snapshot_t saved, next;
    pxsys_system_status_snapshot_init(&saved);
    assert(pxsys_system_status_service_get(pxsys_standard_system_status(system), &saved) == PXSYS_STATUS_OK);
    next = saved;
    next.wifi_supported = next.wifi_enabled = next.wifi_connected = 1;
    uint64_t levels[5];
    uint8_t pixels[5][16 * 16 * 3];
    for (uint8_t level = 0; level <= 4; ++level) {
        next.wifi_signal_level = level;
        assert(pxsys_system_status_service_update(pxsys_standard_system_status(system), &next) == PXSYS_STATUS_OK);
        levels[level] = wifi_pixels(ui, pixels[level]);
        for (uint8_t previous = 0; previous < level; ++previous)
            assert(levels[previous] != levels[level]);
        if (level) {
            int left = 16, right = -1;
            for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
                const int at = (y * 16 + x) * 3;
                if (memcmp(pixels[level] + at, pixels[level - 1] + at, 3) != 0) {
                    if (x < left) left = x;
                    if (x > right) right = x;
                }
            }
            assert(right >= left && left + right == 15); /* Dot and arcs share x=7.5. */
        }
    }
    const pxsys_window_snapshot_t saved_window = ui->window;
    const uint8_t saved_content = ui->content_active;
    pxsys_window_snapshot_t window = saved_window;
    ui->content_active = 0;
    window.status_bar_color = UINT32_C(0x7f7f7fff);
    window.status_bar_icons = PXSYS_WINDOW_ICON_LIGHT;
    window_changed(ui, &window);
    uint8_t light_pixels[16 * 16 * 3], dark_pixels[16 * 16 * 3];
    const uint64_t light = wifi_pixels(ui, light_pixels);
    window.status_bar_icons = PXSYS_WINDOW_ICON_DARK;
    window_changed(ui, &window);
    assert(light != wifi_pixels(ui, dark_pixels));
    for (int channel = 0; channel < 3; ++channel) {
        const int at = (12 * 16 + 7) * 3 + channel;
        assert(light_pixels[at] > dark_pixels[at] + 64); /* Antialiased 2x2 dot. */
    }
    ui->content_active = saved_content;
    window_changed(ui, &saved_window);
    next.wifi_connected = 0; /* Stale strong RSSI must not look connected. */
    assert(pxsys_system_status_service_update(pxsys_standard_system_status(system), &next) == PXSYS_STATUS_OK);
    const uint64_t disconnected = wifi_pixels(ui, NULL);
    for (uint8_t level = 0; level <= 4; ++level) assert(disconnected != levels[level]);
    next.wifi_signal_level = 0;
    assert(pxsys_system_status_service_update(pxsys_standard_system_status(system), &next) == PXSYS_STATUS_OK);
    assert(disconnected == wifi_pixels(ui, NULL));
    next.wifi_enabled = 0;
    assert(pxsys_system_status_service_update(pxsys_standard_system_status(system), &next) == PXSYS_STATUS_OK);
    assert(status_wifi_icon(ui) == NULL);
    next.wifi_supported = next.cellular_supported = 0;
    next.network_type = PXSYS_NETWORK_WIFI;
    next.network_connected = 1;
    next.network_signal_level = 2;
    assert(pxsys_system_status_service_update(pxsys_standard_system_status(system), &next) == PXSYS_STATUS_OK);
    assert(levels[2] == wifi_pixels(ui, NULL)); /* Legacy providers still drive the icon. */
    assert(pxsys_system_status_service_update(pxsys_standard_system_status(system), &saved) == PXSYS_STATUS_OK);
}
#endif

static int status_has_power_icon(pxsys_reference_lvgl_t* ui, const char* symbol) {
    for (uint32_t i = 0; i < lv_obj_get_child_count(ui->status_bar); ++i) {
        lv_obj_t* child = lv_obj_get_child(ui->status_bar, (int32_t)i);
        if (lv_obj_check_type(child, &lv_label_class) &&
            strcmp(lv_label_get_text(child), symbol) == 0) return 1;
    }
    return 0;
}

static void check_external_power(pxsys_standard_system_t* system, pxsys_reference_lvgl_t* ui) {
    pxsys_system_status_snapshot_t saved, next;
    pxsys_system_status_snapshot_init(&saved);
    assert(pxsys_system_status_service_get(pxsys_standard_system_status(system), &saved) == PXSYS_STATUS_OK);
    next = saved;
    next.external_power_supported = next.external_power_connected = 1;
    next.charging = 0;
    assert(pxsys_system_status_service_update(pxsys_standard_system_status(system), &next) == PXSYS_STATUS_OK);
    assert(status_has_power_icon(ui, LV_SYMBOL_USB));
    next.battery_valid = 0;
    assert(pxsys_system_status_service_update(pxsys_standard_system_status(system), &next) == PXSYS_STATUS_OK);
    assert(status_has_power_icon(ui, LV_SYMBOL_USB)); /* Supply presence survives unavailable ADC. */
    next.external_power_connected = 0;
    assert(pxsys_system_status_service_update(pxsys_standard_system_status(system), &next) == PXSYS_STATUS_OK);
    assert(!status_has_power_icon(ui, LV_SYMBOL_USB));
    next.battery_valid = 1;
    next.external_power_connected = 1;
    next.charging = 1;
    assert(pxsys_system_status_service_update(pxsys_standard_system_status(system), &next) == PXSYS_STATUS_OK);
    assert(!status_has_power_icon(ui, LV_SYMBOL_USB));
    assert(status_has_power_icon(ui, LV_SYMBOL_CHARGE));
    assert(!status_has_power_icon(ui, "42%"));
    for (uint32_t i = 0; i < lv_obj_get_child_count(ui->status_bar); ++i)
        assert(lv_obj_get_style_border_width(lv_obj_get_child(ui->status_bar, (int32_t)i), 0) != 1);
    next.battery_valid = 0;
    assert(pxsys_system_status_service_update(pxsys_standard_system_status(system), &next) == PXSYS_STATUS_OK);
    assert(status_has_power_icon(ui, LV_SYMBOL_CHARGE));
    assert(pxsys_system_status_service_update(pxsys_standard_system_status(system), &saved) == PXSYS_STATUS_OK);
}

static void check_idle_inhibition(pxsys_reference_lvgl_t* ui) {
    pxsys_reference_idle_policy_t saved = ui->idle_policy;
    ui->idle_policy.dim_after_seconds = 2;
    ui->idle_policy.lock_after_seconds = 4;
    ui->idle_policy.dim_percent = 20;
    if (!ui->idle_timer) ui->idle_timer = lv_timer_create(idle_policy_poll, 500, ui);
    assert(ui->idle_timer);
    pxsys_window_snapshot_t next = ui->window;
    next.keep_screen_on = 1;
    window_changed(ui, &next);
    lv_tick_inc(6000);
    ui->idle_dimmed = 1;
    idle_policy_poll(ui->idle_timer);
    assert(!ui->locked && !ui->idle_dimmed);
    assert(pxsys_reference_lvgl_set_locked(ui, true) == PXSYS_STATUS_OK);
    idle_policy_poll(ui->idle_timer);
    assert(ui->locked); /* Explicit Lock must win over the app's request. */
    assert(pxsys_reference_lvgl_set_locked(ui, false) == PXSYS_STATUS_OK);
    next.keep_screen_on = 0;
    window_changed(ui, &next);
    lv_tick_inc(1500);
    idle_policy_poll(ui->idle_timer);
    assert(!ui->locked && !ui->idle_dimmed);
    lv_tick_inc(1000);
    idle_policy_poll(ui->idle_timer);
    assert(!ui->locked && ui->idle_dimmed);
    lv_tick_inc(2000);
    idle_policy_poll(ui->idle_timer);
    assert(ui->locked);
    assert(pxsys_reference_lvgl_set_locked(ui, false) == PXSYS_STATUS_OK);
    ui->idle_policy = saved;
}

static void test_release(void* context, void* memory) {
    (void)context;
    free(memory);
}

#if PXSYS_REFERENCE_UI_BUILTIN_SETTINGS
static void check_dialog_actions(pxsys_reference_lvgl_t* ui) {
    const pxsys_theme_snapshot_t saved = ui->theme;
    lv_obj_t* parent = lv_obj_create(ui->root);
    lv_style_t board_focus;
    lv_style_init(&board_focus);
    lv_style_set_outline_color(&board_focus, lv_color_hex(0xffb74d));
    lv_style_set_outline_width(&board_focus, 2);
    for(unsigned scheme = 0; scheme < 2; ++scheme) {
        for(unsigned palette = 0; palette < PXSYS_THEME_PALETTE_COUNT; ++palette) {
            pxsys_theme_snapshot_init(&ui->theme, (pxsys_color_scheme_t)scheme);
            assert(pxsys_theme_snapshot_apply_palette(&ui->theme,
                (pxsys_theme_palette_t)palette) == PXSYS_STATUS_OK);
            for(unsigned kind = 0; kind <= PXA_LVGL_ACTION_DANGER; ++kind) {
                const bool primary = kind == PXA_LVGL_ACTION_PRIMARY;
                const bool danger = kind == PXA_LVGL_ACTION_DANGER;
                lv_obj_t* button = make_page_button(ui, parent, "Action",
                    (pxa_lvgl_action_t)kind, NULL, NULL);
                lv_obj_t* label = lv_obj_get_child(button, 0);
                const lv_font_t* font = typography_font(ui, PXSYS_TYPOGRAPHY_LABEL);
                assert(lv_obj_get_style_text_font(label, 0) == (font ? font : LV_FONT_DEFAULT));
                assert(lv_obj_get_style_border_width(button, 0) == (!primary && !danger));
                assert(lv_obj_get_style_radius(button, 0) == ui->theme.base_radius_px * 2);
                lv_obj_add_style(button, &board_focus, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
                lv_obj_add_state(button, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY | LV_STATE_PRESSED);
                const pxsys_color_token_t pressed = danger ? PXSYS_COLOR_ON_ERROR_CONTAINER :
                    PXSYS_COLOR_PRIMARY_CONTAINER;
                const pxsys_color_token_t on_pressed = danger ? PXSYS_COLOR_ERROR_CONTAINER :
                    PXSYS_COLOR_ON_PRIMARY_CONTAINER;
                assert(lv_color_eq(lv_obj_get_style_bg_color(button, 0), color_token(ui, pressed)));
                assert(lv_color_eq(lv_obj_get_style_text_color(label, 0), color_token(ui, on_pressed)));
                assert(lv_color_eq(lv_obj_get_style_outline_color(button, 0), color_token(ui, on_pressed)));
                lv_obj_add_state(button, LV_STATE_DISABLED);
                assert(lv_obj_get_style_outline_width(button, 0) == 0);
                assert(lv_color_eq(lv_obj_get_style_text_color(label, 0),
                    color_token(ui, PXSYS_COLOR_TEXT_SECONDARY)));
                lv_obj_remove_state(button, LV_STATE_DISABLED | LV_STATE_PRESSED);
                assert(lv_color_eq(lv_obj_get_style_text_color(label, 0), color_token(ui,
                    primary ? PXSYS_COLOR_ON_ACCENT : danger ? PXSYS_COLOR_ON_ERROR_CONTAINER :
                    PXSYS_COLOR_TEXT_PRIMARY)));
                lv_obj_delete(button);
            }
        }
    }
    lv_style_reset(&board_focus);
    lv_obj_delete(parent);
    ui->theme = saved;
}

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
#if PXSYS_REFERENCE_UI_BUILTIN_SETTINGS
    if(features == PXSYS_REFERENCE_UI_ALL) check_dialog_actions(ui);
#endif
    check_role(system, PXSYS_ROLE_HOME, (features & PXSYS_REFERENCE_UI_HOME) != 0);
    check_role(system, PXSYS_ROLE_SETTINGS, (features & PXSYS_REFERENCE_UI_SETTINGS) != 0);
    check_role(system, PXSYS_ROLE_STATUS_BAR, (features & PXSYS_REFERENCE_UI_STATUS_BAR) != 0);
    check_role(system, PXSYS_ROLE_NAVIGATION_BAR, (features & PXSYS_REFERENCE_UI_NAVIGATION_BAR) != 0);
    check_role(system, PXSYS_ROLE_SOUND_SETTINGS,
        PXSYS_REFERENCE_UI_BUILTIN_SETTINGS && (features & PXSYS_REFERENCE_UI_SOUND_SETTINGS));
    assert((ui->app_ime_timer != NULL) == !!PXSYS_REFERENCE_UI_BUILTIN_SETTINGS);
    assert(pxsys_reference_lvgl_start(ui) == PXSYS_STATUS_OK);
    assert((ui->status_bar != NULL) == !!(features & PXSYS_REFERENCE_UI_STATUS_BAR));
    if (features == PXSYS_REFERENCE_UI_ALL) check_external_power(system, ui);
#if PXSYS_REFERENCE_UI_WIFI && LV_USE_SNAPSHOT
    if (features == PXSYS_REFERENCE_UI_ALL) check_wifi_signal(system, ui);
#endif
    assert((ui->navigation_bar != NULL) == !!(features & PXSYS_REFERENCE_UI_NAVIGATION_BAR));
    assert(!!ui->content_active == !!(features & PXSYS_REFERENCE_UI_HOME));
    if (features == PXSYS_REFERENCE_UI_ALL) check_window_chrome(ui);
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
        lv_obj_t* panel = lv_obj_get_child(ui->confirm_dialog, 0);
        lv_obj_t* question = lv_obj_get_child(panel, 0);
        lv_obj_t* actions = lv_obj_get_child(panel, 1);
        assert(lv_label_get_long_mode(question) == LV_LABEL_LONG_MODE_WRAP);
        assert((uintptr_t)lv_obj_get_user_data(lv_obj_get_child(actions, 0)) == 0);
        assert((uintptr_t)lv_obj_get_user_data(lv_obj_get_child(actions, 1)) == 1);
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
    check_idle_inhibition(ui);
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
