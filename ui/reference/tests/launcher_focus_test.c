/* Exercise real LVGL group focus and launcher paging together. */
#undef NDEBUG
#include "../lvgl/src/reference_lvgl.c"
#include <assert.h>

static void* allocate(void* context, size_t bytes) {
    (void)context;
    return malloc(bytes);
}

static void release(void* context, void* memory) {
    (void)context;
    free(memory);
}

static void flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels) {
    (void)area;
    (void)pixels;
    lv_display_flush_ready(display);
}

typedef struct { size_t live, acquired, released; } icon_counts_t;
typedef struct {
    lv_image_dsc_t image;
    icon_counts_t* counts;
    uint32_t pixels[4];
} owned_icon_t;

static void release_icon(void* context) {
    owned_icon_t* icon = context;
    assert(icon->counts->live > 0);
    --icon->counts->live;
    ++icon->counts->released;
    lv_image_cache_drop(&icon->image);
    free(icon);
}

static bool resolve_icon(void* context, const pxsys_app_descriptor_t* app,
                         pxsys_reference_lvgl_app_icon_t* output) {
    (void)app;
    owned_icon_t* icon = calloc(1, sizeof(*icon));
    assert(icon != NULL);
    icon->counts = context;
    ++icon->counts->live;
    ++icon->counts->acquired;
    icon->image.header.magic = LV_IMAGE_HEADER_MAGIC;
    icon->image.header.cf = LV_COLOR_FORMAT_ARGB8888;
    icon->image.header.w = icon->image.header.h = 2;
    icon->image.header.stride = 2 * sizeof(uint32_t);
    icon->image.data_size = sizeof(icon->pixels);
    icon->image.data = (const uint8_t*)icon->pixels;
    *output = (pxsys_reference_lvgl_app_icon_t){
        &icon->image, release_icon, icon};
    return true;
}

static void focus_tile(pxsys_reference_lvgl_t* ui, lv_obj_t* tile,
                       uint16_t page) {
    lv_group_focus_obj(tile);
    /* The board adapter makes this call after changing group focus. */
    lv_obj_scroll_to_view_recursive(tile, LV_ANIM_OFF);
    lv_tick_inc(500);
    (void)lv_timer_handler();
    lv_obj_update_layout(ui->content);
    assert(ui->launcher_page == page);
    assert(lv_obj_get_scroll_x(ui->content) ==
           (int32_t)page * lv_obj_get_width(ui->content));
    lv_area_t viewport, bounds;
    lv_obj_get_coords(ui->content, &viewport);
    lv_obj_get_coords(tile, &bounds);
    assert(bounds.x1 >= viewport.x1 && bounds.x2 <= viewport.x2);
    assert(bounds.y1 >= viewport.y1 && bounds.y2 <= viewport.y2);
}

/* Match the board adapter: rebuilding its ordered group must suppress LVGL's
 * first-item autofocus while the restored target is still alive. */
static void collect_launcher(pxsys_reference_lvgl_t* ui, lv_group_t* group) {
    lv_obj_t* previous = lv_group_get_focused(group);
    lv_group_remove_all_objs(group);
    if (previous != NULL) lv_group_focus_freeze(group, true);
    for (size_t i = 0; i < ui->launcher_count; ++i)
        lv_group_add_obj(group, lv_obj_get_child(ui->content, (int32_t)i));
    if (previous != NULL) {
        lv_group_focus_freeze(group, false);
        lv_group_focus_obj(previous);
    }
}

static void check_paging(lv_display_t* display, bool animations) {
    const size_t per_page = PXSYS_REFERENCE_UI_LAUNCHER_COLUMNS *
                            PXSYS_REFERENCE_UI_LAUNCHER_ROWS;
    const size_t count = per_page * 2u + 1u;
    pxsys_standard_system_config_t system_config;
    pxsys_standard_system_config_init(&system_config);
    system_config.allocator.context = NULL;
    system_config.allocator.allocate = allocate;
    system_config.allocator.release = release;
    system_config.max_apps = count + REFERENCE_APP_COUNT;
    pxsys_display_profile_init(&system_config.initial_display,
        lv_display_get_horizontal_resolution(display),
        lv_display_get_vertical_resolution(display));
    pxsys_standard_system_t* system = NULL;
    assert(pxsys_standard_system_create(&system_config, &system) == PXSYS_STATUS_OK);
    for (size_t i = 0; i < count; ++i) {
        char id[32];
        snprintf(id, sizeof(id), "focus-test-%u", (unsigned)i);
        pxsys_app_descriptor_t app = {.struct_size = sizeof(app)};
        app.identity.app_id = pxsys_string_from_cstr(id);
        app.display_name = app.identity.app_id;
        app.version = pxsys_string_from_cstr("1.0.0");
        app.runtime_id = pxsys_string_from_cstr(PXSYS_NATIVE_RUNTIME_ID);
        app.flags = PXSYS_APP_FLAG_ENABLED | PXSYS_APP_FLAG_LAUNCHER;
        assert(pxsys_app_registry_register(pxsys_standard_system_apps(system),
                                           &app) == PXSYS_STATUS_OK);
    }
    pxsys_reference_lvgl_config_t config;
    pxsys_reference_lvgl_config_init(&config);
    config.system = system;
    config.allocator = system_config.allocator;
    config.parent = lv_display_get_layer_top(display);
    config.features = PXSYS_REFERENCE_UI_HOME | PXSYS_REFERENCE_UI_STATUS_BAR |
                      PXSYS_REFERENCE_UI_NAVIGATION_BAR;
    config.max_launcher_apps = count;
    icon_counts_t icons = {0};
    config.app_icon_context = &icons;
    config.resolve_app_icon = resolve_icon;
    pxsys_reference_lvgl_t* ui = NULL;
    assert(pxsys_reference_lvgl_create(&config, &ui) == PXSYS_STATUS_OK);
    assert(pxsys_reference_lvgl_set_animations_enabled(ui, animations) == PXSYS_STATUS_OK);
    assert(pxsys_reference_lvgl_start(ui) == PXSYS_STATUS_OK);
    assert(ui->launcher_count == count);
    assert(icons.live == count);
    /* Starting the chrome must not reload all icons for intermediate trees. */
    assert(icons.acquired == count);
    lv_group_t* group = lv_group_create();
    lv_indev_t* keypad = lv_indev_create();
    lv_indev_set_type(keypad, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_display(keypad, display);
    lv_indev_set_group(keypad, group);
    collect_launcher(ui, group);
    /* Forward/backward, both edge columns, and a last page with one icon. */
    const size_t indices[] = {per_page - 1u, per_page, per_page * 2u,
                             per_page * 2u - 1u, 0u, count - 1u, 0u};
    for (size_t i = 0; i < sizeof(indices) / sizeof(indices[0]); ++i)
        focus_tile(ui, lv_obj_get_child(ui->content, (int32_t)indices[i]),
                   (uint16_t)(indices[i] / per_page));
    /* Reordered icons use their current slot, not their registration order. */
    lv_obj_t* moved = lv_obj_get_child(ui->content, 0);
    lv_obj_move_to_index(moved, (int32_t)per_page);
    launcher_position_tiles(ui);
    focus_tile(ui, lv_obj_get_child(ui->content, 1), 0);
    focus_tile(ui, moved, 1);
    /* Reordering/rebuilding follows the signed application identity, even
     * when the same tile's new slot belongs to another page. */
    const uint32_t moved_key = navigation_object_key(ui->content, moved);
    rebuild(ui);
    assert(navigation_object_key(ui->content, lv_group_get_focused(group)) == moved_key);
    assert(ui->launcher_page == 0);
    collect_launcher(ui, group);
    focus_tile(ui, lv_obj_get_child(ui->content, (int32_t)(per_page + 1u)), 1);
    const uint32_t selected_key = navigation_object_key(ui->content, lv_group_get_focused(group));
    /* Leaving home must retire icons immediately, including when another
     * app owns the screen. Repeated background/rebuild must not double free. */
    app_context_t home = {ui, REFERENCE_PAGE_HOME};
    for (unsigned round = 0; round < 3; ++round) {
        assert(app_background(NULL, &home) == PXSYS_STATUS_OK);
        assert(icons.live == 0);
        assert(icons.acquired == icons.released);
        rebuild(ui);
        assert(icons.live == 0);
        assert(app_foreground(NULL, &home) == PXSYS_STATUS_OK);
        assert(ui->launcher_count == count && icons.live == count);
        lv_tick_inc(500);
        (void)lv_timer_handler();
        assert(ui->launcher_page == 1);
        assert(navigation_object_key(ui->content, lv_group_get_focused(group)) == selected_key);
        /* GameKit consumes a held Back/Home key after foreground() and clears
         * its group until release. The next scope query precedes collection. */
        lv_group_remove_all_objs(group);
        assert(lv_group_get_obj_count(group) == 0);
        assert(pxsys_reference_lvgl_key_navigation_scope(ui) == NULL);
        assert(navigation_object_key(ui->content, lv_group_get_focused(group)) == selected_key);
        collect_launcher(ui, group);
        assert(navigation_object_key(ui->content, lv_group_get_focused(group)) == selected_key);
    }
    /* Desktop selection survives eviction of the bounded page/folder cache. */
    memset(ui->navigation_states, 0, sizeof(ui->navigation_states));
    lv_group_remove_all_objs(group);
    (void)pxsys_reference_lvgl_key_navigation_scope(ui);
    assert(navigation_object_key(ui->content, lv_group_get_focused(group)) == selected_key);
    /* Removing that application falls back to a remaining tile on the same
     * page. The removed identity must never remain as a dangling LVGL object. */
    pxsys_app_identity_t removed = ui->launcher_items[per_page + 1u].identity;
    assert(pxsys_app_registry_unregister(pxsys_standard_system_apps(system), &removed) == PXSYS_STATUS_OK);
    rebuild(ui);
    assert(ui->launcher_count == count - 1u && ui->launcher_page == 1);
    assert(lv_group_get_focused(group) == lv_obj_get_child(ui->content, (int32_t)per_page));
    lv_indev_delete(keypad);
    lv_group_delete(group);
    assert(pxsys_task_manager_finish_all(pxsys_standard_system_tasks(system),
        PXSYS_STOP_SHUTDOWN) == PXSYS_STATUS_OK);
    assert(pxsys_reference_lvgl_destroy(ui) == PXSYS_STATUS_OK);
    assert(icons.live == 0 && icons.acquired == icons.released);
    assert(pxsys_standard_system_destroy(system) == PXSYS_STATUS_OK);
}

static lv_obj_t* recent_control(lv_obj_t* object) {
    for (uint32_t i = 0; i < lv_obj_get_event_count(object); ++i)
        if (lv_event_dsc_get_cb(lv_obj_get_event_dsc(object, i)) == recent_key)
            return object;
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i) {
        lv_obj_t* found = recent_control(lv_obj_get_child(object, (int32_t)i));
        if (found != NULL) return found;
    }
    return NULL;
}

static void check_recent_delete(lv_display_t* display) {
    pxsys_standard_system_config_t system_config;
    pxsys_standard_system_config_init(&system_config);
    system_config.allocator.allocate = allocate;
    system_config.allocator.release = release;
    pxsys_display_profile_init(&system_config.initial_display, 320, 240);
    pxsys_standard_system_t* system = NULL;
    assert(pxsys_standard_system_create(&system_config, &system) == PXSYS_STATUS_OK);
    pxsys_reference_lvgl_config_t config;
    pxsys_reference_lvgl_config_init(&config);
    config.system = system;
    config.allocator = system_config.allocator;
    config.parent = lv_display_get_layer_top(display);
    config.features = PXSYS_REFERENCE_UI_HOME | PXSYS_REFERENCE_UI_SETTINGS;
    pxsys_reference_lvgl_t* ui = NULL;
    assert(pxsys_reference_lvgl_create(&config, &ui) == PXSYS_STATUS_OK);
    assert(pxsys_reference_lvgl_start(ui) == PXSYS_STATUS_OK);
    assert(open_role(ui, PXSYS_ROLE_SETTINGS) == PXSYS_STATUS_OK);
    assert(pxsys_reference_lvgl_home(ui) == PXSYS_STATUS_OK);
    assert(pxsys_reference_lvgl_show_recents(ui) == PXSYS_STATUS_OK);
    lv_obj_t* control = recent_control(ui->task_switcher);
    assert(control != NULL);
    lv_group_t* group = lv_group_create();
    lv_group_add_obj(group, control);
    lv_group_focus_obj(control);
    pxsys_task_manager_t* tasks = pxsys_standard_system_tasks(system);
    const size_t before = pxsys_task_manager_count(tasks);
    uint32_t key = LV_KEY_DEL;
    lv_obj_send_event(control, LV_EVENT_KEY, &key);
    lv_tick_inc(20);
    (void)lv_timer_handler();
    assert(pxsys_task_manager_count(tasks) + 1u == before);
    assert(ui->recent_history_count == 0);
    lv_group_delete(group);
    assert(pxsys_task_manager_finish_all(tasks, PXSYS_STOP_SHUTDOWN) == PXSYS_STATUS_OK);
    assert(pxsys_reference_lvgl_destroy(ui) == PXSYS_STATUS_OK);
    assert(pxsys_standard_system_destroy(system) == PXSYS_STATUS_OK);
}

#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && LV_USE_SNAPSHOT
static size_t preview_color_count(const lv_draw_buf_t* image, uint32_t color) {
    size_t count = 0;
    uint16_t pixel = lv_color_to_u16(lv_color_hex(color));
    for (uint32_t y = 0; y < image->header.h; ++y) {
        const uint16_t* row = (const uint16_t*)(image->data + y * image->header.stride);
        for (uint32_t x = 0; x < image->header.w; ++x)
            if (row[x] == pixel) ++count;
    }
    return count;
}

static void check_hardware_navigation_previews(lv_display_t* display) {
    pxsys_standard_system_config_t system_config;
    pxsys_standard_system_config_init(&system_config);
    system_config.allocator.allocate = allocate;
    system_config.allocator.release = release;
    pxsys_display_profile_init(&system_config.initial_display, 320, 240);
    pxsys_standard_system_t* system = NULL;
    assert(pxsys_standard_system_create(&system_config, &system) == PXSYS_STATUS_OK);
    pxsys_reference_lvgl_config_t config;
    pxsys_reference_lvgl_config_init(&config);
    config.system = system;
    config.allocator = system_config.allocator;
    config.parent = lv_display_get_layer_top(display);
    config.features = PXSYS_REFERENCE_UI_HOME | PXSYS_REFERENCE_UI_SETTINGS;
    pxsys_reference_lvgl_t* ui = NULL;
    assert(pxsys_reference_lvgl_create(&config, &ui) == PXSYS_STATUS_OK);
    assert(pxsys_reference_lvgl_set_animations_enabled(ui, false) == PXSYS_STATUS_OK);
    assert(pxsys_reference_lvgl_start(ui) == PXSYS_STATUS_OK);
    assert(open_role(ui, PXSYS_ROLE_SETTINGS) == PXSYS_STATUS_OK);
    pxsys_instance_ref_t current;
    pxsys_instance_snapshot_t instance = {.struct_size = sizeof(instance)};
    pxsys_task_manager_t* tasks = pxsys_standard_system_tasks(system);
    assert(pxsys_task_manager_task(tasks, 0, &current, &instance) == PXSYS_STATUS_OK);
    lv_obj_t* swatch = lv_obj_create(application_root(ui));
    lv_obj_remove_style_all(swatch);
    lv_obj_set_pos(swatch, 20, 20);
    lv_obj_set_size(swatch, 80, 40);
    lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(swatch, lv_color_hex(0xff0000), 0);
    assert(find_task_preview(ui, current) == NULL);
    /* The public entry is used by physical MENU/START and desktop keys. */
    assert(pxsys_reference_lvgl_show_recents(ui) == PXSYS_STATUS_OK);
    task_preview_t* preview = find_task_preview(ui, current);
    assert(preview != NULL && preview->image != NULL);
    assert(preview_color_count(preview->image, 0xff0000) >= 3000);
    assert(task_switcher_is_open(ui));
    /* Closing and reopening refreshes content instead of reusing a stale image. */
    assert(pxsys_reference_lvgl_show_recents(ui) == PXSYS_STATUS_OK);
    assert(!task_switcher_is_open(ui));
    lv_obj_set_style_bg_color(swatch, lv_color_hex(0x0000ff), 0);
    assert(pxsys_reference_lvgl_show_recents(ui) == PXSYS_STATUS_OK);
    preview = find_task_preview(ui, current);
    assert(preview != NULL && preview_color_count(preview->image, 0x0000ff) >= 3000);
    assert(pxsys_reference_lvgl_show_recents(ui) == PXSYS_STATUS_OK);
    /* Physical Home must retain the leaving app before the launcher takes over. */
    lv_obj_set_style_bg_color(swatch, lv_color_hex(0x00ff00), 0);
    assert(pxsys_reference_lvgl_home(ui) == PXSYS_STATUS_OK);
    preview = find_task_preview(ui, current);
    assert(preview != NULL && preview_color_count(preview->image, 0x00ff00) >= 3000);
    assert(pxsys_reference_lvgl_show_recents(ui) == PXSYS_STATUS_OK);
    preview = find_task_preview(ui, current);
    assert(preview != NULL && preview_color_count(preview->image, 0x00ff00) >= 3000);
    /* A physical keypad has no pointer coordinates. ENTER must activate the
     * selected preview even though (0,0) is outside its letterboxed image. */
    lv_indev_t* keypad = lv_indev_create();
    lv_indev_set_type(keypad, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_display(keypad, display);
    lv_obj_t* card = recent_control(ui->task_switcher);
    assert(card != NULL);
    lv_indev_t* pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(pointer, display);
    lv_obj_send_event(card, LV_EVENT_CLICKED, pointer);
    pxsys_instance_ref_t active;
    assert(pxsys_task_manager_current(tasks, &active) == PXSYS_STATUS_OK);
    assert(!instance_equal(active, current));
    assert(!task_switcher_is_open(ui));
    lv_indev_delete(pointer);
    assert(pxsys_reference_lvgl_show_recents(ui) == PXSYS_STATUS_OK);
    card = recent_control(ui->task_switcher);
    assert(card != NULL);
    lv_obj_send_event(card, LV_EVENT_CLICKED, keypad);
    assert(pxsys_task_manager_current(tasks, &active) == PXSYS_STATUS_OK);
    assert(instance_equal(active, current));
    assert(!task_switcher_is_open(ui));
    lv_indev_delete(keypad);
    assert(pxsys_task_manager_finish_all(tasks, PXSYS_STOP_SHUTDOWN) == PXSYS_STATUS_OK);
    assert(pxsys_reference_lvgl_destroy(ui) == PXSYS_STATUS_OK);
    assert(pxsys_standard_system_destroy(system) == PXSYS_STATUS_OK);
}
#endif

int main(void) {
    static uint32_t buffer[320 * 240];
    lv_init();
    lv_display_t* display = lv_display_create(320, 240);
    lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    const int sizes[][2] = {{320, 240}, {240, 320}, {480, 320}};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        lv_display_set_resolution(display, sizes[i][0], sizes[i][1]);
        check_paging(display, false);
        check_paging(display, true);
    }
    lv_display_set_resolution(display, 320, 240);
    check_recent_delete(display);
#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && LV_USE_SNAPSHOT
    check_hardware_navigation_previews(display);
#endif
    lv_deinit();
    puts("Launcher focus: paging, task deletion and hardware navigation previews passed");
    return 0;
}
