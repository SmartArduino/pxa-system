#include "pxsys/reference_lvgl.h"

#include <stdio.h>
#include <string.h>

#include "pxsys/reference_layout.h"

#define REFERENCE_MAGIC UINT32_C(0x50585255)
#define REFERENCE_APP_COUNT 4u
#define TRANSIENT_BAR_TIMEOUT_MS 2500u
#define NAVIGATION_GESTURE_COMMIT_DISTANCE 32
#define NAVIGATION_GESTURE_HOLD_MS 450u

typedef enum {
    REFERENCE_PAGE_HOME = 0,
    REFERENCE_PAGE_SETTINGS,
    REFERENCE_PAGE_STATUS_BAR,
    REFERENCE_PAGE_NAVIGATION_BAR,
} reference_page_t;

typedef enum {
    NAVIGATION_ICON_BACK = 0,
    NAVIGATION_ICON_HOME,
    NAVIGATION_ICON_RECENTS,
} navigation_icon_t;

typedef struct {
    struct pxsys_reference_lvgl* ui;
    reference_page_t page;
} app_context_t;

typedef struct {
    struct pxsys_reference_lvgl* ui;
    pxsys_app_identity_t identity;
    char app_id[65];
    char runtime_name[12];
} launcher_item_t;

typedef struct {
    struct pxsys_reference_lvgl* ui;
    pxsys_instance_ref_t instance;
    char label[72];
} recent_item_t;

#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && LV_USE_SNAPSHOT
typedef struct {
    pxsys_instance_ref_t instance;
    lv_draw_buf_t* image;
    uint32_t last_used;
} task_preview_t;
#endif

struct pxsys_reference_lvgl {
    uint32_t magic;
    pxsys_standard_system_t* system;
    pxsys_allocator_t allocator;
    lv_obj_t* parent;
    lv_obj_t* root;
    lv_obj_t* content;
    lv_obj_t* title;
    lv_obj_t* page_indicator;
    lv_obj_t* toast;
    lv_obj_t* toast_label;
    lv_obj_t* notification_shade;
    lv_obj_t* notification_panel;
    lv_obj_t* task_switcher;
    lv_obj_t* navigation_handle;
    lv_timer_t* toast_timer;
    lv_timer_t* transient_timer;
    const lv_font_t* text_font;
    const lv_font_t* title_font;
    uint32_t features;
    size_t max_launcher_apps;
    size_t launcher_count;
    launcher_item_t* launcher_items;
    recent_item_t recent_items[8];
#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && LV_USE_SNAPSHOT
    task_preview_t task_previews[PXSYS_REFERENCE_UI_TASK_PREVIEW_COUNT];
    uint32_t preview_clock;
#endif
    pxsys_app_identity_t identities[REFERENCE_APP_COUNT];
    app_context_t contexts[REFERENCE_APP_COUNT];
    char app_ids[REFERENCE_APP_COUNT][32];
    uint8_t publisher_root[PXSYS_PUBLISHER_ROOT_BYTES];
    uint8_t apps_registered;
    uint8_t roles_registered;
    uint8_t display_subscribed;
    uint8_t theme_subscribed;
    uint8_t status_subscribed;
    uint8_t toast_subscribed;
    uint8_t window_subscribed;
    uint8_t toast_visible;
    pxsys_toast_tone_t toast_tone;
    uint8_t active_chrome;
    uint8_t content_active;
    uint8_t notification_shade_open;
    uint8_t transient_revealed;
    uint8_t animations_enabled;
    uint8_t navigation_from_home;
    int32_t status_press_y;
    int32_t navigation_press_x;
    int32_t navigation_press_y;
    uint32_t navigation_press_tick;
    pxsys_navigation_mode_t navigation_mode;
    reference_page_t active_page;
    pxsys_display_profile_t display;
    pxsys_theme_snapshot_t theme;
    pxsys_system_status_snapshot_t system_status;
    pxsys_window_snapshot_t window;
};

static void rebuild(pxsys_reference_lvgl_t* ui);
static void build_task_switcher(pxsys_reference_lvgl_t* ui);
static void capture_current_task(pxsys_reference_lvgl_t* ui);

static void rebuild_async(void* context) {
    rebuild((pxsys_reference_lvgl_t*)context);
}

static int ui_valid(const pxsys_reference_lvgl_t* ui) {
    return ui != NULL && ui->magic == REFERENCE_MAGIC;
}

static lv_color_t color_token(const pxsys_reference_lvgl_t* ui,
                              pxsys_color_token_t token) {
    return lv_color_hex(ui->theme.colors[token] & UINT32_C(0x00ffffff));
}

static void style_plain(lv_obj_t* object) {
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_remove_flag(object,
                       LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

static pxsys_status_t open_role(pxsys_reference_lvgl_t* ui, const char* role) {
    pxsys_intent_t intent = {0};
    pxsys_instance_ref_t instance;
    intent.struct_size = sizeof(intent);
    intent.action = pxsys_string_from_cstr("system.intent.main");
    intent.flags = PXSYS_INTENT_FLAG_CLEAR_TOP;
    return pxsys_task_manager_start_role(
        pxsys_standard_system_tasks(ui->system),
        pxsys_standard_system_roles(ui->system), NULL,
        pxsys_string_from_cstr(role), &intent, &instance);
}

static void back_clicked(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    pxsys_back_result_t result;
    if (pxsys_reference_lvgl_dismiss_overlay(ui)) return;
    (void)pxsys_task_manager_back(pxsys_standard_system_tasks(ui->system),
                                  &result);
}

static void theme_clicked(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    pxsys_theme_snapshot_t next = ui->theme;
    pxsys_color_scheme_t scheme =
        ui->theme.effective_scheme == PXSYS_COLOR_SCHEME_DARK
            ? PXSYS_COLOR_SCHEME_LIGHT : PXSYS_COLOR_SCHEME_DARK;
    pxsys_theme_snapshot_init(&next, scheme);
    next.configured_mode = scheme == PXSYS_COLOR_SCHEME_DARK
                               ? PXSYS_THEME_MODE_DARK
                               : PXSYS_THEME_MODE_LIGHT;
    (void)pxsys_theme_service_update(pxsys_standard_system_theme(ui->system),
                                     &next);
}

static void launcher_clicked(lv_event_t* event) {
    launcher_item_t* item = (launcher_item_t*)lv_event_get_user_data(event);
    pxsys_intent_t intent = {0};
    pxsys_instance_ref_t instance;
    if (item == NULL || !ui_valid(item->ui)) return;
    intent.struct_size = sizeof(intent);
    intent.target = &item->identity;
    intent.action = pxsys_string_from_cstr("system.intent.main");
    intent.flags = PXSYS_INTENT_FLAG_CLEAR_TOP;
    (void)pxsys_task_manager_start(pxsys_standard_system_tasks(item->ui->system),
                                   &intent, &instance);
}

static lv_obj_t* make_label(lv_obj_t* parent, const char* text,
                            const lv_font_t* font, lv_color_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_color(label, color, 0);
    if (font != NULL) lv_obj_set_style_text_font(label, font, 0);
    return label;
}

static lv_obj_t* make_button(pxsys_reference_lvgl_t* ui, lv_obj_t* parent,
                             const char* text, lv_event_cb_t clicked,
                             void* user_data) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_style_radius(button, ui->theme.base_radius_px * 2u, 0);
    lv_obj_set_style_bg_color(button, color_token(ui, PXSYS_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_color(button, color_token(ui, PXSYS_COLOR_BORDER),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, color_token(ui, PXSYS_COLOR_BORDER), 0);
    lv_obj_set_style_pad_all(button, 8, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    make_label(button, text, ui->text_font,
               color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
    lv_obj_set_width(lv_obj_get_child(button, 0), LV_PCT(100));
    lv_obj_set_style_text_align(lv_obj_get_child(button, 0), LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(lv_obj_get_child(button, 0));
    if (clicked != NULL)
        lv_obj_add_event_cb(button, clicked, LV_EVENT_CLICKED, user_data);
    return button;
}

static lv_obj_t* make_navigation_action(pxsys_reference_lvgl_t* ui,
                                        lv_obj_t* parent,
                                        navigation_icon_t icon,
                                        lv_event_cb_t clicked) {
    static const lv_point_precise_t back_points[] = {
        {10, 0}, {0, 7}, {10, 14}, {10, 0}};
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_t* glyph;
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(button, color_token(ui, PXSYS_COLOR_BORDER),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_40, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_radius(button, ui->theme.base_radius_px, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    if (icon == NAVIGATION_ICON_BACK) {
        glyph = lv_line_create(button);
        lv_line_set_points(glyph, back_points,
                           sizeof(back_points) / sizeof(back_points[0]));
        lv_obj_set_style_line_width(glyph, 2, 0);
        lv_obj_set_style_line_rounded(glyph, true, 0);
        lv_obj_set_style_line_color(
            glyph, color_token(ui, PXSYS_COLOR_TEXT_PRIMARY), 0);
    } else {
        glyph = lv_obj_create(button);
        style_plain(glyph);
        lv_obj_set_size(glyph, icon == NAVIGATION_ICON_HOME ? 15 : 14,
                        icon == NAVIGATION_ICON_HOME ? 15 : 14);
        lv_obj_set_style_bg_opa(glyph, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(glyph, 2, 0);
        lv_obj_set_style_border_color(
            glyph, color_token(ui, PXSYS_COLOR_TEXT_PRIMARY), 0);
        lv_obj_set_style_radius(
            glyph, icon == NAVIGATION_ICON_HOME ? LV_RADIUS_CIRCLE : 2, 0);
    }
    lv_obj_center(glyph);
    lv_obj_add_event_cb(button, clicked, LV_EVENT_CLICKED, ui);
    return button;
}

static lv_obj_t* application_root(const pxsys_reference_lvgl_t* ui) {
    lv_display_t* display = lv_obj_get_display(ui->root);
    if (ui->content_active && ui->content != NULL) return ui->content;
    return display == NULL ? NULL : lv_display_get_screen_active(display);
}

static void application_backdrop_apply(pxsys_reference_lvgl_t* ui) {
    lv_display_t* display = lv_obj_get_display(ui->root);
    lv_obj_t* app =
        display == NULL ? NULL : lv_display_get_screen_active(display);
    lv_obj_t* backdrop;
    if (app == NULL) return;
    backdrop = lv_display_get_layer_bottom(display);
    if (backdrop != NULL) {
        lv_obj_set_style_bg_color(
            backdrop, color_token(ui, PXSYS_COLOR_BACKGROUND), 0);
        lv_obj_set_style_bg_opa(backdrop, LV_OPA_COVER, 0);
    }
    lv_obj_set_style_bg_color(app, color_token(ui, PXSYS_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(app, LV_OPA_COVER, 0);
}

static void application_scale_set(void* object, int32_t value) {
    lv_obj_set_style_transform_scale_x((lv_obj_t*)object, value, 0);
    lv_obj_set_style_transform_scale_y((lv_obj_t*)object, value, 0);
}

static void application_y_set(void* object, int32_t value) {
    lv_obj_set_style_translate_y((lv_obj_t*)object, value, 0);
}

static void application_x_set(void* object, int32_t value) {
    lv_obj_t* screen = (lv_obj_t*)object;
    uint32_t index;
    lv_obj_set_style_translate_x(screen, value, 0);
    if (lv_obj_get_parent(screen) == NULL) {
        uint32_t count = lv_obj_get_child_count(screen);
        /* Screen roots retain translate-x but do not offset their draw tree. */
        for (index = 0; index < count; ++index)
            lv_obj_set_style_translate_x(lv_obj_get_child(screen, index), value,
                                         0);
    }
}

static void application_motion_set(pxsys_reference_lvgl_t* ui, int32_t scale,
                                   int32_t translate_x, int32_t translate_y,
                                   int32_t radius) {
    lv_obj_t* app = application_root(ui);
    if (app == NULL) return;
    lv_obj_set_style_transform_pivot_x(app, lv_obj_get_width(app) / 2, 0);
    lv_obj_set_style_transform_pivot_y(app, lv_obj_get_height(app) / 2, 0);
    application_scale_set(app, scale);
    application_x_set(app, translate_x);
    application_y_set(app, translate_y);
    lv_obj_set_style_radius(app, radius, 0);
    lv_obj_set_style_clip_corner(app, radius > 0, 0);
}

#if PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS
static void object_y_set(void* object, int32_t value) {
    lv_obj_set_y((lv_obj_t*)object, value);
}
#endif

static void application_scale_reset(pxsys_reference_lvgl_t* ui) {
    lv_obj_t* app = application_root(ui);
    if (app == NULL) return;
    lv_anim_delete(app, application_scale_set);
    lv_anim_delete(app, application_x_set);
    lv_anim_delete(app, application_y_set);
    application_motion_set(ui, 256, 0, 0, 0);
}

static void home_animation_completed(lv_anim_t* animation) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_anim_get_user_data(animation);
    if (!ui_valid(ui)) return;
    (void)open_role(ui, PXSYS_ROLE_HOME);
    application_scale_reset(ui);
}

static void animate_application(pxsys_reference_lvgl_t* ui,
                                int32_t target_scale, int32_t target_x,
                                int32_t target_y,
                                lv_anim_completed_cb_t completed) {
    lv_obj_t* app = application_root(ui);
    if (app == NULL) return;
#if PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS
    if (ui->animations_enabled) {
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, app);
        lv_anim_set_exec_cb(&animation, application_scale_set);
        lv_anim_set_values(
            &animation, lv_obj_get_style_transform_scale_x(app, 0),
            target_scale);
        lv_anim_set_duration(&animation, 180);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
        lv_anim_set_user_data(&animation, ui);
        if (completed != NULL) lv_anim_set_completed_cb(&animation, completed);
        lv_anim_start(&animation);
        lv_anim_set_exec_cb(&animation, application_x_set);
        lv_anim_set_values(&animation,
                           lv_obj_get_style_translate_x(app, 0), target_x);
        lv_anim_set_completed_cb(&animation, NULL);
        lv_anim_start(&animation);
        lv_anim_set_exec_cb(&animation, application_y_set);
        lv_anim_set_values(&animation, lv_obj_get_style_translate_y(app, 0),
                           target_y);
        lv_anim_set_completed_cb(&animation, NULL);
        lv_anim_start(&animation);
        return;
    }
#else
    (void)target_scale;
    (void)target_x;
    (void)target_y;
#endif
    if (completed == home_animation_completed) {
        (void)open_role(ui, PXSYS_ROLE_HOME);
        application_scale_reset(ui);
    } else {
        application_scale_reset(ui);
    }
}

static int app_is_system_home(
    pxsys_reference_lvgl_t* ui, const pxsys_app_descriptor_t* app) {
    const pxsys_app_descriptor_t* home = NULL;
    if (app == NULL ||
        pxsys_role_resolve(pxsys_standard_system_roles(ui->system),
                           pxsys_string_from_cstr(PXSYS_ROLE_HOME), &home) !=
            PXSYS_STATUS_OK ||
        home == NULL)
        return 0;
    return pxsys_app_identity_equal(&app->identity, &home->identity);
}

#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && LV_USE_SNAPSHOT
static int instance_equal(pxsys_instance_ref_t left,
                          pxsys_instance_ref_t right) {
    return left.slot == right.slot && left.generation == right.generation;
}

static task_preview_t* find_task_preview(pxsys_reference_lvgl_t* ui,
                                         pxsys_instance_ref_t instance) {
    size_t index;
    for (index = 0; index < PXSYS_REFERENCE_UI_TASK_PREVIEW_COUNT; ++index) {
        if (ui->task_previews[index].image != NULL &&
            instance_equal(ui->task_previews[index].instance, instance))
            return &ui->task_previews[index];
    }
    return NULL;
}

static void capture_current_task(pxsys_reference_lvgl_t* ui) {
    pxsys_task_manager_t* tasks;
    pxsys_instance_ref_t current;
    pxsys_instance_snapshot_t snapshot = {0};
    task_preview_t* preview = NULL;
    size_t index;
    if (!ui_valid(ui)) return;
    tasks = pxsys_standard_system_tasks(ui->system);
    snapshot.struct_size = sizeof(snapshot);
    if (pxsys_task_manager_task(tasks, 0, &current, &snapshot) !=
            PXSYS_STATUS_OK ||
        app_is_system_home(ui, snapshot.app))
        return;
    preview = find_task_preview(ui, current);
    if (preview == NULL) {
        for (index = 0; index < PXSYS_REFERENCE_UI_TASK_PREVIEW_COUNT; ++index) {
            if (ui->task_previews[index].image == NULL) {
                preview = &ui->task_previews[index];
                break;
            }
            if (preview == NULL || ui->task_previews[index].last_used <
                                       preview->last_used)
                preview = &ui->task_previews[index];
        }
    }
    if (preview == NULL) return;
    if (preview->image != NULL) lv_draw_buf_destroy(preview->image);
    application_scale_reset(ui);
    preview->image =
        lv_snapshot_take(application_root(ui), LV_COLOR_FORMAT_RGB565);
    preview->instance = current;
    preview->last_used = ++ui->preview_clock;
}
#else
static void capture_current_task(pxsys_reference_lvgl_t* ui) {
    (void)ui;
}
#endif

static void close_task_switcher(pxsys_reference_lvgl_t* ui) {
    if (ui->task_switcher != NULL) lv_obj_delete(ui->task_switcher);
    ui->task_switcher = NULL;
    application_scale_reset(ui);
}

static void recent_clicked(lv_event_t* event) {
    recent_item_t* item = (recent_item_t*)lv_event_get_user_data(event);
    if (item == NULL || !ui_valid(item->ui)) return;
    if (pxsys_task_manager_activate(
            pxsys_standard_system_tasks(item->ui->system), item->instance) ==
        PXSYS_STATUS_OK)
        close_task_switcher(item->ui);
}

static void home_clicked(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    if (!ui_valid(ui)) return;
    capture_current_task(ui);
    animate_application(ui, 212, 0, -(int32_t)ui->display.height / 3,
                        home_animation_completed);
}

static void recents_clicked(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    if (!ui_valid(ui)) return;
    capture_current_task(ui);
    build_task_switcher(ui);
    animate_application(ui, 232, 0, -18, NULL);
}

static void task_switcher_background_clicked(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    if (ui_valid(ui) && lv_event_get_target(event) == ui->task_switcher)
        close_task_switcher(ui);
}

#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && LV_USE_SNAPSHOT
static lv_obj_t* make_recent_card(pxsys_reference_lvgl_t* ui,
                                  lv_obj_t* parent, recent_item_t* item,
                                  int32_t card_width, int32_t card_height,
                                  uint32_t padding) {
    task_preview_t* preview = find_task_preview(ui, item->instance);
    lv_obj_t* card = lv_button_create(parent);
    lv_obj_t* preview_area;
    lv_obj_t* label;
    int32_t label_height = ui->text_font != NULL
                               ? (int32_t)ui->text_font->line_height + 6
                               : 22;
    int32_t preview_height = card_height - label_height - (int32_t)padding;
    lv_obj_set_size(card, card_width, card_height);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_opa(card, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_SNAPPABLE);

    preview_area = lv_obj_create(card);
    style_plain(preview_area);
    lv_obj_set_pos(preview_area, 0, 0);
    lv_obj_set_size(preview_area, card_width, preview_height);
    lv_obj_set_style_radius(preview_area, ui->theme.base_radius_px * 3u, 0);
    lv_obj_set_style_bg_color(preview_area,
                              color_token(ui, PXSYS_COLOR_SURFACE), 0);
    lv_obj_set_style_clip_corner(preview_area, true, 0);
    if (preview != NULL && preview->image != NULL &&
        preview->image->header.w != 0 && preview->image->header.h != 0) {
        lv_obj_t* image = lv_image_create(preview_area);
        uint32_t scale_x = (uint32_t)card_width * 256u /
                           preview->image->header.w;
        uint32_t scale_y = (uint32_t)preview_height * 256u /
                           preview->image->header.h;
        lv_image_set_src(image, preview->image);
        lv_image_set_scale(image, scale_x < scale_y ? scale_x : scale_y);
        lv_obj_center(image);
    }

    label = make_label(card, item->label, ui->text_font,
                       color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
    lv_obj_set_width(label, card_width);
    lv_obj_set_height(label, label_height);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(card, recent_clicked, LV_EVENT_CLICKED, item);
    return card;
}
#endif

static void build_task_switcher(pxsys_reference_lvgl_t* ui) {
    pxsys_reference_layout_t layout;
    pxsys_task_manager_t* tasks;
    lv_obj_t* panel;
    size_t count;
    size_t index;
    if (!ui_valid(ui) ||
        pxsys_reference_layout_compute(&ui->display, &layout) != PXSYS_STATUS_OK)
        return;
    close_task_switcher(ui);
    tasks = pxsys_standard_system_tasks(ui->system);
    count = pxsys_task_manager_count(tasks);
    if (count > sizeof(ui->recent_items) / sizeof(ui->recent_items[0]))
        count = sizeof(ui->recent_items) / sizeof(ui->recent_items[0]);
    ui->task_switcher = lv_obj_create(ui->parent);
    style_plain(ui->task_switcher);
    lv_obj_set_size(ui->task_switcher, (lv_coord_t)ui->display.width,
                    (lv_coord_t)ui->display.height);
    lv_obj_set_style_bg_color(ui->task_switcher,
                              color_token(ui, PXSYS_COLOR_SCRIM), 0);
    lv_obj_set_style_bg_opa(ui->task_switcher, (lv_opa_t)208, 0);
    lv_obj_add_flag(ui->task_switcher, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui->task_switcher, task_switcher_background_clicked,
                        LV_EVENT_CLICKED, ui);
    panel = lv_obj_create(ui->task_switcher);
    lv_obj_set_pos(panel, layout.safe_area.x + layout.outer_padding,
                   layout.safe_area.y + layout.outer_padding);
    lv_obj_set_size(
        panel,
        (lv_coord_t)(layout.safe_area.width - 2u * layout.outer_padding),
        (lv_coord_t)(layout.safe_area.height - 2u * layout.outer_padding));
    style_plain(panel);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && LV_USE_SNAPSHOT
    {
        lv_obj_t* strip = lv_obj_create(panel);
        int32_t panel_width =
            (int32_t)layout.safe_area.width - (int32_t)layout.outer_padding * 2;
        int32_t panel_height =
            (int32_t)layout.safe_area.height - (int32_t)layout.outer_padding * 2;
        int32_t card_width = panel_width * 4 / 5;
        int32_t card_height = panel_height;
        uint32_t card_padding = layout.outer_padding > 8
                                    ? 8u : layout.outer_padding;
        if (card_width < 96) card_width = 96;
        if (card_height < 96) card_height = 96;
        style_plain(strip);
        lv_obj_set_pos(strip, 0, 0);
        lv_obj_set_size(strip, panel_width, card_height);
        lv_obj_set_style_bg_opa(strip, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_left(strip, (panel_width - card_width) / 2, 0);
        lv_obj_set_style_pad_right(strip, (panel_width - card_width) / 2, 0);
        lv_obj_set_style_pad_column(strip, layout.item_gap, 0);
        lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
        lv_obj_set_scroll_dir(strip, LV_DIR_HOR);
        lv_obj_set_scroll_snap_x(strip, LV_SCROLL_SNAP_CENTER);
        lv_obj_add_flag(strip, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ONE);
        for (index = 0; index < count; ++index) {
            pxsys_instance_snapshot_t snapshot = {0};
            recent_item_t* item = &ui->recent_items[index];
            snapshot.struct_size = sizeof(snapshot);
            item->ui = ui;
            if (pxsys_task_manager_task(tasks, index, &item->instance,
                                        &snapshot) != PXSYS_STATUS_OK ||
                snapshot.app == NULL || app_is_system_home(ui, snapshot.app))
                continue;
            snprintf(item->label, sizeof(item->label), "%.*s",
                     (int)snapshot.app->display_name.size,
                     snapshot.app->display_name.data);
            (void)make_recent_card(ui, strip, item, card_width, card_height,
                                   card_padding);
        }
    }
#else
    lv_obj_set_style_pad_top(panel, 0, 0);
    lv_obj_set_style_pad_row(panel, layout.item_gap, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    for (index = 0; index < count; ++index) {
        pxsys_instance_snapshot_t snapshot = {0};
        recent_item_t* item = &ui->recent_items[index];
        snapshot.struct_size = sizeof(snapshot);
        item->ui = ui;
        if (pxsys_task_manager_task(tasks, index, &item->instance, &snapshot) !=
                PXSYS_STATUS_OK ||
            snapshot.app == NULL || app_is_system_home(ui, snapshot.app))
            continue;
        snprintf(item->label, sizeof(item->label), "%.*s",
                 (int)snapshot.app->display_name.size,
                 snapshot.app->display_name.data);
        {
            lv_obj_t* button = make_button(ui, panel, item->label,
                                           recent_clicked, item);
            lv_obj_set_width(button, LV_PCT(100));
            lv_obj_set_height(button,
                              layout.size_class == PXSYS_UI_SIZE_COMPACT ? 38 : 48);
        }
    }
#endif
    lv_obj_move_foreground(ui->task_switcher);
#if PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS
    if (ui->animations_enabled) {
        lv_obj_set_style_opa(ui->task_switcher, LV_OPA_TRANSP, 0);
        lv_obj_fade_in(ui->task_switcher, 160, 0);
    }
#endif
}

static int is_reference_app(const pxsys_reference_lvgl_t* ui,
                            const pxsys_app_identity_t* identity) {
    size_t index;
    for (index = 0; index < REFERENCE_APP_COUNT; ++index) {
        if (pxsys_app_identity_equal(&ui->identities[index], identity)) return 1;
    }
    return 0;
}

static lv_obj_t* make_launcher_tile(pxsys_reference_lvgl_t* ui,
                                    const char* name,
                                    launcher_item_t* item) {
    lv_obj_t* tile = lv_button_create(ui->content);
    lv_obj_t* marker;
    lv_obj_t* label;
    char initial[2] = {'?', '\0'};
    if (name != NULL && name[0] >= 0x20 && (unsigned char)name[0] < 0x80)
        initial[0] = name[0];
    else if (item != NULL && item->runtime_name[0] != '\0')
        initial[0] = item->runtime_name[0];
    lv_obj_set_style_radius(tile, ui->theme.base_radius_px * 2u, 0);
    lv_obj_set_style_bg_color(tile, color_token(ui, PXSYS_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_color(tile, color_token(ui, PXSYS_COLOR_BORDER),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(tile, 0, 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_border_color(tile, color_token(ui, PXSYS_COLOR_BORDER), 0);
    lv_obj_set_style_pad_all(tile, 8, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    marker = lv_obj_create(tile);
    style_plain(marker);
    lv_obj_set_size(marker, 32, 32);
    lv_obj_set_style_radius(marker, 6, 0);
    lv_obj_set_style_bg_color(marker, color_token(ui, PXSYS_COLOR_ACCENT), 0);
    make_label(marker, initial, ui->text_font,
               color_token(ui, PXSYS_COLOR_ON_ACCENT));
    lv_obj_center(lv_obj_get_child(marker, 0));
    lv_obj_align(marker, LV_ALIGN_TOP_LEFT, 0, 0);
    label = make_label(tile, name, ui->text_font,
                       color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_height(label, ui->text_font != NULL
                                 ? (lv_coord_t)ui->text_font->line_height + 2 : 18);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    label = make_label(tile, item->runtime_name, NULL,
                       color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
    lv_obj_align(label, LV_ALIGN_TOP_RIGHT, 0, 4);
    lv_obj_add_event_cb(tile, launcher_clicked, LV_EVENT_CLICKED, item);
    return tile;
}

static void build_home(pxsys_reference_lvgl_t* ui,
                       const pxsys_reference_layout_t* layout) {
    pxsys_app_registry_t* registry = pxsys_standard_system_apps(ui->system);
    size_t index;
    uint32_t row_height;
    uint32_t columns = layout->grid_columns;
    uint32_t column_width;
    ui->launcher_count = 0;
    lv_obj_set_layout(ui->content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ui->content, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(ui->content, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    column_width = (layout->content.width -
                    (columns - 1u) * layout->item_gap) / columns;
    row_height = layout->size_class == PXSYS_UI_SIZE_COMPACT ? 72u : 88u;
    lv_obj_add_flag(ui->content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ui->content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui->content, LV_SCROLLBAR_MODE_AUTO);

    for (index = 0; index < pxsys_app_registry_count(registry) &&
                    ui->launcher_count < ui->max_launcher_apps; ++index) {
        const pxsys_app_descriptor_t* app = pxsys_app_registry_at(registry, index);
        launcher_item_t* item;
        lv_obj_t* tile;
        char name[96];
        size_t name_size;
        if (app == NULL || !(app->flags & PXSYS_APP_FLAG_ENABLED) ||
            !(app->flags & PXSYS_APP_FLAG_LAUNCHER) ||
            is_reference_app(ui, &app->identity)) continue;
        item = &ui->launcher_items[ui->launcher_count];
        memset(item, 0, sizeof(*item));
        if (app->identity.app_id.size >= sizeof(item->app_id)) continue;
        item->ui = ui;
        memcpy(item->identity.publisher_root, app->identity.publisher_root,
               PXSYS_PUBLISHER_ROOT_BYTES);
        memcpy(item->app_id, app->identity.app_id.data, app->identity.app_id.size);
        item->app_id[app->identity.app_id.size] = '\0';
        item->identity.app_id = pxsys_string_from_cstr(item->app_id);
        snprintf(item->runtime_name, sizeof(item->runtime_name), "%s",
                 app->runtime_id.size == strlen(PXSYS_NATIVE_RUNTIME_ID) &&
                         memcmp(app->runtime_id.data, PXSYS_NATIVE_RUNTIME_ID,
                                app->runtime_id.size) == 0
                     ? "Native" : "PXA");
        name_size = app->display_name.size < sizeof(name) - 1u
                        ? app->display_name.size : sizeof(name) - 1u;
        memcpy(name, app->display_name.data, name_size);
        name[name_size] = '\0';
        tile = make_launcher_tile(ui, name, item);
        lv_obj_set_size(tile, (lv_coord_t)column_width, (lv_coord_t)row_height);
        ui->launcher_count++;
    }
    if (ui->launcher_count == 0) {
        lv_obj_t* empty = make_label(ui->content, "No apps installed",
                                    ui->text_font,
                                    color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
        lv_obj_center(empty);
    }
    lv_obj_update_layout(ui->content);
    lv_obj_scroll_to_y(ui->content, 0, LV_ANIM_OFF);
}

static void build_settings(pxsys_reference_lvgl_t* ui,
                           const pxsys_reference_layout_t* layout) {
    lv_obj_t* appearance;
    lv_obj_t* display;
    char appearance_text[96];
    char geometry[96];
    lv_obj_set_layout(ui->content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ui->content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui->content, layout->item_gap, 0);
    lv_obj_add_flag(ui->content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ui->content, LV_DIR_VER);
    if (ui->theme.configured_mode == PXSYS_THEME_MODE_CUSTOM)
        snprintf(appearance_text, sizeof(appearance_text), "Appearance  %.*s",
                 (int)ui->theme.theme_id_size, ui->theme.theme_id);
    else
        snprintf(appearance_text, sizeof(appearance_text), "Appearance  %s",
                 ui->theme.effective_scheme == PXSYS_COLOR_SCHEME_DARK
                     ? "Dark" : "Light");
    appearance = make_button(ui, ui->content, appearance_text, theme_clicked, ui);
    lv_obj_set_width(appearance, LV_PCT(100));
    lv_obj_set_height(appearance,
                      layout->size_class == PXSYS_UI_SIZE_COMPACT ? 48 : 58);
    snprintf(geometry, sizeof(geometry), "Display  %ux%u  %u dpi",
             (unsigned)ui->display.width, (unsigned)ui->display.height,
             (unsigned)ui->display.density_dpi);
    display = make_button(ui, ui->content, geometry, NULL, NULL);
    lv_obj_set_width(display, LV_PCT(100));
    lv_obj_set_height(display,
                      layout->size_class == PXSYS_UI_SIZE_COMPACT ? 48 : 58);
}

static void close_notification_shade(pxsys_reference_lvgl_t* ui) {
    if (!ui_valid(ui) || ui->notification_shade == NULL) return;
    ui->notification_shade_open = 0;
    lv_obj_add_flag(ui->notification_shade, LV_OBJ_FLAG_HIDDEN);
}

static int has_transient_chrome(const pxsys_reference_lvgl_t* ui) {
    return ui->window.status_bar_mode == PXSYS_WINDOW_BAR_TRANSIENT ||
           ui->window.navigation_bar_mode == PXSYS_WINDOW_BAR_TRANSIENT;
}

static int bar_is_visible(const pxsys_reference_lvgl_t* ui,
                          pxsys_window_bar_mode_t mode) {
    return mode == PXSYS_WINDOW_BAR_VISIBLE ||
           (mode == PXSYS_WINDOW_BAR_TRANSIENT && ui->transient_revealed);
}

static void transient_timeout(lv_timer_t* timer) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_timer_get_user_data(timer);
    if (!ui_valid(ui)) return;
    lv_timer_pause(timer);
    if (!ui->transient_revealed) return;
    ui->transient_revealed = 0;
    close_notification_shade(ui);
    rebuild(ui);
}

static void reveal_transient_chrome(pxsys_reference_lvgl_t* ui) {
    if (!ui_valid(ui) || !has_transient_chrome(ui)) return;
    ui->transient_revealed = 1;
    if (ui->transient_timer == NULL) {
        ui->transient_timer =
            lv_timer_create(transient_timeout, TRANSIENT_BAR_TIMEOUT_MS, ui);
    } else {
        lv_timer_set_period(ui->transient_timer, TRANSIENT_BAR_TIMEOUT_MS);
        lv_timer_reset(ui->transient_timer);
        lv_timer_resume(ui->transient_timer);
    }
    if (lv_async_call(rebuild_async, ui) != LV_RESULT_OK) rebuild(ui);
}

static void shade_close_clicked(lv_event_t* event) {
    close_notification_shade(
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event));
}

static void shade_panel_event(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    lv_indev_t* indev = lv_event_get_indev(event);
    lv_point_t point;
    if (!ui_valid(ui) || indev == NULL) return;
    lv_indev_get_point(indev, &point);
    if (lv_event_get_code(event) == LV_EVENT_PRESSED)
        ui->status_press_y = point.y;
    else if ((lv_event_get_code(event) == LV_EVENT_RELEASED ||
              lv_event_get_code(event) == LV_EVENT_GESTURE) &&
             point.y - ui->status_press_y < -32)
        close_notification_shade(ui);
}

static void build_notification_shade(pxsys_reference_lvgl_t* ui) {
    pxsys_reference_layout_t layout;
    lv_obj_t* title;
    lv_obj_t* close;
    lv_obj_t* time;
    lv_obj_t* status;
    lv_obj_t* divider;
    lv_obj_t* empty;
    char time_text[16];
    char status_text[96];
    uint32_t panel_height;
    int animate;
    if (!ui_valid(ui) ||
        pxsys_reference_layout_compute(&ui->display, &layout) != PXSYS_STATUS_OK)
        return;
    animate = !ui->notification_shade_open && ui->animations_enabled;
    if (ui->notification_shade != NULL)
        lv_obj_delete(ui->notification_shade);
    ui->notification_shade = lv_obj_create(ui->parent);
    style_plain(ui->notification_shade);
    lv_obj_set_pos(ui->notification_shade, 0, 0);
    lv_obj_set_size(ui->notification_shade, (lv_coord_t)ui->display.width,
                    (lv_coord_t)ui->display.height);
    lv_obj_set_style_bg_color(ui->notification_shade,
                              color_token(ui, PXSYS_COLOR_SCRIM), 0);
    lv_obj_set_style_bg_opa(ui->notification_shade, LV_OPA_50, 0);
    lv_obj_add_flag(ui->notification_shade, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui->notification_shade, shade_close_clicked,
                        LV_EVENT_CLICKED, ui);

    panel_height = layout.safe_area.height * 3u / 4u;
    if (panel_height < 150u) panel_height = layout.safe_area.height;
    if (panel_height > 360u) panel_height = 360u;
    ui->notification_panel = lv_obj_create(ui->notification_shade);
    lv_obj_set_pos(ui->notification_panel, layout.safe_area.x,
                   layout.safe_area.y);
    lv_obj_set_size(ui->notification_panel,
                    (lv_coord_t)layout.safe_area.width,
                    (lv_coord_t)panel_height);
    lv_obj_set_style_bg_color(ui->notification_panel,
                              color_token(ui, PXSYS_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(ui->notification_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui->notification_panel, 0, 0);
    lv_obj_set_style_radius(ui->notification_panel,
                            ui->theme.base_radius_px * 3u, 0);
    lv_obj_set_style_pad_all(ui->notification_panel,
                             layout.outer_padding + 4u, 0);
    lv_obj_clear_flag(ui->notification_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ui->notification_panel,
                    LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(ui->notification_panel, shade_panel_event,
                        LV_EVENT_PRESSED, ui);
    lv_obj_add_event_cb(ui->notification_panel, shade_panel_event,
                        LV_EVENT_RELEASED, ui);
    lv_obj_add_event_cb(ui->notification_panel, shade_panel_event,
                        LV_EVENT_GESTURE, ui);

    title = make_label(ui->notification_panel, "Notifications", ui->title_font,
                       color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    close = make_button(ui, ui->notification_panel, "X", shade_close_clicked, ui);
    lv_obj_set_size(close, 34, 30);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, -2);
    snprintf(time_text, sizeof(time_text),
             ui->system_status.time_valid ? "%02u:%02u" : "--:--",
             (unsigned)ui->system_status.hour,
             (unsigned)ui->system_status.minute);
    time = make_label(ui->notification_panel, time_text, ui->title_font,
                      color_token(ui, PXSYS_COLOR_ACCENT));
    lv_obj_align(time, LV_ALIGN_TOP_LEFT, 0, 42);
    if (ui->system_status.battery_valid) {
        snprintf(status_text, sizeof(status_text), "%s  %s%u%%",
                 ui->system_status.network_connected
                     ? ui->system_status.network_type == PXSYS_NETWORK_WIFI
                           ? "Wi-Fi" : "Connected"
                     : "Offline",
                 ui->system_status.charging ? "Charging  " : "Battery  ",
                 (unsigned)ui->system_status.battery_percent);
    } else {
        snprintf(status_text, sizeof(status_text), "%s  Battery  --",
                 ui->system_status.network_connected
                     ? ui->system_status.network_type == PXSYS_NETWORK_WIFI
                           ? "Wi-Fi" : "Connected"
                     : "Offline");
    }
    status = make_label(ui->notification_panel, status_text, ui->text_font,
                        color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
    lv_obj_set_width(status, LV_PCT(100));
    lv_obj_align(status, LV_ALIGN_TOP_LEFT, 0, 72);
    divider = lv_obj_create(ui->notification_panel);
    style_plain(divider);
    lv_obj_set_size(divider, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(divider, color_token(ui, PXSYS_COLOR_BORDER), 0);
    lv_obj_align(divider, LV_ALIGN_TOP_LEFT, 0, 104);
    empty = make_label(ui->notification_panel, "No notifications", ui->text_font,
                       color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
    lv_obj_align(empty, LV_ALIGN_TOP_MID, 0, 122);
    lv_obj_remove_flag(ui->notification_shade, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui->notification_shade);
    ui->notification_shade_open = 1;
#if PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS
    if (animate) {
        lv_anim_t animation;
        lv_obj_set_y(ui->notification_panel,
                     layout.safe_area.y - (int32_t)panel_height);
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, ui->notification_panel);
        lv_anim_set_exec_cb(&animation, object_y_set);
        lv_anim_set_values(&animation,
                           layout.safe_area.y - (int32_t)panel_height,
                           layout.safe_area.y);
        lv_anim_set_duration(&animation, 220);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
        lv_anim_start(&animation);
        lv_obj_set_style_opa(ui->notification_shade, LV_OPA_TRANSP, 0);
        lv_obj_fade_in(ui->notification_shade, 160, 0);
    }
#else
    (void)animate;
#endif
    if (ui->toast_visible && ui->toast != NULL) lv_obj_move_foreground(ui->toast);
}

static void status_bar_event(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    lv_indev_t* indev = lv_event_get_indev(event);
    lv_point_t point;
    if (!ui_valid(ui) || indev == NULL) return;
    lv_indev_get_point(indev, &point);
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
        ui->status_press_y = point.y;
    } else if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
        if (!ui->transient_revealed && has_transient_chrome(ui) &&
            point.y - ui->status_press_y >= 8) {
            reveal_transient_chrome(ui);
        } else if (point.y >= ui->status_press_y &&
                   bar_is_visible(ui, ui->window.status_bar_mode)) {
            build_notification_shade(ui);
        }
    }
}

static void navigation_gesture_event(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    lv_indev_t* indev = lv_event_get_indev(event);
    lv_point_t point;
    if (!ui_valid(ui) || indev == NULL) return;
    lv_indev_get_point(indev, &point);
    lv_event_code_t code = lv_event_get_code(event);
    int32_t distance;
    if (code == LV_EVENT_PRESSED) {
        ui->navigation_press_x = point.x;
        ui->navigation_press_y = point.y;
        ui->navigation_press_tick = lv_tick_get();
        return;
    }
    distance = ui->navigation_press_y - point.y;
    if (code == LV_EVENT_PRESSING || code == LV_EVENT_GESTURE) {
#if PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS
        if (ui->animations_enabled && distance > 0 &&
            (!has_transient_chrome(ui) || ui->transient_revealed)) {
            int32_t max_progress = (int32_t)ui->display.height * 2 / 3;
            int32_t progress = distance > max_progress ? max_progress : distance;
            int32_t horizontal =
                (point.x - ui->navigation_press_x) * 3 / 4;
            int32_t scale = 256 - progress * 36 / max_progress;
            int32_t max_horizontal =
                (int32_t)ui->display.width * (256 - scale) / 512;
            int32_t translate_y = -progress * 3 / 4;
            int32_t radius = progress / 4;
            if (horizontal > max_horizontal) horizontal = max_horizontal;
            if (horizontal < -max_horizontal) horizontal = -max_horizontal;
            if (radius > 18) radius = 18;
            application_motion_set(ui, scale, horizontal, translate_y, radius);
            if (ui->navigation_handle != NULL) {
                lv_obj_set_width(ui->navigation_handle, 56 + progress / 2);
                lv_obj_align(ui->navigation_handle, LV_ALIGN_BOTTOM_MID,
                             horizontal / 3,
                             -5 - progress / 8);
            }
        }
#endif
        return;
    }
    if (code != LV_EVENT_RELEASED) return;
    if (distance > NAVIGATION_GESTURE_COMMIT_DISTANCE) {
        if (!ui->transient_revealed && has_transient_chrome(ui))
            reveal_transient_chrome(ui);
        else if (lv_tick_elaps(ui->navigation_press_tick) >=
                 NAVIGATION_GESTURE_HOLD_MS) {
            capture_current_task(ui);
            build_task_switcher(ui);
            animate_application(ui, 232, 0, -18, NULL);
        } else {
            capture_current_task(ui);
            animate_application(ui, 212, 0,
                                -(int32_t)ui->display.height / 3,
                                home_animation_completed);
        }
    } else {
        animate_application(ui, 256, 0, 0, NULL);
    }
}

static void make_signal_icon(pxsys_reference_lvgl_t* ui, lv_obj_t* parent,
                             int32_t x, int32_t center_y) {
    uint8_t index;
    for (index = 0; index < 4; ++index) {
        lv_obj_t* bar = lv_obj_create(parent);
        int32_t height = 3 + (int32_t)index * 3;
        style_plain(bar);
        lv_obj_set_size(bar, 3, height);
        lv_obj_set_pos(bar, x + (int32_t)index * 5, center_y - height / 2);
        lv_obj_set_style_radius(bar, 1, 0);
        lv_obj_set_style_bg_color(
            bar,
            color_token(ui,
                        ui->system_status.network_connected &&
                                index < ui->system_status.network_signal_level
                            ? PXSYS_COLOR_TEXT_PRIMARY : PXSYS_COLOR_BORDER),
            0);
    }
}

static void make_battery_icon(pxsys_reference_lvgl_t* ui, lv_obj_t* parent,
                              int32_t x, int32_t center_y) {
    lv_obj_t* body = lv_obj_create(parent);
    lv_obj_t* fill;
    lv_obj_t* cap;
    uint32_t percent = ui->system_status.battery_valid
                           ? ui->system_status.battery_percent : 0;
    int32_t fill_width = (int32_t)(16u * percent / 100u);
    style_plain(body);
    lv_obj_set_pos(body, x, center_y - 6);
    lv_obj_set_size(body, 22, 12);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 1, 0);
    lv_obj_set_style_border_color(body,
                                  color_token(ui, PXSYS_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_radius(body, 3, 0);
    fill = lv_obj_create(body);
    style_plain(fill);
    if (fill_width < 2 && percent != 0) fill_width = 2;
    lv_obj_set_size(fill, fill_width, 6);
    lv_obj_align(fill, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_style_radius(fill, 1, 0);
    lv_obj_set_style_bg_color(
        fill, color_token(ui, ui->system_status.charging
                                 ? PXSYS_COLOR_SUCCESS
                                 : percent <= 15 && ui->system_status.battery_valid
                                       ? PXSYS_COLOR_ERROR
                                       : PXSYS_COLOR_TEXT_PRIMARY), 0);
    cap = lv_obj_create(parent);
    style_plain(cap);
    lv_obj_set_pos(cap, x + 23, center_y - 3);
    lv_obj_set_size(cap, 2, 6);
    lv_obj_set_style_radius(cap, 1, 0);
    lv_obj_set_style_bg_color(cap,
                              color_token(ui, PXSYS_COLOR_TEXT_PRIMARY), 0);
}

static void build_status_bar(pxsys_reference_lvgl_t* ui,
                             const pxsys_reference_layout_t* layout) {
    lv_obj_t* status = lv_obj_create(ui->root);
    lv_obj_t* time_label;
    lv_obj_t* percent_label;
    char time_text[8];
    char percent_text[8];
    int32_t safe_right = layout->safe_area.x +
                         (int32_t)layout->safe_area.width;
    int32_t safe_top = layout->safe_area.y;
    int32_t center_y = safe_top +
        ((int32_t)layout->status_bar.height - safe_top) / 2;
    int32_t align_y = center_y - (int32_t)layout->status_bar.height / 2;
    style_plain(status);
    lv_obj_set_pos(status, layout->status_bar.x, layout->status_bar.y);
    lv_obj_set_size(status, (lv_coord_t)layout->status_bar.width,
                    (lv_coord_t)layout->status_bar.height);
    lv_obj_set_style_bg_color(status,
                              color_token(ui, PXSYS_COLOR_BACKGROUND), 0);
    if (ui->window.status_bar_mode == PXSYS_WINDOW_BAR_TRANSIENT)
        lv_obj_set_style_bg_opa(status, (lv_opa_t)224, 0);
    if (ui->features & PXSYS_REFERENCE_UI_NOTIFICATION_SHADE) {
        lv_obj_add_flag(status, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(status, status_bar_event, LV_EVENT_PRESSED, ui);
        lv_obj_add_event_cb(status, status_bar_event, LV_EVENT_RELEASED, ui);
    }
    snprintf(time_text, sizeof(time_text),
             ui->system_status.time_valid ? "%02u:%02u" : "--:--",
             (unsigned)ui->system_status.hour,
             (unsigned)ui->system_status.minute);
    time_label = make_label(status, time_text, ui->text_font,
                            color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
    lv_obj_align(time_label, LV_ALIGN_LEFT_MID,
                 layout->safe_area.x + (int32_t)layout->outer_padding,
                 align_y);
    make_signal_icon(ui, status,
                     safe_right - (int32_t)layout->outer_padding - 78,
                     center_y);
    snprintf(percent_text, sizeof(percent_text),
             ui->system_status.battery_valid ? "%u%%" : "--",
             (unsigned)ui->system_status.battery_percent);
    percent_label = make_label(status, percent_text, NULL,
                               color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
    lv_obj_set_width(percent_label, 28);
    lv_obj_set_style_text_align(percent_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(percent_label, LV_ALIGN_RIGHT_MID,
                 -((int32_t)layout->viewport.width - safe_right) -
                     (int32_t)layout->outer_padding - 29,
                 align_y);
    make_battery_icon(ui, status,
                      safe_right - (int32_t)layout->outer_padding - 25,
                      center_y);
}

static void toast_apply_theme(pxsys_reference_lvgl_t* ui,
                              pxsys_toast_tone_t tone) {
    pxsys_color_token_t border = PXSYS_COLOR_BORDER;
    if (ui->toast == NULL) return;
    if (tone == PXSYS_TOAST_SUCCESS) border = PXSYS_COLOR_SUCCESS;
    else if (tone == PXSYS_TOAST_WARNING) border = PXSYS_COLOR_WARNING;
    else if (tone == PXSYS_TOAST_ERROR) border = PXSYS_COLOR_ERROR;
    lv_obj_set_style_bg_color(ui->toast,
                              color_token(ui, PXSYS_COLOR_SURFACE), 0);
    lv_obj_set_style_border_color(ui->toast, color_token(ui, border), 0);
    lv_obj_set_style_shadow_color(ui->toast,
                                  color_token(ui, PXSYS_COLOR_SCRIM), 0);
    lv_obj_set_style_text_color(ui->toast_label,
                                color_token(ui, PXSYS_COLOR_TEXT_PRIMARY), 0);
}

static void toast_reposition(pxsys_reference_lvgl_t* ui) {
    pxsys_reference_layout_t layout;
    int32_t bottom_offset;
    int32_t max_width;
    if (ui->toast == NULL ||
        pxsys_reference_layout_compute(&ui->display, &layout) != PXSYS_STATUS_OK)
        return;
    max_width = (int32_t)layout.safe_area.width -
                2 * (int32_t)layout.outer_padding;
    if (max_width > 420) max_width = 420;
    if (max_width < 96) max_width = 96;
    lv_obj_set_style_max_width(ui->toast, max_width, 0);
    lv_obj_set_style_max_width(ui->toast_label, max_width - 24, 0);
    if (!(ui->active_chrome & PXSYS_REFERENCE_UI_NAVIGATION_BAR) ||
        !bar_is_visible(ui, ui->window.navigation_bar_mode)) {
        bottom_offset = 16;
    } else if (ui->navigation_mode == PXSYS_NAVIGATION_GESTURES) {
        bottom_offset = layout.size_class == PXSYS_UI_SIZE_COMPACT ? 26 : 34;
    } else {
        bottom_offset = (int32_t)layout.navigation_bar.height + 10;
    }
    lv_obj_align(ui->toast, LV_ALIGN_BOTTOM_MID, 0, -bottom_offset);
}

static void toast_hide(lv_timer_t* timer) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_timer_get_user_data(timer);
    if (!ui_valid(ui) || ui->toast == NULL || !ui->toast_visible) return;
    ui->toast_visible = 0;
    lv_obj_fade_out(ui->toast, 160, 0);
}

static void toast_posted(void* context, const pxsys_toast_message_t* toast) {
    pxsys_reference_lvgl_t* ui = (pxsys_reference_lvgl_t*)context;
    uint32_t duration;
    if (!ui_valid(ui) || toast == NULL) return;
    if (ui->toast == NULL) {
        ui->toast = lv_obj_create(ui->parent);
        lv_obj_set_size(ui->toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_min_width(ui->toast, 120, 0);
        lv_obj_set_style_min_height(ui->toast, 42, 0);
        lv_obj_set_style_radius(ui->toast, ui->theme.base_radius_px * 3u, 0);
        lv_obj_set_style_border_width(ui->toast, 1, 0);
        lv_obj_set_style_pad_hor(ui->toast, 12, 0);
        lv_obj_set_style_pad_ver(ui->toast, 9, 0);
        lv_obj_set_style_shadow_width(ui->toast, 14, 0);
        lv_obj_set_style_shadow_opa(ui->toast, LV_OPA_30, 0);
        lv_obj_set_style_shadow_offset_y(ui->toast, 4, 0);
        lv_obj_clear_flag(ui->toast, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(ui->toast, LV_OBJ_FLAG_CLICKABLE);
        ui->toast_label = make_label(ui->toast, "", ui->text_font,
                                     color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
        lv_label_set_long_mode(ui->toast_label, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_align(ui->toast_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(ui->toast_label);
    }
    lv_label_set_text_fmt(ui->toast_label, "%.*s", (int)toast->message.size,
                          toast->message.data);
    toast_apply_theme(ui, toast->tone);
    ui->toast_tone = toast->tone;
    toast_reposition(ui);
    lv_obj_remove_flag(ui->toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui->toast);
    ui->toast_visible = 1;
    lv_anim_delete(ui->toast, NULL);
    lv_obj_set_style_opa(ui->toast, LV_OPA_TRANSP, 0);
    lv_obj_fade_in(ui->toast, 140, 0);
    duration = toast->duration_ms == 0 ? 2500 : toast->duration_ms;
    if (ui->toast_timer == NULL)
        ui->toast_timer = lv_timer_create(toast_hide, duration, ui);
    else {
        lv_timer_set_period(ui->toast_timer, duration);
        lv_timer_reset(ui->toast_timer);
    }
}

static void rebuild(pxsys_reference_lvgl_t* ui) {
    pxsys_reference_layout_t layout;
    lv_obj_t* navigation;
    lv_obj_t* action;
    const char* title;
    if (!ui_valid(ui) || ui->root == NULL ||
        pxsys_reference_layout_compute(&ui->display, &layout) != PXSYS_STATUS_OK)
        return;
    application_backdrop_apply(ui);
    lv_obj_clean(ui->root);
    ui->navigation_handle = NULL;
    lv_obj_set_pos(ui->root, 0, 0);
    lv_obj_set_size(ui->root, (lv_coord_t)ui->display.width,
                    (lv_coord_t)ui->display.height);
    lv_obj_set_style_bg_color(ui->root,
                              color_token(ui, PXSYS_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(ui->root,
                            ui->content_active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);

    if ((ui->active_chrome & PXSYS_REFERENCE_UI_STATUS_BAR) &&
        ui->window.status_bar_mode != PXSYS_WINDOW_BAR_HIDDEN) {
        if (bar_is_visible(ui, ui->window.status_bar_mode)) {
            build_status_bar(ui, &layout);
        } else {
            lv_obj_t* catcher = lv_obj_create(ui->root);
            int32_t height = layout.safe_area.y + 10;
            if (height < 14) height = 14;
            style_plain(catcher);
            lv_obj_set_pos(catcher, 0, 0);
            lv_obj_set_size(catcher, (lv_coord_t)ui->display.width, height);
            lv_obj_set_style_bg_opa(catcher, LV_OPA_TRANSP, 0);
            lv_obj_add_flag(catcher,
                            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
            lv_obj_add_event_cb(catcher, status_bar_event, LV_EVENT_PRESSED, ui);
            lv_obj_add_event_cb(catcher, status_bar_event, LV_EVENT_RELEASED, ui);
        }
    }

    ui->content = NULL;
    ui->title = NULL;
    if (ui->content_active) {
        ui->content = lv_obj_create(ui->root);
        style_plain(ui->content);
        lv_obj_set_pos(ui->content, layout.content.x, layout.content.y);
        lv_obj_set_size(ui->content, (lv_coord_t)layout.content.width,
                        (lv_coord_t)layout.content.height);
        lv_obj_set_style_bg_opa(ui->content, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_column(ui->content, layout.item_gap, 0);
        lv_obj_set_style_pad_row(ui->content, layout.item_gap, 0);
        title = ui->active_page == REFERENCE_PAGE_HOME ? "Home" : "Settings";
        ui->title = make_label(ui->content, title, ui->title_font,
                               color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
        lv_obj_set_width(ui->title, LV_PCT(100));
        lv_obj_set_height(ui->title,
                          layout.size_class == PXSYS_UI_SIZE_COMPACT ? 28 : 36);
        if (ui->active_page == REFERENCE_PAGE_HOME)
            build_home(ui, &layout);
        else
            build_settings(ui, &layout);
    }

    if ((ui->active_chrome & PXSYS_REFERENCE_UI_NAVIGATION_BAR) &&
        ui->window.navigation_bar_mode != PXSYS_WINDOW_BAR_HIDDEN) {
        int visible = bar_is_visible(ui, ui->window.navigation_bar_mode);
        int gesture_height = layout.size_class == PXSYS_UI_SIZE_COMPACT ? 22 :
                             layout.size_class == PXSYS_UI_SIZE_REGULAR ? 28 : 34;
        navigation = lv_obj_create(ui->root);
        style_plain(navigation);
        if (ui->navigation_mode == PXSYS_NAVIGATION_GESTURES) {
            lv_obj_set_pos(navigation, 0,
                           (int32_t)ui->display.height - gesture_height);
            lv_obj_set_size(navigation, (lv_coord_t)ui->display.width,
                            gesture_height);
            lv_obj_set_style_bg_opa(navigation, LV_OPA_TRANSP, 0);
            if (visible) {
                action = lv_obj_create(navigation);
                style_plain(action);
                lv_obj_set_size(
                    action,
                    layout.size_class == PXSYS_UI_SIZE_COMPACT ? 56 : 84, 4);
                lv_obj_set_style_radius(action, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_bg_color(
                    action, color_token(ui, PXSYS_COLOR_TEXT_PRIMARY), 0);
                lv_obj_align(action, LV_ALIGN_BOTTOM_MID, 0, -5);
                ui->navigation_handle = action;
            }
            lv_obj_add_flag(navigation,
                            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
            lv_obj_add_event_cb(navigation, navigation_gesture_event,
                                LV_EVENT_PRESSED, ui);
            lv_obj_add_event_cb(navigation, navigation_gesture_event,
                                LV_EVENT_PRESSING, ui);
            lv_obj_add_event_cb(navigation, navigation_gesture_event,
                                LV_EVENT_RELEASED, ui);
            lv_obj_add_event_cb(navigation, navigation_gesture_event,
                                LV_EVENT_GESTURE, ui);
        } else if (!visible) {
            lv_obj_set_pos(navigation, 0,
                           (int32_t)ui->display.height - gesture_height);
            lv_obj_set_size(navigation, (lv_coord_t)ui->display.width,
                            gesture_height);
            lv_obj_set_style_bg_opa(navigation, LV_OPA_TRANSP, 0);
            lv_obj_add_flag(navigation,
                            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
            lv_obj_add_event_cb(navigation, navigation_gesture_event,
                                LV_EVENT_PRESSED, ui);
            lv_obj_add_event_cb(navigation, navigation_gesture_event,
                                LV_EVENT_RELEASED, ui);
        } else {
            int32_t action_width;
            int32_t action_height;
            lv_obj_set_pos(navigation, layout.navigation_bar.x,
                           layout.navigation_bar.y);
            lv_obj_set_size(navigation, (lv_coord_t)layout.navigation_bar.width,
                            (lv_coord_t)layout.navigation_bar.height);
            lv_obj_set_style_bg_color(navigation,
                                      color_token(ui, PXSYS_COLOR_SURFACE), 0);
            if (ui->window.navigation_bar_mode == PXSYS_WINDOW_BAR_TRANSIENT)
                lv_obj_set_style_bg_opa(navigation, (lv_opa_t)224, 0);
            action_width = (int32_t)layout.navigation_bar.width / 3;
            action_height = layout.navigation_bar.height > 8
                                ? (int32_t)layout.navigation_bar.height - 8 : 1;
            action = make_navigation_action(ui, navigation,
                                            NAVIGATION_ICON_BACK, back_clicked);
            lv_obj_set_size(action, action_width, action_height);
            lv_obj_align(action, LV_ALIGN_LEFT_MID, 0, 0);
            action = make_navigation_action(ui, navigation,
                                            NAVIGATION_ICON_HOME, home_clicked);
            lv_obj_set_size(action, action_width, action_height);
            lv_obj_center(action);
            action = make_navigation_action(
                ui, navigation, NAVIGATION_ICON_RECENTS, recents_clicked);
            lv_obj_set_size(action, action_width, action_height);
            lv_obj_align(action, LV_ALIGN_RIGHT_MID, 0, 0);
        }
    }
    lv_obj_move_foreground(ui->root);
    toast_reposition(ui);
    if (ui->toast_visible) lv_obj_move_foreground(ui->toast);
    if (ui->notification_shade_open) build_notification_shade(ui);
}

static void display_changed(void* context,
                            const pxsys_display_profile_t* profile) {
    pxsys_reference_lvgl_t* ui = (pxsys_reference_lvgl_t*)context;
    if (!ui_valid(ui)) return;
    ui->display = *profile;
    rebuild(ui);
}

static void theme_changed(void* context,
                          const pxsys_theme_snapshot_t* theme) {
    pxsys_reference_lvgl_t* ui = (pxsys_reference_lvgl_t*)context;
    if (!ui_valid(ui)) return;
    ui->theme = *theme;
    rebuild(ui);
    toast_apply_theme(ui, ui->toast_tone);
}

static void system_status_changed(
    void* context, const pxsys_system_status_snapshot_t* status) {
    pxsys_reference_lvgl_t* ui = (pxsys_reference_lvgl_t*)context;
    if (!ui_valid(ui) || status == NULL) return;
    ui->system_status = *status;
    rebuild(ui);
}

static void window_changed(void* context,
                           const pxsys_window_snapshot_t* window) {
    pxsys_reference_lvgl_t* ui = (pxsys_reference_lvgl_t*)context;
    if (!ui_valid(ui) || window == NULL) return;
    ui->window = *window;
    ui->transient_revealed = 0;
    if (ui->transient_timer != NULL) lv_timer_pause(ui->transient_timer);
    close_task_switcher(ui);
    close_notification_shade(ui);
    rebuild(ui);
}

static pxsys_status_t app_create(void* context,
                                 const pxsys_app_descriptor_t* app,
                                 uint64_t instance_id, void** instance) {
    (void)app;
    (void)instance_id;
    if (context == NULL || instance == NULL) return PXSYS_STATUS_INVALID_ARGUMENT;
    *instance = context;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t app_start(void* context, void* instance,
                                const pxsys_message_t* launch) {
    (void)context;
    (void)instance;
    (void)launch;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t app_foreground(void* context, void* instance) {
    app_context_t* app = (app_context_t*)instance;
    (void)context;
    if (app == NULL || !ui_valid(app->ui)) return PXSYS_STATUS_BAD_STATE;
    if (app->page == REFERENCE_PAGE_HOME ||
        app->page == REFERENCE_PAGE_SETTINGS) {
        app->ui->active_page = app->page;
        app->ui->content_active = 1;
    } else {
        app->ui->active_chrome |= (uint8_t)(1u << app->page);
    }
    lv_obj_remove_flag(app->ui->root, LV_OBJ_FLAG_HIDDEN);
    rebuild(app->ui);
    return PXSYS_STATUS_OK;
}

static pxsys_status_t app_background(void* context, void* instance) {
    app_context_t* app = (app_context_t*)instance;
    (void)context;
    if (app == NULL || !ui_valid(app->ui)) return PXSYS_STATUS_BAD_STATE;
    if (app->page == REFERENCE_PAGE_HOME ||
        app->page == REFERENCE_PAGE_SETTINGS)
        app->ui->content_active = 0;
    else
        app->ui->active_chrome &= (uint8_t)~(1u << app->page);
    rebuild(app->ui);
    return PXSYS_STATUS_OK;
}

static pxsys_status_t app_event(void* context, void* instance,
                                const pxsys_message_t* event) {
    (void)context;
    (void)instance;
    (void)event;
    return PXSYS_STATUS_OK;
}

static pxsys_back_result_t app_back(void* context, void* instance) {
    (void)context;
    (void)instance;
    return PXSYS_BACK_UNHANDLED;
}

static void app_stop(void* context, void* instance,
                     pxsys_stop_reason_t reason) {
    (void)context;
    (void)instance;
    (void)reason;
}

static void app_destroy(void* context, void* instance) {
    (void)context;
    (void)instance;
}

void pxsys_reference_lvgl_config_init(pxsys_reference_lvgl_config_t* config) {
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->features = PXSYS_REFERENCE_UI_ALL;
    config->max_launcher_apps = 24;
    config->navigation_mode = PXSYS_NAVIGATION_BUTTONS;
    config->animations_enabled = PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS ? 1 : 0;
    config->allocator.struct_size = sizeof(config->allocator);
}

static pxsys_status_t register_apps(pxsys_reference_lvgl_t* ui) {
    static const char* const app_ids[] = {
        "system.home", "system.settings", "system.status-bar",
        "system.navigation-bar"};
    static const char* const names[] = {
        "System Home", "System Settings", "System Status Bar",
        "System Navigation Bar"};
    static const char* const roles[] = {
        PXSYS_ROLE_HOME, PXSYS_ROLE_SETTINGS, PXSYS_ROLE_STATUS_BAR,
        PXSYS_ROLE_NAVIGATION_BAR};
    size_t index;
    for (index = 0; index < REFERENCE_APP_COUNT; ++index) {
        pxsys_app_descriptor_t descriptor = {0};
        pxsys_native_app_t native = {0};
        pxsys_role_candidate_t role = {0};
        pxsys_status_t status;
        snprintf(ui->app_ids[index], sizeof(ui->app_ids[index]), "%s", app_ids[index]);
        memcpy(ui->identities[index].publisher_root, ui->publisher_root,
               PXSYS_PUBLISHER_ROOT_BYTES);
        ui->identities[index].app_id = pxsys_string_from_cstr(ui->app_ids[index]);
        ui->contexts[index].ui = ui;
        ui->contexts[index].page = (reference_page_t)index;
        if (!(ui->features & (uint32_t)(1u << index))) continue;
        descriptor.struct_size = sizeof(descriptor);
        descriptor.identity = ui->identities[index];
        descriptor.display_name = pxsys_string_from_cstr(names[index]);
        descriptor.version = pxsys_string_from_cstr("1.0.0");
        descriptor.runtime_id = pxsys_string_from_cstr(PXSYS_NATIVE_RUNTIME_ID);
        descriptor.flags = PXSYS_APP_FLAG_SYSTEM | PXSYS_APP_FLAG_ENABLED |
                           PXSYS_APP_FLAG_SINGLE_INSTANCE;
        status = pxsys_app_registry_register(
            pxsys_standard_system_apps(ui->system), &descriptor);
        if (status != PXSYS_STATUS_OK) return status;
        ui->apps_registered |= (uint8_t)(1u << index);
        native.struct_size = sizeof(native);
        native.identity = ui->identities[index];
        native.context = &ui->contexts[index];
        native.create = app_create;
        native.start = app_start;
        native.foreground = app_foreground;
        native.background = app_background;
        native.event = app_event;
        native.back = app_back;
        native.stop = app_stop;
        native.destroy = app_destroy;
        status = pxsys_native_runtime_register_app(
            pxsys_standard_system_native_runtime(ui->system), &native);
        if (status != PXSYS_STATUS_OK) return status;
        role.struct_size = sizeof(role);
        role.role_id = pxsys_string_from_cstr(roles[index]);
        role.app = ui->identities[index];
        role.priority = 100;
        status = pxsys_role_candidate_register(
            pxsys_standard_system_roles(ui->system), &role);
        if (status != PXSYS_STATUS_OK) return status;
        ui->roles_registered |= (uint8_t)(1u << index);
    }
    return PXSYS_STATUS_OK;
}

static void unregister_apps(pxsys_reference_lvgl_t* ui) {
    size_t index = REFERENCE_APP_COUNT;
    while (index != 0) {
        index--;
        if (ui->roles_registered & (uint8_t)(1u << index))
            (void)pxsys_role_candidates_unregister(
                pxsys_standard_system_roles(ui->system), &ui->identities[index]);
        if (ui->apps_registered & (uint8_t)(1u << index)) {
            (void)pxsys_native_runtime_unregister_app(
                pxsys_standard_system_native_runtime(ui->system), &ui->identities[index]);
            (void)pxsys_app_registry_unregister(
                pxsys_standard_system_apps(ui->system), &ui->identities[index]);
        }
    }
    ui->apps_registered = 0;
    ui->roles_registered = 0;
}

pxsys_status_t pxsys_reference_lvgl_create(
    const pxsys_reference_lvgl_config_t* config,
    pxsys_reference_lvgl_t** output) {
    pxsys_reference_lvgl_t* ui;
    pxsys_status_t status;
    if (output == NULL) return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (config == NULL || config->struct_size < sizeof(*config) ||
        config->system == NULL || config->parent == NULL ||
        config->max_launcher_apps == 0 ||
        config->navigation_mode > PXSYS_NAVIGATION_GESTURES ||
        config->allocator.struct_size < sizeof(config->allocator) ||
        config->allocator.allocate == NULL || config->allocator.release == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    ui = (pxsys_reference_lvgl_t*)config->allocator.allocate(
        config->allocator.context, sizeof(*ui));
    if (ui == NULL) return PXSYS_STATUS_NO_MEMORY;
    memset(ui, 0, sizeof(*ui));
    ui->launcher_items = (launcher_item_t*)config->allocator.allocate(
        config->allocator.context,
        config->max_launcher_apps * sizeof(*ui->launcher_items));
    if (ui->launcher_items == NULL) {
        config->allocator.release(config->allocator.context, ui);
        return PXSYS_STATUS_NO_MEMORY;
    }
    memset(ui->launcher_items, 0,
           config->max_launcher_apps * sizeof(*ui->launcher_items));
    ui->system = config->system;
    ui->allocator = config->allocator;
    ui->parent = config->parent;
    ui->text_font = config->text_font;
    ui->title_font = config->title_font != NULL ? config->title_font
                                                : config->text_font;
    ui->features = config->features;
    ui->max_launcher_apps = config->max_launcher_apps;
    ui->navigation_mode = config->navigation_mode;
    ui->animations_enabled =
        PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS && config->animations_enabled;
    memcpy(ui->publisher_root, config->publisher_root,
           PXSYS_PUBLISHER_ROOT_BYTES);
    ui->magic = REFERENCE_MAGIC;
    ui->root = lv_obj_create(ui->parent);
    style_plain(ui->root);
    /* The transparent system-chrome root sits above application surfaces.
     * Only its concrete bars and overlays may participate in hit testing. */
    lv_obj_remove_flag(ui->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(ui->root, LV_OBJ_FLAG_HIDDEN);
    ui->display.struct_size = sizeof(ui->display);
    status = pxsys_display_service_get(
        pxsys_standard_system_display(ui->system), &ui->display);
    if (status != PXSYS_STATUS_OK) goto failed;
    ui->theme.struct_size = sizeof(ui->theme);
    status = pxsys_theme_service_get(
        pxsys_standard_system_theme(ui->system), &ui->theme);
    if (status != PXSYS_STATUS_OK) goto failed;
    ui->system_status.struct_size = sizeof(ui->system_status);
    status = pxsys_system_status_service_get(
        pxsys_standard_system_status(ui->system), &ui->system_status);
    if (status != PXSYS_STATUS_OK) goto failed;
    ui->window.struct_size = sizeof(ui->window);
    status = pxsys_window_service_get(
        pxsys_standard_system_window(ui->system), &ui->window);
    if (status != PXSYS_STATUS_OK) goto failed;
    status = register_apps(ui);
    if (status != PXSYS_STATUS_OK) goto failed;
    status = pxsys_display_service_subscribe(
        pxsys_standard_system_display(ui->system), ui, display_changed);
    if (status != PXSYS_STATUS_OK) goto failed;
    ui->display_subscribed = 1;
    status = pxsys_theme_service_subscribe(
        pxsys_standard_system_theme(ui->system), ui, theme_changed);
    if (status != PXSYS_STATUS_OK) goto failed;
    ui->theme_subscribed = 1;
    status = pxsys_system_status_service_subscribe(
        pxsys_standard_system_status(ui->system), ui, system_status_changed);
    if (status != PXSYS_STATUS_OK) goto failed;
    ui->status_subscribed = 1;
    status = pxsys_window_service_subscribe(
        pxsys_standard_system_window(ui->system), ui, window_changed);
    if (status != PXSYS_STATUS_OK) goto failed;
    ui->window_subscribed = 1;
    status = pxsys_toast_service_subscribe(
        pxsys_standard_system_toasts(ui->system), ui, toast_posted);
    if (status != PXSYS_STATUS_OK) goto failed;
    ui->toast_subscribed = 1;
    *output = ui;
    return PXSYS_STATUS_OK;

failed:
    (void)pxsys_reference_lvgl_destroy(ui);
    return status;
}

pxsys_status_t pxsys_reference_lvgl_start(pxsys_reference_lvgl_t* ui) {
    pxsys_intent_t intent = {0};
    pxsys_instance_ref_t instance;
    pxsys_status_t status;
    if (!ui_valid(ui)) return PXSYS_STATUS_INVALID_ARGUMENT;
    if (!(ui->features & PXSYS_REFERENCE_UI_HOME))
        return PXSYS_STATUS_NOT_FOUND;
    status = open_role(ui, PXSYS_ROLE_HOME);
    if (status != PXSYS_STATUS_OK && status != PXSYS_STATUS_PENDING)
        return status;
    intent.struct_size = sizeof(intent);
    intent.action = pxsys_string_from_cstr("system.intent.main");
    if (ui->features & PXSYS_REFERENCE_UI_STATUS_BAR) {
        status = pxsys_role_host_start(
            pxsys_standard_system_role_host(ui->system), NULL,
            pxsys_string_from_cstr(PXSYS_ROLE_STATUS_BAR), &intent, &instance);
        if (status != PXSYS_STATUS_OK && status != PXSYS_STATUS_PENDING)
            return status;
    }
    if (ui->features & PXSYS_REFERENCE_UI_NAVIGATION_BAR) {
        status = pxsys_role_host_start(
            pxsys_standard_system_role_host(ui->system), NULL,
            pxsys_string_from_cstr(PXSYS_ROLE_NAVIGATION_BAR), &intent, &instance);
        if (status != PXSYS_STATUS_OK && status != PXSYS_STATUS_PENDING)
            return status;
    }
    return PXSYS_STATUS_OK;
}

void pxsys_reference_lvgl_refresh_apps(pxsys_reference_lvgl_t* ui) {
    if (ui_valid(ui) && ui->active_page == REFERENCE_PAGE_HOME) rebuild(ui);
}

pxsys_status_t pxsys_reference_lvgl_set_navigation_mode(
    pxsys_reference_lvgl_t* ui, pxsys_navigation_mode_t mode) {
    if (!ui_valid(ui) || mode > PXSYS_NAVIGATION_GESTURES)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (ui->navigation_mode != mode) {
        ui->navigation_mode = mode;
        rebuild(ui);
    }
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_reference_lvgl_set_animations_enabled(
    pxsys_reference_lvgl_t* ui, bool enabled) {
    if (!ui_valid(ui)) return PXSYS_STATUS_INVALID_ARGUMENT;
#if PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS
    ui->animations_enabled = enabled ? 1 : 0;
#else
    (void)enabled;
    ui->animations_enabled = 0;
#endif
    return PXSYS_STATUS_OK;
}

bool pxsys_reference_lvgl_animations_enabled(
    const pxsys_reference_lvgl_t* ui) {
    return ui_valid(ui) && ui->animations_enabled;
}

bool pxsys_reference_lvgl_dismiss_overlay(pxsys_reference_lvgl_t* ui) {
    if (!ui_valid(ui)) return false;
    if (ui->task_switcher != NULL) {
        close_task_switcher(ui);
        return true;
    }
    if (ui->notification_shade_open) {
        close_notification_shade(ui);
        return true;
    }
    if (has_transient_chrome(ui) && !ui->transient_revealed) {
        reveal_transient_chrome(ui);
        return true;
    }
    return false;
}

pxsys_status_t pxsys_reference_lvgl_destroy(pxsys_reference_lvgl_t* ui) {
    pxsys_allocator_t allocator;
#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && LV_USE_SNAPSHOT
    size_t preview_index;
#endif
    if (!ui_valid(ui)) return PXSYS_STATUS_INVALID_ARGUMENT;
    (void)lv_async_call_cancel(rebuild_async, ui);
    if (ui->toast_subscribed)
        (void)pxsys_toast_service_unsubscribe(
            pxsys_standard_system_toasts(ui->system), ui, toast_posted);
    if (ui->window_subscribed)
        (void)pxsys_window_service_unsubscribe(
            pxsys_standard_system_window(ui->system), ui, window_changed);
    if (ui->status_subscribed)
        (void)pxsys_system_status_service_unsubscribe(
            pxsys_standard_system_status(ui->system), ui,
            system_status_changed);
    if (ui->theme_subscribed)
        (void)pxsys_theme_service_unsubscribe(
            pxsys_standard_system_theme(ui->system), ui, theme_changed);
    if (ui->display_subscribed)
        (void)pxsys_display_service_unsubscribe(
            pxsys_standard_system_display(ui->system), ui, display_changed);
    if (ui->features & PXSYS_REFERENCE_UI_NAVIGATION_BAR)
        (void)pxsys_role_host_stop(pxsys_standard_system_role_host(ui->system),
                                   pxsys_string_from_cstr(PXSYS_ROLE_NAVIGATION_BAR),
                                   PXSYS_STOP_SHUTDOWN);
    if (ui->features & PXSYS_REFERENCE_UI_STATUS_BAR)
        (void)pxsys_role_host_stop(pxsys_standard_system_role_host(ui->system),
                                   pxsys_string_from_cstr(PXSYS_ROLE_STATUS_BAR),
                                   PXSYS_STOP_SHUTDOWN);
    (void)pxsys_task_manager_finish_all(pxsys_standard_system_tasks(ui->system),
                                        PXSYS_STOP_SHUTDOWN);
    unregister_apps(ui);
    if (ui->toast_timer != NULL) lv_timer_delete(ui->toast_timer);
    if (ui->transient_timer != NULL) lv_timer_delete(ui->transient_timer);
    if (ui->toast != NULL) lv_obj_delete(ui->toast);
    if (ui->notification_shade != NULL) lv_obj_delete(ui->notification_shade);
    if (ui->task_switcher != NULL) lv_obj_delete(ui->task_switcher);
    if (ui->root != NULL) lv_obj_delete(ui->root);
#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && LV_USE_SNAPSHOT
    for (preview_index = 0;
         preview_index < PXSYS_REFERENCE_UI_TASK_PREVIEW_COUNT;
         ++preview_index) {
        if (ui->task_previews[preview_index].image != NULL)
            lv_draw_buf_destroy(ui->task_previews[preview_index].image);
    }
#endif
    allocator = ui->allocator;
    ui->magic = 0;
    allocator.release(allocator.context, ui->launcher_items);
    allocator.release(allocator.context, ui);
    return PXSYS_STATUS_OK;
}
