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
static void check_input_method_settings(pxsys_reference_lvgl_t *ui) {
    ui->input_method_list=ime_list; ui->input_method_select=ime_select;
    ui->input_method_preferences=ime_preferences_control;
    assert(open_role(ui,PXSYS_ROLE_SETTINGS)==PXSYS_STATUS_OK);
    input_method_open(ui); assert(!ui->language_dialog && ui->active_page==REFERENCE_PAGE_INPUT_METHOD && ime_lists==1);
    lv_obj_t *list=ui->content;
    lv_obj_t *layout=NULL,*language=NULL;
    for (unsigned i=0;i<lv_obj_get_child_count(list);++i) {
        lv_obj_t *child=lv_obj_get_child(list,i);
        if (lv_obj_has_class(child,&lv_dropdown_class)) {
            if (lv_obj_get_user_data(child)) language=child; else layout=child;
        }
    }
    assert(layout && language);
    lv_obj_update_layout(ui->content);
    lv_dropdown_open(layout); input_method_popup_fit(ui,layout);
    lv_obj_update_layout(lv_dropdown_get_list(layout));
    lv_area_t popup_area,panel_area;
    lv_obj_get_coords(lv_dropdown_get_list(layout),&popup_area);
    lv_obj_get_coords(ui->content,&panel_area);
    assert(popup_area.x1>=panel_area.x1 && popup_area.x2<=panel_area.x2);
    assert(popup_area.y1>=panel_area.y1 && popup_area.y2<=panel_area.y2);
    lv_dropdown_close(layout);

    lv_dropdown_set_selected(layout,PXSYS_INPUT_LAYOUT_NINE); lv_obj_send_event(layout,LV_EVENT_VALUE_CHANGED,NULL);
    lv_dropdown_set_selected(language,PXSYS_INPUT_ENGLISH); lv_obj_send_event(language,LV_EVENT_VALUE_CHANGED,NULL);
    assert(ime_preferences.layout==PXSYS_INPUT_LAYOUT_NINE && ime_preferences.language==PXSYS_INPUT_ENGLISH);
    ime_save_fails=true;
    lv_dropdown_set_selected(layout,PXSYS_INPUT_LAYOUT_FULL); lv_obj_send_event(layout,LV_EVENT_VALUE_CHANGED,NULL);
    assert(ime_preferences.layout==PXSYS_INPUT_LAYOUT_NINE && lv_dropdown_get_selected(layout)==PXSYS_INPUT_LAYOUT_NINE);
    ime_save_fails=false;
    lv_obj_t *label=find_label(list,"Pinyin IME"); assert(label);
    lv_obj_t *row=lv_obj_get_parent(label);
    while (lv_obj_get_parent(row)!=list) row=lv_obj_get_parent(row);
    lv_obj_send_event(row,LV_EVENT_CLICKED,NULL);
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
static const char *const manager_ids[]={"test:enabled-ime","test:disabled-ime"};
static size_t managed_apps(void *context,pxsys_reference_managed_app_t *output,size_t capacity,
    pxsys_reference_app_filter_t filter) {
    (void)context; size_t count=0;
    const unsigned ordinary=PXSYS_REFERENCE_MANAGED_APP_MAX+2;
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
static void check_input_method_app_manager(pxsys_reference_lvgl_t *ui) {
    ui->app_list=managed_apps; ui->app_action=managed_action;
    input_method_open(ui);
    click_label(ui->content,translated(ui,"input-method.manage","Manage installed input methods"));
    assert(ui->active_page==REFERENCE_PAGE_INPUT_METHOD_APPS && ui->managed_app_count==2);
    assert(find_label(ui->content,"Enabled IME") && find_label(ui->content,"Disabled IME"));
    assert(!find_label(ui->content,"Ordinary game"));
    click_label(ui->content,"Disabled IME");
    assert(!strcmp(ui->pending_identity,manager_ids[1]));
    click_label(ui->app_dialog,translated(ui,"settings.apps.enable","Enable"));
    assert(manager_enabled[1] && ui->managed_app_count==2 && !ui->app_dialog);
    click_label(ui->content,"Disabled IME");
    click_label(ui->app_dialog,translated(ui,"settings.apps.disable","Disable"));
    assert(!manager_enabled[1] && find_label(ui->content,"Disabled IME"));
    for (unsigned i=0;i<2;++i) {
        click_label(ui->content,i?"Disabled IME":"Enabled IME");
        click_label(ui->app_dialog,translated(ui,"settings.apps.uninstall","Uninstall"));
        assert(ui->confirm_dialog);
        click_label(ui->confirm_dialog,translated(ui,"settings.confirm","Confirm"));
        assert(!manager_present[i] && ui->managed_app_count==1-i);
    }
    assert(find_label(ui->content,translated(ui,"input-method.apps.empty","No installed input method apps")));
    navigation_back(ui); assert(ui->active_page==REFERENCE_PAGE_INPUT_METHOD);
    assert(!open_role(ui,PXSYS_ROLE_APP_MANAGER));
    assert(ui->managed_app_count==PXSYS_REFERENCE_MANAGED_APP_MAX);
    assert(find_label(ui->content,"Ordinary game"));
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
    check_input_method_settings(ui);
    check_input_method_app_manager(ui);
    assert(!pxsys_reference_lvgl_destroy(ui));
    assert(!pxsys_standard_system_destroy(system_under_test));
    assert(!pxsys_lvgl_renderer_destroy(renderer));
    lv_deinit();
    puts("Fullscreen system gestures: Back, reveal-first status/shade, timeout, cancellation, mixed bar modes, Home, resume and Guest exit passed");
    return 0;
}
