/* Real LVGL trees and task callbacks: bookmarks must outlive only the tree. */
#undef NDEBUG
#include "../lvgl/src/reference_lvgl.c"
#include <assert.h>

#define TEST_ITEMS 18u

typedef struct {
    pxsys_standard_system_t* system;
    pxsys_reference_lvgl_t* ui;
    lv_indev_t* device;
    lv_group_t* group;
    unsigned order[TEST_ITEMS];
    size_t app_count;
} fixture_t;

static void* allocate(void* context, size_t bytes) {
    (void)context;
    return malloc(bytes);
}
static void release(void* context, void* memory) {
    (void)context;
    free(memory);
}
static void flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels) {
    (void)area; (void)pixels;
    lv_display_flush_ready(display);
}
static void read_device(lv_indev_t* device, lv_indev_data_t* data) {
    (void)device;
    data->state = LV_INDEV_STATE_RELEASED;
}
static void settle(pxsys_reference_lvgl_t* ui) {
    lv_tick_inc(400);
    (void)lv_timer_handler();
    if (ui->content != NULL) lv_obj_update_layout(ui->content);
}
static size_t apps(void* context, pxsys_reference_managed_app_t* output,
                   size_t capacity, pxsys_reference_app_filter_t filter) {
    fixture_t* f = context;
    (void)filter;
    if (output == NULL) return f->app_count;
    size_t count = f->app_count < capacity ? f->app_count : capacity;
    for (size_t i = 0; i < count; ++i) {
        memset(&output[i], 0, sizeof(output[i]));
        snprintf(output[i].identity, sizeof(output[i].identity), "test:app-%02u", f->order[i]);
        /* Deliberately duplicate labels: focus must follow identity. */
        snprintf(output[i].name, sizeof(output[i].name), "Shared app title");
        snprintf(output[i].version, sizeof(output[i].version), "1.0");
        output[i].installed = output[i].enabled = 1;
    }
    return count;
}
static bool app_action(void* context, const char* identity,
                       pxsys_reference_app_action_t action) {
    (void)context; (void)identity; (void)action;
    return true;
}
static size_t files(void* context, const char* path,
                    pxsys_reference_file_entry_t* output, size_t capacity) {
    (void)context;
    size_t count = capacity < TEST_ITEMS ? capacity : TEST_ITEMS;
    for (size_t i = 0; i < count; ++i) {
        memset(&output[i], 0, sizeof(output[i]));
        snprintf(output[i].name, sizeof(output[i].name), "item-%02u", (unsigned)i);
        snprintf(output[i].path, sizeof(output[i].path), "%s/item-%02u", path, (unsigned)i);
        output[i].is_directory = 1;
    }
    return count;
}
static bool file_action(void* context, const char* path, const char* destination,
                        pxsys_reference_file_action_t action) {
    (void)context; (void)path; (void)destination; (void)action;
    return true;
}
static void device_info(void* context,
    char values[PXSYS_REFERENCE_DEVICE_FIELD_COUNT][PXSYS_REFERENCE_DEVICE_VALUE_MAX]) {
    (void)context;
    for (size_t i = 0; i < PXSYS_REFERENCE_DEVICE_FIELD_COUNT; ++i)
        snprintf(values[i], PXSYS_REFERENCE_DEVICE_VALUE_MAX, "Navigation test");
}
static void methods(void* context, pxsys_reference_input_method_emit_fn emit, void* output) {
    (void)context;
    emit(output, "", pxsys_string_from_cstr("System keyboard"), true);
}
static size_t wifi_scan(void* context, pxsys_reference_wifi_network_t* output, size_t capacity) {
    (void)context; (void)output; (void)capacity;
    return 0;
}
static bool wifi_connect(void* context, const char* ssid, const char* password) {
    (void)context; (void)ssid; (void)password;
    return true;
}
/* Locate controls by their real action, independently of display language. */
static lv_obj_t* control(lv_obj_t* root, lv_event_cb_t callback, const char* identity) {
    if (root == NULL) return NULL;
    for (uint32_t i = 0; i < lv_obj_get_event_count(root); ++i) {
        lv_event_dsc_t* event = lv_obj_get_event_dsc(root, i);
        if (lv_event_dsc_get_cb(event) != callback) continue;
        if (callback == settings_action_clicked && identity != NULL) {
            settings_action_t* action = lv_event_dsc_get_user_data(event);
            if (strcmp(action->role, identity) != 0) continue;
        }
        return root;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        lv_obj_t* found = control(lv_obj_get_child(root, i), callback, identity);
        if (found != NULL) return found;
    }
    return NULL;
}
static lv_obj_t* app_row(pxsys_reference_lvgl_t* ui, lv_obj_t* root, const char* identity) {
    if (root == NULL) return NULL;
    if (control(root, app_row_clicked, NULL) == root) {
        size_t index = (size_t)(uintptr_t)lv_obj_get_user_data(root);
        if (index < ui->managed_app_count && !strcmp(ui->managed_apps[index].identity, identity))
            return root;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        lv_obj_t* found = app_row(ui, lv_obj_get_child(root, i), identity);
        if (found != NULL) return found;
    }
    return NULL;
}
static void click(lv_obj_t* object) {
    assert(object != NULL);
    lv_obj_send_event(object, LV_EVENT_CLICKED, NULL);
}
static lv_obj_t* header_back(pxsys_reference_lvgl_t* ui) {
    lv_obj_t* button = control(ui->content, back_clicked, NULL);
    assert(button != NULL && lv_obj_is_clickable(button));
    assert(lv_obj_get_parent(button) == lv_obj_get_parent(ui->title));
    assert(lv_obj_get_child_count(button) == 1);
    assert(!strcmp(lv_label_get_text(lv_obj_get_child(button, 0)), LV_SYMBOL_LEFT));
    return button;
}
static int32_t scroll_to(fixture_t* f, lv_obj_t* focused, int32_t y) {
    if (f->group != NULL && focused != NULL) {
        lv_group_add_obj(f->group, focused);
        lv_group_focus_obj(focused);
    }
    settle(f->ui);
    lv_obj_scroll_to_y(f->ui->content, y, LV_ANIM_OFF);
    return lv_obj_get_scroll_y(f->ui->content);
}
static void create(fixture_t* f, lv_display_t* display, bool animations, lv_indev_type_t type) {
    memset(f, 0, sizeof(*f));
    f->app_count = TEST_ITEMS;
    for (unsigned i = 0; i < TEST_ITEMS; ++i) f->order[i] = i;
    lv_group_set_default(NULL);
    f->device = lv_indev_create();
    lv_indev_set_display(f->device, display);
    lv_indev_set_type(f->device, type);
    lv_indev_set_read_cb(f->device, read_device);
    if (type != LV_INDEV_TYPE_POINTER) {
        f->group = lv_group_create();
        lv_indev_set_group(f->device, f->group);
    }
    pxsys_standard_system_config_t system_config;
    pxsys_standard_system_config_init(&system_config);
    system_config.allocator.allocate = allocate;
    system_config.allocator.release = release;
    pxsys_display_profile_init(&system_config.initial_display,
        lv_display_get_horizontal_resolution(display), lv_display_get_vertical_resolution(display));
    system_config.initial_system_status.wifi_supported = 1;
    system_config.initial_system_status.wifi_enabled = 1;
    assert(pxsys_standard_system_create(&system_config, &f->system) == PXSYS_STATUS_OK);
    pxsys_reference_lvgl_config_t config;
    pxsys_reference_lvgl_config_init(&config);
    config.system = f->system;
    config.allocator = system_config.allocator;
    config.parent = lv_display_get_layer_top(display);
    config.app_manager_context = f;
    config.app_list = apps; config.app_action = app_action;
    config.file_list = files; config.file_action = file_action;
    config.device_info = device_info; config.input_method_list = methods;
    config.wifi_scan = wifi_scan; config.wifi_connect = wifi_connect;
    assert(pxsys_reference_lvgl_create(&config, &f->ui) == PXSYS_STATUS_OK);
    assert(pxsys_reference_lvgl_set_animations_enabled(f->ui, animations) == PXSYS_STATUS_OK);
    assert(pxsys_reference_lvgl_start(f->ui) == PXSYS_STATUS_OK);
    assert(f->ui->active_page == REFERENCE_PAGE_HOME && f->ui->title == NULL);
    assert(control(f->ui->content, back_clicked, NULL) == NULL);
    assert(open_role(f->ui, PXSYS_ROLE_SETTINGS) == PXSYS_STATUS_OK);
    assert(control(f->ui->content, back_clicked, NULL) == NULL);
}
static void destroy(fixture_t* f) {
    lv_indev_delete(f->device);
    if (f->group != NULL) lv_group_delete(f->group);
    assert(pxsys_task_manager_finish_all(pxsys_standard_system_tasks(f->system),
        PXSYS_STOP_SHUTDOWN) == PXSYS_STATUS_OK);
    assert(pxsys_reference_lvgl_destroy(f->ui) == PXSYS_STATUS_OK);
    assert(pxsys_standard_system_destroy(f->system) == PXSYS_STATUS_OK);
}
static void check_roles(fixture_t* f) {
    static const char* const roles[] = {PXSYS_ROLE_SOUND_SETTINGS, PXSYS_ROLE_DEVICE_INFO,
        PXSYS_ROLE_APP_MANAGER, PXSYS_ROLE_FILE_MANAGER};
    static const reference_page_t pages[] = {REFERENCE_PAGE_SOUND_SETTINGS,
        REFERENCE_PAGE_DEVICE_INFO, REFERENCE_PAGE_APP_MANAGER, REFERENCE_PAGE_FILE_MANAGER};
    pxsys_reference_lvgl_t* ui = f->ui;
    for (size_t i = 0; i < sizeof(roles) / sizeof(roles[0]); ++i) {
        lv_obj_t* row = control(ui->content, settings_action_clicked, roles[i]);
        assert(row != NULL);
        uint32_t key = navigation_object_key(ui->content, row);
        int32_t saved_y = scroll_to(f, row, 100 + (int32_t)i * 11);
        assert(saved_y > 0);
        size_t tasks = pxsys_task_manager_count(pxsys_standard_system_tasks(f->system));
        click(row); /* Includes background -> empty chrome -> foreground rebuilds. */
        assert(ui->active_page == pages[i] && ui->content_active);
        assert(pxsys_task_manager_count(pxsys_standard_system_tasks(f->system)) == tasks + 1);
        if (pages[i] == REFERENCE_PAGE_FILE_MANAGER) {
            lv_obj_t* plus = control(ui->content, file_new_folder_clicked, NULL);
            assert(plus != NULL && lv_obj_get_parent(plus) == lv_obj_get_parent(ui->title));
        }
        click(header_back(ui));
        settle(ui);
        assert(ui->active_page == REFERENCE_PAGE_SETTINGS);
        assert(pxsys_task_manager_count(pxsys_standard_system_tasks(f->system)) == tasks);
        assert(lv_obj_get_scroll_y(ui->content) == saved_y);
        row = control(ui->content, settings_action_clicked, roles[i]);
        assert(navigation_object_key(ui->content, row) == key);
        if (f->group != NULL) assert(lv_group_get_focused(f->group) == row);
        else assert(navigation_keypad_group(ui) == NULL && lv_obj_get_group(row) == NULL);
        /* Chrome-only passes and subsequent ordinary rebuilds cannot overwrite the bookmark. */
        assert(app_background(NULL, &ui->contexts[REFERENCE_PAGE_SETTINGS]) == PXSYS_STATUS_OK);
        assert(ui->content == NULL && ui->rendered_view_key == 0);
        rebuild(ui); rebuild(ui);
        assert(app_foreground(NULL, &ui->contexts[REFERENCE_PAGE_SETTINGS]) == PXSYS_STATUS_OK);
        rebuild(ui);
        assert(lv_obj_get_scroll_y(ui->content) == saved_y);
        if (f->group != NULL)
            assert(lv_group_get_focused(f->group) == control(ui->content, settings_action_clicked, roles[i]));
    }
}
static void check_subpages(fixture_t* f) {
    pxsys_reference_lvgl_t* ui = f->ui;
    const lv_event_cb_t callbacks[] = {theme_clicked, input_method_clicked, wifi_settings_clicked};
    const reference_page_t pages[] = {REFERENCE_PAGE_APPEARANCE, REFERENCE_PAGE_INPUT_METHOD, REFERENCE_PAGE_WIFI};
    for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); ++i) {
        int32_t saved_y = scroll_to(f, control(ui->content, callbacks[i], NULL), 91);
        click(control(ui->content, callbacks[i], NULL));
        assert(ui->active_page == pages[i]);
        (void)header_back(ui);
        if (pages[i] == REFERENCE_PAGE_INPUT_METHOD) {
            click(control(ui->content, input_method_manage_clicked, NULL));
            assert(ui->active_page == REFERENCE_PAGE_INPUT_METHOD_APPS);
            click(header_back(ui));
            assert(ui->active_page == REFERENCE_PAGE_INPUT_METHOD);
        } else if (pages[i] == REFERENCE_PAGE_WIFI) {
            uint32_t list_key = ui->rendered_view_key;
            click(control(ui->content, wifi_add_clicked, NULL));
            assert(ui->wifi_input_stage == 1 && ui->rendered_view_key != list_key);
            click(header_back(ui));
            assert(ui->wifi_input_stage == 0 && ui->rendered_view_key == list_key);
        }
        click(header_back(ui));
        assert(ui->active_page == REFERENCE_PAGE_SETTINGS);
        assert(lv_obj_get_scroll_y(ui->content) == saved_y);
        if (f->group != NULL) assert(lv_group_get_focused(f->group) == control(ui->content, callbacks[i], NULL));
    }
}
static void check_files_and_cache(fixture_t* f) {
    pxsys_reference_lvgl_t* ui = f->ui;
    assert(open_role(ui, PXSYS_ROLE_FILE_MANAGER) == PXSYS_STATUS_OK);
    lv_obj_t* directory = control(ui->content, file_entry_clicked, NULL);
    int32_t parent_y = scroll_to(f, directory, 143);
    uint32_t parent_key = ui->rendered_view_key;
    assert(parent_y > 0);
    lv_obj_send_event(directory, LV_EVENT_SHORT_CLICKED, NULL);
    assert(!strcmp(ui->file_path, "/item-00") && ui->rendered_view_key != parent_key);
    assert(lv_obj_get_scroll_y(ui->content) == 0);
    int32_t child_y = scroll_to(f, control(ui->content, file_entry_clicked, NULL), 67);
    click(header_back(ui));
    assert(!ui->file_path[0] && ui->rendered_view_key == parent_key);
    assert(lv_obj_get_scroll_y(ui->content) == parent_y);
    lv_obj_send_event(control(ui->content, file_entry_clicked, NULL), LV_EVENT_SHORT_CLICKED, NULL);
    assert(lv_obj_get_scroll_y(ui->content) == child_y);
    uint32_t child_key = ui->rendered_view_key;
    click(control(ui->content, file_new_folder_clicked, NULL));
    assert(ui->file_edit_mode != 0 && ui->rendered_view_key != child_key);
    click(header_back(ui));
    assert(ui->file_edit_mode == 0 && lv_obj_get_scroll_y(ui->content) == child_y);
    /* A bounded scalar cache is independent of the number of visited folders. */
    assert(REFERENCE_NAVIGATION_STATE_COUNT == 24);
    assert(sizeof(navigation_state_t) == 12 && sizeof(ui->navigation_states) == 288);
    for (unsigned i = 0; i < REFERENCE_NAVIGATION_STATE_COUNT + 8u; ++i) {
        snprintf(ui->file_path, sizeof(ui->file_path), "/cache-%02u", i);
        ui->file_entries_loaded = 0;
        rebuild(ui);
        (void)scroll_to(f, NULL, 20 + (int32_t)i);
    }
    navigation_state_capture(ui);
    assert(navigation_state_find(ui, parent_key) == NULL);
    for (size_t i = 0; i < REFERENCE_NAVIGATION_STATE_COUNT; ++i) {
        assert(ui->navigation_states[i].view_key != 0);
        for (size_t j = i + 1; j < REFERENCE_NAVIGATION_STATE_COUNT; ++j)
            assert(ui->navigation_states[i].view_key != ui->navigation_states[j].view_key);
    }
    ui->file_path[0] = '\0'; ui->file_entries_loaded = 0; rebuild(ui);
    assert(lv_obj_get_scroll_y(ui->content) == 0);
    click(header_back(ui));
    assert(ui->active_page == REFERENCE_PAGE_SETTINGS);
}
static void check_dynamic_apps(fixture_t* f) {
    pxsys_reference_lvgl_t* ui = f->ui;
    assert(open_role(ui, PXSYS_ROLE_APP_MANAGER) == PXSYS_STATUS_OK);
    lv_obj_t* row = app_row(ui, ui->content, "test:app-08");
    assert(row != NULL);
    int32_t saved_y = scroll_to(f, row, 137);
    uint32_t key = navigation_object_key(ui->content, row);
    unsigned moved = f->order[8];
    memmove(&f->order[1], &f->order[0], 8 * sizeof(f->order[0]));
    f->order[0] = moved;
    pxsys_reference_lvgl_refresh_apps(ui);
    assert(lv_obj_get_scroll_y(ui->content) == saved_y);
    row = app_row(ui, ui->content, "test:app-08");
    assert(row != NULL && navigation_object_key(ui->content, row) == key);
    if (f->group != NULL) assert(lv_group_get_focused(f->group) == row);
    /* Remove the focused identity and shrink the list below the saved viewport. */
    memmove(&f->order[0], &f->order[1], (TEST_ITEMS - 1) * sizeof(f->order[0]));
    f->app_count = 1;
    pxsys_reference_lvgl_refresh_apps(ui);
    settle(ui);
    assert(app_row(ui, ui->content, "test:app-08") == NULL);
    assert(navigation_find_focus(ui->content, ui->content, key) == NULL);
    const int32_t scroll_y = lv_obj_get_scroll_y(ui->content);
    /* LVGL reports a negative bottom distance when all content fits. */
    const int32_t max_scroll_y = LV_MAX(0, scroll_y + lv_obj_get_scroll_bottom(ui->content));
    assert(scroll_y >= 0 && scroll_y == LV_MIN(saved_y, max_scroll_y));
    if (f->group != NULL) {
        lv_obj_t* focused = lv_group_get_focused(f->group);
        assert(focused == NULL || (lv_obj_is_valid(focused) && object_is_within(focused, ui->content)));
    }
    click(header_back(ui));
}
int main(void) {
    static uint32_t pixels[480 * 16];
    lv_init();
    lv_display_t* display = lv_display_create(320, 240);
    lv_display_set_buffers(display, pixels, NULL, sizeof(pixels), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    const int sizes[][2] = {{320, 240}, {240, 320}, {480, 320}};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        lv_display_set_resolution(display, sizes[i][0], sizes[i][1]);
        for (unsigned mode = 0; mode < 3; ++mode) {
            fixture_t f;
            create(&f, display, mode == 1, mode == 0 ? LV_INDEV_TYPE_KEYPAD :
                mode == 1 ? LV_INDEV_TYPE_ENCODER : LV_INDEV_TYPE_POINTER);
            check_roles(&f);
            check_subpages(&f);
            check_files_and_cache(&f);
            check_dynamic_apps(&f);
            destroy(&f);
        }
    }
    lv_deinit();
    puts("Settings navigation: child back, task lifecycle, scroll, controller focus, folders and bounded cache passed");
    return 0;
}
