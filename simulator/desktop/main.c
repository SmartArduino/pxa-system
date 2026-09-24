#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <png.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <SDL2/SDL.h>

#include "lvgl.h"
#include "pxa/package.h"
#include "pxsys/lvgl_renderer.h"
#include "pxsys/pxa_catalog.h"
#include "pxsys/reference_lvgl.h"
#include "pxsys/standard_system.h"
#include "pxadb_control.h"
#include "simulator_runtime.h"
#include "src/core/lv_obj_event_private.h"
#include "src/drivers/sdl/lv_sdl_private.h"
#include "src/drivers/sdl/lv_sdl_keyboard.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#include "src/misc/cache/instance/lv_image_cache.h"

typedef struct {
    size_t allocations;
} simulator_memory_t;

#define PXSYS_DESKTOP_ICON_MAX_BYTES (256u * 1024u)
#define PXSYS_DESKTOP_ICON_MAX_DIMENSION 512u
#define PXSYS_DESKTOP_MANIFEST_MAX_BYTES (128u * 1024u)
#define PXSYS_DESKTOP_RUNTIME_ID "pxa-sim"

typedef struct {
    const char* installed_packages_root;
} simulator_icon_resolver_t;

typedef struct {
    lv_image_dsc_t descriptor;
    uint8_t* bytes;
} simulator_icon_t;

typedef struct {
    lv_font_t* display;
    lv_font_t* headline;
    lv_font_t* title;
    lv_font_t* body;
    lv_font_t* label;
    lv_font_t* caption;
} simulator_ui_fonts_t;

typedef struct {
    pxsys_standard_system_t* system;
    pxsys_reference_lvgl_t* ui;
    const char* installed_packages_root;
} simulator_catalog_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t storage_bytes;
    uint8_t round;
} simulator_device_info_t;

typedef struct {
    char ssid[PXSYS_REFERENCE_WIFI_SSID_MAX];
    bool connected;
} simulator_wifi_t;

static simulator_ui_fonts_t simulator_ui_fonts;

typedef struct {
    pxsys_reference_lvgl_t* ui;
    lv_obj_t* screen_off_overlay;
    uint32_t window_id;
    uint32_t pressed_at_ms;
    uint8_t screen_off;
    uint8_t key_down;
    uint8_t press_woke_screen;
    uint8_t long_press_handled;
    uint8_t short_press_pending;
    uint8_t restart_pending;
    uint8_t quit_requested;
} simulator_power_state_t;

static void simulator_screen_off(simulator_power_state_t* state) {
    if (state == NULL || state->ui == NULL || state->screen_off) return;
    if (pxsys_reference_lvgl_set_locked(state->ui, true) != PXSYS_STATUS_OK)
        return;
    state->screen_off_overlay = lv_obj_create(lv_layer_top());
    if (state->screen_off_overlay == NULL) return;
    lv_obj_set_size(state->screen_off_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(state->screen_off_overlay, 0, 0);
    lv_obj_set_style_bg_color(state->screen_off_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(state->screen_off_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(state->screen_off_overlay, 0, 0);
    lv_obj_set_style_radius(state->screen_off_overlay, 0, 0);
    lv_obj_set_style_pad_all(state->screen_off_overlay, 0, 0);
    lv_obj_add_flag(state->screen_off_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(state->screen_off_overlay, LV_OBJ_FLAG_SCROLLABLE);
    state->screen_off = 1;
}

static void simulator_screen_wake(simulator_power_state_t* state) {
    if (state == NULL || !state->screen_off) return;
    if (state->screen_off) {
        if (state->screen_off_overlay != NULL)
            lv_obj_delete(state->screen_off_overlay);
        state->screen_off_overlay = NULL;
        state->screen_off = 0;
    }
}

static int simulator_power_event_watch(void* context, SDL_Event* event) {
    simulator_power_state_t* state = (simulator_power_state_t*)context;
    if (state == NULL || event == NULL ||
        (event->type != SDL_KEYDOWN && event->type != SDL_KEYUP) ||
        (event->key.keysym.sym != SDLK_p &&
         event->key.keysym.sym != SDLK_POWER) ||
        (state->window_id != 0 && event->key.windowID != state->window_id))
        return 1;
    if (event->type == SDL_KEYDOWN) {
        if (event->key.repeat || state->key_down) return 1;
        state->key_down = 1;
        state->long_press_handled = 0;
        state->press_woke_screen = state->screen_off;
        state->pressed_at_ms = SDL_GetTicks();
    } else if (state->key_down) {
        if (!state->long_press_handled && !state->press_woke_screen)
            state->short_press_pending = 1;
        state->key_down = 0;
    }
    return 1;
}

static void simulator_power_action(
    void* context, pxsys_reference_power_action_t action) {
    simulator_power_state_t* state = (simulator_power_state_t*)context;
    if (state == NULL) return;
    if (action == PXSYS_REFERENCE_POWER_ACTION_SHUTDOWN)
        state->quit_requested = 1;
    else
        state->restart_pending = 1;
}

static void simulator_power_poll(simulator_power_state_t* state) {
    if (state == NULL || state->ui == NULL) return;
    if (state->press_woke_screen && state->screen_off)
        simulator_screen_wake(state);
    if (state->key_down && !state->press_woke_screen &&
        !state->long_press_handled &&
        SDL_GetTicks() - state->pressed_at_ms >= 1500u) {
        state->long_press_handled = 1;
        (void)pxsys_reference_lvgl_show_power_menu(state->ui);
    }
    if (state->short_press_pending) {
        state->short_press_pending = 0;
        simulator_screen_off(state);
    }
    if (state->restart_pending) {
        state->restart_pending = 0;
        simulator_screen_wake(state);
        (void)pxsys_reference_lvgl_set_locked(state->ui, false);
        (void)pxsys_reference_lvgl_home(state->ui);
    }
}

static int create_ui_font(lv_font_t** destination, uint32_t size,
                          const lv_font_t* symbol_fallback) {
    *destination = lv_freetype_font_create(
        PXSYS_DESKTOP_TEXT_FONT, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, size,
        LV_FREETYPE_FONT_STYLE_NORMAL);
    if (*destination == NULL) return 0;
    (*destination)->fallback = symbol_fallback;
    return 1;
}

static int create_ui_fonts(void) {
    if (!create_ui_font(&simulator_ui_fonts.display, 28,
                        &lv_font_montserrat_28) ||
        !create_ui_font(&simulator_ui_fonts.headline, 24,
                        &lv_font_montserrat_24) ||
        !create_ui_font(&simulator_ui_fonts.title, 20,
                        &lv_font_montserrat_20) ||
        !create_ui_font(&simulator_ui_fonts.body, 16,
                        &lv_font_montserrat_16) ||
        !create_ui_font(&simulator_ui_fonts.label, 14,
                        &lv_font_montserrat_14) ||
        !create_ui_font(&simulator_ui_fonts.caption, 12,
                        &lv_font_montserrat_12))
        return 0;
    return 1;
}

static void destroy_ui_fonts(void) {
    lv_font_t** fonts[] = {
        &simulator_ui_fonts.display,
        &simulator_ui_fonts.headline,
        &simulator_ui_fonts.title,
        &simulator_ui_fonts.body,
        &simulator_ui_fonts.label,
        &simulator_ui_fonts.caption,
    };
    size_t index;
    for (index = 0; index < sizeof(fonts) / sizeof(fonts[0]); ++index) {
        if (*fonts[index] != NULL) {
            lv_freetype_font_delete(*fonts[index]);
            *fonts[index] = NULL;
        }
    }
}

typedef enum {
    SIMULATOR_SHAPE_BACKGROUND_BLACK = 0,
    SIMULATOR_SHAPE_BACKGROUND_MATTE,
} simulator_shape_background_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t duration_ms;
    pxsys_color_scheme_t scheme;
    pxsys_navigation_mode_t navigation;
    const char* locale;
    const char* launch_app;
    const char* screenshot;
    pxsys_display_shape_t shape;
    uint16_t corner_radius;
    simulator_shape_background_t shape_background;
    pxsys_insets_t safe_insets;
    uint8_t custom_theme;
    uint8_t self_test;
    uint8_t hour;
    uint8_t minute;
    uint8_t battery;
    uint8_t network_signal;
    pxsys_network_type_t network;
    uint8_t permission_allowed;
    uint8_t locked;
    uint32_t storage_bytes;
    const char* installed_packages_root;
    const char* product_runner;
    const char* publisher_key;
    const char* state_root;
    const char* pxadb_control_socket;
} simulator_options_t;

static pxsys_display_profile_t s_display_profile;
static simulator_shape_background_t s_shape_background =
    SIMULATOR_SHAPE_BACKGROUND_MATTE;

static uint32_t read_be_u32(const uint8_t* value) {
    return ((uint32_t)value[0] << 24) | ((uint32_t)value[1] << 16) |
           ((uint32_t)value[2] << 8) | (uint32_t)value[3];
}

static int path_part_is_safe(const char* data, size_t size) {
    size_t index;
    if (data == NULL || size == 0 || (size == 1 && data[0] == '.') ||
        (size == 2 && data[0] == '.' && data[1] == '.'))
        return 0;
    for (index = 0; index < size; ++index) {
        if (data[index] == '\\' || (unsigned char)data[index] < 0x20u)
            return 0;
    }
    return 1;
}

static int icon_path_is_safe(pxsys_string_t path) {
    size_t part_start = 0;
    size_t index;
    if (path.data == NULL || path.size == 0 ||
        path.size > PXSYS_APP_ICON_REFERENCE_MAX_BYTES || path.data[0] == '/')
        return 0;
    for (index = 0; index <= path.size; ++index) {
        if (index != path.size && path.data[index] != '/') continue;
        if (!path_part_is_safe(path.data + part_start, index - part_start))
            return 0;
        part_start = index + 1;
    }
    return 1;
}

static int app_id_is_safe(pxsys_string_t app_id) {
    size_t index;
    if (app_id.data == NULL || app_id.size == 0 || app_id.size > 120) return 0;
    for (index = 0; index < app_id.size; ++index) {
        unsigned char character = (unsigned char)app_id.data[index];
        if (!(character == '-' || character == '_' || character == '.' ||
              (character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'z')))
            return 0;
    }
    return 1;
}

static void release_launcher_icon(void* context) {
    simulator_icon_t* icon = (simulator_icon_t*)context;
    if (icon == NULL) return;
    if (icon->descriptor.header.magic == LV_IMAGE_HEADER_MAGIC)
        lv_image_cache_drop(&icon->descriptor);
    free(icon->bytes);
    free(icon);
}

static int read_icon_file(const char* path,
                          pxsys_reference_lvgl_app_icon_t* output) {
    static const uint8_t png_signature[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
    };
    struct stat metadata;
    simulator_icon_t* icon;
    int descriptor;
    size_t offset = 0;
    if (path == NULL || output == NULL) return 0;
    descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0 || fstat(descriptor, &metadata) != 0 ||
        !S_ISREG(metadata.st_mode) || metadata.st_size < 24 ||
        (uintmax_t)metadata.st_size > PXSYS_DESKTOP_ICON_MAX_BYTES) {
        if (descriptor >= 0) close(descriptor);
        return 0;
    }
    icon = calloc(1, sizeof(*icon));
    if (icon == NULL) {
        close(descriptor);
        return 0;
    }
    icon->bytes = malloc((size_t)metadata.st_size);
    if (icon->bytes == NULL) {
        close(descriptor);
        free(icon);
        return 0;
    }
    while (offset < (size_t)metadata.st_size) {
        ssize_t count = read(descriptor, icon->bytes + offset,
                             (size_t)metadata.st_size - offset);
        if (count <= 0) {
            close(descriptor);
            release_launcher_icon(icon);
            return 0;
        }
        offset += (size_t)count;
    }
    close(descriptor);
    if (memcmp(icon->bytes, png_signature, sizeof(png_signature)) != 0 ||
        memcmp(icon->bytes + 12, "IHDR", 4) != 0 ||
        read_be_u32(icon->bytes + 16) == 0 ||
        read_be_u32(icon->bytes + 20) == 0 ||
        read_be_u32(icon->bytes + 16) > PXSYS_DESKTOP_ICON_MAX_DIMENSION ||
        read_be_u32(icon->bytes + 20) > PXSYS_DESKTOP_ICON_MAX_DIMENSION) {
        release_launcher_icon(icon);
        return 0;
    }
    icon->descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    icon->descriptor.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
    icon->descriptor.header.w = read_be_u32(icon->bytes + 16);
    icon->descriptor.header.h = read_be_u32(icon->bytes + 20);
    icon->descriptor.data_size = (size_t)metadata.st_size;
    icon->descriptor.data = icon->bytes;
    output->source = &icon->descriptor;
    output->release = release_launcher_icon;
    output->release_context = icon;
    return 1;
}

static bool resolve_launcher_icon(
    void* context, const pxsys_app_descriptor_t* app,
    pxsys_reference_lvgl_app_icon_t* output) {
    const simulator_icon_resolver_t* resolver =
        (const simulator_icon_resolver_t*)context;
    char path[1400];
    if (app == NULL || output == NULL || !app_id_is_safe(app->identity.app_id) ||
        !icon_path_is_safe(app->icon_reference))
        return false;
    memset(output, 0, sizeof(*output));
    if (resolver != NULL && resolver->installed_packages_root != NULL &&
        snprintf(path, sizeof(path), "%s/%.*s/%.*s",
                 resolver->installed_packages_root,
                 (int)app->identity.app_id.size, app->identity.app_id.data,
                 (int)app->icon_reference.size, app->icon_reference.data) <
            (int)sizeof(path) &&
        read_icon_file(path, output))
        return true;
    if (snprintf(path, sizeof(path), "%s/%.*s/%.*s",
                 PXSYS_DESKTOP_APP_SOURCE_ROOT,
                 (int)app->identity.app_id.size, app->identity.app_id.data,
                 (int)app->icon_reference.size, app->icon_reference.data) >=
        (int)sizeof(path))
        return false;
    return read_icon_file(path, output) != 0;
}

static int read_regular_file(const char* path, size_t maximum_size,
                             uint8_t** output, size_t* output_size) {
    struct stat metadata;
    uint8_t* bytes;
    int descriptor;
    size_t offset = 0;
    if (path == NULL || output == NULL || output_size == NULL) return 0;
    *output = NULL;
    *output_size = 0;
    descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0 || fstat(descriptor, &metadata) != 0 ||
        !S_ISREG(metadata.st_mode) || metadata.st_size <= 0 ||
        (uintmax_t)metadata.st_size > maximum_size) {
        if (descriptor >= 0) close(descriptor);
        return 0;
    }
    bytes = malloc((size_t)metadata.st_size);
    if (bytes == NULL) {
        close(descriptor);
        return 0;
    }
    while (offset < (size_t)metadata.st_size) {
        ssize_t count = read(descriptor, bytes + offset,
                             (size_t)metadata.st_size - offset);
        if (count <= 0) {
            close(descriptor);
            free(bytes);
            return 0;
        }
        offset += (size_t)count;
    }
    close(descriptor);
    *output = bytes;
    *output_size = (size_t)metadata.st_size;
    return 1;
}

static int manifest_app_id_matches(const pxa_package_manifest_t* manifest,
                                   const char* app_id) {
    const size_t app_id_size = app_id == NULL ? 0 : strlen(app_id);
    return manifest != NULL && manifest->app_id.size == app_id_size &&
           memcmp(manifest->app_id.data, app_id, app_id_size) == 0;
}

static pxsys_status_t publish_installed_package(
    pxsys_app_registry_t* apps, const char* root, const char* app_id,
    const pxsys_locale_snapshot_t* locale) {
    char manifest_path[1400];
    uint8_t* encoded = NULL;
    size_t encoded_size = 0;
    pxa_package_limits_t limits;
    pxa_package_manifest_t* manifest = NULL;
    pxa_package_manifest_t localized_manifest;
    pxa_package_metadata_t localized_metadata;
    void* workspace = NULL;
    size_t workspace_size;
    pxsys_pxa_catalog_change_t change;
    pxsys_status_t result = PXSYS_STATUS_INVALID_ARGUMENT;
    if (apps == NULL || root == NULL || app_id == NULL ||
        snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.pxm", root) >=
            (int)sizeof(manifest_path) ||
        !read_regular_file(manifest_path, PXSYS_DESKTOP_MANIFEST_MAX_BYTES,
                           &encoded, &encoded_size))
        return PXSYS_STATUS_NOT_FOUND;
    pxa_package_limits_init(&limits);
    workspace_size = pxa_package_manifest_workspace_size(&limits);
    workspace = malloc(workspace_size);
    if (workspace == NULL) {
        result = PXSYS_STATUS_NO_MEMORY;
        goto done;
    }
    if (pxa_package_manifest_parse(
            workspace, workspace_size, (pxa_bytes_t){encoded, encoded_size},
            &limits, &manifest) != PXA_STATUS_OK ||
        !manifest_app_id_matches(manifest, app_id)) {
        result = PXSYS_STATUS_INVALID_ARGUMENT;
        goto done;
    }
    if (locale != NULL &&
        pxa_package_metadata_resolve(
            manifest, (pxa_bytes_t){(const uint8_t*)locale->tag,
                                     locale->tag_size},
            &localized_metadata) == PXA_STATUS_OK) {
        localized_manifest = *manifest;
        localized_manifest.name = localized_metadata.name;
        localized_manifest.description = localized_metadata.description;
        localized_manifest.icon_path = localized_metadata.icon_path;
        manifest = &localized_manifest;
    }
    result = pxsys_pxa_catalog_publish(
        apps, manifest, pxsys_string_from_cstr(PXSYS_DESKTOP_RUNTIME_ID),
        PXSYS_APP_FLAG_REMOVABLE | PXSYS_APP_FLAG_ENABLED |
            PXSYS_APP_FLAG_LAUNCHER,
        &change);
done:
    free(workspace);
    free(encoded);
    return result;
}

static pxsys_status_t sync_installed_catalog(simulator_catalog_t* catalog) {
    DIR* directory;
    struct dirent* entry;
    pxsys_app_registry_t* apps;
    pxsys_locale_snapshot_t locale = {.struct_size = sizeof(locale)};
    if (catalog == NULL || catalog->system == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (catalog->installed_packages_root == NULL) return PXSYS_STATUS_OK;
    if (pxsys_locale_service_get(pxsys_standard_system_locale(catalog->system),
                                 &locale) != PXSYS_STATUS_OK)
        return PXSYS_STATUS_INTERNAL;
    directory = opendir(catalog->installed_packages_root);
    if (directory == NULL)
        return errno == ENOENT ? PXSYS_STATUS_OK : PXSYS_STATUS_UNAVAILABLE;
    apps = pxsys_standard_system_apps(catalog->system);
    while ((entry = readdir(directory)) != NULL) {
        char package_root[1400];
        char current_root[1400];
        struct stat metadata;
        pxsys_status_t status;
        pxsys_string_t app_id = pxsys_string_from_cstr(entry->d_name);
        const char* selected_root = package_root;
        if (!app_id_is_safe(app_id) ||
            snprintf(package_root, sizeof(package_root), "%s/%s",
                     catalog->installed_packages_root, entry->d_name) >=
                (int)sizeof(package_root) ||
            stat(package_root, &metadata) != 0 || !S_ISDIR(metadata.st_mode))
            continue;
        if (snprintf(current_root, sizeof(current_root), "%s/current",
                     package_root) < (int)sizeof(current_root) &&
            stat(current_root, &metadata) == 0 && S_ISDIR(metadata.st_mode))
            selected_root = current_root;
        status = publish_installed_package(apps, selected_root, entry->d_name,
                                           &locale);
        if (status != PXSYS_STATUS_OK && status != PXSYS_STATUS_ALREADY_EXISTS &&
            status != PXSYS_STATUS_INVALID_ARGUMENT &&
            status != PXSYS_STATUS_NOT_FOUND) {
            closedir(directory);
            return status;
        }
    }
    closedir(directory);
    return PXSYS_STATUS_OK;
}

static void catalog_locale_changed(
    void* context, const pxsys_locale_snapshot_t* locale) {
    simulator_catalog_t* catalog = (simulator_catalog_t*)context;
    (void)locale;
    if (catalog != NULL) (void)sync_installed_catalog(catalog);
}

static int refresh_installed_catalog(void* context) {
    simulator_catalog_t* catalog = (simulator_catalog_t*)context;
    if (sync_installed_catalog(catalog) != PXSYS_STATUS_OK) return 0;
    if (catalog->ui != NULL) pxsys_reference_lvgl_refresh_apps(catalog->ui);
    return 1;
}

static void copy_pxsys_string(char* destination, size_t capacity,
                              pxsys_string_t source) {
    size_t size;
    if (destination == NULL || capacity == 0) return;
    size = source.data == NULL ? 0 : source.size;
    if (size >= capacity) size = capacity - 1;
    if (size != 0) memcpy(destination, source.data, size);
    destination[size] = '\0';
}

static void simulator_device_info(
    void* context,
    char values[PXSYS_REFERENCE_DEVICE_FIELD_COUNT]
               [PXSYS_REFERENCE_DEVICE_VALUE_MAX]) {
    const simulator_device_info_t* info =
        (const simulator_device_info_t*)context;
    if (info == NULL || values == NULL) return;
    snprintf(values[PXSYS_REFERENCE_DEVICE_FIRMWARE_NAME],
             PXSYS_REFERENCE_DEVICE_VALUE_MAX, "PXA Desktop Simulator");
    snprintf(values[PXSYS_REFERENCE_DEVICE_FIRMWARE_VERSION],
             PXSYS_REFERENCE_DEVICE_VALUE_MAX, "sensecap-watcher profile");
    snprintf(values[PXSYS_REFERENCE_DEVICE_SYSTEM_VERSION],
             PXSYS_REFERENCE_DEVICE_VALUE_MAX, "PXA System 0.1");
    snprintf(values[PXSYS_REFERENCE_DEVICE_DISPLAY],
             PXSYS_REFERENCE_DEVICE_VALUE_MAX, "%u x %u%s", info->width,
             info->height, info->round ? ", round" : "");
    snprintf(values[PXSYS_REFERENCE_DEVICE_MEMORY],
             PXSYS_REFERENCE_DEVICE_VALUE_MAX, "Desktop host");
    snprintf(values[PXSYS_REFERENCE_DEVICE_STORAGE],
             PXSYS_REFERENCE_DEVICE_VALUE_MAX, "%lu KiB",
             (unsigned long)(info->storage_bytes / 1024u));
}

static bool simulator_memory_info(void* context, uint64_t* available_bytes,
                                  uint64_t* total_bytes) {
    long available_pages;
    long total_pages;
    long page_size;
    (void)context;
    if (available_bytes == NULL || total_bytes == NULL) return false;
    available_pages = sysconf(_SC_AVPHYS_PAGES);
    total_pages = sysconf(_SC_PHYS_PAGES);
    page_size = sysconf(_SC_PAGESIZE);
    if (available_pages < 0 || total_pages <= 0 || page_size <= 0) return false;
    *available_bytes = (uint64_t)available_pages * (uint64_t)page_size;
    *total_bytes = (uint64_t)total_pages * (uint64_t)page_size;
    return true;
}

static size_t simulator_wifi_scan(void* context,
                                  pxsys_reference_wifi_network_t* networks,
                                  size_t capacity) {
    static const pxsys_reference_wifi_network_t available[] = {
        {.ssid = "PXA Simulator", .rssi = -46, .secured = 1},
        {.ssid = "PXA Guest", .rssi = -68, .secured = 0},
    };
    size_t count = sizeof(available) / sizeof(available[0]);
    (void)context;
    if (count > capacity) count = capacity;
    if (count != 0 && networks != NULL)
        memcpy(networks, available, count * sizeof(*networks));
    return count;
}

static bool simulator_wifi_connect(void* context, const char* ssid,
                                   const char* password) {
    simulator_wifi_t* wifi = (simulator_wifi_t*)context;
    if (wifi == NULL || ssid == NULL || strlen(ssid) == 0 ||
        strlen(ssid) >= sizeof(wifi->ssid))
        return false;
    (void)password;
    snprintf(wifi->ssid, sizeof(wifi->ssid), "%s", ssid);
    wifi->connected = true;
    return true;
}

static bool simulator_wifi_current(void* context, char* ssid, size_t capacity) {
    const simulator_wifi_t* wifi = (const simulator_wifi_t*)context;
    if (wifi == NULL || ssid == NULL || capacity == 0 || !wifi->connected)
        return false;
    snprintf(ssid, capacity, "%s", wifi->ssid);
    return true;
}

static size_t simulator_list_apps(
    void* context, pxsys_reference_managed_app_t* output, size_t capacity) {
    const simulator_catalog_t* catalog = (const simulator_catalog_t*)context;
    pxsys_app_registry_t* apps;
    size_t count;
    size_t index;
    size_t written = 0;
    if (catalog == NULL || catalog->system == NULL) return 0;
    apps = pxsys_standard_system_apps(catalog->system);
    count = pxsys_app_registry_count(apps);
    if (output == NULL) return count;
    for (index = 0; index < count && written < capacity; ++index) {
        const pxsys_app_descriptor_t* app = pxsys_app_registry_at(apps, index);
        pxsys_reference_managed_app_t* managed;
        if (app == NULL) continue;
        managed = &output[written++];
        memset(managed, 0, sizeof(*managed));
        copy_pxsys_string(managed->identity, sizeof(managed->identity),
                          app->identity.app_id);
        copy_pxsys_string(managed->name, sizeof(managed->name),
                          app->display_name);
        if (managed->name[0] == '\0')
            copy_pxsys_string(managed->name, sizeof(managed->name),
                              app->identity.app_id);
        copy_pxsys_string(managed->version, sizeof(managed->version),
                          app->version);
        managed->built_in =
            (app->flags & PXSYS_APP_FLAG_SYSTEM) != 0 ? 1 : 0;
        managed->enabled =
            (app->flags & PXSYS_APP_FLAG_ENABLED) != 0 ? 1 : 0;
        /* Package removal and data deletion need a persistent package-store
         * transaction, which this desktop registry adapter does not own. */
        managed->installed = 0;
        managed->has_private_data = 0;
    }
    return written;
}

static bool simulator_app_action(void* context, const char* identity,
                                 pxsys_reference_app_action_t action) {
    const simulator_catalog_t* catalog = (const simulator_catalog_t*)context;
    pxsys_app_registry_t* apps;
    size_t index;
    if (catalog == NULL || catalog->system == NULL || identity == NULL)
        return false;
    if (action != PXSYS_REFERENCE_APP_ACTION_ENABLE &&
        action != PXSYS_REFERENCE_APP_ACTION_DISABLE)
        return false;
    apps = pxsys_standard_system_apps(catalog->system);
    for (index = 0; index < pxsys_app_registry_count(apps); ++index) {
        const pxsys_app_descriptor_t* app = pxsys_app_registry_at(apps, index);
        pxsys_app_descriptor_t replacement;
        if (app == NULL || app->identity.app_id.size != strlen(identity) ||
            memcmp(app->identity.app_id.data, identity,
                   app->identity.app_id.size) != 0)
            continue;
        replacement = *app;
        if (action == PXSYS_REFERENCE_APP_ACTION_ENABLE)
            replacement.flags |= PXSYS_APP_FLAG_ENABLED;
        else
            replacement.flags &= ~PXSYS_APP_FLAG_ENABLED;
        return pxsys_app_registry_update(apps, &replacement) == PXSYS_STATUS_OK;
    }
    return false;
}

static bool simulator_launcher_load(void* context, char* order,
                                    size_t capacity) {
    char path[512];
    const char* root = context != NULL ? (const char*)context : getenv("HOME");
    FILE* file;
    size_t count;
    if (root == NULL || order == NULL || capacity < 2u ||
        snprintf(path, sizeof(path), "%s/%s", root,
                 context != NULL ? "launcher-order" :
                                   ".pxa-simulator-launcher-order") >=
            (int)sizeof(path)) return false;
    file = fopen(path, "rb");
    if (file == NULL) return false;
    count = fread(order, 1, capacity - 1u, file);
    fclose(file);
    order[count] = '\0';
    return true;
}

static void simulator_launcher_save(void* context, const char* order) {
    char path[512];
    const char* root = context != NULL ? (const char*)context : getenv("HOME");
    FILE* file;
    if (root == NULL || order == NULL ||
        snprintf(path, sizeof(path), "%s/%s", root,
                 context != NULL ? "launcher-order" :
                                   ".pxa-simulator-launcher-order") >=
            (int)sizeof(path)) return;
    file = fopen(path, "wb");
    if (file == NULL) return;
    (void)fwrite(order, 1, strlen(order), file);
    fclose(file);
}

/* Permissions. The desktop adapter reads declared permissions from the
 * installed package manifest and keeps toggle decisions in memory: the UI
 * simulator does not own a persistent permission store. */
#define SIMULATOR_PERMISSION_OVERRIDE_MAX 48

typedef struct {
    char app_id[PXSYS_REFERENCE_MANAGED_APP_IDENTITY_MAX];
    uint8_t permission_index;
    uint8_t granted;
    uint8_t used;
} simulator_permission_override_t;

static simulator_permission_override_t
    simulator_permission_overrides[SIMULATOR_PERMISSION_OVERRIDE_MAX];

static void copy_permission_text(char* destination, size_t capacity,
                                 pxa_bytes_t source) {
    size_t size = source.data == NULL ? 0 : source.size;
    if (capacity == 0) return;
    if (size >= capacity) size = capacity - 1;
    if (size != 0) memcpy(destination, source.data, size);
    destination[size] = '\0';
}

static int simulator_permission_decision(const char* app_id,
                                         size_t permission_index,
                                         bool* granted) {
    size_t index;
    for (index = 0; index < SIMULATOR_PERMISSION_OVERRIDE_MAX; ++index) {
        const simulator_permission_override_t* override =
            &simulator_permission_overrides[index];
        if (!override->used || override->permission_index != permission_index ||
            strcmp(override->app_id, app_id) != 0)
            continue;
        *granted = override->granted != 0;
        return 1;
    }
    return 0;
}

/* Parses the installed manifest. Callers release *encoded and *workspace. */
static int simulator_load_manifest(const simulator_catalog_t* catalog,
                                   const char* app_id, uint8_t** encoded,
                                   void** workspace,
                                   pxa_package_manifest_t** manifest) {
    char package_root[1400];
    char current_root[1400];
    char manifest_path[1400];
    struct stat metadata;
    const char* selected_root = package_root;
    pxa_package_limits_t limits;
    size_t encoded_size = 0;
    size_t workspace_size;
    *encoded = NULL;
    *workspace = NULL;
    *manifest = NULL;
    if (catalog == NULL || catalog->installed_packages_root == NULL ||
        app_id == NULL ||
        !app_id_is_safe(pxsys_string_from_cstr(app_id)) ||
        snprintf(package_root, sizeof(package_root), "%s/%s",
                 catalog->installed_packages_root, app_id) >=
            (int)sizeof(package_root) ||
        stat(package_root, &metadata) != 0 || !S_ISDIR(metadata.st_mode))
        return 0;
    if (snprintf(current_root, sizeof(current_root), "%s/current",
                 package_root) < (int)sizeof(current_root) &&
        stat(current_root, &metadata) == 0 && S_ISDIR(metadata.st_mode))
        selected_root = current_root;
    if (snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.pxm",
                 selected_root) >= (int)sizeof(manifest_path) ||
        !read_regular_file(manifest_path, PXSYS_DESKTOP_MANIFEST_MAX_BYTES,
                           encoded, &encoded_size))
        return 0;
    pxa_package_limits_init(&limits);
    workspace_size = pxa_package_manifest_workspace_size(&limits);
    *workspace = malloc(workspace_size);
    if (*workspace == NULL ||
        pxa_package_manifest_parse(
            *workspace, workspace_size, (pxa_bytes_t){*encoded, encoded_size},
            &limits, manifest) != PXA_STATUS_OK ||
        !manifest_app_id_matches(*manifest, app_id)) {
        free(*workspace);
        free(*encoded);
        *workspace = NULL;
        *encoded = NULL;
        *manifest = NULL;
        return 0;
    }
    return 1;
}

static size_t simulator_list_app_permissions(
    void* context, const char* identity,
    pxsys_reference_app_permission_t* output, size_t capacity) {
    const simulator_catalog_t* catalog = (const simulator_catalog_t*)context;
    uint8_t* encoded = NULL;
    void* workspace = NULL;
    pxa_package_manifest_t* manifest = NULL;
    size_t count;
    size_t index;
    if (!simulator_load_manifest(catalog, identity, &encoded, &workspace,
                                 &manifest))
        return 0;
    count = manifest->permission_count;
    if (output == NULL) {
        free(workspace);
        free(encoded);
        return count;
    }
    if (count > capacity) count = capacity;
    for (index = 0; index < count; ++index) {
        const pxa_package_permission_t* permission =
            &manifest->permissions[index];
        bool granted = false;
        memset(&output[index], 0, sizeof(output[index]));
        copy_permission_text(output[index].name, sizeof(output[index].name),
                             permission->name);
        copy_permission_text(output[index].scope, sizeof(output[index].scope),
                             permission->scope);
        output[index].required = permission->required != 0 ? 1 : 0;
        (void)simulator_permission_decision(identity, index, &granted);
        output[index].granted = granted ? 1 : 0;
    }
    free(workspace);
    free(encoded);
    return count;
}

static bool simulator_set_app_permission(void* context, const char* identity,
                                         size_t permission_index,
                                         bool granted) {
    const simulator_catalog_t* catalog = (const simulator_catalog_t*)context;
    uint8_t* encoded = NULL;
    void* workspace = NULL;
    pxa_package_manifest_t* manifest = NULL;
    size_t index;
    simulator_permission_override_t* target = NULL;
    if (identity == NULL ||
        !simulator_load_manifest(catalog, identity, &encoded, &workspace,
                                 &manifest) ||
        permission_index >= manifest->permission_count) {
        free(workspace);
        free(encoded);
        return false;
    }
    free(workspace);
    free(encoded);
    for (index = 0; index < SIMULATOR_PERMISSION_OVERRIDE_MAX; ++index) {
        if (simulator_permission_overrides[index].used &&
            strcmp(simulator_permission_overrides[index].app_id, identity) == 0 &&
            simulator_permission_overrides[index].permission_index ==
                permission_index) {
            target = &simulator_permission_overrides[index];
            break;
        }
    }
    if (target == NULL) {
        for (index = 0; index < SIMULATOR_PERMISSION_OVERRIDE_MAX; ++index) {
            if (!simulator_permission_overrides[index].used) {
                target = &simulator_permission_overrides[index];
                break;
            }
        }
    }
    if (target == NULL) return false;
    memset(target, 0, sizeof(*target));
    snprintf(target->app_id, sizeof(target->app_id), "%s", identity);
    target->permission_index = (uint8_t)permission_index;
    target->granted = granted ? 1 : 0;
    target->used = 1;
    return true;
}

static void pump_pxadb_control(void* context) {
    pxsys_pxadb_control_poll((pxsys_pxadb_control_t*)context);
}

static void* simulator_allocate(void* context, size_t size) {
    simulator_memory_t* memory = (simulator_memory_t*)context;
    void* allocation = malloc(size);
    if (allocation != NULL) memory->allocations++;
    return allocation;
}

static void simulator_release(void* context, void* allocation) {
    simulator_memory_t* memory = (simulator_memory_t*)context;
    if (allocation != NULL) memory->allocations--;
    free(allocation);
}

static pxsys_allocator_t simulator_allocator(simulator_memory_t* memory) {
    pxsys_allocator_t allocator = {0};
    allocator.struct_size = sizeof(allocator);
    allocator.context = memory;
    allocator.allocate = simulator_allocate;
    allocator.release = simulator_release;
    return allocator;
}

static void sleep_ms(uint32_t milliseconds) {
    struct timespec delay = {
        (time_t)(milliseconds / 1000u),
        (long)(milliseconds % 1000u) * 1000000L,
    };
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
    }
}

static int parse_u32(const char* value, uint32_t minimum, uint32_t maximum,
                     uint32_t* output) {
    char* end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (value[0] == '\0' || end == NULL || *end != '\0' || parsed < minimum ||
        parsed > maximum) {
        return 0;
    }
    *output = (uint32_t)parsed;
    return 1;
}

static int parse_shape_background(const char* value,
                                  simulator_shape_background_t* output) {
    if (strcmp(value, "black") == 0)
        *output = SIMULATOR_SHAPE_BACKGROUND_BLACK;
    else if (strcmp(value, "matte") == 0)
        *output = SIMULATOR_SHAPE_BACKGROUND_MATTE;
    else
        return 0;
    return 1;
}

static void draw_mask_rect(lv_layer_t* layer,
                           const lv_draw_rect_dsc_t* descriptor, int32_t x1,
                           int32_t y1, int32_t x2, int32_t y2) {
    lv_area_t area;
    if (layer == NULL || descriptor == NULL || x1 > x2 || y1 > y2) return;
    area.x1 = x1;
    area.y1 = y1;
    area.x2 = x2;
    area.y2 = y2;
    lv_draw_rect(layer, descriptor, &area);
}

static int32_t circle_extent(int32_t radius, int32_t delta) {
    int64_t square = (int64_t)radius * radius - (int64_t)delta * delta;
    return square > 0 ? (int32_t)sqrt((double)square) : 0;
}

static void display_shape_mask_draw(lv_event_t* event) {
    lv_layer_t* layer = lv_event_get_layer(event);
    lv_draw_rect_dsc_t descriptor;
    int32_t width = (int32_t)s_display_profile.width;
    int32_t height = (int32_t)s_display_profile.height;
    int32_t y;
    if (layer == NULL || width <= 0 || height <= 0) return;
    if (s_display_profile.shape != PXSYS_DISPLAY_SHAPE_ROUNDED_RECTANGLE &&
        s_display_profile.shape != PXSYS_DISPLAY_SHAPE_CIRCLE)
        return;

    lv_draw_rect_dsc_init(&descriptor);
    descriptor.bg_color = s_shape_background == SIMULATOR_SHAPE_BACKGROUND_BLACK
                              ? lv_color_black()
                              : lv_color_hex(UINT32_C(0x7a8494));
    descriptor.bg_opa = LV_OPA_COVER;

    if (s_display_profile.shape == PXSYS_DISPLAY_SHAPE_CIRCLE) {
        int32_t radius = width < height ? width / 2 : height / 2;
        int32_t center_x = width / 2;
        int32_t center_y = height / 2;
        for (y = 0; y < height; ++y) {
            int32_t delta = y - center_y;
            int32_t extent;
            int32_t left;
            int32_t right;
            if (delta < -radius || delta > radius) {
                draw_mask_rect(layer, &descriptor, 0, y, width - 1, y);
                continue;
            }
            extent = circle_extent(radius, delta);
            left = center_x - extent;
            right = center_x + extent;
            draw_mask_rect(layer, &descriptor, 0, y, left - 1, y);
            draw_mask_rect(layer, &descriptor, right + 1, y, width - 1, y);
        }
        return;
    }

#define DRAW_CORNER_MASK(radius_value, top_side, left_side)                 \
    do {                                                                      \
        int32_t radius = (int32_t)(radius_value);                             \
        int32_t row;                                                          \
        for (row = 0; row < radius; ++row) {                                  \
            int32_t draw_y = (top_side) ? row : height - row - 1;             \
            int32_t extent = circle_extent(radius, row - radius);             \
            int32_t masked = radius - extent;                                 \
            if (left_side)                                                     \
                draw_mask_rect(layer, &descriptor, 0, draw_y, masked - 1,     \
                               draw_y);                                        \
            else                                                               \
                draw_mask_rect(layer, &descriptor, width - masked, draw_y,    \
                               width - 1, draw_y);                             \
        }                                                                      \
    } while (0)
    DRAW_CORNER_MASK(s_display_profile.corner_radii.top_left, 1, 1);
    DRAW_CORNER_MASK(s_display_profile.corner_radii.top_right, 1, 0);
    DRAW_CORNER_MASK(s_display_profile.corner_radii.bottom_left, 0, 1);
    DRAW_CORNER_MASK(s_display_profile.corner_radii.bottom_right, 0, 0);
#undef DRAW_CORNER_MASK
}

static void display_shape_input_filter(lv_event_t* event) {
    lv_hit_test_info_t* hit_test = lv_event_get_hit_test_info(event);
    if (hit_test == NULL || hit_test->point == NULL) return;
    /* Consume only clicks outside the physical display shape. */
    hit_test->res = !pxsys_display_contains_point(&s_display_profile,
                                                  hit_test->point->x,
                                                  hit_test->point->y);
}

static void apply_display_shape_mask(
    lv_display_t* display, const pxsys_display_profile_t* profile,
    simulator_shape_background_t shape_background) {
    lv_obj_t* overlay;
    if (display == NULL || profile == NULL) return;
    s_display_profile = *profile;
    s_shape_background = shape_background;
    if (profile->shape != PXSYS_DISPLAY_SHAPE_ROUNDED_RECTANGLE &&
        profile->shape != PXSYS_DISPLAY_SHAPE_CIRCLE)
        return;
    overlay = lv_obj_create(lv_display_get_layer_sys(display));
    if (overlay == NULL) return;
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay,
                    LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_add_event_cb(overlay, display_shape_input_filter,
                        LV_EVENT_HIT_TEST, NULL);
    lv_obj_add_event_cb(overlay, display_shape_mask_draw, LV_EVENT_DRAW_MAIN,
                        NULL);
    lv_obj_move_foreground(overlay);
}

static int parse_options(int argc, char** argv, simulator_options_t* options) {
    int index;
    *options = (simulator_options_t){
        480, 320, 0, PXSYS_COLOR_SCHEME_LIGHT,
        PXSYS_NAVIGATION_BUTTONS, "en-US", NULL, NULL,
        PXSYS_DISPLAY_SHAPE_RECTANGLE, 0, SIMULATOR_SHAPE_BACKGROUND_MATTE,
        {0, 0, 0, 0}, 0, 0,
        9, 41, 82, 4, PXSYS_NETWORK_WIFI, 1, 0, 24u * 1024u,
        NULL, NULL, NULL, NULL, NULL,
    };
    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--dark") == 0) {
            options->scheme = PXSYS_COLOR_SCHEME_DARK;
        } else if (strcmp(argv[index], "--light") == 0) {
            options->scheme = PXSYS_COLOR_SCHEME_LIGHT;
        } else if (strcmp(argv[index], "--gestures") == 0) {
            options->navigation = PXSYS_NAVIGATION_GESTURES;
        } else if (strcmp(argv[index], "--smoke-test") == 0) {
            options->duration_ms = 300;
        } else if (strcmp(argv[index], "--self-test") == 0) {
            options->self_test = 1;
            options->duration_ms = 350;
        } else if (strcmp(argv[index], "--custom-theme") == 0) {
            options->custom_theme = 1;
        } else if (strcmp(argv[index], "--round") == 0) {
            options->shape = PXSYS_DISPLAY_SHAPE_CIRCLE;
            options->corner_radius = 0;
        } else if (strcmp(argv[index], "--corner-radius") == 0 &&
                   index + 1 < argc) {
            uint32_t radius;
            if (!parse_u32(argv[++index], 0, UINT16_MAX, &radius)) return 0;
            options->corner_radius = (uint16_t)radius;
            options->shape = radius == 0 ? PXSYS_DISPLAY_SHAPE_RECTANGLE
                                         : PXSYS_DISPLAY_SHAPE_ROUNDED_RECTANGLE;
        } else if (strcmp(argv[index], "--shape-background") == 0 &&
                   index + 1 < argc) {
            if (!parse_shape_background(argv[++index],
                                        &options->shape_background))
                return 0;
        } else if (strcmp(argv[index], "--profile") == 0 && index + 1 < argc) {
            const char* profile = argv[++index];
            if (strcmp(profile, "compact") == 0) {
                options->width = 296;
                options->height = 240;
            } else if (strcmp(profile, "phone") == 0) {
                options->width = 390;
                options->height = 844;
                options->navigation = PXSYS_NAVIGATION_GESTURES;
            } else if (strcmp(profile, "round") == 0) {
                options->width = 454;
                options->height = 454;
                options->shape = PXSYS_DISPLAY_SHAPE_CIRCLE;
                options->safe_insets = (pxsys_insets_t){67, 67, 67, 67};
                options->navigation = PXSYS_NAVIGATION_GESTURES;
            } else {
                return 0;
            }
        } else if (strcmp(argv[index], "--safe-insets") == 0 &&
                   index + 1 < argc) {
            unsigned top, right, bottom, left;
            char tail;
            if (sscanf(argv[++index], "%u,%u,%u,%u%c", &top, &right,
                       &bottom, &left, &tail) != 4 || top > UINT16_MAX ||
                right > UINT16_MAX || bottom > UINT16_MAX ||
                left > UINT16_MAX)
                return 0;
            options->safe_insets = (pxsys_insets_t){
                (uint16_t)top, (uint16_t)right, (uint16_t)bottom,
                (uint16_t)left};
        } else if (strcmp(argv[index], "--launch") == 0 &&
                   index + 1 < argc) {
            options->launch_app = argv[++index];
        } else if (strcmp(argv[index], "--screenshot") == 0 &&
                   index + 1 < argc) {
            options->screenshot = argv[++index];
        } else if (strcmp(argv[index], "--time") == 0 &&
                   index + 1 < argc) {
            unsigned hour, minute;
            char tail;
            if (sscanf(argv[++index], "%u:%u%c", &hour, &minute, &tail) != 2 ||
                hour > 23u || minute > 59u)
                return 0;
            options->hour = (uint8_t)hour;
            options->minute = (uint8_t)minute;
        } else if (strcmp(argv[index], "--battery") == 0 &&
                   index + 1 < argc) {
            uint32_t battery;
            if (!parse_u32(argv[++index], 0, 100, &battery)) return 0;
            options->battery = (uint8_t)battery;
        } else if (strcmp(argv[index], "--network") == 0 &&
                   index + 1 < argc) {
            const char* network = argv[++index];
            if (strcmp(network, "none") == 0)
                options->network = PXSYS_NETWORK_NONE;
            else if (strcmp(network, "wifi") == 0)
                options->network = PXSYS_NETWORK_WIFI;
            else if (strcmp(network, "cellular") == 0)
                options->network = PXSYS_NETWORK_CELLULAR;
            else if (strcmp(network, "ethernet") == 0)
                options->network = PXSYS_NETWORK_ETHERNET;
            else
                return 0;
        } else if (strcmp(argv[index], "--network-signal") == 0 &&
                   index + 1 < argc) {
            uint32_t signal;
            if (!parse_u32(argv[++index], 0, 4, &signal)) return 0;
            options->network_signal = (uint8_t)signal;
        } else if (strcmp(argv[index], "--permission") == 0 &&
                   index + 1 < argc) {
            const char* permission = argv[++index];
            if (strcmp(permission, "allow") == 0)
                options->permission_allowed = 1;
            else if (strcmp(permission, "deny") == 0)
                options->permission_allowed = 0;
            else
                return 0;
        } else if (strcmp(argv[index], "--storage-bytes") == 0 &&
                   index + 1 < argc) {
            if (!parse_u32(argv[++index], 0, UINT32_MAX,
                           &options->storage_bytes))
                return 0;
        } else if (strcmp(argv[index], "--installed-packages-root") == 0 &&
                   index + 1 < argc) {
            options->installed_packages_root = argv[++index];
        } else if (strcmp(argv[index], "--product-runner") == 0 &&
                   index + 1 < argc) {
            options->product_runner = argv[++index];
        } else if (strcmp(argv[index], "--publisher-key") == 0 &&
                   index + 1 < argc) {
            options->publisher_key = argv[++index];
        } else if (strcmp(argv[index], "--state-root") == 0 &&
                   index + 1 < argc) {
            options->state_root = argv[++index];
        } else if (strcmp(argv[index], "--pxadb-control-socket") == 0 &&
                   index + 1 < argc) {
            options->pxadb_control_socket = argv[++index];
        } else if (strcmp(argv[index], "--locale") == 0 && index + 1 < argc) {
            options->locale = argv[++index];
        } else if (strcmp(argv[index], "--width") == 0 && index + 1 < argc) {
            if (!parse_u32(argv[++index], 240, 4096, &options->width)) return 0;
        } else if (strcmp(argv[index], "--height") == 0 && index + 1 < argc) {
            if (!parse_u32(argv[++index], 240, 4096, &options->height)) return 0;
        } else if (strcmp(argv[index], "--duration-ms") == 0 && index + 1 < argc) {
            if (!parse_u32(argv[++index], 1, 3600000, &options->duration_ms))
                return 0;
        } else if (strcmp(argv[index], "--locked") == 0) {
            options->locked = 1;
        } else {
            return 0;
        }
    }
    return 1;
}

static void print_usage(const char* program) {
    fprintf(stderr,
            "Usage: %s [--light|--dark] [--gestures] [--locale TAG] "
            "[--profile compact|phone|round] [--width PX] [--height PX] "
            "[--round|--corner-radius PX] [--shape-background matte|black] "
            "[--safe-insets T,R,B,L] [--custom-theme] "
            "[--time HH:MM] [--battery 0..100] "
            "[--network none|wifi|cellular|ethernet] [--network-signal 0..4] "
            "[--permission allow|deny] [--storage-bytes N] "
            "[--installed-packages-root DIR --product-runner PATH "
            "--publisher-key DER --state-root DIR] "
            "[--launch APP_ID] [--locked] [--screenshot PNG] [--duration-ms MS] "
            "[--smoke-test|--self-test]\n",
            program);
}

static int save_png(lv_display_t* display, const char* path) {
    lv_draw_buf_t* draw_buffer;
    png_structp png = NULL;
    png_infop info = NULL;
    FILE* file = NULL;
    uint8_t* pixels = NULL;
    png_bytep* rows = NULL;
    int width;
    int height;
    uint32_t source_stride;
    int x;
    int y;
    int ok = 0;
    if (display == NULL || path == NULL) return 0;
    lv_refr_now(display);
    draw_buffer = lv_display_get_buf_active(display);
    width = lv_display_get_horizontal_resolution(display);
    height = lv_display_get_vertical_resolution(display);
    source_stride = lv_draw_buf_width_to_stride(
        (uint32_t)width, lv_display_get_color_format(display));
    if (draw_buffer == NULL || draw_buffer->data == NULL || width <= 0 ||
        height <= 0 ||
        draw_buffer->data_size < (size_t)source_stride * (size_t)height)
        return 0;
    pixels = malloc((size_t)width * (size_t)height * 4u);
    rows = malloc((size_t)height * sizeof(*rows));
    if (pixels == NULL || rows == NULL) goto done;
    if (SDL_ConvertPixels(width, height, SDL_PIXELFORMAT_RGB888,
                          draw_buffer->data, (int)source_stride,
                          SDL_PIXELFORMAT_RGBA32, pixels, width * 4) != 0)
        goto done;
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            uint8_t* pixel = pixels + ((size_t)y * (size_t)width + (size_t)x) * 4u;
            if (!pxsys_display_contains_point(&s_display_profile, x, y)) {
                pixel[0] = 0;
                pixel[1] = 0;
                pixel[2] = 0;
                pixel[3] = 0;
            }
        }
    }
    file = fopen(path, "wb");
    if (file == NULL) goto done;
    png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png == NULL) goto done;
    info = png_create_info_struct(png);
    if (info == NULL || setjmp(png_jmpbuf(png))) goto done;
    png_init_io(png, file);
    png_set_IHDR(png, info, (png_uint_32)width, (png_uint_32)height, 8,
                 PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (y = 0; y < height; ++y)
        rows[y] = pixels + (size_t)y * (size_t)width * 4u;
    png_write_image(png, rows);
    png_write_end(png, NULL);
    ok = 1;
done:
    if (png != NULL)
        png_destroy_write_struct(&png, info == NULL ? NULL : &info);
    if (file != NULL) fclose(file);
    free(rows);
    free(pixels);
    return ok;
}

static pxsys_status_t launch_app(pxsys_standard_system_t* system,
                                 const char* app_id) {
    pxsys_app_identity_t identity = {0};
    pxsys_intent_t intent = {0};
    pxsys_instance_ref_t instance;
    pxsys_app_registry_t* apps;
    pxsys_status_t status;
    size_t index;
    if (system == NULL || app_id == NULL) return PXSYS_STATUS_INVALID_ARGUMENT;
    apps = pxsys_standard_system_apps(system);
    for (index = 0; index < pxsys_app_registry_count(apps); ++index) {
        const pxsys_app_descriptor_t* app = pxsys_app_registry_at(apps, index);
        if (app != NULL && (app->flags & PXSYS_APP_FLAG_LAUNCHER) != 0 &&
            app->identity.app_id.size == strlen(app_id) &&
            memcmp(app->identity.app_id.data, app_id,
                   app->identity.app_id.size) == 0) {
            identity = app->identity;
            break;
        }
    }
    if (identity.app_id.data == NULL) {
        fprintf(stderr, "PXA simulator: launcher app is not registered: %s\n",
                app_id);
        return PXSYS_STATUS_NOT_FOUND;
    }
    intent.struct_size = sizeof(intent);
    intent.target = &identity;
    intent.action = pxsys_string_from_cstr("system.intent.main");
    intent.flags = PXSYS_INTENT_FLAG_CLEAR_TOP;
    status = pxsys_task_manager_start(pxsys_standard_system_tasks(system),
                                      &intent, &instance);
    if (status != PXSYS_STATUS_OK)
        fprintf(stderr, "PXA simulator: launch status=%d for %s\n",
                (int)status, app_id);
    return status;
}

static int run_simulator(const simulator_options_t* options) {
    simulator_memory_t memory = {0};
    pxsys_allocator_t allocator = simulator_allocator(&memory);
    pxsys_lvgl_renderer_config_t renderer_config;
    pxsys_lvgl_renderer_t* renderer = NULL;
    pxsys_renderer_provider_t renderer_provider;
    pxsys_standard_system_config_t system_config;
    pxsys_standard_system_t* system = NULL;
    pxsys_desktop_runtime_t* simulator_runtime = NULL;
    pxsys_runtime_provider_t runtime_provider;
    pxsys_desktop_runtime_fixture_t runtime_fixture;
    simulator_icon_resolver_t icon_resolver = {0};
    simulator_catalog_t catalog = {0};
    simulator_device_info_t device_info;
    simulator_wifi_t simulated_wifi = {0};
    pxsys_reference_lvgl_config_t ui_config;
    pxsys_reference_lvgl_t* ui = NULL;
    lv_display_t* display = NULL;
    lv_indev_t* mouse = NULL;
    lv_indev_t* keyboard = NULL;
    simulator_power_state_t power_state = {0};
    pxsys_pxadb_control_t pxadb_control = {.listener = -1};
    uint32_t started;
    uint8_t publisher_root[PXSYS_PUBLISHER_ROOT_BYTES];
    int result = 1;
    uint8_t catalog_locale_subscribed = 0;
    uint8_t power_event_watch_added = 0;

    memset(publisher_root, 0x52, sizeof(publisher_root));

    lv_init();
    if (!create_ui_fonts()) goto done;
    display = lv_sdl_window_create((int32_t)options->width,
                                   (int32_t)options->height);
    if (display == NULL) goto done;
    if (options->shape == PXSYS_DISPLAY_SHAPE_CIRCLE ||
        options->shape == PXSYS_DISPLAY_SHAPE_ROUNDED_RECTANGLE) {
        uint16_t radius = options->shape == PXSYS_DISPLAY_SHAPE_CIRCLE
                              ? LV_RADIUS_CIRCLE
                              : options->corner_radius;
        lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
        lv_obj_set_style_radius(lv_screen_active(), radius, 0);
        lv_obj_set_style_clip_corner(lv_screen_active(), true, 0);
    }
    lv_sdl_window_set_title(display, "PXA System Standard UI");
    lv_sdl_window_set_resizeable(display, true);
    mouse = lv_sdl_mouse_create();
    keyboard = lv_sdl_keyboard_create();
    if (mouse == NULL || keyboard == NULL) goto done;
    lv_indev_set_display(mouse, display);
    lv_indev_set_display(keyboard, display);

    pxsys_lvgl_renderer_config_init(&renderer_config);
    renderer_config.parent = lv_screen_active();
    renderer_config.allocator = allocator;
    if (pxsys_lvgl_renderer_create(&renderer_config, &renderer) !=
        PXSYS_STATUS_OK) goto done;
    if (pxsys_lvgl_renderer_provider(renderer, &renderer_provider) !=
        PXSYS_STATUS_OK) goto done;

    pxsys_standard_system_config_init(&system_config);
    system_config.allocator = allocator;
    system_config.initial_renderer = &renderer_provider;
    pxsys_display_profile_init(&system_config.initial_display,
                               options->width, options->height);
    system_config.initial_display.shape = options->shape;
    system_config.initial_display.safe_insets = options->safe_insets;
    if (options->shape == PXSYS_DISPLAY_SHAPE_CIRCLE ||
        options->shape == PXSYS_DISPLAY_SHAPE_ROUNDED_RECTANGLE) {
        uint16_t radius = options->shape == PXSYS_DISPLAY_SHAPE_CIRCLE
                              ? (uint16_t)((options->width < options->height
                                                ? options->width : options->height) / 2u)
                              : options->corner_radius;
        system_config.initial_display.corner_radii =
            (pxsys_corner_radii_t){radius, radius, radius, radius};
    }
    if (pxsys_display_profile_validate(&system_config.initial_display) !=
        PXSYS_STATUS_OK)
        goto done;
    apply_display_shape_mask(display, &system_config.initial_display,
                             options->shape_background);
    if (options->custom_theme) {
        if (pxsys_theme_snapshot_init_custom(
                &system_config.initial_theme,
                pxsys_string_from_cstr("simulator-mint"), options->scheme) !=
            PXSYS_STATUS_OK)
            goto done;
        system_config.initial_theme.colors[PXSYS_COLOR_BACKGROUND] =
            UINT32_C(0xfff3f7f5);
        system_config.initial_theme.colors[PXSYS_COLOR_SURFACE] =
            UINT32_C(0xffffffff);
        system_config.initial_theme.colors[PXSYS_COLOR_TEXT_PRIMARY] =
            UINT32_C(0xff15251f);
        system_config.initial_theme.colors[PXSYS_COLOR_ACCENT] =
            UINT32_C(0xff087f5b);
        system_config.initial_theme.colors[PXSYS_COLOR_ON_ACCENT] =
            UINT32_C(0xffffffff);
    } else {
        pxsys_theme_snapshot_init(&system_config.initial_theme,
                                  options->scheme);
    }
    if (pxsys_locale_snapshot_init(
            &system_config.initial_locale,
            pxsys_string_from_cstr(options->locale)) != PXSYS_STATUS_OK) goto done;
    pxsys_system_status_snapshot_init(&system_config.initial_system_status);
    system_config.initial_system_status.time_valid = 1;
    system_config.initial_system_status.hour = options->hour;
    system_config.initial_system_status.minute = options->minute;
    system_config.initial_system_status.battery_valid = 1;
    system_config.initial_system_status.battery_percent = options->battery;
    system_config.initial_system_status.network_connected =
        options->network != PXSYS_NETWORK_NONE;
    system_config.initial_system_status.network_signal_level =
        options->network_signal;
    system_config.initial_system_status.network_type = options->network;
    system_config.initial_system_status.wifi_supported = 1;
    system_config.initial_system_status.wifi_enabled =
        options->network == PXSYS_NETWORK_WIFI;
    system_config.initial_system_status.wifi_connected =
        options->network == PXSYS_NETWORK_WIFI;
    system_config.initial_system_status.wifi_signal_level =
        options->network == PXSYS_NETWORK_WIFI ? options->network_signal : 0;
    system_config.initial_system_status.cellular_supported = 1;
    system_config.initial_system_status.cellular_enabled =
        options->network == PXSYS_NETWORK_CELLULAR;
    system_config.initial_system_status.cellular_connected =
        options->network == PXSYS_NETWORK_CELLULAR;
    system_config.initial_system_status.cellular_signal_level =
        options->network == PXSYS_NETWORK_CELLULAR ? options->network_signal : 0;
    system_config.initial_system_status.volume_supported = 1;
    system_config.initial_system_status.volume_percent = 64;
    system_config.initial_system_status.brightness_supported = 1;
    system_config.initial_system_status.brightness_percent = 72;
    system_config.initial_system_status.bluetooth_supported = 1;
    if (pxsys_standard_system_create(&system_config, &system) !=
        PXSYS_STATUS_OK) goto done;

    runtime_fixture.permission_allowed = options->permission_allowed;
    runtime_fixture.storage_bytes = options->storage_bytes;
    runtime_fixture.installed_packages_root = options->installed_packages_root;
    runtime_fixture.publisher_key = options->publisher_key;
    runtime_fixture.state_root = options->state_root;
    runtime_fixture.navigation_gestures =
        options->navigation == PXSYS_NAVIGATION_GESTURES;
    runtime_fixture.pump_context = &pxadb_control;
    runtime_fixture.pump = pump_pxadb_control;
    icon_resolver.installed_packages_root = options->installed_packages_root;
    catalog.system = system;
    catalog.installed_packages_root = options->installed_packages_root;
    device_info.width = options->width;
    device_info.height = options->height;
    device_info.storage_bytes = options->storage_bytes;
    device_info.round = options->shape == PXSYS_DISPLAY_SHAPE_CIRCLE ? 1 : 0;
    if (pxsys_desktop_runtime_create(system, renderer, &runtime_fixture, allocator,
                                     &simulator_runtime) != PXSYS_STATUS_OK)
        goto done;
    if (pxsys_desktop_runtime_provider(simulator_runtime,
                                        &runtime_provider) != PXSYS_STATUS_OK ||
        pxsys_runtime_register_provider(pxsys_standard_system_runtime(system),
                                        &runtime_provider) != PXSYS_STATUS_OK)
        goto done;
    if (pxsys_locale_service_subscribe(pxsys_standard_system_locale(system),
                                       &catalog, catalog_locale_changed) !=
        PXSYS_STATUS_OK)
        goto done;
    catalog_locale_subscribed = 1;
    if (sync_installed_catalog(&catalog) != PXSYS_STATUS_OK)
        goto done;

    pxsys_reference_lvgl_config_init(&ui_config);
    ui_config.system = system;
    simulated_wifi.connected = options->network == PXSYS_NETWORK_WIFI;
    snprintf(simulated_wifi.ssid, sizeof(simulated_wifi.ssid), "PXA Simulator");
    ui_config.wifi_context = &simulated_wifi;
    ui_config.wifi_scan = simulator_wifi_scan;
    ui_config.wifi_connect = simulator_wifi_connect;
    ui_config.wifi_current = simulator_wifi_current;
    /* The standard system UI lives on the top layer, matching the product
     * integration viewport: application surfaces stay below the system chrome
     * so the status bar, navigation gestures and recents keep working while a
     * Guest application is foreground. */
    ui_config.parent = lv_display_get_layer_top(display);
    ui_config.text_font = simulator_ui_fonts.body;
    ui_config.title_font = simulator_ui_fonts.headline;
    ui_config.fonts[PXSYS_TYPOGRAPHY_DISPLAY] = simulator_ui_fonts.display;
    ui_config.fonts[PXSYS_TYPOGRAPHY_HEADLINE] = ui_config.title_font;
    ui_config.fonts[PXSYS_TYPOGRAPHY_TITLE] = simulator_ui_fonts.title;
    ui_config.fonts[PXSYS_TYPOGRAPHY_BODY] = ui_config.text_font;
    ui_config.fonts[PXSYS_TYPOGRAPHY_LABEL] = simulator_ui_fonts.label;
    ui_config.fonts[PXSYS_TYPOGRAPHY_CAPTION] = simulator_ui_fonts.caption;
    ui_config.allocator = allocator;
    ui_config.navigation_mode = options->navigation;
    ui_config.app_icon_context = &icon_resolver;
    ui_config.resolve_app_icon = resolve_launcher_icon;
    ui_config.device_info_context = &device_info;
    ui_config.device_info = simulator_device_info;
    ui_config.memory_info = simulator_memory_info;
    ui_config.app_manager_context = &catalog;
    ui_config.app_list = simulator_list_apps;
    ui_config.app_action = simulator_app_action;
    ui_config.launcher_order_context = (void*)options->state_root;
    ui_config.launcher_order_load = simulator_launcher_load;
    ui_config.launcher_order_save = simulator_launcher_save;
    ui_config.app_permission_context = &catalog;
    ui_config.app_permission_list = simulator_list_app_permissions;
    ui_config.app_permission_set = simulator_set_app_permission;
    memcpy(ui_config.publisher_root, publisher_root,
           sizeof(ui_config.publisher_root));
    if (pxsys_reference_lvgl_create(&ui_config, &ui) != PXSYS_STATUS_OK)
        goto done;
    if (pxsys_reference_lvgl_start(ui) != PXSYS_STATUS_OK) goto done;
    catalog.ui = ui;
    if (options->locked &&
        pxsys_reference_lvgl_set_locked(ui, true) != PXSYS_STATUS_OK)
        goto done;
    power_state.ui = ui;
    power_state.window_id = SDL_GetWindowID(lv_sdl_window_get_window(display));
    (void)pxsys_reference_lvgl_set_power_action_callback(
        ui, &power_state, simulator_power_action);
    SDL_AddEventWatch(simulator_power_event_watch, &power_state);
    power_event_watch_added = 1;

    if (options->pxadb_control_socket != NULL &&
        !pxsys_pxadb_control_start(&pxadb_control,
                                   options->pxadb_control_socket, display,
                                   &catalog, refresh_installed_catalog))
        goto done;

    if (options->launch_app != NULL &&
        launch_app(system, options->launch_app) !=
            PXSYS_STATUS_OK) {
        fprintf(stderr, "PXA simulator: cannot launch %s\n",
                options->launch_app);
        goto done;
    }
    if (options->self_test) {
        pxsys_theme_snapshot_t changed;
        pxsys_locale_snapshot_t locale;
        pxsys_theme_snapshot_init(
            &changed, options->scheme == PXSYS_COLOR_SCHEME_DARK
                          ? PXSYS_COLOR_SCHEME_LIGHT
                          : PXSYS_COLOR_SCHEME_DARK);
        if (pxsys_theme_service_update(pxsys_standard_system_theme(system),
                                       &changed) != PXSYS_STATUS_OK ||
            pxsys_locale_snapshot_init(&locale,
                                       pxsys_string_from_cstr("zh-CN")) !=
                PXSYS_STATUS_OK ||
            pxsys_locale_service_update(pxsys_standard_system_locale(system),
                                        &locale) != PXSYS_STATUS_OK)
            goto done;
    }
    started = lv_tick_get();
    while (lv_display_get_default() != NULL &&
           !power_state.quit_requested &&
           (options->duration_ms == 0 ||
            lv_tick_elaps(started) < options->duration_ms)) {
        pxsys_desktop_runtime_poll(simulator_runtime);
        pxsys_pxadb_control_poll(&pxadb_control);
        uint32_t delay = lv_timer_handler();
        simulator_power_poll(&power_state);
        if (delay < 1) delay = 1;
        if (delay > 16) delay = 16;
        sleep_ms(delay);
    }
    if (options->screenshot != NULL &&
        !save_png(display, options->screenshot)) {
        fprintf(stderr, "PXA simulator: cannot write screenshot %s\n",
                options->screenshot);
        goto done;
    }
    if (lv_display_get_default() == NULL) {
        /* SDL has already destroyed the display and every LVGL object below
         * it. Do not run App-stop or UI-destroy callbacks against those stale
         * objects; this desktop process is terminating immediately. */
        return 0;
    }
    result = 0;

done:
    if (power_event_watch_added)
        SDL_DelEventWatch(simulator_power_event_watch, &power_state);
    pxsys_pxadb_control_stop(&pxadb_control);
    if (power_state.screen_off_overlay != NULL &&
        lv_display_get_default() != NULL)
        lv_obj_delete(power_state.screen_off_overlay);
    if (catalog_locale_subscribed)
        (void)pxsys_locale_service_unsubscribe(
            pxsys_standard_system_locale(system), &catalog,
            catalog_locale_changed);
    if (system != NULL)
        (void)pxsys_task_manager_finish_all(
            pxsys_standard_system_tasks(system), PXSYS_STOP_SHUTDOWN);
    if (ui != NULL && pxsys_reference_lvgl_destroy(ui) != PXSYS_STATUS_OK)
        result = 1;
    if (system != NULL && simulator_runtime != NULL) {
        (void)pxsys_runtime_unregister_provider(
            pxsys_standard_system_runtime(system),
            pxsys_string_from_cstr("pxa-sim"));
        if (pxsys_desktop_runtime_destroy(simulator_runtime) !=
            PXSYS_STATUS_OK)
            result = 1;
    }
    if (system != NULL && pxsys_standard_system_destroy(system) != PXSYS_STATUS_OK)
        result = 1;
    if (renderer != NULL && pxsys_lvgl_renderer_destroy(renderer) != PXSYS_STATUS_OK)
        result = 1;
    if (display != NULL && lv_display_get_default() != NULL)
        lv_display_delete(display);
    destroy_ui_fonts();
    lv_deinit();
    if (memory.allocations != 0) {
        fprintf(stderr, "PXA simulator leaked %zu owned allocations\n",
                memory.allocations);
        result = 1;
    }
    return result;
}

int main(int argc, char** argv) {
    simulator_options_t options;
    if (!parse_options(argc, argv, &options)) {
        print_usage(argv[0]);
        return 2;
    }
    return run_simulator(&options);
}
