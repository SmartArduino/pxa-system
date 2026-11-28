/* Desktop simulator launcher workload. Run each app count in a fresh process:
 * SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software pxsys_launcher_benchmark 80
 */
#define _POSIX_C_SOURCE 200809L
#undef NDEBUG
#ifndef PXSYS_BENCH_REFERENCE_SOURCE
#define PXSYS_BENCH_REFERENCE_SOURCE "../lvgl/src/reference_lvgl.c"
#endif
#include PXSYS_BENCH_REFERENCE_SOURCE

#include <assert.h>
#include <malloc.h>
#include <SDL.h>
#include <unistd.h>

static uint32_t simulated_ms;

static uint32_t benchmark_ticks(void) { return simulated_ms; }

static uint64_t monotonic_us(void) {
    struct timespec now;
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
}

static void* allocate(void* context, size_t bytes) {
    (void)context;
    return malloc(bytes);
}

static void release(void* context, void* memory) {
    (void)context;
    free(memory);
}

typedef struct {
    lv_image_dsc_t image;
    uint32_t pixels[96 * 96];
} benchmark_icon_t;

static void release_icon(void* context) {
    benchmark_icon_t* icon = context;
    lv_image_cache_drop(&icon->image);
    free(icon);
}

static bool resolve_icon(void* context, const pxsys_app_descriptor_t* app,
                         pxsys_reference_lvgl_app_icon_t* output) {
    (void)context;
    benchmark_icon_t* icon = malloc(sizeof(*icon));
    assert(icon != NULL);
    for (size_t i = 0; i < sizeof(icon->pixels) / sizeof(icon->pixels[0]); ++i)
        icon->pixels[i] = UINT32_C(0xff208080) + (uint8_t)app->identity.app_id.data[0];
    icon->image = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888,
                   .w = 96, .h = 96, .stride = 96 * sizeof(uint32_t)},
        .data_size = sizeof(icon->pixels), .data = (const uint8_t*)icon->pixels,
    };
    *output = (pxsys_reference_lvgl_app_icon_t){&icon->image, release_icon, icon};
    return true;
}

static size_t object_count(lv_obj_t* node) {
    if (node == NULL) return 0;
    size_t count = 1;
    for (uint32_t i = 0; i < lv_obj_get_child_count(node); ++i)
        count += object_count(lv_obj_get_child(node, (int32_t)i));
    return count;
}

static void report(const pxsys_reference_lvgl_t* ui, const char* stage,
                   size_t page, uint64_t elapsed_us) {
    const size_t heap = mallinfo2().uordblks;
    size_t rss_pages = 0;
    FILE* statm = fopen("/proc/self/statm", "r");
    if (statm != NULL) {
        size_t total_pages;
        if (fscanf(statm, "%zu %zu", &total_pages, &rss_pages) != 2) rss_pages = 0;
        fclose(statm);
    }
    size_t icons = 0;
    if (ui != NULL) {
        for (size_t i = 0; i < ui->launcher_count; ++i)
            if (ui->launcher_items[i].icon.source != NULL) ++icons;
    }
    printf("%s,%zu,%llu,%zu,%zu,%zu,%zu\n", stage, page,
           (unsigned long long)elapsed_us, ui ? object_count(ui->root) : 0,
           icons, heap, rss_pages * (size_t)sysconf(_SC_PAGESIZE));
}

static void settle(void) {
    const size_t per_page = PXSYS_REFERENCE_UI_LAUNCHER_COLUMNS *
                            PXSYS_REFERENCE_UI_LAUNCHER_ROWS;
    for (size_t i = 0; i < 6u * per_page; ++i) {
        simulated_ms += 32;
        (void)lv_timer_handler();
    }
}

int main(int argc, char** argv) {
    const size_t count = argc > 1 ? (size_t)strtoul(argv[1], NULL, 10) : 80;
    const bool touch = argc > 2 && strcmp(argv[2], "--touch") == 0;
    assert(count > 0 && count <= 512);
    SDL_SetMainReady();
    lv_init();
    lv_display_t* display = lv_sdl_window_create(320, 240);
    assert(display != NULL);
    lv_tick_set_cb(benchmark_ticks);
    lv_group_t* group = NULL;
    lv_indev_t* keypad = NULL;
    if (!touch) {
        group = lv_group_create();
        keypad = lv_indev_create();
        lv_indev_set_type(keypad, LV_INDEV_TYPE_KEYPAD);
        lv_indev_set_display(keypad, display);
        lv_indev_set_group(keypad, group);
    }

    pxsys_standard_system_config_t system_config;
    pxsys_standard_system_config_init(&system_config);
    system_config.allocator.allocate = allocate;
    system_config.allocator.release = release;
    system_config.max_apps = count + REFERENCE_APP_COUNT;
    pxsys_display_profile_init(&system_config.initial_display, 320, 240);
    pxsys_standard_system_t* system = NULL;
    assert(pxsys_standard_system_create(&system_config, &system) == PXSYS_STATUS_OK);
    for (size_t i = 0; i < count; ++i) {
        char id[32];
        snprintf(id, sizeof(id), "bench-app-%03zu", i);
        pxsys_app_descriptor_t app = {.struct_size = sizeof(app)};
        app.identity.app_id = pxsys_string_from_cstr(id);
        app.display_name = app.identity.app_id;
        app.version = pxsys_string_from_cstr("1.0.0");
        app.runtime_id = pxsys_string_from_cstr(PXSYS_NATIVE_RUNTIME_ID);
        app.flags = PXSYS_APP_FLAG_ENABLED | PXSYS_APP_FLAG_LAUNCHER;
        assert(pxsys_app_registry_register(pxsys_standard_system_apps(system),
                                           &app) == PXSYS_STATUS_OK);
    }
    printf("stage,page,elapsed_us,objects,icons,heap_bytes,rss_bytes\n");
    report(NULL, "catalog_ready", 0, 0);

    pxsys_reference_lvgl_config_t config;
    pxsys_reference_lvgl_config_init(&config);
    config.system = system;
    config.allocator = system_config.allocator;
    config.parent = lv_display_get_layer_top(display);
    config.features = PXSYS_REFERENCE_UI_HOME | PXSYS_REFERENCE_UI_SETTINGS |
                      PXSYS_REFERENCE_UI_STATUS_BAR | PXSYS_REFERENCE_UI_NAVIGATION_BAR;
    config.max_launcher_apps = count;
    config.resolve_app_icon = resolve_icon;
    config.animations_enabled = 0;
    pxsys_reference_lvgl_t* ui = NULL;
    uint64_t start = monotonic_us();
    assert(pxsys_reference_lvgl_create(&config, &ui) == PXSYS_STATUS_OK);
    assert(pxsys_reference_lvgl_start(ui) == PXSYS_STATUS_OK);
    lv_refr_now(display);
    report(ui, "first_frame", 0, monotonic_us() - start);
    assert(ui->launcher_count == count);
    settle();
    report(ui, "first_settled", 0, 0);

    const size_t per_page = PXSYS_REFERENCE_UI_LAUNCHER_COLUMNS *
                            PXSYS_REFERENCE_UI_LAUNCHER_ROWS;
    for (size_t slot = per_page; slot < count; slot += per_page) {
        start = monotonic_us();
        if (touch) {
            ui->launcher_page = (uint16_t)(slot / per_page);
            launcher_page_selected(ui);
            lv_obj_scroll_to_x(ui->content, ui->launcher_page * lv_obj_get_width(ui->content),
                               LV_ANIM_OFF);
        } else {
            lv_obj_t* tile = lv_obj_get_child(ui->content, (int32_t)slot);
            lv_group_add_obj(group, tile);
            lv_group_focus_obj(tile);
        }
        lv_refr_now(display);
        report(ui, "page_frame", slot / per_page, monotonic_us() - start);
        assert(ui->launcher_page == slot / per_page);
        settle();
        report(ui, "page_settled", slot / per_page, 0);
    }

    assert(open_role(ui, PXSYS_ROLE_SETTINGS) == PXSYS_STATUS_OK);
    lv_refr_now(display);
    report(ui, "home_background", ui->launcher_page, 0);
    start = monotonic_us();
    navigation_back(ui);
    lv_refr_now(display);
    report(ui, "return_frame", ui->launcher_page, monotonic_us() - start);
    assert(ui->launcher_page == (count - 1u) / per_page);
    settle();
    report(ui, "return_settled", ui->launcher_page, 0);

    if (keypad != NULL) {
        lv_indev_delete(keypad);
        lv_group_delete(group);
    }
    assert(pxsys_reference_lvgl_destroy(ui) == PXSYS_STATUS_OK);
    assert(pxsys_standard_system_destroy(system) == PXSYS_STATUS_OK);
    lv_display_delete(display);
    lv_deinit();
    return 0;
}
