#include "simulator_runtime.h"

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <SDL2/SDL.h>

#define PXSYS_DESKTOP_RUNTIME_MAGIC UINT32_C(0x50584452)
#define PXSYS_DESKTOP_RUNTIME_ID "pxa-sim"

typedef struct simulator_instance {
    struct pxsys_desktop_runtime* runtime;
    struct simulator_instance* next;
    const pxsys_app_descriptor_t* app;
    pxsys_surface_ref_t surface;
    lv_obj_t* root;
    lv_obj_t* title;
    lv_obj_t* description;
    lv_obj_t* status;
    pxsys_rect_t content_rect;
    uint32_t display_width;
    uint32_t display_height;
    pid_t product_process;
} simulator_instance_t;

struct pxsys_desktop_runtime {
    uint32_t magic;
    pxsys_standard_system_t* system;
    pxsys_lvgl_renderer_t* renderer;
    pxsys_allocator_t allocator;
    pxsys_pxa_runtime_t* pxa;
    simulator_instance_t* instances;
    pxsys_theme_snapshot_t theme;
    pxsys_locale_snapshot_t locale;
    pxsys_desktop_runtime_fixture_t fixture;
};

static lv_color_t color(uint32_t argb) {
    return lv_color_hex(argb & UINT32_C(0x00ffffff));
}

static void style_instance(simulator_instance_t* instance) {
    const pxsys_theme_snapshot_t* theme = &instance->runtime->theme;
    if (instance->root == NULL) return;
    lv_obj_set_style_bg_color(
        instance->root, color(theme->colors[PXSYS_COLOR_BACKGROUND]), 0);
    lv_obj_set_style_bg_opa(instance->root, LV_OPA_COVER, 0);
    if (instance->title != NULL)
        lv_obj_set_style_text_color(
            instance->title, color(theme->colors[PXSYS_COLOR_TEXT_PRIMARY]), 0);
    if (instance->description != NULL)
        lv_obj_set_style_text_color(
            instance->description,
            color(theme->colors[PXSYS_COLOR_TEXT_SECONDARY]), 0);
    if (instance->status != NULL) {
        lv_obj_set_style_text_color(
            instance->status, color(theme->colors[PXSYS_COLOR_ON_ACCENT]), 0);
        lv_obj_set_style_bg_color(
            instance->status, color(theme->colors[PXSYS_COLOR_ACCENT]), 0);
        lv_obj_set_style_bg_opa(instance->status, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(instance->status, theme->base_radius_px, 0);
    }
}

static void update_locale(simulator_instance_t* instance) {
    char status[128];
    const pxsys_locale_snapshot_t* locale = &instance->runtime->locale;
    if (instance->status == NULL) return;
    (void)snprintf(status, sizeof(status),
                   "%s  |  %.*s  |  %lu KiB storage",
                   instance->runtime->fixture.permission_allowed
                       ? "permission: allow"
                       : "permission: deny",
                   (int)locale->tag_size, locale->tag,
                   (unsigned long)(instance->runtime->fixture.storage_bytes /
                                   1024u));
    lv_label_set_text(instance->status, status);
}

static void set_desktop_window_visible(simulator_instance_t* instance,
                                       int visible) {
    SDL_Window* window;
    if (instance == NULL) return;
    window = (SDL_Window*)instance->runtime->fixture.desktop_window;
    if (window == NULL) return;
    if (visible) {
        SDL_ShowWindow(window);
        SDL_RaiseWindow(window);
    } else {
        SDL_HideWindow(window);
    }
}

static int launch_installed_application(simulator_instance_t* instance) {
    const pxsys_desktop_runtime_fixture_t* fixture;
    char package_path[1200];
    char width[16];
    char height[16];
    struct stat metadata;
    pid_t child;
    if (instance == NULL || instance->app == NULL) return 0;
    fixture = &instance->runtime->fixture;
    if (fixture->installed_packages_root == NULL || fixture->product_runner == NULL ||
        fixture->publisher_key == NULL || fixture->state_root == NULL ||
        instance->app->identity.app_id.size == 0 ||
        instance->app->identity.app_id.size > 120)
        return 0;
    if (snprintf(package_path, sizeof(package_path), "%s/%.*s",
                 fixture->installed_packages_root,
                 (int)instance->app->identity.app_id.size,
                 instance->app->identity.app_id.data) >= (int)sizeof(package_path) ||
        stat(package_path, &metadata) != 0 || !S_ISDIR(metadata.st_mode))
        return 0;
    if (instance->product_process > 0) return 1;
    if (snprintf(width, sizeof(width), "%u", instance->display_width) >=
            (int)sizeof(width) ||
        snprintf(height, sizeof(height), "%u", instance->display_height) >=
            (int)sizeof(height))
        return 0;
    child = fork();
    if (child < 0) return 0;
    if (child == 0) {
        if (fixture->pxadb_control_socket != NULL) {
            execl(fixture->product_runner, fixture->product_runner,
                  "--package", package_path, "--publisher-key",
                  fixture->publisher_key, "--state-root", fixture->state_root,
                  "--width", width, "--height", height,
                  "--pxadb-control-socket", fixture->pxadb_control_socket,
                  (char*)NULL);
        } else {
            execl(fixture->product_runner, fixture->product_runner,
                  "--package", package_path, "--publisher-key",
                  fixture->publisher_key, "--state-root", fixture->state_root,
                  "--width", width, "--height", height, (char*)NULL);
        }
        _exit(127);
    }
    instance->product_process = child;
    set_desktop_window_visible(instance, 0);
    return 1;
}

static void theme_changed(void* context,
                          const pxsys_theme_snapshot_t* theme) {
    pxsys_desktop_runtime_t* runtime = (pxsys_desktop_runtime_t*)context;
    simulator_instance_t* instance;
    if (runtime == NULL || runtime->magic != PXSYS_DESKTOP_RUNTIME_MAGIC)
        return;
    runtime->theme = *theme;
    for (instance = runtime->instances; instance != NULL;
         instance = instance->next)
        style_instance(instance);
}

static void locale_changed(void* context,
                           const pxsys_locale_snapshot_t* locale) {
    pxsys_desktop_runtime_t* runtime = (pxsys_desktop_runtime_t*)context;
    simulator_instance_t* instance;
    if (runtime == NULL || runtime->magic != PXSYS_DESKTOP_RUNTIME_MAGIC)
        return;
    runtime->locale = *locale;
    for (instance = runtime->instances; instance != NULL;
         instance = instance->next)
        update_locale(instance);
}

static pxsys_status_t backend_instantiate(
    void* context, const pxsys_app_descriptor_t* app, uint64_t instance_id,
    void** output) {
    pxsys_desktop_runtime_t* runtime = (pxsys_desktop_runtime_t*)context;
    simulator_instance_t* instance;
    pxsys_display_profile_t display;
    pxsys_surface_config_t config = {0};
    (void)instance_id;
    if (runtime == NULL || app == NULL || output == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    instance = runtime->allocator.allocate(runtime->allocator.context,
                                            sizeof(*instance));
    if (instance == NULL) return PXSYS_STATUS_NO_MEMORY;
    memset(instance, 0, sizeof(*instance));
    instance->surface.slot = UINT32_MAX;
    instance->runtime = runtime;
    instance->app = app;
    if (pxsys_display_service_get(pxsys_standard_system_display(runtime->system),
                                  &display) != PXSYS_STATUS_OK) {
        runtime->allocator.release(runtime->allocator.context, instance);
        return PXSYS_STATUS_BAD_STATE;
    }
    if (pxsys_display_content_rect(&display, &instance->content_rect) !=
        PXSYS_STATUS_OK) {
        runtime->allocator.release(runtime->allocator.context, instance);
        return PXSYS_STATUS_BAD_STATE;
    }
    instance->display_width = display.width;
    instance->display_height = display.height;
    config.struct_size = sizeof(config);
    config.width = display.width;
    config.height = display.height;
    config.role = PXSYS_SURFACE_APPLICATION;
    if (pxsys_renderer_surface_create(
            pxsys_standard_system_renderer(runtime->system), &config,
            &instance->surface) != PXSYS_STATUS_OK ||
        pxsys_lvgl_renderer_surface_root(runtime->renderer, instance->surface,
                                         &instance->root) != PXSYS_STATUS_OK) {
        if (instance->surface.slot != UINT32_MAX)
            (void)pxsys_renderer_surface_destroy(
                pxsys_standard_system_renderer(runtime->system),
                instance->surface);
        runtime->allocator.release(runtime->allocator.context, instance);
        return PXSYS_STATUS_INTERNAL;
    }
    instance->next = runtime->instances;
    runtime->instances = instance;
    *output = instance;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t backend_start(void* context, void* opaque,
                                    const pxsys_message_t* launch) {
    simulator_instance_t* instance = (simulator_instance_t*)opaque;
    lv_obj_t* content;
    (void)context;
    (void)launch;
    if (instance == NULL || instance->root == NULL)
        return PXSYS_STATUS_BAD_STATE;
    if (launch_installed_application(instance)) return PXSYS_STATUS_OK;
    content = lv_obj_create(instance->root);
    lv_obj_set_size(content,
                    (int32_t)(instance->content_rect.width * 9u / 10u),
                    LV_SIZE_CONTENT);
    lv_obj_set_pos(
        content,
        instance->content_rect.x +
            (int32_t)(instance->content_rect.width / 20u),
        instance->content_rect.y);
    lv_obj_set_layout(content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_row(content, 12, 0);
    instance->title = lv_label_create(content);
    lv_label_set_text_fmt(instance->title, "%.*s",
                          (int)instance->app->display_name.size,
                          instance->app->display_name.data);
    lv_label_set_long_mode(instance->title, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(instance->title, LV_PCT(100));
    lv_obj_set_style_text_align(instance->title, LV_TEXT_ALIGN_CENTER, 0);
    instance->description = lv_label_create(content);
    lv_label_set_text_fmt(instance->description, "%.*s",
                          (int)instance->app->description.size,
                          instance->app->description.data);
    lv_label_set_long_mode(instance->description, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(instance->description, LV_PCT(100));
    lv_obj_set_style_text_align(instance->description, LV_TEXT_ALIGN_CENTER, 0);
    instance->status = lv_label_create(content);
    lv_obj_set_width(instance->status, LV_PCT(100));
    lv_label_set_long_mode(instance->status, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(instance->status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_hor(instance->status, 12, 0);
    lv_obj_set_style_pad_ver(instance->status, 7, 0);
    update_locale(instance);
    style_instance(instance);
    lv_obj_update_layout(content);
    lv_obj_set_y(content,
                 instance->content_rect.y +
                     ((int32_t)instance->content_rect.height -
                      lv_obj_get_height(content)) /
                         2);
    return PXSYS_STATUS_OK;
}

static pxsys_status_t backend_foreground(void* context, void* opaque) {
    simulator_instance_t* instance = (simulator_instance_t*)opaque;
    (void)context;
    return instance == NULL ? PXSYS_STATUS_BAD_STATE
                            : pxsys_renderer_surface_set_visible(
                                  pxsys_standard_system_renderer(
                                      instance->runtime->system),
                                  instance->surface, 1);
}

static pxsys_status_t backend_background(void* context, void* opaque) {
    simulator_instance_t* instance = (simulator_instance_t*)opaque;
    (void)context;
    return instance == NULL ? PXSYS_STATUS_BAD_STATE
                            : pxsys_renderer_surface_set_visible(
                                  pxsys_standard_system_renderer(
                                      instance->runtime->system),
                                  instance->surface, 0);
}

static pxsys_status_t backend_deliver(void* context, void* opaque,
                                      const pxsys_message_t* message) {
    (void)context;
    (void)message;
    return opaque == NULL ? PXSYS_STATUS_BAD_STATE : PXSYS_STATUS_OK;
}

static pxsys_back_result_t backend_back(void* context, void* opaque) {
    (void)context;
    (void)opaque;
    return PXSYS_BACK_UNHANDLED;
}

static void backend_stop(void* context, void* opaque,
                         pxsys_stop_reason_t reason) {
    simulator_instance_t* instance = (simulator_instance_t*)opaque;
    (void)reason;
    if (instance != NULL && instance->product_process > 0) {
        (void)kill(instance->product_process, SIGTERM);
        (void)waitpid(instance->product_process, NULL, 0);
        instance->product_process = 0;
    }
    (void)backend_background(context, opaque);
    set_desktop_window_visible(instance, 1);
}

void pxsys_desktop_runtime_poll(pxsys_desktop_runtime_t* runtime) {
    simulator_instance_t* instance;
    if (runtime == NULL || runtime->magic != PXSYS_DESKTOP_RUNTIME_MAGIC)
        return;
    for (instance = runtime->instances; instance != NULL;
         instance = instance->next) {
        if (instance->product_process <= 0 ||
            waitpid(instance->product_process, NULL, WNOHANG) !=
                instance->product_process)
            continue;
        instance->product_process = 0;
        (void)pxsys_task_manager_finish_top(
            pxsys_standard_system_tasks(runtime->system), PXSYS_STOP_NORMAL);
        set_desktop_window_visible(instance, 1);
        break;
    }
}

int pxsys_desktop_runtime_has_active_product(
    const pxsys_desktop_runtime_t* runtime) {
    const simulator_instance_t* instance;
    if (runtime == NULL || runtime->magic != PXSYS_DESKTOP_RUNTIME_MAGIC)
        return 0;
    for (instance = runtime->instances; instance != NULL;
         instance = instance->next) {
        if (instance->product_process > 0) return 1;
    }
    return 0;
}

static void backend_destroy(void* context, void* opaque) {
    pxsys_desktop_runtime_t* runtime = (pxsys_desktop_runtime_t*)context;
    simulator_instance_t* instance = (simulator_instance_t*)opaque;
    simulator_instance_t** cursor;
    if (runtime == NULL || instance == NULL) return;
    cursor = &runtime->instances;
    while (*cursor != NULL && *cursor != instance) cursor = &(*cursor)->next;
    if (*cursor == instance) *cursor = instance->next;
    (void)pxsys_renderer_surface_destroy(
        pxsys_standard_system_renderer(runtime->system), instance->surface);
    runtime->allocator.release(runtime->allocator.context, instance);
}

pxsys_status_t pxsys_desktop_runtime_create(
    pxsys_standard_system_t* system, pxsys_lvgl_renderer_t* renderer,
    const pxsys_desktop_runtime_fixture_t* fixture,
    pxsys_allocator_t allocator, pxsys_desktop_runtime_t** output) {
    pxsys_desktop_runtime_t* runtime;
    pxsys_pxa_runtime_config_t config = {0};
    if (system == NULL || renderer == NULL || fixture == NULL || output == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    runtime = allocator.allocate(allocator.context, sizeof(*runtime));
    if (runtime == NULL) return PXSYS_STATUS_NO_MEMORY;
    memset(runtime, 0, sizeof(*runtime));
    runtime->magic = PXSYS_DESKTOP_RUNTIME_MAGIC;
    runtime->system = system;
    runtime->renderer = renderer;
    runtime->allocator = allocator;
    runtime->fixture = *fixture;
    (void)pxsys_theme_service_get(pxsys_standard_system_theme(system),
                                  &runtime->theme);
    (void)pxsys_locale_service_get(pxsys_standard_system_locale(system),
                                   &runtime->locale);
    config.struct_size = sizeof(config);
    config.runtime_id = pxsys_string_from_cstr(PXSYS_DESKTOP_RUNTIME_ID);
    config.version = (pxsys_version_t){0, 1};
    config.allocator = allocator;
    config.backend.struct_size = sizeof(config.backend);
    config.backend.context = runtime;
    config.backend.instantiate = backend_instantiate;
    config.backend.start = backend_start;
    config.backend.foreground = backend_foreground;
    config.backend.background = backend_background;
    config.backend.deliver = backend_deliver;
    config.backend.back = backend_back;
    config.backend.stop = backend_stop;
    config.backend.destroy = backend_destroy;
    if (pxsys_pxa_runtime_create(&config, &runtime->pxa) != PXSYS_STATUS_OK) {
        allocator.release(allocator.context, runtime);
        return PXSYS_STATUS_INTERNAL;
    }
    if (pxsys_theme_service_subscribe(pxsys_standard_system_theme(system),
                                      runtime, theme_changed) != PXSYS_STATUS_OK) {
        (void)pxsys_pxa_runtime_destroy(runtime->pxa);
        allocator.release(allocator.context, runtime);
        return PXSYS_STATUS_INTERNAL;
    }
    if (pxsys_locale_service_subscribe(pxsys_standard_system_locale(system),
                                       runtime, locale_changed) != PXSYS_STATUS_OK) {
        (void)pxsys_theme_service_unsubscribe(
            pxsys_standard_system_theme(system), runtime, theme_changed);
        (void)pxsys_pxa_runtime_destroy(runtime->pxa);
        allocator.release(allocator.context, runtime);
        return PXSYS_STATUS_INTERNAL;
    }
    *output = runtime;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_desktop_runtime_provider(
    pxsys_desktop_runtime_t* runtime, pxsys_runtime_provider_t* output) {
    return runtime == NULL || runtime->magic != PXSYS_DESKTOP_RUNTIME_MAGIC
               ? PXSYS_STATUS_INVALID_ARGUMENT
               : pxsys_pxa_runtime_provider(runtime->pxa, output);
}

pxsys_status_t pxsys_desktop_runtime_destroy(pxsys_desktop_runtime_t* runtime) {
    pxsys_allocator_t allocator;
    if (runtime == NULL || runtime->magic != PXSYS_DESKTOP_RUNTIME_MAGIC)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (runtime->instances != NULL) return PXSYS_STATUS_BUSY;
    (void)pxsys_theme_service_unsubscribe(
        pxsys_standard_system_theme(runtime->system), runtime, theme_changed);
    (void)pxsys_locale_service_unsubscribe(
        pxsys_standard_system_locale(runtime->system), runtime, locale_changed);
    if (pxsys_pxa_runtime_destroy(runtime->pxa) != PXSYS_STATUS_OK)
        return PXSYS_STATUS_BUSY;
    allocator = runtime->allocator;
    runtime->magic = 0;
    allocator.release(allocator.context, runtime);
    return PXSYS_STATUS_OK;
}
