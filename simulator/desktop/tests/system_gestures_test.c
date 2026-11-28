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
static unsigned edge_presses, edge_releases, edge_clicks;
static void edge_input(lv_event_t* event) {
    switch(lv_event_get_code(event)) {
    case LV_EVENT_PRESSED: ++edge_presses;break;
    case LV_EVENT_RELEASED: ++edge_releases;break;
    case LV_EVENT_CLICKED: ++edge_clicks;break;
    default:break;
    }
}
static pxsys_standard_system_t *system_under_test;
static unsigned level_writes;

static pxsys_status_t set_test_level(void *context, pxsys_level_control_t level,
                                     uint8_t percent) {
    (void)context;
    pxsys_system_status_service_t *service = pxsys_standard_system_status(system_under_test);
    pxsys_system_status_snapshot_t status = {.struct_size = sizeof(status)};
    assert(pxsys_system_status_service_get(service, &status) == PXSYS_STATUS_OK);
    if (level == PXSYS_LEVEL_CONTROL_VOLUME) status.volume_percent = percent;
    else status.brightness_percent = percent;
    ++level_writes;
    return pxsys_system_status_service_update(service, &status);
}

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

typedef struct {
    unsigned count;
    lv_area_t regions[4];
} shade_damage_t;

static void record_shade_damage(lv_event_t *event) {
    shade_damage_t *damage = lv_event_get_user_data(event);
    assert(damage->count < sizeof(damage->regions) / sizeof(damage->regions[0]));
    damage->regions[damage->count] = *(const lv_area_t *)lv_event_get_param(event);
    ++damage->count;
}

static void check_shade_damage(pxsys_reference_lvgl_t *ui, lv_display_t *display) {
    const int32_t height = ui->notification_shade_height;
    const int32_t travel = ui->notification_panel_height;
    const int32_t mid_progress = travel * 128 / height;
    const int32_t mid_height = height * mid_progress / 256;
    lv_area_t before, after, time_before, time_after, viewport;
    lv_obj_get_coords(ui->notification_viewport, &viewport);
    assert(viewport.y1 == 0 && viewport.y2 == height - 1);
    assert(viewport.x1 == 0 && viewport.x2 == (int32_t)ui->display.width - 1);
    lv_obj_get_coords(ui->notification_panel, &before);
    lv_obj_get_coords(ui->notification_time, &time_before);
    lv_refr_now(display);
    shade_damage_t damage = {0};
    lv_display_add_event_cb(display, record_shade_damage,
                            LV_EVENT_INVALIDATE_AREA, &damage);
    notification_shade_progress_set(ui, mid_progress);
    lv_obj_get_coords(ui->notification_panel, &after);
    lv_obj_get_coords(ui->notification_time, &time_after);
    assert(after.y1 == before.y1 + mid_height - travel &&
           after.y2 == before.y2 + mid_height - travel);
    assert(time_after.y1 == time_before.y1 + mid_height - travel);
    assert(damage.count >= 2 && damage.regions[0].x1 == viewport.x1 &&
           damage.regions[0].x2 == viewport.x2 &&
           damage.regions[0].y1 == mid_height &&
           damage.regions[0].y2 == height - 1);
    assert(damage.regions[1].x1 == before.x1 &&
           damage.regions[1].x2 == before.x2 &&
           damage.regions[1].y2 == mid_height - 1);
    lv_display_remove_event_cb_with_user_data(display, record_shade_damage, &damage);
    const int32_t docked_progress = (travel * 256 + height - 1) / height;
    const int32_t docked_height = height * docked_progress / 256;
    notification_shade_progress_set(ui, docked_progress);
    lv_obj_get_coords(ui->notification_panel, &after);
    assert(after.y1 == before.y1);
    lv_refr_now(display);
    damage.count = 0;
    lv_display_add_event_cb(display, record_shade_damage,
                            LV_EVENT_INVALIDATE_AREA, &damage);
    notification_shade_progress_set(ui, 256);
    assert(damage.count >= 1 && damage.count <= 3 &&
           damage.regions[0].y1 == docked_height &&
           damage.regions[0].y2 == height - 1);
    lv_obj_get_coords(ui->notification_panel, &after);
    assert(after.y1 == before.y1);
    lv_display_remove_event_cb_with_user_data(display, record_shade_damage, &damage);

    /* Several input samples should produce one layout at the latest position. */
    assert(ui->notification_refresh_hook);
    notification_drag_progress(ui, mid_progress / 2);
    notification_drag_progress(ui, mid_progress);
    lv_obj_get_coords(ui->notification_panel, &after);
    assert(ui->notification_motion_pending && after.y1 == before.y1);
    lv_refr_now(display);
    lv_obj_get_coords(ui->notification_panel, &after);
    assert(!ui->notification_motion_pending &&
           after.y1 == before.y1 + mid_height - travel);
    notification_shade_progress_set(ui, 256);
}

static void check_shade_levels(pxsys_reference_lvgl_t *ui, lv_indev_t *indev) {
    lv_obj_t *shade = ui->notification_shade;
    lv_obj_t *volume = ui->notification_volume;
    lv_obj_t *brightness = ui->notification_brightness;
    assert(volume && brightness);
    lv_area_t track;
    lv_obj_get_coords(volume, &track);
    const int y = (track.y1 + track.y2) / 2;
    touch(indev, track.x1 + 1, y, LV_INDEV_STATE_PRESSED);
    assert(lv_slider_get_value(volume) <= 1 && level_writes == 0);
    touch(indev, track.x2, y + 3, LV_INDEV_STATE_PRESSED);
    assert(lv_slider_get_value(volume) >= 99 && level_writes == 0);
    assert(ui->notification_shade == shade && ui->notification_progress == 256);
    touch(indev, track.x2, y + 3, LV_INDEV_STATE_RELEASED);
    assert(lv_slider_get_value(volume) == 100 && level_writes == 1);
    assert(strcmp(lv_label_get_text(ui->shade_levels[0].readout), "100%") == 0);

    lv_obj_get_coords(brightness, &track);
    const int brightness_y = (track.y1 + track.y2) / 2;
    touch(indev, track.x1 + 1, brightness_y, LV_INDEV_STATE_PRESSED);
    assert(lv_slider_get_value(brightness) <= 3 && level_writes == 1);
    lv_indev_wait_release(indev);
    touch(indev, track.x1, brightness_y, LV_INDEV_STATE_RELEASED);
    assert(lv_slider_get_value(brightness) == 70 && level_writes == 1);

    touch(indev, track.x1, brightness_y, LV_INDEV_STATE_PRESSED);
    touch(indev, track.x1, brightness_y, LV_INDEV_STATE_RELEASED);
    assert(lv_slider_get_value(brightness) == 2 && level_writes == 2);
    const uint32_t key = LV_KEY_RIGHT;
    lv_obj_send_event(brightness, LV_EVENT_KEY, (void *)&key);
    assert(lv_slider_get_value(brightness) == 3 && level_writes == 3);
    assert(strcmp(lv_label_get_text(ui->shade_levels[1].readout), "3%") == 0);
    assert(ui->notification_shade_open && !ui->volume_osd_visible);
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

static unsigned ime_selections,ime_lists;
static bool ime_save_fails;
static pxsys_reference_input_method_preferences_t ime_preferences;
static void ime_list(void *context,pxsys_reference_input_method_emit_fn emit,void *output) {
    (void)context; ++ime_lists;
    emit(output,"",pxsys_string_from_cstr("System keyboard"),ime_selections==0);
    emit(output,"test:ime",pxsys_string_from_cstr("Pinyin IME"),ime_selections!=0);
}
static bool ime_select(void *context,const char *identity) {
    (void)context; assert(!strcmp(identity,"test:ime")); ++ime_selections; return true;
}
static bool ime_preferences_control(void *context,pxsys_reference_input_method_preferences_t *value,bool save) {
    (void)context;
    if (!save) { *value=ime_preferences; return true; }
    if (ime_save_fails) return false;
    assert(pxsys_reference_input_method_preferences_valid(*value)); ime_preferences=*value; return true;
}
static lv_obj_t *find_label(lv_obj_t *root,const char *text) {
    if (lv_obj_has_class(root,&lv_label_class) && !strcmp(lv_label_get_text(root),text)) return root;
    for (unsigned i=0;i<lv_obj_get_child_count(root);++i) {
        lv_obj_t *found=find_label(lv_obj_get_child(root,i),text); if (found) return found;
    }
    return NULL;
}
static lv_obj_t *find_ime_dropdown(lv_obj_t *root, bool language) {
    if (lv_obj_has_class(root, &lv_dropdown_class) &&
        (lv_obj_get_user_data(root) != NULL) == language) return root;
    for (uint32_t index = 0; index < lv_obj_get_child_count(root); ++index) {
        lv_obj_t *child = find_ime_dropdown(lv_obj_get_child(root, index), language);
        if (child != NULL) return child;
    }
    return NULL;
}
static void click_label(lv_obj_t *root, const char *text);

static void check_input_method_settings(pxsys_reference_lvgl_t *ui) {
    ui->input_method_list=ime_list; ui->input_method_select=ime_select;
    ui->input_method_preferences=ime_preferences_control;
    assert(open_role(ui,PXSYS_ROLE_SETTINGS)==PXSYS_STATUS_OK);
    input_method_open(ui); assert(!ui->language_dialog && ui->active_page==REFERENCE_PAGE_INPUT_METHOD && ime_lists==1);
    lv_obj_t *list=ui->content;
    lv_obj_t *layout = find_ime_dropdown(list, false);
    lv_obj_t *language = find_ime_dropdown(list, true);
    assert(layout && language);
    lv_obj_update_layout(ui->content);
    lv_dropdown_open(layout); input_method_popup_fit(ui,layout);
    lv_obj_update_layout(lv_dropdown_get_list(layout));
    lv_area_t popup_area,panel_area;
    lv_obj_get_coords(lv_dropdown_get_list(layout),&popup_area);
    lv_obj_get_coords(ui->content,&panel_area);
    assert(popup_area.x1>=panel_area.x1 && popup_area.x2<=panel_area.x2);
    assert(popup_area.y1>=panel_area.y1 && popup_area.y2<=panel_area.y2);
    navigation_back(ui);
    assert(ui->active_page==REFERENCE_PAGE_INPUT_METHOD && !lv_dropdown_is_open(layout));

    lv_dropdown_set_selected(layout,PXSYS_INPUT_LAYOUT_NINE); lv_obj_send_event(layout,LV_EVENT_VALUE_CHANGED,NULL);
    lv_dropdown_set_selected(language,PXSYS_INPUT_ENGLISH); lv_obj_send_event(language,LV_EVENT_VALUE_CHANGED,NULL);
    assert(ime_preferences.layout==PXSYS_INPUT_LAYOUT_NINE && ime_preferences.language==PXSYS_INPUT_ENGLISH);
    ime_save_fails=true;
    lv_dropdown_set_selected(layout,PXSYS_INPUT_LAYOUT_FULL); lv_obj_send_event(layout,LV_EVENT_VALUE_CHANGED,NULL);
    assert(ime_preferences.layout==PXSYS_INPUT_LAYOUT_NINE && lv_dropdown_get_selected(layout)==PXSYS_INPUT_LAYOUT_NINE);
    ime_save_fails=false;
    click_label(list, "Pinyin IME");
    assert(ime_selections==1 && !ui->language_dialog && ui->active_page==REFERENCE_PAGE_INPUT_METHOD && ime_lists==2);
    navigation_back(ui); assert(ui->active_page==REFERENCE_PAGE_SETTINGS);
    input_method_open(ui); assert(ime_lists==3 && !ui->language_dialog && ui->active_page==REFERENCE_PAGE_INPUT_METHOD);
    pxsys_back_result_t back_result;
    assert(pxsys_task_manager_back(pxsys_standard_system_tasks(ui->system),&back_result)==PXSYS_STATUS_OK);
    assert(back_result==PXSYS_BACK_HANDLED && ui->active_page==REFERENCE_PAGE_SETTINGS);
    assert(ime_preferences.layout==PXSYS_INPUT_LAYOUT_NINE && ime_preferences.language==PXSYS_INPUT_ENGLISH);
}

static bool manager_enabled[]={true,false},manager_present[]={true,true};
static unsigned manager_moves;
enum { ordinary_app_count = 50 };
static const char *const manager_ids[]={"test:enabled-ime","test:disabled-ime"};
static size_t managed_apps(void *context,pxsys_reference_managed_app_t *output,size_t capacity,
    pxsys_reference_app_filter_t filter) {
    (void)context; size_t count=0;
    const unsigned ordinary=ordinary_app_count;
    for (unsigned i=0;i<ordinary+2;++i) {
        bool ime=i>=ordinary;
        if (ime && !manager_present[i-ordinary]) continue;
        if (filter==PXSYS_REFERENCE_APPS_INPUT_METHODS && !ime) continue;
        if (!output) { ++count; continue; }
        if (count==capacity) break;
        pxsys_reference_managed_app_t *app=&output[count++]; memset(app,0,sizeof(*app));
        if (ime) snprintf(app->identity,sizeof(app->identity),"%s",manager_ids[i-ordinary]);
        else snprintf(app->identity,sizeof(app->identity),"test:game-%u",i);
        snprintf(app->name,sizeof(app->name),"%s",ime?(i==ordinary?"Enabled IME":"Disabled IME"):"Ordinary game");
        app->installed=1; app->enabled=ime?manager_enabled[i-ordinary]:1;
    }
    return count;
}
static bool managed_action(void *context,const char *identity,pxsys_reference_app_action_t action) {
    (void)context;
    if(action==PXSYS_REFERENCE_APP_ACTION_MOVE) {
        assert(!strcmp(identity,"test:game-0"));++manager_moves;return true;
    }
    unsigned index=0; while (index<2 && strcmp(identity,manager_ids[index])) ++index;
    assert(index<2);
    if (action==PXSYS_REFERENCE_APP_ACTION_ENABLE) manager_enabled[index]=true;
    else if (action==PXSYS_REFERENCE_APP_ACTION_DISABLE) manager_enabled[index]=false;
    else if (action==PXSYS_REFERENCE_APP_ACTION_UNINSTALL) manager_present[index]=false;
    else assert(0);
    return true;
}
static void click_label(lv_obj_t *root,const char *text) {
    lv_obj_t *object=find_label(root,text); assert(object);
    while (object && !lv_obj_is_clickable(object)) object=lv_obj_get_parent(object);
    assert(object); lv_obj_send_event(object,LV_EVENT_CLICKED,NULL);
}
static void click_managed_app(pxsys_reference_lvgl_t *ui, size_t index) {
    assert(index < ui->managed_app_count);
    lv_obj_t *row = lv_obj_get_child(ui->managed_app_rows, (int32_t)index);
    assert(row && lv_obj_is_clickable(row));
    lv_obj_send_event(row, LV_EVENT_CLICKED, NULL);
}

static void check_input_method_app_manager(pxsys_reference_lvgl_t *ui) {
    ui->app_list=managed_apps; ui->app_action=managed_action;
    input_method_open(ui);
    click_label(ui->content,translated(ui,"input-method.manage","Manage installed input methods"));
    assert(ui->active_page==REFERENCE_PAGE_INPUT_METHOD_APPS && ui->managed_app_count==2);
    assert(!strcmp(ui->managed_apps[0].name,"Enabled IME"));
    assert(!strcmp(ui->managed_apps[1].name,"Disabled IME"));
    click_managed_app(ui,1);
    assert(!strcmp(ui->pending_identity,manager_ids[1]));
    click_label(ui->app_dialog,translated(ui,"settings.apps.enable","Enable"));
    assert(manager_enabled[1] && ui->managed_app_count==2 && !ui->app_dialog);
    click_managed_app(ui,1);
    click_label(ui->app_dialog,translated(ui,"settings.apps.disable","Disable"));
    assert(!manager_enabled[1] && !ui->managed_apps[1].enabled);
    for (unsigned i=0;i<2;++i) {
        click_managed_app(ui,0);
        assert(!strcmp(ui->pending_identity,manager_ids[i]));
        click_label(ui->app_dialog,translated(ui,"settings.apps.uninstall","Uninstall"));
        assert(ui->confirm_dialog);
        click_label(ui->confirm_dialog,translated(ui,"settings.confirm","Confirm"));
        assert(!manager_present[i] && ui->managed_app_count==1-i);
    }
    assert(find_label(ui->content,translated(ui,"input-method.apps.empty","No installed input method apps")));
    navigation_back(ui); assert(ui->active_page==REFERENCE_PAGE_INPUT_METHOD);
    assert(!open_role(ui,PXSYS_ROLE_APP_MANAGER));
    assert(ui->managed_app_count==ordinary_app_count);
    assert(!strcmp(ui->managed_apps[0].name,"Ordinary game"));
    ui->managed_apps[0].movable=1;
    show_app_detail(ui,&ui->managed_apps[0]);
    click_label(ui->app_dialog,translated(ui,"settings.apps.move","Move app and data"));
    assert(manager_moves==1 && !ui->app_dialog && ui->managed_apps_loaded);
    /* Active apps and unsupported legacy homes do not offer this action. */
    ui->managed_apps[0].active=1;show_app_detail(ui,&ui->managed_apps[0]);
    assert(!find_label(ui->app_dialog,translated(ui,"settings.apps.move","Move app and data")));
    page_close_dialogs(ui);ui->managed_apps[0].active=0;ui->managed_apps[0].movable=0;
    show_app_detail(ui,&ui->managed_apps[0]);
    assert(!find_label(ui->app_dialog,translated(ui,"settings.apps.move","Move app and data")));
    page_close_dialogs(ui);
    ui->managed_apps[0].archived=1;ui->managed_apps[0].installed=0;
    ui->managed_apps[0].enabled=0;ui->managed_apps[0].movable=1;ui->managed_apps[0].has_private_data=1;
    show_app_detail(ui,&ui->managed_apps[0]);
    assert(!find_label(ui->app_dialog,translated(ui,"settings.apps.enable","Enable")));
    assert(!find_label(ui->app_dialog,translated(ui,"settings.apps.disable","Disable")));
    assert(find_label(ui->app_dialog,translated(ui,"settings.apps.clear","Clear data")));
    assert(find_label(ui->app_dialog,translated(ui,"settings.apps.uninstall","Uninstall")));
    click_label(ui->app_dialog,translated(ui,"settings.apps.move_archive","Move saved data"));
    assert(manager_moves==2 && !ui->app_dialog);
    ui->managed_apps[0].archived=0;ui->managed_apps[0].installed=1;ui->managed_apps[0].enabled=1;
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
    system_config.initial_system_status.volume_supported = 1;
    system_config.initial_system_status.volume_percent = 40;
    system_config.initial_system_status.brightness_supported = 1;
    system_config.initial_system_status.brightness_percent = 70;
    system_config.set_level = set_test_level;
    assert(!pxsys_standard_system_create(&system_config, &system_under_test));

    pxsys_reference_lvgl_config_t ui_config;
    pxsys_reference_lvgl_config_init(&ui_config);
    ui_config.system = system_under_test;
    ui_config.allocator = allocator;
    ui_config.parent = lv_layer_top();
    ui_config.navigation_mode = PXSYS_NAVIGATION_GESTURES;
    ui_config.app_list=managed_apps; ui_config.app_action=managed_action;
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
    assert(pxsys_reference_layout_back_gesture_width() == 8);
    lv_obj_t* edge_button = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(edge_button, 0, 220);
    lv_obj_set_size(edge_button, 60, 80);
    lv_obj_add_event_cb(edge_button, edge_input, LV_EVENT_ALL, NULL);
    lv_obj_update_layout(edge_button);
    touch(indev, 4, 250, LV_INDEV_STATE_PRESSED);
    assert(!edge_presses && !edge_releases && !edge_clicks);
    touch(indev, 4, 250, LV_INDEV_STATE_RELEASED);
    assert(edge_presses == 1 && edge_releases == 1 && edge_clicks == 1);
    assert(!back_calls);
    swipe(indev, 4, 250, 6, 290);
    assert(edge_clicks == 1 && !back_calls);
    touch(indev, 4, 250, LV_INDEV_STATE_PRESSED);
    advance(500);
    touch(indev, 4, 250, LV_INDEV_STATE_RELEASED);
    assert(edge_clicks == 1 && !back_calls);
    lv_obj_add_state(edge_button, LV_STATE_DISABLED);
    touch(indev, 4, 250, LV_INDEV_STATE_PRESSED);
    touch(indev, 4, 250, LV_INDEV_STATE_RELEASED);
    assert(edge_clicks == 1);
    lv_obj_remove_state(edge_button, LV_STATE_DISABLED);
    touch(indev, 4, 250, LV_INDEV_STATE_PRESSED);
    lv_obj_delete(edge_button);
    assert(!ui->navigation_back_tap_armed && !ui->navigation_back_tap_target);
    edge_button = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(edge_button, 0, 220);lv_obj_set_size(edge_button, 60, 80);
    lv_obj_add_event_cb(edge_button, edge_input, LV_EVENT_ALL, NULL);
    lv_obj_update_layout(edge_button);
    touch(indev, 4, 250, LV_INDEV_STATE_RELEASED);
    assert(edge_clicks == 1); /* Never replay into a replacement object. */
    /* Outside the narrowed strip, ordinary native input is immediate. */
    touch(indev, 24, 250, LV_INDEV_STATE_PRESSED);
    assert(edge_presses == 2);
    touch(indev, 24, 250, LV_INDEV_STATE_RELEASED);
    assert(edge_clicks == 2 && edge_releases == 2);
    /* Fullscreen target edges must remain touchable at the physical x=0. */
    swipe(indev, 0, 240, 9, 240);
    assert(!back_calls);
    touch(indev, 0, 240, LV_INDEV_STATE_PRESSED);
    touch(indev, 18, 240, LV_INDEV_STATE_PRESSED);
    lv_area_t indicator_start, indicator_move;
    assert(ui->navigation_back_indicator != NULL);
    lv_obj_get_coords(ui->navigation_back_indicator, &indicator_start);
    touch(indev, 60, 261, LV_INDEV_STATE_PRESSED);
    lv_obj_get_coords(ui->navigation_back_indicator, &indicator_move);
    assert(indicator_move.y1 == indicator_start.y1 &&
           indicator_move.y2 == indicator_start.y2);
    assert(indicator_start.x1 == 0 && indicator_move.x1 == 0 &&
           indicator_move.x2 > indicator_start.x2);
    touch(indev, 110, 219, LV_INDEV_STATE_PRESSED);
    touch(indev, 110, 219, LV_INDEV_STATE_RELEASED);
    advance(350);
    assert(back_calls == 1 && !background_calls && !ui->content_active);
    assert(edge_clicks == 2);
    lv_obj_delete(edge_button);
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
    /* The revealed bar has glyphs, unlike its empty input catcher. Its
     * background still honors the Guest's RGBA, including transparent. */
    assert(lv_obj_get_child_count(ui->status_bar) > 0);
    assert(lv_obj_get_style_bg_opa(ui->status_bar, 0) == (ui->window.status_bar_color & 255u));
    assert(!bar_is_visible(ui, navigation_bar_mode(ui)));
    assert(!ui->navigation_handle);
    assert(ui->content_inset_top == 0 && ui->content_inset_bottom == 0);
    assert(!ui->content_active && !background_calls);
    /* Status samples must update the temporarily shown bar as well. */
    lv_obj_t *clock = ui->status_clock;
    ui->system_status.time_valid = 1;
    ui->system_status.hour = 12;
    ui->system_status.minute = 34;
    ui->status_refresh_pending = 1;
    status_refresh_poll(ui->status_refresh_timer);
    assert(clock == ui->status_clock);
    assert(strcmp(lv_label_get_text(lv_obj_get_child(ui->status_bar, 0)),
                  "12:34") == 0);
    touch(indev, 240, 3, LV_INDEV_STATE_PRESSED);
    touch(indev, 240, 100, LV_INDEV_STATE_PRESSED);
    lv_area_t panel;
    lv_obj_get_coords(ui->notification_viewport, &panel);
    assert(panel.y2 >= 97 && panel.y2 <= 101);
    assert(pxsys_reference_lvgl_covered_top(ui) == panel.y2 + 1);
    touch(indev, 240, 70, LV_INDEV_STATE_PRESSED);
    lv_obj_get_coords(ui->notification_viewport, &panel);
    assert(panel.y2 >= 67 && panel.y2 <= 71);
    touch(indev, 240, 280, LV_INDEV_STATE_PRESSED);
    touch(indev, 240, 280, LV_INDEV_STATE_RELEASED);
    advance(350);
    assert(ui->notification_shade_open && ui->notification_progress == 256);
    assert(pxsys_reference_lvgl_covered_top(ui) == ui->notification_shade_height);
#if !PXSYS_REFERENCE_UI_GESTURE_HANDLE
    assert(ui->notification_grip == NULL);
#endif
    check_shade_damage(ui, display);
    check_shade_levels(ui, indev);
    const int grip_y = ui->notification_shade_height - 15;
    touch(indev, 240, grip_y, LV_INDEV_STATE_PRESSED);
    touch(indev, 240, grip_y - 55, LV_INDEV_STATE_PRESSED);
    lv_obj_get_coords(ui->notification_viewport, &panel);
    assert(panel.y2 >= grip_y + 13 - 55 && panel.y2 <= grip_y + 14 - 55);
    touch(indev, 240, grip_y, LV_INDEV_STATE_PRESSED);
    lv_obj_get_coords(ui->notification_viewport, &panel);
    assert(panel.y2 == ui->notification_shade_height - 1);
    touch(indev, 240, grip_y, LV_INDEV_STATE_RELEASED);
    assert(ui->notification_shade_open);
    advance(TRANSIENT_BAR_TIMEOUT_MS + 100);
    assert(ui->notification_shade_open && status_bar_is_visible(ui));
    swipe(indev, 0, 240, 110, 240);
    assert(!ui->notification_shade_open && back_calls == 1);
    assert(pxsys_reference_lvgl_covered_top(ui) == 0);
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

    ui->system_status.volume_supported = 1;
    build_notification_shade(ui, 256);
    pxsys_system_status_snapshot_t volume = ui->system_status;
    volume.volume_percent = (uint8_t)(volume.volume_percent == 65 ? 66 : 65);
    system_status_changed(ui, &volume);
    assert(lv_slider_get_value(ui->notification_volume) == volume.volume_percent);
    assert(!ui->volume_osd_visible);
    close_notification_shade(ui);
    volume.volume_percent = 50;
    system_status_changed(ui, &volume);
    assert(ui->volume_osd_visible && lv_bar_get_value(ui->volume_osd_bar) == 50);
    assert(strcmp(lv_label_get_text(ui->volume_osd_value), "50%") == 0);
    assert(lv_obj_get_style_shadow_width(ui->volume_osd, 0) == 0);
    advance(1900);
    assert(!ui->volume_osd_visible);

    assert(pxsys_reference_lvgl_set_locked(ui, true) == PXSYS_STATUS_OK);
    lv_obj_update_layout(ui->lock_screen);
    lv_area_t clock_card_before, clock_card_after;
    lv_obj_get_coords(ui->lock_clock_card, &clock_card_before);
    pxsys_system_status_snapshot_t locked_status = ui->system_status;
    locked_status.time_valid = locked_status.date_valid = 1;
    locked_status.hour = 14;
    locked_status.minute = 52;
    locked_status.year = 2026;
    locked_status.month = 11;
    locked_status.day = 20;
    system_status_changed(ui, &locked_status);
    assert(strcmp(lv_label_get_text(ui->lock_time), "14:52") == 0);
    assert(strcmp(lv_label_get_text(ui->lock_date), "2026-11-20") == 0);
    lv_obj_update_layout(ui->lock_screen);
    lv_obj_get_coords(ui->lock_clock_card, &clock_card_after);
    assert(memcmp(&clock_card_before, &clock_card_after, sizeof(clock_card_before)) == 0);
    swipe(indev, 240, 460, 240, 430);
    assert(ui->locked);
    swipe(indev, 240, 460, 240, 282);
    assert(!ui->locked && ui->lock_screen == NULL);

    assert(!pxsys_task_manager_finish_all(
        pxsys_standard_system_tasks(system_under_test), PXSYS_STOP_SHUTDOWN));
    check_input_method_settings(ui);
    check_input_method_app_manager(ui);
    assert(!pxsys_reference_lvgl_destroy(ui));
    assert(!pxsys_standard_system_destroy(system_under_test));
    assert(!pxsys_lvgl_renderer_destroy(renderer));
    lv_deinit();
    puts("Fullscreen system gestures: Back, reveal-first status/shade, timeout, cancellation, mixed bar modes, Home, resume and Guest exit passed");
    return 0;
}
