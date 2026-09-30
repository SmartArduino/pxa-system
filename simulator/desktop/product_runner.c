#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <limits.h>
#include <math.h>

#include <SDL2/SDL.h>
#include <vorbis/vorbisfile.h>
#include <opus/opusfile.h>

#include "lvgl.h"
#include "pxa/activation.h"
#include "pxa/audio.h"
#include "pxa/audio_mixer.h"
#include "pxa/clock.h"
#include "pxa/game_render.h"
#include "pxa/assets.h"
#include "pxa/asset_stream.h"
#include "pxa/posix/pxa_posix_asset.h"
#include "pxa/posix/pxa_posix_asset_worker.h"
#include "pxa/raster_assets.h"
#include "pxa/resource_budget.h"
#include "pxa/audio_buffer.h"
#include "pxa/log.h"
#include "pxa/lvgl/pxa_lvgl_ui.h"
#include "pxa/openssl/pxa_openssl.h"
#include "pxa/package.h"
#include "pxa/permission.h"
#include "pxa/posix/pxa_posix_installer.h"
#include "pxa/posix/pxa_posix_storage.h"
#include "pxa/posix/pxa_posix_fs.h"
#include "pxa/fs.h"
#include "pxa/ipc.h"
#include "pxa/runtime.h"
#include "pxa/scheduler.h"
#include "pxa/service.h"
#include "pxa/storage.h"
#include "pxa/surface.h"
#include "pxa/ui.h"
#include "pxa/wasi.h"
#include "pxa/wamr/pxa_wamr_engine.h"
#include "pxa/wire.h"
#include "pxa/window.h"
#include "pxadb_control.h"
#include "product_runner.h"
#include "src/core/lv_obj_event_private.h"
#include "include/lvgl/drivers/sdl/lv_sdl_keyboard.h"
#include "desktop_net.h"
#include "pxa/device.h"
#include "pxa/sensor.h"
#include "src/misc/cache/instance/lv_image_cache.h"

#define PRODUCT_CLOCK_SERVICE PXA_CLOCK_SERVICE_ID
#define PRODUCT_CLOCK_NOW PXA_CLOCK_NOW
#define PRODUCT_CLOCK_TICK PXA_CLOCK_TICK
#define PRODUCT_CLOCK_NOW_RESULT PXA_CLOCK_NOW_RESULT
#define PRODUCT_LIFECYCLE_SERVICE UINT16_C(17)
#define PRODUCT_LIFECYCLE_EVENT UINT16_C(0x8005)
#define PRODUCT_COMPONENTS UINT16_C(8)
#define PRODUCT_SYSTEM_CONFIG_ENVIRONMENT UINT16_C(12)
#define PRODUCT_SYSTEM_CONFIGURATION_LOCALE UINT16_C(1)
#define PRODUCT_LOCALE_MAX_BYTES 63u
#define PRODUCT_SYSTEM_GESTURE_EDGE_WIDTH 16
#define PRODUCT_SYSTEM_GESTURE_HOME_HEIGHT 20
#define PRODUCT_SYSTEM_GESTURE_COMMIT_DISTANCE 32
#define PRODUCT_SURFACE_FLAG_GAME_RENDER UINT8_C(8)
#define PRODUCT_SURFACE_FLAG_RASTER_NO_SCRATCH UINT8_C(16)
#define PRODUCT_SURFACE_FLAG_RASTER_COVERAGE UINT8_C(32)
#define PRODUCT_ASSET_MAX_BYTES (4u * 1024u * 1024u)
#define PRODUCT_ASSET_MAX_DIMENSION 4096u
#define PRODUCT_AUDIO_SOUND_CACHE 8u
#define PRODUCT_AUDIO_SOUND_VOICES 6u

typedef struct {
    const char *package_path;
    const char *publisher_key;
    const char *state_root;
    const char *locale;
    const char *pxadb_control_socket;
    uint32_t width;
    uint32_t height;
    uint32_t safe_insets[4];
    uint32_t display_shape;
    uint32_t corner_radius;
    uint8_t shape_background_matte;
} options_t;

static options_t s_shape_profile;

typedef struct {
    lv_image_dsc_t descriptor;
    lv_color32_t *pixels;
} shape_mask_image_t;

static int shape_contains(int32_t x, int32_t y) {
    const int32_t width = (int32_t)s_shape_profile.width;
    const int32_t height = (int32_t)s_shape_profile.height;
    const int32_t radius = (int32_t)s_shape_profile.corner_radius;
    if (x < 0 || y < 0 || x >= width || y >= height) return 0;
    if (s_shape_profile.display_shape == 2u) {
        const int32_t diameter = width < height ? width : height;
        const int64_t dx = 2 * (int64_t)x + 1 - width;
        const int64_t dy = 2 * (int64_t)y + 1 - height;
        return dx * dx + dy * dy <= (int64_t)diameter * diameter;
    }
    if (s_shape_profile.display_shape == 1u && radius > 0) {
        const int32_t center_x = x < radius ? radius :
                                 x >= width - radius ? width - radius - 1 : -1;
        const int32_t center_y = y < radius ? radius :
                                 y >= height - radius ? height - radius - 1 : -1;
        if (center_x >= 0 && center_y >= 0) {
            const int64_t dx = x - center_x;
            const int64_t dy = y - center_y;
            return dx * dx + dy * dy <= (int64_t)radius * radius;
        }
    }
    return 1;
}

static void shape_mask_hit_test(lv_event_t *event) {
    lv_hit_test_info_t *hit_test = lv_event_get_hit_test_info(event);
    if (hit_test == NULL || hit_test->point == NULL) return;
    hit_test->res = !shape_contains(hit_test->point->x, hit_test->point->y);
}

static int install_shape_mask(lv_display_t *display,
                              const options_t *options,
                              shape_mask_image_t *mask) {
    lv_obj_t *image;
    lv_obj_t *overlay;
    size_t pixel_count;
    if (display == NULL || options == NULL || mask == NULL) return 0;
    if (options->display_shape == 0u) return 1;
    if (options->width == 0 || options->height == 0 ||
        (size_t)options->width > SIZE_MAX / options->height)
        return 0;
    pixel_count = (size_t)options->width * options->height;
    if (pixel_count > SIZE_MAX / sizeof(lv_color32_t) ||
        pixel_count > UINT32_MAX / sizeof(lv_color32_t))
        return 0;
    s_shape_profile = *options;
    mask->pixels = malloc(pixel_count * sizeof(lv_color32_t));
    if (mask->pixels == NULL) return 0;
    for (uint32_t y = 0; y < options->height; ++y) {
        for (uint32_t x = 0; x < options->width; ++x) {
            const uint8_t alpha = shape_contains((int32_t)x, (int32_t)y)
                                      ? 0 : 255;
            mask->pixels[(size_t)y * options->width + x] =
                options->shape_background_matte
                    ? lv_color32_make(0x7a, 0x84, 0x94, alpha)
                    : lv_color32_make(0, 0, 0, alpha);
        }
    }
    mask->descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    mask->descriptor.header.cf = LV_COLOR_FORMAT_ARGB8888;
    mask->descriptor.header.w = options->width;
    mask->descriptor.header.h = options->height;
    mask->descriptor.header.stride = options->width * sizeof(lv_color32_t);
    mask->descriptor.data_size = pixel_count * sizeof(lv_color32_t);
    mask->descriptor.data = (const uint8_t *)mask->pixels;
    image = lv_image_create(lv_display_get_layer_sys(display));
    if (image == NULL) return 0;
    lv_image_set_src(image, &mask->descriptor);
    lv_obj_set_clickable(image, false);
    overlay = lv_obj_create(lv_display_get_layer_sys(display));
    if (overlay == NULL) return 0;
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_set_scrollable(overlay, false);
    lv_obj_set_clickable(overlay, true);
    lv_obj_set_adv_hittest(overlay, true);
    lv_obj_add_event_cb(overlay, shape_mask_hit_test, LV_EVENT_HIT_TEST, NULL);
    lv_obj_move_foreground(overlay);
    return 1;
}

typedef struct {
    pxa_asset_object_t *asset;
    const uint8_t *pcm;
    uint32_t samples;
    uint32_t position;
    int32_t gain_q15;
    uint64_t session;
    uint8_t paused;
} product_sound_voice_t;

typedef struct {
    lv_display_t *display;
    lv_obj_t *content_parent;
    pxa_runtime_t *runtime;
    pxa_assets_service_t *assets;
    pxa_posix_asset_worker_t *asset_worker;
    pxa_posix_storage_gate_t *storage_gate;
    SDL_atomic_t asset_ready;
    pxa_memory_budget_t resource_budget;
    pxa_memory_owner_t resource_owner;
    pxa_memory_allocator_t resource_allocators[PXA_MEMORY_CLASSES][PXA_MEMORY_KINDS];
    SDL_mutex *resource_mutex;
    pxa_window_service_t *window;
    pxsys_product_simulator_window_fn window_changed;
    void *window_context;
    const pxsys_theme_snapshot_t *host_theme;
    uint64_t theme_generation;
    pxsys_insets_t host_bar_insets;
    pxa_ui_service_t *ui;
    pxa_ui_backend_t ui_backend;
    pxa_lvgl_ui_t *ui_adapter;
    pxa_audio_service_t *audio;
    SDL_AudioDeviceID audio_device;
    uint8_t focused;
    uint8_t pending_lifecycle;
    pxa_audio_mixer_t audio_mixer;
    uint64_t audio_sessions[PXA_AUDIO_MIXER_VOICES];
    uint64_t audio_next_session;
    uint64_t music_session;
    int16_t music_ring[8192];
    uint32_t music_read, music_count;
    pxa_audio_buffer_t music_buffer;
    uint64_t music_accepted_us, music_ready_max_us, music_decode_us, music_decode_calls, music_decode_max_us;
    uint64_t music_token;
    pxa_audio_playback_queue_t music_events;
    pxa_status_t music_error;
    uint8_t music_pending, music_finished;
    pxa_asset_block_map_t music_map;
    uint64_t music_read_bytes, music_read_calls;
    SDL_Thread *music_thread;
    SDL_atomic_t music_stop;
    int16_t music_gain_db_q8;
    int32_t music_gain_q15;
    int32_t music_output_gain_q15;
    int32_t music_source_gain_q15;
    int16_t music_last_sample;
    uint8_t music_loop;
    uint8_t music_paused;
    product_sound_voice_t sound_voices[PRODUCT_AUDIO_SOUND_VOICES];
    pxa_permission_service_t *permissions;
    pxa_surface_service_t *surfaces;
    pxa_game_render_service_t *game_render;
    pxa_device_service_t *device;
    pxa_sensor_service_t *sensor;
    int32_t sensor_temperature_milli_celsius;
    pxa_net_service_t *net;
    pxa_log_service_t *log;
    volatile uint8_t net_completion_ready;
    lv_obj_t *surface_image;
    lv_image_dsc_t surface_bitmap;
    uint8_t *surface_buffers;
    uint8_t *surface_display_buffer;
    uint32_t surface_frame_bytes;
    uint32_t surface_stride_bytes;
    uint32_t surface_display_frame_bytes;
    uint32_t surface_display_stride_bytes;
    uint16_t surface_width;
    uint16_t surface_height;
    uint16_t surface_display_width;
    uint16_t surface_display_height;
    uint8_t surface_buffer_count;
    uint8_t surface_scale;
    uint8_t surface_flags;
    uint8_t raster_scratch_mode;
    uint32_t raster_max_draw_bytes;
    uint8_t surface_registered;
    int8_t surface_writing_buffer;
    int8_t surface_pending_buffer;
    uint64_t surface_pending_frame_id;
    uint64_t surface_last_frame_id;
    uint64_t surface_submitted_frames;
    uint64_t surface_presented_frames;
    uint64_t surface_dropped_frames;
    uint64_t surface_replaced_frames;
    uint64_t surface_released_frames;
    uint8_t surface_release_head;
    uint8_t surface_release_count;
    uint8_t surface_release_pending_mask;
    pxa_surface_release_t surface_releases[3];
    pxa_surface_layer_t surface_layer;
    uint8_t *raster_buffers[2];
    uint16_t *raster_depth_buffer;
    uint8_t *raster_draw_lists[2];
    uint32_t raster_draw_sizes[2];
    int8_t raster_current_buffer;
    int8_t raster_draw_pending;
    uint64_t raster_last_frame_id;
    pxa_raster_bindings_t raster_bindings;
    pxa_raster_bindings_t raster_frame_bindings[2];
    pxa_raster_draw_list_view_t raster_draw_views[2];
    pxa_raster_telemetry_t raster_telemetry;
    pxa_wamr_engine_t *engine;
    pxa_component_engine_t engine_ops;
    pxa_activation_coordinator_t *coordinator;
    const pxa_package_manifest_t *manifest;
    pxa_ipc_broker_t *ipc;
    pxa_scheduler_service_t *scheduler;
    pxa_scheduler_entry_t queued_work[8];
    size_t queued_work_count;
    pxa_scheduler_entry_t running_work;
    pxa_component_t running_work_component;
    uint64_t running_work_deadline_ms;
    uint64_t running_work_grace_ms;
    uint64_t running_work_finish_at_ms;
    uint8_t running_work_starting;
    uint8_t running_work_stop_posted;
    uint8_t running_work_finished;
    uint8_t running_work_cancelled;
    pxa_component_t active_component;
    pxa_component_t service_components[PRODUCT_COMPONENTS];
    uint16_t service_component_count;
    uint64_t next_instance_id;
    char package_root[1024];
    char locale[PRODUCT_LOCALE_MAX_BYTES + 1u];
    lv_font_t *body_font;
    lv_font_t *title_font;
    lv_font_t *caption_font;
    lv_font_t *label_font;
    lv_font_t *headline_font;
    lv_font_t *display_font;
    uint32_t width;
    uint32_t height;
    uint32_t safe_insets[4];
    uint32_t display_shape;
    uint32_t corner_radius;
    uint32_t corner_radii[4];
    uint16_t clock_period_ms;
    uint64_t next_clock_tick_us;
    lv_obj_t *system_back_gesture;
    lv_obj_t *system_home_gesture;
    lv_obj_t *system_back_indicator;
    lv_obj_t *toast;
    lv_timer_t *toast_timer;
    uint8_t navigation_mode;
    int32_t system_gesture_press_x;
    int32_t system_gesture_press_y;
    uint8_t exit_requested;
    SDL_atomic_t back_requested;
    SDL_atomic_t home_requested;
    uint32_t input_window_id;
} product_host_t;

/* The allocator descriptors outlive every asset and are immutable after init.
 * File assets and dynamic uploads use the same class/category; no fallback to
 * uncharged malloc is permitted. Decoder/library internals are tracked
 * separately until those libraries expose bounded allocation hooks. */
#define PRODUCT_RESOURCE_BUDGET 1
static void resource_lock(void *context) { SDL_LockMutex(context); }
static void resource_unlock(void *context) { SDL_UnlockMutex(context); }
static void *resource_raw_allocate(void *context, size_t bytes) {
    (void)context;
    return malloc(bytes);
}
static void resource_raw_release(void *context, void *memory) {
    (void)context;
    free(memory);
}
static void resource_reclaim(void *context, pxa_memory_owner_t owner, uint8_t cls, size_t needed) {
    product_host_t *host = context;
    (void)owner;
    (void)pxa_posix_asset_worker_trim(host->asset_worker, cls, needed);
}
static pxa_status_t resource_memory_init(product_host_t *host,
    size_t internal, size_t external, size_t temporary_internal, size_t temporary_external) {
    pxa_memory_budget_config_t config = {
        .limit = {internal, external}, .lock = resource_lock, .unlock = resource_unlock,
        .reclaim_context = host, .reclaim = resource_reclaim,
        .temporary_limit = {temporary_internal, temporary_external}
    };
    host->resource_mutex = SDL_CreateMutex();
    if (!host->resource_mutex) return PXA_STATUS_RESOURCE_LIMIT;
    config.lock_context = host->resource_mutex;
    pxa_status_t status = pxa_memory_budget_init(&host->resource_budget, &config);
    if (status == PXA_STATUS_OK)
        status = pxa_memory_owner_open(&host->resource_budget, config.limit,
                                       &host->resource_owner);
    if (status != PXA_STATUS_OK) {
        SDL_DestroyMutex(host->resource_mutex);
        host->resource_mutex = NULL;
        return status;
    }
    for (unsigned c = 0; c < PXA_MEMORY_CLASSES; ++c)
        for (unsigned k = 0; k < PXA_MEMORY_KINDS; ++k)
            host->resource_allocators[c][k] = (pxa_memory_allocator_t){
                &host->resource_budget, host->resource_owner, (uint8_t)c,
                (uint8_t)k, NULL, resource_raw_allocate, resource_raw_release};
    return PXA_STATUS_OK;
}
static pxa_status_t resource_memory_end(product_host_t *host) {
    if (!host->resource_mutex) return PXA_STATUS_OK;
    pxa_status_t status = pxa_memory_owner_close(&host->resource_budget,
                                               host->resource_owner);
    if (status != PXA_STATUS_OK) return status;
    host->resource_owner = 0;
    SDL_DestroyMutex(host->resource_mutex);
    host->resource_mutex = NULL;
    return PXA_STATUS_OK;
}
static void *resource_allocate(product_host_t *host, unsigned cls,
                                unsigned kind, size_t bytes) {
    return pxa_memory_allocate(&host->resource_allocators[cls][kind], bytes);
}

typedef struct {
    lv_image_dsc_t descriptor;
    uint8_t *bytes;
} product_asset_t;

static uint32_t read_be_u32(const uint8_t *value) {
    return ((uint32_t)value[0] << 24) | ((uint32_t)value[1] << 16) |
           ((uint32_t)value[2] << 8) | (uint32_t)value[3];
}

static lv_font_t *load_product_font(uint32_t size,
                                    const lv_font_t *symbol_fallback) {
#ifdef PXSYS_DESKTOP_TEXT_FONT
    lv_font_t *font = lv_freetype_font_create(
        PXSYS_DESKTOP_TEXT_FONT, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, size,
        LV_FREETYPE_FONT_STYLE_NORMAL);
    if (font != NULL) font->fallback = symbol_fallback;
    return font;
#else
    (void)size;
    (void)symbol_fallback;
    return NULL;
#endif
}

static void map_host_theme(const pxsys_theme_snapshot_t *source,
                           uint32_t rgba[PXA_UI_THEME_ROLE_COUNT]) {
    static const pxsys_color_token_t tokens[PXA_UI_THEME_ROLE_COUNT] = {
        PXSYS_COLOR_BACKGROUND, PXSYS_COLOR_SURFACE,
        PXSYS_COLOR_ACCENT, PXSYS_COLOR_ON_ACCENT,
        PXSYS_COLOR_TEXT_PRIMARY, PXSYS_COLOR_TEXT_SECONDARY,
        PXSYS_COLOR_BORDER, PXSYS_COLOR_SUCCESS,
        PXSYS_COLOR_WARNING, PXSYS_COLOR_ERROR,
        PXSYS_COLOR_SURFACE_CONTAINER_LOW, PXSYS_COLOR_SURFACE_CONTAINER,
        PXSYS_COLOR_SURFACE_CONTAINER_HIGH,
        PXSYS_COLOR_SURFACE_CONTAINER_HIGHEST,
        PXSYS_COLOR_SURFACE_VARIANT, PXSYS_COLOR_ON_SURFACE_VARIANT,
        PXSYS_COLOR_PRIMARY_CONTAINER, PXSYS_COLOR_ON_PRIMARY_CONTAINER,
        PXSYS_COLOR_SECONDARY, PXSYS_COLOR_ON_SECONDARY,
        PXSYS_COLOR_SECONDARY_CONTAINER, PXSYS_COLOR_ON_SECONDARY_CONTAINER,
        PXSYS_COLOR_TERTIARY, PXSYS_COLOR_ON_TERTIARY,
        PXSYS_COLOR_TERTIARY_CONTAINER, PXSYS_COLOR_ON_TERTIARY_CONTAINER,
        PXSYS_COLOR_OUTLINE_VARIANT, PXSYS_COLOR_ERROR_CONTAINER,
        PXSYS_COLOR_ON_ERROR_CONTAINER, PXSYS_COLOR_INVERSE_SURFACE,
        PXSYS_COLOR_INVERSE_ON_SURFACE, PXSYS_COLOR_INVERSE_PRIMARY,
    };
    for (size_t index = 0; index < PXA_UI_THEME_ROLE_COUNT; ++index) {
        uint32_t argb = source->colors[tokens[index]];
        rgba[index] = (argb << 8u) | (argb >> 24u);
    }
}

/* The device port ships the primary UI environment (display size, density,
 * features) in the start configuration; echo it here so product Guests see the
 * same environment on the desktop profile instead of falling back to a
 * hard-coded logical size. */
static pxa_status_t configure_start_locale(product_host_t *host,
                                           uint64_t instance_id,
                                           const pxa_ui_config_t *ui_config) {
    uint8_t environment[4u + PRODUCT_LOCALE_MAX_BYTES];
    uint8_t ui_environment[128];
    uint8_t config[8u + sizeof(ui_environment) + sizeof(environment)];
    pxa_ui_environment_t ui_environment_value;
    pxa_writer_t environment_writer;
    pxa_writer_t config_writer;
    size_t locale_size;
    size_t ui_environment_size = 0;
    pxa_status_t status;
    if (host == NULL || host->engine == NULL || ui_config == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    locale_size = strlen(host->locale);
    if (locale_size < 2u || locale_size > PRODUCT_LOCALE_MAX_BYTES)
        return PXA_STATUS_INVALID_ARGUMENT;
    memset(&ui_environment_value, 0, sizeof(ui_environment_value));
    ui_environment_value.surface = PXA_UI_PRIMARY_SURFACE;
    ui_environment_value.width = ui_config->primary_width;
    ui_environment_value.height = ui_config->primary_height;
    ui_environment_value.density_q16 = ui_config->density_q16 != 0
        ? ui_config->density_q16 : (UINT32_C(1) << 16);
    ui_environment_value.font_scale_q16 = ui_config->font_scale_q16 != 0
        ? ui_config->font_scale_q16 : (UINT32_C(1) << 16);
    ui_environment_value.recommended_write_bytes = 1024;
    ui_environment_value.color_scheme = ui_config->color_scheme;
    ui_environment_value.features = ui_config->features;
    for (size_t index = 0; index < 4; ++index)
        ui_environment_value.safe_insets[index] = host->safe_insets[index];
    ui_environment_value.display_shape = host->display_shape;
    for (size_t index = 0; index < 4; ++index)
        ui_environment_value.corner_radii[index] = host->corner_radii[index];
    status = pxa_ui_encode_environment(&ui_environment_value, ui_environment,
                                       sizeof(ui_environment),
                                       &ui_environment_size);
    if (status != PXA_STATUS_OK) return status;
    pxa_writer_init(&environment_writer, environment, sizeof(environment));
    status = pxa_writer_record(&environment_writer,
                               PRODUCT_SYSTEM_CONFIGURATION_LOCALE,
                               host->locale, locale_size);
    if (status != PXA_STATUS_OK) return status;
    pxa_writer_init(&config_writer, config, sizeof(config));
    status = pxa_writer_record(&config_writer, PXA_UI_CONFIG_ENVIRONMENT,
                               ui_environment, ui_environment_size);
    if (status != PXA_STATUS_OK) return status;
    status = pxa_writer_record(&config_writer, PRODUCT_SYSTEM_CONFIG_ENVIRONMENT,
                               environment_writer.data, environment_writer.size);
    if (status != PXA_STATUS_OK) return status;
    return pxa_wamr_engine_set_config(
        host->engine, instance_id,
        (pxa_bytes_t){config_writer.data, config_writer.size});
}

static int asset_path_is_safe(const uint8_t *path, size_t path_size) {
    size_t index;
    size_t part_start = 0;
    if (path == NULL || path_size == 0 || path[0] == '/') return 0;
    for (index = 0; index <= path_size; ++index) {
        if (index != path_size && path[index] != '/') {
            if (path[index] == '\\' || path[index] == '\0' || path[index] < 0x20)
                return 0;
            continue;
        }
        if (index == part_start ||
            (index - part_start == 1 && path[part_start] == '.') ||
            (index - part_start == 2 && path[part_start] == '.' &&
             path[part_start + 1] == '.'))
            return 0;
        part_start = index + 1;
    }
    return 1;
}

static pxa_status_t simulator_log_write(
    void *context, pxa_component_t component, pxa_bytes_t app_id,
    pxa_log_level_t level, pxa_bytes_t message) {
    static const char *const names[] = {
        "TRACE", "DEBUG", "INFO", "WARN", "ERROR",
    };
    static const char *const colors[] = {
        "\033[90m", "\033[36m", "\033[32m", "\033[33m", "\033[31m",
    };
    (void)context;
    if (level > PXA_LOG_LEVEL_ERROR) return PXA_STATUS_INVALID_ARGUMENT;
#ifdef PXSYS_PRODUCT_LOG_OBSERVER
    PXSYS_PRODUCT_LOG_OBSERVER(component, level, message.data, message.size);
#endif
    fprintf(stderr,
            "%s[PXA app=%.*s component=%u level=%s] %.*s\033[0m\n",
            colors[level], (int)app_id.size, (const char *)app_id.data,
            (unsigned)component, names[level], (int)message.size,
            (const char *)message.data);
    return PXA_STATUS_OK;
}

static uint64_t now_us(void *context) {
    struct timespec now;
    (void)context;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000);
}

static uint64_t work_now_ms(void *context) {
    return now_us(context) / UINT64_C(1000);
}

static pxa_status_t work_store_load(void *context,
                                    pxa_scheduler_entry_t *entries,
                                    size_t capacity, size_t *count) {
    product_host_t *host = context;
    if (host->queued_work_count > capacity) return PXA_STATUS_RESOURCE_LIMIT;
    memcpy(entries, host->queued_work,
           host->queued_work_count * sizeof(*entries));
    *count = host->queued_work_count;
    return PXA_STATUS_OK;
}

static pxa_status_t work_store_save(void *context,
                                    const pxa_scheduler_entry_t *entries,
                                    size_t count) {
    product_host_t *host = context;
    if (count > 8) return PXA_STATUS_RESOURCE_LIMIT;
    memcpy(host->queued_work, entries, count * sizeof(*entries));
    host->queued_work_count = count;
    return PXA_STATUS_OK;
}

static pxa_status_t work_complete(void *context, pxa_component_t component,
                                   uint32_t id, pxa_work_result_t result) {
    product_host_t *host = context;
    pxa_status_t status = PXA_STATUS_OK;
    if ((!host->running_work_starting &&
         host->running_work_component != component) ||
        host->running_work.id != id || host->running_work_finished ||
        host->running_work_cancelled) return PXA_STATUS_NOT_FOUND;
    if (result == PXA_WORK_RESULT_RETRY &&
        host->running_work.attempt < host->running_work.max_attempts)
        status = pxa_scheduler_retry(host->scheduler, &host->running_work);
    if (status == PXA_STATUS_OK) {
        host->running_work_finished = 1;
        host->running_work_finish_at_ms = work_now_ms(NULL) + 1;
    }
    return status;
}

static pxa_status_t work_cancel(void *context, uint32_t id) {
    product_host_t *host = context;
    if ((!host->running_work_starting &&
         host->running_work_component == PXA_COMPONENT_INVALID) ||
        host->running_work.id != id || host->running_work_finished)
        return PXA_STATUS_NOT_FOUND;
    host->running_work_cancelled = 1;
    return PXA_STATUS_OK;
}

static pxa_status_t pump_work(product_host_t *host) {
    const uint64_t now = work_now_ms(NULL);
    pxa_status_t status;
    if (host->scheduler == NULL || host->coordinator == NULL)
        return PXA_STATUS_OK;
    if (host->running_work_component != PXA_COMPONENT_INVALID) {
        pxa_component_snapshot_t snapshot;
        const int stopped = pxa_component_snapshot(
            host->runtime, host->running_work_component, &snapshot) !=
                PXA_STATUS_OK || snapshot.state != PXA_COMPONENT_RUNNING;
        if (!stopped && !host->running_work_finished &&
            !host->running_work_cancelled &&
            now >= host->running_work_deadline_ms &&
            !host->running_work_stop_posted) {
            status = pxa_scheduler_post_work_stop(host->scheduler,
                host->running_work_component, host->running_work.id,
                host->running_work_deadline_ms);
            if (status == PXA_STATUS_OK) {
                host->running_work_stop_posted = 1;
                host->running_work_grace_ms = now + 500;
            } else if (status != PXA_STATUS_WOULD_BLOCK) {
                host->running_work_grace_ms = now;
            }
        }
        if (!stopped && !host->running_work_finished &&
            !host->running_work_cancelled &&
            (!host->running_work_grace_ms ||
             now < host->running_work_grace_ms)) return PXA_STATUS_OK;
        if (!stopped && host->running_work_finished &&
            now < host->running_work_finish_at_ms) return PXA_STATUS_OK;
        if (!host->running_work_finished && !host->running_work_cancelled &&
            host->running_work.attempt < host->running_work.max_attempts) {
            status = pxa_scheduler_retry(host->scheduler, &host->running_work);
            if (status != PXA_STATUS_OK) return status;
        }
        {
            const pxa_bytes_t id = {
                host->running_work.component_id,
                host->running_work.component_id_size};
            status = pxa_activation_deactivate(host->coordinator, id,
                host->running_work_cancelled ? PXA_STOP_POLICY :
                host->running_work_finished ? PXA_STOP_NORMAL : PXA_STOP_FAULT);
            if (status != PXA_STATUS_OK && status != PXA_STATUS_NOT_FOUND)
                return status;
        }
        host->running_work_component = PXA_COMPONENT_INVALID;
        memset(&host->running_work, 0, sizeof(host->running_work));
        host->running_work_finished = 0;
        host->running_work_cancelled = 0;
        host->running_work_stop_posted = 0;
        host->running_work_grace_ms = 0;
        host->running_work_finish_at_ms = 0;
    }
    {
        pxa_scheduler_entry_t due[8];
        size_t count = 0;
        status = pxa_scheduler_take_due(host->scheduler, due, 8, &count);
        if (status != PXA_STATUS_OK || count == 0) return status;
        for (size_t i = 1; i < count; ++i) {
            status = pxa_scheduler_defer(host->scheduler, &due[i], 1000);
            if (status != PXA_STATUS_OK) return status;
        }
        const uint64_t deadline = now + due[0].max_execution_ms;
        uint8_t config[64];
        size_t config_size = 0;
        const uint64_t instance = ++host->next_instance_id;
        status = pxa_scheduler_encode_start_config(
            &due[0], deadline, config, sizeof(config), &config_size);
        if (status == PXA_STATUS_OK)
            status = pxa_wamr_engine_set_config(host->engine, instance,
                (pxa_bytes_t){config, config_size});
        if (status == PXA_STATUS_OK) {
            host->running_work = due[0];
            host->running_work_deadline_ms = deadline;
            host->running_work_starting = 1;
            status = pxa_activation_activate(host->coordinator,
                (pxa_bytes_t){due[0].component_id, due[0].component_id_size},
                instance, &host->running_work_component);
            host->running_work_starting = 0;
        }
        if (status != PXA_STATUS_OK) {
            host->running_work_component = PXA_COMPONENT_INVALID;
            if (!host->running_work_finished &&
                due[0].attempt < due[0].max_attempts) {
                pxa_status_t retry_status = pxa_scheduler_retry(
                    host->scheduler, &due[0]);
                if (retry_status != PXA_STATUS_OK) return retry_status;
            }
            memset(&host->running_work, 0, sizeof(host->running_work));
            host->running_work_finished = 0;
            host->running_work_cancelled = 0;
            host->running_work_finish_at_ms = 0;
            return PXA_STATUS_OK;
        }
    }
    return PXA_STATUS_OK;
}

static void system_back_indicator_reset(product_host_t *host) {
    if (host->system_back_indicator == NULL) return;
    lv_obj_delete(host->system_back_indicator);
    host->system_back_indicator = NULL;
}

static void system_back_indicator_update(product_host_t *host, int32_t x,
                                         int32_t y) {
    lv_obj_t *label;
    int32_t distance = x - host->system_gesture_press_x;
    int32_t indicator_x;
    int32_t indicator_y;
    if (host->display == NULL || distance <= 0) return;
    if (host->system_back_indicator == NULL) {
        host->system_back_indicator =
            lv_obj_create(lv_display_get_layer_top(host->display));
        lv_obj_set_size(host->system_back_indicator, 32, 32);
        lv_obj_set_style_bg_color(host->system_back_indicator,
                                  lv_color_hex(0x3e526d), 0);
        lv_obj_set_style_bg_opa(host->system_back_indicator, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(host->system_back_indicator, 0, 0);
        lv_obj_set_style_radius(host->system_back_indicator, LV_RADIUS_CIRCLE,
                                0);
        lv_obj_set_style_pad_all(host->system_back_indicator, 0, 0);
        label = lv_label_create(host->system_back_indicator);
        lv_label_set_text(label, "<");
        lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
        lv_obj_center(label);
    }
    if (distance > PRODUCT_SYSTEM_GESTURE_COMMIT_DISTANCE)
        distance = PRODUCT_SYSTEM_GESTURE_COMMIT_DISTANCE;
    indicator_x = -22 + distance * 22 / PRODUCT_SYSTEM_GESTURE_COMMIT_DISTANCE;
    indicator_y = y - 16;
    if (indicator_y < 0) indicator_y = 0;
    if (indicator_y > (int32_t)host->height - 32)
        indicator_y = (int32_t)host->height - 32;
    lv_obj_set_pos(host->system_back_indicator, indicator_x, indicator_y);
}

static int SDLCALL product_key_event_watch(void *context, SDL_Event *event) {
    product_host_t *host = context;
    if (event->type == SDL_KEYUP &&
        event->key.windowID == host->input_window_id) {
        if (event->key.keysym.sym == SDLK_ESCAPE)
            SDL_AtomicSet(&host->back_requested, 1);
        else if (event->key.keysym.sym == SDLK_HOME)
            SDL_AtomicSet(&host->home_requested, 1);
    }
    return 0;
}

static void system_back_gesture_event(lv_event_t *event) {
    product_host_t *host = (product_host_t *)lv_event_get_user_data(event);
    lv_indev_t *indev = lv_event_get_indev(event);
    lv_point_t point;
    int32_t horizontal;
    int32_t vertical;
    if (host == NULL || indev == NULL) return;
    lv_indev_get_point(indev, &point);
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
        host->system_gesture_press_x = point.x;
        host->system_gesture_press_y = point.y;
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_PRESSING) {
        system_back_indicator_update(host, point.x, point.y);
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_PRESS_LOST) {
        system_back_indicator_reset(host);
        return;
    }
    if (lv_event_get_code(event) != LV_EVENT_RELEASED) return;
    horizontal = point.x - host->system_gesture_press_x;
    vertical = point.y - host->system_gesture_press_y;
    if (vertical < 0) vertical = -vertical;
    system_back_indicator_reset(host);
    if (horizontal >= PRODUCT_SYSTEM_GESTURE_COMMIT_DISTANCE &&
        horizontal > vertical)
        SDL_AtomicSet(&host->back_requested, 1);
}

static void system_home_gesture_event(lv_event_t *event) {
    product_host_t *host = (product_host_t *)lv_event_get_user_data(event);
    lv_indev_t *indev = lv_event_get_indev(event);
    lv_point_t point;
    int32_t vertical;
    int32_t horizontal;
    if (host == NULL || indev == NULL) return;
    lv_indev_get_point(indev, &point);
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
        host->system_gesture_press_x = point.x;
        host->system_gesture_press_y = point.y;
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_PRESS_LOST) return;
    if (lv_event_get_code(event) != LV_EVENT_RELEASED) return;
    vertical = host->system_gesture_press_y - point.y;
    horizontal = point.x - host->system_gesture_press_x;
    if (horizontal < 0) horizontal = -horizontal;
    if (vertical >= PRODUCT_SYSTEM_GESTURE_COMMIT_DISTANCE &&
        vertical > horizontal)
        host->exit_requested = 1;
}

static void system_button_bar_event(lv_event_t *event) {
    product_host_t *host = (product_host_t *)lv_event_get_user_data(event);
    lv_indev_t *indev;
    lv_point_t point;
    if (host == NULL || lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    indev = lv_event_get_indev(event);
    if (indev == NULL) return;
    lv_indev_get_point(indev, &point);
    if (point.x < (int32_t)host->width / 3)
        SDL_AtomicSet(&host->back_requested, 1);
    else
        host->exit_requested = 1;
}

static void install_system_gestures(product_host_t *host) {
    lv_obj_t *layer;
    int32_t home_height;
    if (host == NULL || host->display == NULL) return;
    layer = lv_display_get_layer_top(host->display);
    home_height = host->navigation_mode == 1u ? 2 :
                  host->navigation_mode == 2u ? 36 : PRODUCT_SYSTEM_GESTURE_HOME_HEIGHT;
    if (host->navigation_mode == 2u) {
        lv_obj_t *button_bar = lv_obj_create(layer);
        lv_obj_t *label;
        lv_obj_set_pos(button_bar, 0, (int32_t)host->height - home_height);
        lv_obj_set_size(button_bar, (int32_t)host->width, home_height);
        lv_obj_set_style_radius(button_bar, 0, 0);
        lv_obj_set_style_border_width(button_bar, 0, 0);
        lv_obj_set_style_pad_all(button_bar, 0, 0);
        lv_obj_set_style_bg_color(button_bar, lv_color_hex(0x1d2126), 0);
        label = lv_label_create(button_bar);
        lv_label_set_text(label, "<      O      []");
        lv_obj_center(label);
        lv_obj_add_event_cb(button_bar, system_button_bar_event,
                            LV_EVENT_CLICKED, host);
        return;
    }
    host->system_back_gesture = lv_obj_create(layer);
    lv_obj_set_pos(host->system_back_gesture, 0, 0);
    lv_obj_set_size(host->system_back_gesture,
                    PRODUCT_SYSTEM_GESTURE_EDGE_WIDTH,
                    (int32_t)host->height - home_height);
    lv_obj_set_style_bg_opa(host->system_back_gesture, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(host->system_back_gesture, 0, 0);
    lv_obj_set_style_pad_all(host->system_back_gesture, 0, 0);
    lv_obj_set_scrollable(host->system_back_gesture, false);
    lv_obj_set_clickable(host->system_back_gesture, true);
    lv_obj_set_press_lock(host->system_back_gesture, true);
    lv_obj_add_event_cb(host->system_back_gesture, system_back_gesture_event,
                        LV_EVENT_PRESSED, host);
    lv_obj_add_event_cb(host->system_back_gesture, system_back_gesture_event,
                        LV_EVENT_PRESSING, host);
    lv_obj_add_event_cb(host->system_back_gesture, system_back_gesture_event,
                        LV_EVENT_RELEASED, host);
    lv_obj_add_event_cb(host->system_back_gesture, system_back_gesture_event,
                        LV_EVENT_PRESS_LOST, host);
    host->system_home_gesture = lv_obj_create(layer);
    lv_obj_set_pos(host->system_home_gesture, 0,
                   (int32_t)host->height - home_height);
    lv_obj_set_size(host->system_home_gesture, (int32_t)host->width,
                    home_height);
    lv_obj_set_style_bg_opa(host->system_home_gesture, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(host->system_home_gesture, 0, 0);
    lv_obj_set_style_pad_all(host->system_home_gesture, 0, 0);
    lv_obj_set_scrollable(host->system_home_gesture, false);
    lv_obj_set_clickable(host->system_home_gesture, true);
    lv_obj_set_press_lock(host->system_home_gesture, true);
    lv_obj_add_event_cb(host->system_home_gesture, system_home_gesture_event,
                        LV_EVENT_PRESSED, host);
    lv_obj_add_event_cb(host->system_home_gesture, system_home_gesture_event,
                        LV_EVENT_RELEASED, host);
    lv_obj_add_event_cb(host->system_home_gesture, system_home_gesture_event,
                        LV_EVENT_PRESS_LOST, host);
    if (host->navigation_mode == 0u) {
        lv_obj_t *handle = lv_obj_create(layer);
        lv_obj_set_size(handle, 56, 4);
        lv_obj_set_style_radius(handle, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(handle, 0, 0);
        lv_obj_set_style_pad_all(handle, 0, 0);
        lv_obj_set_style_bg_color(handle, lv_color_hex(0xb1b6c0), 0);
        lv_obj_align(handle, LV_ALIGN_BOTTOM_MID, 0, -5);
        lv_obj_set_clickable(handle, false);
    }
}

static void product_toast_hide(lv_timer_t *timer) {
    product_host_t *host = (product_host_t *)lv_timer_get_user_data(timer);
    if (host->toast != NULL) lv_obj_delete(host->toast);
    host->toast = NULL;
    host->toast_timer = NULL;
}

static pxa_status_t window_toast(void *context, const char *text,
                                 uint32_t duration_ms) {
    product_host_t *host = (product_host_t *)context;
    if (host == NULL || host->display == NULL) return PXA_STATUS_UNAVAILABLE;
    if (host->toast_timer != NULL) lv_timer_delete(host->toast_timer);
    if (host->toast != NULL) lv_obj_delete(host->toast);
    host->toast = lv_label_create(lv_display_get_layer_top(host->display));
    lv_label_set_text(host->toast, text);
    lv_label_set_long_mode(host->toast, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(host->toast, LV_PCT(75));
    lv_obj_set_style_bg_color(host->toast, lv_color_hex(0x303134), 0);
    lv_obj_set_style_bg_opa(host->toast, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(host->toast, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_pad_all(host->toast, 9, 0);
    lv_obj_set_style_radius(host->toast, 14, 0);
    lv_obj_align(host->toast, LV_ALIGN_BOTTOM_MID, 0,
                 -(host->navigation_mode == 2u ? 48 : 36));
    host->toast_timer = lv_timer_create(product_toast_hide, duration_ms, host);
    if (host->toast_timer != NULL) lv_timer_set_repeat_count(host->toast_timer, 1);
    return PXA_STATUS_OK;
}

static void *allocate_memory(void *context, size_t size) {
    (void)context;
    return malloc(size);
}

static void release_memory(void *context, void *memory) {
    (void)context;
    free(memory);
}

/* Mirrors pxa_surface_fit_scale in the device component: the simulator shows
 * a Surface at the largest exact 1x/2x/4x scale that fits the profile and
 * centers it, so product behaviour matches the Watcher presenter. */
static uint8_t product_fit_scale(uint32_t surface_width,
                                 uint32_t surface_height,
                                 uint32_t display_width,
                                 uint32_t display_height) {
    uint8_t scale;
    uint8_t best = 0;
    if (surface_width == 0 || surface_height == 0 || display_width == 0 ||
        display_height == 0)
        return 0;
    for (scale = 1; scale <= 4; scale = (uint8_t)(scale * 2)) {
        if (surface_width * scale <= display_width &&
            surface_height * scale <= display_height)
            best = scale;
    }
    return best;
}

static pxa_status_t surface_create_with_raster_capacity(
    void *context, const pxa_surface_desc_t *desc, uint64_t *surface,
    uint32_t *stride, uint32_t raster_draw_capacity) {
    product_host_t *host = context;
    uint8_t scale;
    const int mapped = desc != NULL &&
        (desc->flags & PXA_SURFACE_FLAG_GUEST_MAPPED) != 0;
    const int raster = desc != NULL &&
        (desc->flags & PRODUCT_SURFACE_FLAG_GAME_RENDER) != 0;
    if (host == NULL || desc == NULL || surface == NULL || stride == NULL ||
        desc->format != PXA_SURFACE_FORMAT_RGB565 ||
        (desc->flags & ~(PXA_SURFACE_FLAG_KNOWN_MASK |
                         PRODUCT_SURFACE_FLAG_GAME_RENDER |
                         PRODUCT_SURFACE_FLAG_RASTER_NO_SCRATCH |
                         PRODUCT_SURFACE_FLAG_RASTER_COVERAGE)) != 0 ||
        ((desc->flags & (PRODUCT_SURFACE_FLAG_RASTER_NO_SCRATCH |
                         PRODUCT_SURFACE_FLAG_RASTER_COVERAGE)) ==
         (PRODUCT_SURFACE_FLAG_RASTER_NO_SCRATCH |
          PRODUCT_SURFACE_FLAG_RASTER_COVERAGE)) ||
        (!raster &&
         (desc->flags & (PRODUCT_SURFACE_FLAG_RASTER_NO_SCRATCH |
                         PRODUCT_SURFACE_FLAG_RASTER_COVERAGE)) != 0) ||
        mapped == raster ||
        desc->width == 0 || desc->height == 0 ||
        desc->buffer_count < 2 || desc->buffer_count > 3 ||
        host->surface_frame_bytes != 0)
        return PXA_STATUS_UNSUPPORTED;
    if (raster_draw_capacity != 0 &&
        (!raster || raster_draw_capacity < PXA_RASTER_DRAW_HEADER_BYTES ||
         raster_draw_capacity > PXA_RASTER_MAX_DRAW_BYTES))
        return PXA_STATUS_UNSUPPORTED;
    if (desc->width > host->width || desc->height > host->height)
        return PXA_STATUS_UNSUPPORTED;
    scale = product_fit_scale(desc->width, desc->height, host->width,
                              host->height);
    if (scale == 0)
        return PXA_STATUS_UNSUPPORTED;
    *surface = 1;
    *stride = (uint32_t)desc->width * 2u;
    host->surface_stride_bytes = *stride;
    host->surface_width = desc->width;
    host->surface_height = desc->height;
    host->surface_frame_bytes = *stride * desc->height;
    host->surface_display_width = (uint16_t)host->width;
    host->surface_display_height = (uint16_t)host->height;
    host->surface_display_stride_bytes = host->width * 2u;
    host->surface_display_frame_bytes =
        host->surface_display_stride_bytes * host->height;
    host->surface_buffer_count = desc->buffer_count;
    host->surface_scale = scale;
    host->surface_flags = desc->flags;
    host->raster_max_draw_bytes = raster_draw_capacity != 0
                                      ? raster_draw_capacity
                                      : PXA_RASTER_MAX_DRAW_BYTES;
    host->raster_scratch_mode =
        (desc->flags & PRODUCT_SURFACE_FLAG_RASTER_NO_SCRATCH) != 0
            ? PXA_RASTER_SCRATCH_NONE
            : (desc->flags & PRODUCT_SURFACE_FLAG_RASTER_COVERAGE) != 0
                  ? PXA_RASTER_SCRATCH_COVERAGE_2BIT
                  : PXA_RASTER_SCRATCH_DEPTH16;
    host->surface_writing_buffer = -1;
    host->surface_pending_buffer = -1;
    host->surface_layer.x = 0;
    host->surface_layer.y = 0;
    host->surface_layer.width = desc->width;
    host->surface_layer.height = desc->height;
    host->surface_layer.visible = 1;
    host->raster_current_buffer = -1;
    host->raster_draw_pending = -1;
    if (raster) {
        uint8_t index;
        size_t scratch_bytes = 0;
        if (host->raster_scratch_mode == PXA_RASTER_SCRATCH_DEPTH16)
            scratch_bytes = (size_t)host->surface_width *
                            host->surface_height * sizeof(uint16_t);
        else if (host->raster_scratch_mode ==
                 PXA_RASTER_SCRATCH_COVERAGE_2BIT)
            scratch_bytes = (((size_t)host->surface_width + 7u) >> 3) *
                            host->surface_height * 2u;
        host->surface_display_buffer = malloc(host->surface_display_frame_bytes);
        if (host->surface_display_buffer == NULL) goto failed;
        if (scratch_bytes != 0) {
            host->raster_depth_buffer = malloc(scratch_bytes);
            if (host->raster_depth_buffer == NULL) goto failed;
        }
        for (index = 0; index < 2; ++index) {
            host->raster_buffers[index] = malloc(host->surface_frame_bytes);
            host->raster_draw_lists[index] = malloc(host->raster_max_draw_bytes);
            if (host->raster_buffers[index] == NULL ||
                host->raster_draw_lists[index] == NULL) goto failed;
        }
        host->surface_registered = 1;
        memset(&host->surface_bitmap, 0, sizeof(host->surface_bitmap));
        host->surface_bitmap.header.magic = LV_IMAGE_HEADER_MAGIC;
        host->surface_bitmap.header.cf = LV_COLOR_FORMAT_RGB565;
        host->surface_bitmap.header.w = host->surface_display_width;
        host->surface_bitmap.header.h = host->surface_display_height;
        host->surface_bitmap.header.stride = host->surface_display_stride_bytes;
        host->surface_bitmap.data_size = host->surface_display_frame_bytes;
        host->surface_bitmap.data = host->surface_display_buffer;
        host->surface_image = lv_image_create(host->content_parent);
        if (host->surface_image == NULL) goto failed;
        lv_image_set_src(host->surface_image, &host->surface_bitmap);
        lv_image_set_inner_align(host->surface_image, LV_IMAGE_ALIGN_TOP_LEFT);
        lv_obj_set_scrollable(host->surface_image, false);
        lv_obj_set_clickable(host->surface_image, false);
        lv_obj_set_size(host->surface_image, host->surface_display_width,
                        host->surface_display_height);
        lv_obj_move_background(host->surface_image);
    }
    return PXA_STATUS_OK;
failed:
    for (uint8_t index = 0; index < 2; ++index) {
        free(host->raster_buffers[index]);
        free(host->raster_draw_lists[index]);
        pxa_raster_bindings_release(&host->raster_frame_bindings[index]);
        host->raster_buffers[index] = NULL;
        host->raster_draw_lists[index] = NULL;
    }
    free(host->raster_depth_buffer);
    host->raster_depth_buffer = NULL;
    if (host->surface_image != NULL) lv_obj_delete(host->surface_image);
    host->surface_image = NULL;
    free(host->surface_display_buffer);
    host->surface_display_buffer = NULL;
    host->surface_frame_bytes = 0;
    host->surface_stride_bytes = 0;
    host->surface_display_frame_bytes = 0;
    host->surface_display_stride_bytes = 0;
    host->surface_width = 0;
    host->surface_height = 0;
    host->surface_display_width = 0;
    host->surface_display_height = 0;
    host->surface_buffer_count = 0;
    host->surface_scale = 0;
    host->surface_flags = 0;
    host->surface_registered = 0;
    host->raster_current_buffer = -1;
    host->raster_draw_pending = -1;
    return PXA_STATUS_RESOURCE_LIMIT;
}

static pxa_status_t surface_create(void *context, const pxa_surface_desc_t *desc,
                                   uint64_t *surface, uint32_t *stride) {
    return surface_create_with_raster_capacity(
        context, desc, surface, stride, 0);
}

static pxa_status_t surface_write(void *context, uint64_t surface,
                                  const uint8_t *pixels, size_t size) {
    (void)context;
    (void)surface;
    (void)pixels;
    (void)size;
    return PXA_STATUS_UNSUPPORTED;
}

static pxa_status_t surface_register_buffers(void *context, uint64_t surface,
                                             uint8_t *pixels, size_t size) {
    product_host_t *host = context;
    if (host == NULL || surface != 1 || pixels == NULL ||
        host->surface_registered || host->surface_frame_bytes == 0 ||
        (host->surface_flags & PXA_SURFACE_FLAG_GUEST_MAPPED) == 0 ||
        ((uintptr_t)pixels & 1u) != 0 ||
        size != (size_t)host->surface_frame_bytes * host->surface_buffer_count)
        return PXA_STATUS_INVALID_ARGUMENT;
    host->surface_display_buffer = malloc(host->surface_display_frame_bytes);
    if (host->surface_display_buffer == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    host->surface_buffers = pixels;
    host->surface_registered = 1;
    memset(&host->surface_bitmap, 0, sizeof(host->surface_bitmap));
    host->surface_bitmap.header.magic = LV_IMAGE_HEADER_MAGIC;
    host->surface_bitmap.header.cf = LV_COLOR_FORMAT_RGB565;
    host->surface_bitmap.header.w = host->surface_display_width;
    host->surface_bitmap.header.h = host->surface_display_height;
    host->surface_bitmap.header.stride = host->surface_display_stride_bytes;
    host->surface_bitmap.data_size = host->surface_display_frame_bytes;
    host->surface_bitmap.data = host->surface_display_buffer;
    host->surface_image = lv_image_create(host->content_parent);
    if (host->surface_image == NULL) {
        free(host->surface_display_buffer);
        host->surface_display_buffer = NULL;
        host->surface_buffers = NULL;
        host->surface_registered = 0;
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    lv_image_set_src(host->surface_image, &host->surface_bitmap);
    lv_image_set_inner_align(host->surface_image, LV_IMAGE_ALIGN_TOP_LEFT);
    lv_obj_set_scrollable(host->surface_image, false);
    lv_obj_set_clickable(host->surface_image, false);
    lv_obj_set_size(host->surface_image, host->surface_display_width,
                    host->surface_display_height);
    lv_obj_set_pos(host->surface_image, 0, 0);
    lv_obj_move_background(host->surface_image);
    return PXA_STATUS_OK;
}

static int surface_buffer_available(const product_host_t *host, uint8_t index) {
    return host != NULL && index < host->surface_buffer_count &&
           host->surface_writing_buffer != (int8_t)index &&
           host->surface_pending_buffer != (int8_t)index &&
           (host->surface_release_pending_mask & (uint8_t)(1u << index)) == 0;
}

static int surface_enqueue_release(product_host_t *host, uint8_t buffer_index,
                                   uint64_t frame_id) {
    uint8_t tail;
    if (host == NULL || buffer_index >= host->surface_buffer_count ||
        frame_id == 0 || host->surface_release_count >= host->surface_buffer_count ||
        (host->surface_release_pending_mask & (uint8_t)(1u << buffer_index)) != 0)
        return 0;
    tail = (uint8_t)((host->surface_release_head + host->surface_release_count) %
                     host->surface_buffer_count);
    memset(&host->surface_releases[tail], 0, sizeof(host->surface_releases[tail]));
    host->surface_releases[tail].buffer_index = buffer_index;
    host->surface_releases[tail].frame_id = frame_id;
    host->surface_release_pending_mask |= (uint8_t)(1u << buffer_index);
    ++host->surface_release_count;
    return 1;
}

static pxa_status_t surface_acquire_buffer(void *context, uint64_t surface,
                                           uint8_t *buffer_index) {
    product_host_t *host = context;
    if (host == NULL || surface != 1 || buffer_index == NULL ||
        !host->surface_registered || host->surface_writing_buffer >= 0 ||
        (host->surface_flags & PXA_SURFACE_FLAG_GUEST_MAPPED) == 0)
        return PXA_STATUS_BAD_STATE;
    for (uint8_t index = 0; index < host->surface_buffer_count; ++index) {
        if (surface_buffer_available(host, index)) {
            host->surface_writing_buffer = (int8_t)index;
            *buffer_index = index;
            return PXA_STATUS_OK;
        }
    }
    return PXA_STATUS_WOULD_BLOCK;
}

static pxa_status_t surface_present_buffer(void *context, uint64_t surface,
                                           uint8_t buffer_index, uint64_t frame_id) {
    product_host_t *host = context;
    if (host == NULL || surface != 1 || frame_id == 0 ||
        !host->surface_registered || buffer_index >= host->surface_buffer_count ||
        host->surface_writing_buffer != (int8_t)buffer_index ||
        (host->surface_flags & PXA_SURFACE_FLAG_GUEST_MAPPED) == 0 ||
        frame_id <= host->surface_last_frame_id)
        return PXA_STATUS_BAD_STATE;
    if (host->surface_pending_buffer >= 0) {
        if (!surface_enqueue_release(host, (uint8_t)host->surface_pending_buffer,
                                    host->surface_pending_frame_id))
            return PXA_STATUS_WOULD_BLOCK;
        ++host->surface_dropped_frames;
        ++host->surface_replaced_frames;
    }
    host->surface_pending_buffer = (int8_t)buffer_index;
    host->surface_pending_frame_id = frame_id;
    host->surface_last_frame_id = frame_id;
    host->surface_writing_buffer = -1;
    ++host->surface_submitted_frames;
    return PXA_STATUS_OK;
}

static pxa_status_t surface_queue(void *context, uint64_t surface,
                                  uint64_t frame_id,
                                  const pxa_surface_damage_rect_t *damage,
                                  uint8_t damage_count) {
    (void)context; (void)surface; (void)frame_id; (void)damage; (void)damage_count;
    return PXA_STATUS_OK;
}

static pxa_status_t surface_configure(void *context, uint64_t surface,
                                      const pxa_surface_layer_t *layer) {
    product_host_t *host = context;
    if (host == NULL || surface != 1 || layer == NULL ||
        layer->width != host->surface_width || layer->height != host->surface_height ||
        layer->visible > 1)
        return PXA_STATUS_INVALID_ARGUMENT;
    host->surface_layer = *layer;
    if (host->surface_image != NULL) {
        lv_obj_set_pos(host->surface_image, 0, 0);
        lv_obj_set_hidden(host->surface_image, !layer->visible);
        lv_obj_move_background(host->surface_image);
        lv_obj_invalidate(host->surface_image);
    }
    return PXA_STATUS_OK;
}

static pxa_status_t surface_query(void *context, uint64_t surface,
                                  pxa_surface_state_t *state) {
    product_host_t *host = context;
    uint32_t used = 0;
    if (host == NULL || surface != 1 || state == NULL ||
        host->surface_frame_bytes == 0)
        return PXA_STATUS_NOT_FOUND;
    memset(state, 0, sizeof(*state));
    state->submitted_frames = host->surface_submitted_frames;
    state->presented_frames = host->surface_presented_frames;
    state->dropped_frames = host->surface_dropped_frames;
    state->replaced_frames = host->surface_replaced_frames;
    state->released_frames = host->surface_released_frames;
    for (uint8_t index = 0; index < host->surface_buffer_count; ++index)
        if (!surface_buffer_available(host, index)) ++used;
    state->free_buffers = (host->surface_flags & PXA_SURFACE_FLAG_GUEST_MAPPED) != 0
                              ? host->surface_buffer_count - used : 2;
    state->flags = PXA_SURFACE_STATE_FLAG_SUPPORTS_GUEST_MAPPED;
    return PXA_STATUS_OK;
}

static void *raster_asset_allocate(void *context, size_t bytes) {
    return pxa_memory_allocate(context, bytes);
}

static void raster_asset_free(void *context, void *memory) {
    (void)context;
    pxa_memory_release(memory);
}

static pxa_status_t surface_raster_upload(void *context, uint64_t surface,
                                          const uint8_t *bytes, size_t size) {
    product_host_t *host = context;
    pxa_raster_asset_t *asset;
    pxa_status_t status;
    if (host == NULL || surface != 1 || bytes == NULL ||
        (host->surface_flags & PRODUCT_SURFACE_FLAG_GAME_RENDER) == 0)
        return PXA_STATUS_BAD_STATE;
    pxa_raster_upload_view_t upload;
    status = pxa_raster_decode_upload(bytes, size, &upload);
    if (status != PXA_STATUS_OK) return status;
    unsigned cls = upload.kind == PXA_RASTER_UPLOAD_TEXTURE_INDEX8
                     ? PXA_MEMORY_EXTERNAL : PXA_MEMORY_INTERNAL;
    status = pxa_raster_asset_from_upload(bytes, size, raster_asset_allocate,
        raster_asset_free, &host->resource_allocators[cls][PXA_MEMORY_RASTER], &asset);
    if (status != PXA_STATUS_OK) return status;
    pxa_raster_asset_release(pxa_raster_bindings_replace(
        &host->raster_bindings, bytes[9], asset));
    pxa_raster_asset_release(asset);
    return PXA_STATUS_OK;
}

static pxa_status_t surface_raster_bind_assets(void *context, uint64_t surface,
    const pxa_raster_bindings_t *replacement, uint64_t texture_mask, uint8_t update_palette) {
    product_host_t *host = context;
    pxa_raster_bindings_t retired = {0};
    if (!host || surface != 1 || !(host->surface_flags & PRODUCT_SURFACE_FLAG_GAME_RENDER))
        return PXA_STATUS_BAD_STATE;
    pxa_raster_bindings_update(&host->raster_bindings, replacement, texture_mask, update_palette, &retired);
    pxa_raster_bindings_release(&retired);
    return PXA_STATUS_OK;
}

static pxa_status_t surface_raster_submit(void *context, uint64_t surface,
                                          const uint8_t *bytes, size_t size) {
    product_host_t *host = context;
    pxa_raster_resources_t resources;
    /* The reference executor skips prefilled_commands; the desktop backend
     * materializes every command itself, so the count must be zero. */
    pxa_raster_target_t target = {0};
    pxa_raster_draw_list_view_t list;
    pxa_status_t status;
    uint8_t mailbox;
    if (host == NULL || surface != 1 || bytes == NULL ||
        (host->surface_flags & PRODUCT_SURFACE_FLAG_GAME_RENDER) == 0)
        return PXA_STATUS_BAD_STATE;
    if (size > host->raster_max_draw_bytes)
        return PXA_STATUS_LIMIT_EXCEEDED;
    pxa_raster_bindings_view(&host->raster_bindings,
                              PXA_RASTER_CAP_KNOWN_MASK, &resources);
    target.pixels = (uint16_t *)host->raster_buffers[0];
    target.depth_pixels = host->raster_depth_buffer;
    target.scratch_mode = host->raster_scratch_mode;
    target.stride_pixels = host->surface_stride_bytes / 2u;
    target.depth_stride_pixels = host->surface_width;
    target.width = host->surface_width;
    target.height = host->surface_height;
    status = pxa_raster_validate_draw_list(bytes, size, &target, &resources, &list);
    if (status != PXA_STATUS_OK || list.frame_id <= host->raster_last_frame_id) {
        ++host->raster_telemetry.rejected_lists;
        return status != PXA_STATUS_OK ? status : PXA_STATUS_BAD_STATE;
    }
    mailbox = host->raster_draw_pending >= 0 ?
                  (uint8_t)host->raster_draw_pending : (uint8_t)(list.frame_id & 1u);
    if (host->raster_draw_pending >= 0) {
        ++host->surface_dropped_frames;
        ++host->surface_replaced_frames;
        ++host->raster_telemetry.dropped_frames;
    }
    pxa_raster_bindings_release(&host->raster_frame_bindings[mailbox]);
    pxa_raster_bindings_snapshot_for_draw(&host->raster_frame_bindings[mailbox],
                                          &host->raster_bindings, &list);
    host->raster_draw_views[mailbox] = list;
    memcpy(host->raster_draw_lists[mailbox], bytes, size);
    host->raster_draw_sizes[mailbox] = (uint32_t)size;
    host->raster_draw_pending = (int8_t)mailbox;
    host->raster_last_frame_id = list.frame_id;
    ++host->surface_submitted_frames;
    ++host->raster_telemetry.submitted_frames;
    return PXA_STATUS_OK;
}

static pxa_status_t surface_raster_query(void *context, uint64_t surface,
                                         pxa_raster_telemetry_t *telemetry) {
    product_host_t *host = context;
    if (host == NULL || surface != 1 || telemetry == NULL ||
        (host->surface_flags & PRODUCT_SURFACE_FLAG_GAME_RENDER) == 0)
        return PXA_STATUS_BAD_STATE;
    *telemetry = host->raster_telemetry;
    return PXA_STATUS_OK;
}

static void surface_close(void *context, uint64_t surface) {
    product_host_t *host = context;
    if (host == NULL || surface != 1) return;
    if (host->surface_image != NULL) lv_obj_delete(host->surface_image);
    host->surface_image = NULL;
    host->surface_buffers = NULL;
    free(host->surface_display_buffer);
    host->surface_display_buffer = NULL;
    for (uint8_t index = 0; index < 2; ++index) {
        free(host->raster_buffers[index]);
        free(host->raster_draw_lists[index]);
        pxa_raster_bindings_release(&host->raster_frame_bindings[index]);
        host->raster_buffers[index] = NULL;
        host->raster_draw_lists[index] = NULL;
    }
    free(host->raster_depth_buffer);
    host->raster_depth_buffer = NULL;
    pxa_raster_bindings_release(&host->raster_bindings);
    host->surface_frame_bytes = 0;
    host->surface_stride_bytes = 0;
    host->surface_display_frame_bytes = 0;
    host->surface_display_stride_bytes = 0;
    host->surface_width = 0;
    host->surface_height = 0;
    host->surface_display_width = 0;
    host->surface_display_height = 0;
    host->surface_buffer_count = 0;
    host->surface_scale = 0;
    host->surface_flags = 0;
    host->surface_registered = 0;
    host->surface_writing_buffer = -1;
    host->surface_pending_buffer = -1;
    host->surface_release_head = 0;
    host->surface_release_count = 0;
    host->surface_release_pending_mask = 0;
    host->raster_current_buffer = -1;
    host->raster_draw_pending = -1;
    host->raster_last_frame_id = 0;
    memset(&host->raster_telemetry, 0, sizeof(host->raster_telemetry));
}

static pxa_status_t game_render_create(
    void *context, const pxa_game_render_desc_t *desc,
    uint64_t *provider_context, uint32_t *capabilities) {
    pxa_surface_desc_t surface_desc = {0};
    uint32_t stride;
    pxa_status_t status;
    if (desc == NULL || provider_context == NULL || capabilities == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    surface_desc.width = desc->width;
    surface_desc.height = desc->height;
    surface_desc.format = PXA_SURFACE_FORMAT_RGB565;
    surface_desc.buffer_count = desc->buffer_count;
    surface_desc.flags = PRODUCT_SURFACE_FLAG_GAME_RENDER;
    if (desc->scratch_mode == PXA_GAME_RENDER_SCRATCH_NONE)
        surface_desc.flags |= PRODUCT_SURFACE_FLAG_RASTER_NO_SCRATCH;
    else if (desc->scratch_mode == PXA_GAME_RENDER_SCRATCH_COVERAGE_2BIT)
        surface_desc.flags |= PRODUCT_SURFACE_FLAG_RASTER_COVERAGE;
    else if (desc->scratch_mode != PXA_GAME_RENDER_SCRATCH_DEPTH16)
        return PXA_STATUS_UNSUPPORTED;
    if ((desc->flags & PXA_GAME_RENDER_FLAG_PREFER_DIRECT_SCANOUT) != 0)
        surface_desc.flags |= PXA_SURFACE_FLAG_PREFER_DIRECT_SCANOUT;
    status = surface_create_with_raster_capacity(
        context, &surface_desc, provider_context, &stride,
        desc->max_draw_bytes);
    if (status != PXA_STATUS_OK) return status;
    *capabilities = PXA_RASTER_CAP_FLAT_QUAD |
                    PXA_RASTER_CAP_TEXTURED_QUAD |
                    PXA_RASTER_CAP_ADDITIVE_SPRITE |
                    PXA_RASTER_CAP_SPRITE_BATCH |
                    PXA_RASTER_CAP_TRIANGLE_BATCH |
                    PXA_RASTER_CAP_AFFINE_UV |
                    PXA_RASTER_CAP_TEXTURE_SLOTS_48 |
                    PXA_RASTER_CAP_PAINTER_POLYGON |
                    PXA_RASTER_CAP_LIT_PALETTE_DEPTH |
                    PXA_RASTER_CAP_DEPTH_CUTOUT |
                    PXA_RASTER_CAP_FIXED_ALPHA_BLEND |
                    PXA_RASTER_CAP_COVERAGE_MASK |
                              PXA_RASTER_CAP_SPRITE_PALETTE_RAMP |
                              PXA_RASTER_CAP_SPRITE_TEXEL_ALPHA |
                              PXA_RASTER_CAP_PAINTER_PERSPECTIVE |
                              PXA_RASTER_CAP_PAINTER_DEPTH;
    return PXA_STATUS_OK;
}

static uint16_t surface_blend_alpha(uint16_t base, uint16_t color,
                                    uint8_t alpha) {
    if (alpha == 0) return base;
    if (alpha == 255) return color;
    const uint16_t inverse = (uint16_t)(255u - alpha);
    const uint16_t red = (uint16_t)(
        (((color >> 11) * alpha + (base >> 11) * inverse + 128u) >> 8));
    const uint16_t green = (uint16_t)(
        (((((color >> 5) & 0x3fu) * alpha +
           ((base >> 5) & 0x3fu) * inverse + 128u) >> 8)));
    const uint16_t blue = (uint16_t)(
        (((color & 0x1fu) * alpha + (base & 0x1fu) * inverse + 128u) >> 8));
    return (uint16_t)((red << 11) | (green << 5) | blue);
}

static void surface_composite_ui(product_host_t *host) {
    pxa_lvgl_ui_alpha_plane_t plane;
    if (host->ui_adapter == NULL ||
        !pxa_lvgl_ui_alpha_plane(host->ui_adapter, &plane)) return;
    const int32_t first_x = plane.x < 0 ? 0 : plane.x;
    const int32_t first_y = plane.y < 0 ? 0 : plane.y;
    const int32_t last_x = plane.x + plane.width < host->surface_display_width
                               ? plane.x + plane.width
                               : host->surface_display_width;
    const int32_t last_y = plane.y + plane.height < host->surface_display_height
                               ? plane.y + plane.height
                               : host->surface_display_height;
    if (first_x >= last_x || first_y >= last_y) return;
    for (int32_t y = first_y; y < last_y; ++y) {
        const uint32_t row = (uint32_t)(y - plane.y);
        const uint16_t *colors = (const uint16_t *)(
            (const uint8_t *)plane.pixels + row * plane.pixel_stride_bytes) +
            (first_x - plane.x);
        const uint8_t *alpha = plane.alpha + row * plane.alpha_stride_bytes +
                               (first_x - plane.x);
        uint16_t *destination = (uint16_t *)(host->surface_display_buffer +
            (uint32_t)y * host->surface_display_stride_bytes) + first_x;
        for (int32_t x = first_x; x < last_x; ++x) {
            *destination = surface_blend_alpha(*destination, *colors++, *alpha++);
            ++destination;
        }
    }
}

static int surface_process_pending(product_host_t *host) {
    uint8_t buffer_index = 0;
    uint64_t frame_id = 0;
    const uint16_t *source;
    if (host == NULL || !host->surface_registered ||
        host->surface_image == NULL)
        return 0;
    if ((host->surface_flags & PRODUCT_SURFACE_FLAG_GAME_RENDER) != 0) {
        pxa_raster_resources_t resources;
        /* The desktop backend owns every command; prefilled_commands stays 0
         * or the reference executor would skip the whole list. */
        pxa_raster_target_t target = {0};
        pxa_raster_draw_list_view_t list;
        pxa_raster_telemetry_t frame_telemetry = {0};
        uint64_t raster_started_us;
        uint64_t raster_finished_us;
        const uint8_t draw_index = (uint8_t)host->raster_draw_pending;
        if (host->raster_draw_pending < 0) return 0;
        buffer_index = host->raster_current_buffer == 0 ? 1 : 0;
        pxa_raster_bindings_view(&host->raster_frame_bindings[draw_index],
                                  PXA_RASTER_CAP_KNOWN_MASK, &resources);
        target.pixels = (uint16_t *)host->raster_buffers[buffer_index];
        target.depth_pixels = host->raster_depth_buffer;
        target.scratch_mode = host->raster_scratch_mode;
        target.stride_pixels = host->surface_stride_bytes / 2u;
        target.depth_stride_pixels = host->surface_width;
        target.width = host->surface_width;
        target.height = host->surface_height;
        list = host->raster_draw_views[draw_index];
        host->raster_draw_pending = -1;
        raster_started_us = now_us(NULL);
        pxa_raster_execute_draw_list(host->raster_draw_lists[draw_index], &list,
                                     &target, &resources, &frame_telemetry);
        raster_finished_us = now_us(NULL);
        pxa_raster_bindings_release(&host->raster_frame_bindings[draw_index]);
        ++host->raster_telemetry.rendered_frames;
        host->raster_telemetry.host_raster_us +=
            raster_finished_us - raster_started_us;
        host->raster_telemetry.last_host_raster_us =
            raster_finished_us - raster_started_us > UINT32_MAX
                ? UINT32_MAX
                : (uint32_t)(raster_finished_us - raster_started_us);
        host->raster_telemetry.draw_list_bytes += frame_telemetry.draw_list_bytes;
        host->raster_telemetry.covered_pixels += frame_telemetry.covered_pixels;
        host->raster_telemetry.clear_commands += frame_telemetry.clear_commands;
        host->raster_telemetry.flat_quad_commands += frame_telemetry.flat_quad_commands;
        host->raster_telemetry.textured_quad_commands += frame_telemetry.textured_quad_commands;
        host->raster_telemetry.sprite_commands += frame_telemetry.sprite_commands;
        host->raster_telemetry.last_draw_list_bytes = frame_telemetry.last_draw_list_bytes;
        host->raster_telemetry.last_covered_pixels = frame_telemetry.last_covered_pixels;
        host->raster_current_buffer = (int8_t)buffer_index;
        source = (const uint16_t *)host->raster_buffers[buffer_index];
    } else {
        if (host->surface_pending_buffer < 0) return 0;
        buffer_index = (uint8_t)host->surface_pending_buffer;
        frame_id = host->surface_pending_frame_id;
        source = (const uint16_t *)(host->surface_buffers +
            (size_t)buffer_index * host->surface_frame_bytes);
    }
    {
        uint16_t *destination = (uint16_t *)host->surface_display_buffer;
        const uint32_t source_stride = host->surface_stride_bytes / 2u;
        const uint32_t destination_stride =
            host->surface_display_stride_bytes / 2u;
        const uint32_t display_width = host->surface_display_width;
        const uint32_t display_height = host->surface_display_height;
        const uint32_t drawn_width =
            (uint32_t)host->surface_width * host->surface_scale;
        const uint32_t drawn_height =
            (uint32_t)host->surface_height * host->surface_scale;
        uint32_t origin_x = 0;
        uint32_t origin_y = 0;
        uint32_t copy_width = drawn_width;
        uint32_t copy_height = drawn_height;
        if (host->surface_layer.x > 0) origin_x = (uint32_t)host->surface_layer.x;
        if (host->surface_layer.y > 0) origin_y = (uint32_t)host->surface_layer.y;
        if (origin_x == 0 && origin_y == 0 &&
            (drawn_width < display_width || drawn_height < display_height)) {
            origin_x = (display_width - drawn_width) / 2u;
            origin_y = (display_height - drawn_height) / 2u;
        }
        if (origin_x >= display_width || origin_y >= display_height) {
            copy_width = 0;
            copy_height = 0;
        } else {
            if (copy_width > display_width - origin_x)
                copy_width = display_width - origin_x;
            if (copy_height > display_height - origin_y)
                copy_height = display_height - origin_y;
        }
        if (origin_x != 0 || origin_y != 0 || copy_width != display_width ||
            copy_height != display_height)
            memset(destination, 0, host->surface_display_frame_bytes);
        if (host->surface_scale == 1 && origin_x == 0 && origin_y == 0 &&
            copy_width == display_width && copy_height == display_height &&
            source_stride == destination_stride) {
            memcpy(destination, source, host->surface_display_frame_bytes);
        } else {
            for (uint32_t y = 0; y < copy_height; ++y) {
                const uint16_t *source_row = source +
                    (uint32_t)(y / host->surface_scale) * source_stride;
                uint16_t *destination_row =
                    destination + (origin_y + y) * destination_stride + origin_x;
                if (host->surface_scale == 1) {
                    memcpy(destination_row, source_row,
                           (size_t)copy_width * sizeof(*destination_row));
                } else {
                    for (uint32_t x = 0; x < copy_width; ++x)
                        destination_row[x] =
                            source_row[x / host->surface_scale];
                }
            }
        }
    }
    surface_composite_ui(host);
    if ((host->surface_flags & PXA_SURFACE_FLAG_GUEST_MAPPED) != 0) {
        host->surface_pending_buffer = -1;
        host->surface_pending_frame_id = 0;
    }
    ++host->surface_presented_frames;
    if ((host->surface_flags & PRODUCT_SURFACE_FLAG_GAME_RENDER) != 0)
        ++host->raster_telemetry.visible_frames;
    if ((host->surface_flags & PXA_SURFACE_FLAG_GUEST_MAPPED) != 0 &&
        !surface_enqueue_release(host, buffer_index, frame_id)) return 0;
    lv_image_set_src(host->surface_image, &host->surface_bitmap);
    lv_obj_move_background(host->surface_image);
    lv_obj_invalidate(host->surface_image);
    return 1;
}

static pxa_status_t surface_peek_release(void *context, uint64_t surface,
                                         pxa_surface_release_t *release) {
    product_host_t *host = context;
    if (host == NULL || surface != 1 || release == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (host->surface_release_count == 0) return PXA_STATUS_WOULD_BLOCK;
    *release = host->surface_releases[host->surface_release_head];
    return PXA_STATUS_OK;
}

static void surface_consume_release(void *context, uint64_t surface) {
    product_host_t *host = context;
    pxa_surface_release_t *release;
    if (host == NULL || surface != 1 || host->surface_release_count == 0)
        return;
    release = &host->surface_releases[host->surface_release_head];
    host->surface_release_pending_mask &=
        (uint8_t)~(uint8_t)(1u << release->buffer_index);
    memset(release, 0, sizeof(*release));
    host->surface_release_head =
        (uint8_t)((host->surface_release_head + 1u) % host->surface_buffer_count);
    --host->surface_release_count;
    ++host->surface_released_frames;
}

static void *reallocate_memory(void *context, void *memory, size_t size) {
    (void)context;
    return realloc(memory, size);
}

static pxa_status_t execute_inline(pxa_lvgl_ui_execute_callback_fn callback,
                                   void *data, void *context) {
    (void)context;
    callback(data);
    return PXA_STATUS_OK;
}

static pxa_status_t acquire_ui_image(pxa_component_t component, pxa_handle64_t handle, pxa_asset_object_t **output, void *context) {
    product_host_t *host=context;
    return pxa_assets_acquire_handle(host->runtime,component,handle,PXA_ASSET_IMAGE,output);
}

static const void *resolve_asset(const uint8_t *path, size_t path_size,
                                 void *context) {
#ifdef PXSYS_PRODUCT_ASSET_LOOKUP_OBSERVER
    PXSYS_PRODUCT_ASSET_LOOKUP_OBSERVER(path, path_size);
#endif
    product_host_t *host = context;
    static const uint8_t png_signature[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
    };
    char full_path[1536];
    struct stat metadata;
    FILE *file = NULL;
    product_asset_t *asset = NULL;
    size_t offset = 0;
    if (host == NULL || !asset_path_is_safe(path, path_size) ||
        path_size > 512 || strlen(host->package_root) + path_size + 2 >= 1024)
        return NULL;
    if (snprintf(full_path, sizeof(full_path), "%s/%.*s", host->package_root,
                 (int)path_size, path) >= (int)sizeof(full_path) ||
        stat(full_path, &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
        metadata.st_size < 24 ||
        (uintmax_t)metadata.st_size > PRODUCT_ASSET_MAX_BYTES)
        return NULL;
    asset = resource_allocate(host, PXA_MEMORY_INTERNAL, PXA_MEMORY_METADATA, sizeof(*asset));
    if (asset == NULL) return NULL;
    memset(asset, 0, sizeof(*asset));
    asset->bytes = resource_allocate(host, PXA_MEMORY_EXTERNAL, PXA_MEMORY_IMAGE,
                                      (size_t)metadata.st_size);
    if (asset->bytes == NULL) goto failed;
    file = fopen(full_path, "rb");
    if (file == NULL) goto failed;
    while (offset < (size_t)metadata.st_size) {
        size_t read = fread(asset->bytes + offset, 1,
                            (size_t)metadata.st_size - offset, file);
        if (read == 0) goto failed;
        offset += read;
    }
    fclose(file);
    file = NULL;
    if (memcmp(asset->bytes, png_signature, sizeof(png_signature)) != 0 ||
        memcmp(asset->bytes + 12, "IHDR", 4) != 0 ||
        read_be_u32(asset->bytes + 16) == 0 ||
        read_be_u32(asset->bytes + 20) == 0 ||
        read_be_u32(asset->bytes + 16) > PRODUCT_ASSET_MAX_DIMENSION ||
        read_be_u32(asset->bytes + 20) > PRODUCT_ASSET_MAX_DIMENSION)
        goto failed;
    asset->descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    asset->descriptor.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
    asset->descriptor.header.w = read_be_u32(asset->bytes + 16);
    asset->descriptor.header.h = read_be_u32(asset->bytes + 20);
    asset->descriptor.data_size = (size_t)metadata.st_size;
    asset->descriptor.data = asset->bytes;
    return &asset->descriptor;
failed:
    if (file != NULL) fclose(file);
    pxa_memory_release(asset == NULL ? NULL : asset->bytes);
    pxa_memory_release(asset);
    return NULL;
}

static void release_asset(const void *asset, void *context) {
    (void)context;
    if (asset != NULL) {
        product_asset_t *owned = (product_asset_t *)asset;
        lv_image_cache_drop(&owned->descriptor);
        pxa_memory_release(owned->bytes);
        pxa_memory_release(owned);
    }
}

static pxa_status_t asset_find(void *context, pxa_component_t component,
    pxa_bytes_t path, pxa_asset_info_t *info) {
    product_host_t *host = context;
    (void)component; /* All components belong to this authenticated package. */
    return host->asset_worker ? pxa_posix_asset_worker_find(host->asset_worker, path, info) : PXA_STATUS_NOT_FOUND;
}
static pxa_status_t asset_request(void *context, pxa_component_t component,
    pxa_bytes_t path, uint8_t cls, pxa_asset_ticket_t *ticket) {
    product_host_t *host = context;
    return host->asset_worker ? pxa_posix_asset_worker_request(host->asset_worker, component, path, cls, ticket) : PXA_STATUS_NOT_FOUND;
}
static pxa_status_t asset_prefetch(void *context, pxa_component_t component,
    pxa_bytes_t path, uint8_t cls, pxa_asset_ticket_t *ticket) {
    product_host_t *host = context;
    return host->asset_worker ? pxa_posix_asset_worker_prefetch(host->asset_worker, component, path, cls, ticket) : PXA_STATUS_NOT_FOUND;
}
static pxa_status_t asset_inspect(void *context, pxa_component_t component,
    pxa_bytes_t path, pxa_asset_request_state_t *state) {
    product_host_t *host = context;
    return host->asset_worker ? pxa_posix_asset_worker_inspect(host->asset_worker, component, path, state) : PXA_STATUS_NOT_FOUND;
}
static pxa_status_t asset_query(void *context, pxa_component_t component,
    pxa_asset_ticket_t ticket, pxa_asset_request_state_t *state) {
    product_host_t *host = context;
    return pxa_posix_asset_worker_query(host->asset_worker, component, ticket, state);
}
static pxa_status_t asset_acquire(void *context, pxa_component_t component,
    pxa_asset_ticket_t ticket, pxa_raster_asset_t **asset) {
    product_host_t *host = context;
    return pxa_posix_asset_worker_acquire(host->asset_worker, component, ticket, asset);
}
static void asset_release(void *context, pxa_component_t component, pxa_asset_ticket_t ticket) {
    product_host_t *host = context;
    (void)pxa_posix_asset_worker_release(host->asset_worker, component, ticket);
}
static pxa_status_t asset_read(void *context, pxa_component_t component, pxa_bytes_t path,
    uint32_t offset, uint32_t bytes, uint64_t *ticket) {
    product_host_t *host = context;
    return host->asset_worker ? pxa_posix_asset_worker_read(host->asset_worker,component,path,offset,bytes,ticket) : PXA_STATUS_NOT_FOUND;
}
static pxa_status_t asset_read_result(void *context, pxa_component_t component, uint64_t ticket, pxa_bytes_t *result) {
    product_host_t *host = context;
    return pxa_posix_asset_worker_read_result(host->asset_worker,component,ticket,result);
}
static void asset_read_release(void *context, pxa_component_t component, uint64_t ticket) {
    product_host_t *host = context;
    (void)pxa_posix_asset_worker_read_release(host->asset_worker,component,ticket);
}
static void *asset_allocate(void *context, size_t bytes, uint8_t cls, uint8_t kind) {
    return resource_allocate(context, cls, kind == PXA_ASSET_AUDIO ? PXA_MEMORY_AUDIO :
        kind == PXA_ASSET_IMAGE ? PXA_MEMORY_IMAGE : PXA_MEMORY_RASTER, bytes);
}
static void asset_notify(void *context) {
    product_host_t *host = context;
    SDL_AtomicSet(&host->asset_ready, 1);
}
static int manifest_uses_service(const pxa_package_manifest_t *manifest,uint16_t service) {
    for (uint16_t c = 0; c < manifest->component_count; ++c)
        for (uint16_t s = 0; s < manifest->components[c].service_count; ++s)
            if (manifest->components[c].services[s].service == service)
                return 1;
    return 0;
}
static int manifest_has_music(const pxa_package_manifest_t *manifest) {
    for (uint32_t i=0;i<manifest->file_count;++i) {
        pxa_bytes_t path=manifest->files[i].path;
        if (path.size<4) continue;
        const uint8_t *suffix=path.data+path.size-4;
        if (suffix[0]=='.' && (suffix[1]=='o'||suffix[1]=='O') &&
            (suffix[2]=='g'||suffix[2]=='G') && (suffix[3]=='g'||suffix[3]=='G')) return 1;
    }
    return 0;
}

static size_t asset_setting(const char *name, size_t fallback, size_t maximum) {
    const char *value = getenv(name);
    char *end;
    unsigned long long parsed;
    if (!value || !*value) return fallback;
    errno = 0;
    parsed = strtoull(value, &end, 10);
    if (errno || *end || parsed > maximum) return fallback;
    return (size_t)parsed;
}

static void dispatch_component_events(product_host_t *host) {
    pxa_wamr_event_result_t result;
    if (host == NULL || host->engine == NULL || host->runtime == NULL ||
        host->active_component == PXA_COMPONENT_INVALID)
        return;
    for (unsigned round = 0; round < 64; ++round) {
        int delivered = 0;
        if (host->assets != NULL) pxa_assets_service_poll(host->assets);
        if (host->audio != NULL) pxa_audio_service_poll(host->audio);
        if (host->sensor != NULL && pxa_sensor_has_active_subscriptions(host->sensor)) {
            pxa_component_t affected[PRODUCT_COMPONENTS];
            size_t count = 0;
            (void)pxa_sensor_poll(host->sensor, now_us(NULL), affected,
                                  PRODUCT_COMPONENTS, &count);
        }
        SDL_AtomicSet(&host->asset_ready, 0);
        if (host->ipc != NULL) (void)pxa_ipc_flush(host->ipc);
        for (uint16_t index = 0; index < host->service_component_count;
             ++index) {
            if (pxa_wamr_engine_deliver_event_result(
                    host->engine, host->runtime,
                    host->service_components[index], &result) ==
                PXA_STATUS_OK) delivered = 1;
        }
        if (host->running_work_component != PXA_COMPONENT_INVALID &&
            pxa_wamr_engine_deliver_event_result(
                host->engine, host->runtime, host->running_work_component,
                &result) == PXA_STATUS_OK) delivered = 1;
        if (pxa_wamr_engine_deliver_event_result(
                host->engine, host->runtime, host->active_component,
                &result) == PXA_STATUS_OK) {
            delivered = 1;
            if (result.service == PXA_WINDOW_SERVICE_ID &&
                result.opcode == PXA_WINDOW_BACK_REQUESTED &&
                result.request_id == 0 && result.payload_size == 0 &&
                result.guest_result == 0)
                host->exit_requested = 1;
        }
        if (!delivered) break;
    }
}

static void *ipc_allocate(void *context, size_t size) {
    (void)context;
    return malloc(size);
}

static void ipc_release(void *context, void *memory) {
    (void)context;
    free(memory);
}

static pxa_status_t resolve_ipc_endpoint(
    void *context, pxa_bytes_t name, pxa_component_t *provider) {
    product_host_t *host = context;
    if (host == NULL || host->manifest == NULL || host->coordinator == NULL ||
        provider == NULL) return PXA_STATUS_BAD_STATE;
    *provider = PXA_COMPONENT_INVALID;
    for (uint16_t index = 0; index < host->manifest->ipc_endpoint_count;
         ++index) {
        const pxa_package_ipc_endpoint_t *endpoint =
            &host->manifest->ipc_endpoints[index];
        uint64_t instance_id = 0;
        pxa_status_t status;
        if (endpoint->name.size != name.size ||
            memcmp(endpoint->name.data, name.data, name.size) != 0) continue;
        status = pxa_activation_find(host->coordinator,
                                     endpoint->component_id,
                                     &instance_id, provider);
        if (status == PXA_STATUS_OK) return status;
        if (status != PXA_STATUS_NOT_FOUND) return status;
        if (host->service_component_count >= PRODUCT_COMPONENTS - 1u)
            return PXA_STATUS_RESOURCE_LIMIT;
        status = pxa_activation_activate(host->coordinator,
                                         endpoint->component_id,
                                         ++host->next_instance_id, provider);
        if (status == PXA_STATUS_OK)
            host->service_components[host->service_component_count++] =
                *provider;
        return status;
    }
    return PXA_STATUS_NOT_FOUND;
}

static void sync_host_theme(product_host_t *host, pxa_lvgl_ui_t *adapter,
                            pxa_lvgl_ui_theme_t *adapter_theme) {
    pxa_ui_theme_snapshot_t theme;
    pxa_ui_environment_t environment;
    uint8_t scheme;
    if (host->host_theme == NULL ||
        host->theme_generation == host->host_theme->generation ||
        pxa_ui_get_theme(host->ui, &theme) != PXA_STATUS_OK)
        return;
    scheme = host->host_theme->effective_scheme == PXSYS_COLOR_SCHEME_DARK
                 ? PXA_UI_COLOR_SCHEME_DARK : PXA_UI_COLOR_SCHEME_LIGHT;
    map_host_theme(host->host_theme, adapter_theme->rgba);
    if (adapter != NULL)
        (void)pxa_lvgl_ui_set_theme(adapter, adapter_theme);
    memcpy(theme.rgba, adapter_theme->rgba, sizeof(theme.rgba));
    theme.color_scheme = scheme;
    theme.generation++;
    if (theme.generation == 0) theme.generation = 1;
    if (pxa_ui_update_theme(host->ui, &theme) != PXA_STATUS_OK) return;
    if (host->active_component != PXA_COMPONENT_INVALID &&
        pxa_ui_get_environment(host->ui, host->active_component,
                               PXA_UI_PRIMARY_SURFACE,
                               &environment) == PXA_STATUS_OK &&
        environment.color_scheme != scheme) {
        environment.color_scheme = scheme;
        (void)pxa_ui_update_environment(host->ui, host->active_component,
                                        &environment);
    }
    host->theme_generation = host->host_theme->generation;
    dispatch_component_events(host);
}

static void product_focus_changed(void *context, bool focused) {
    product_host_t *host = context;
    if (host == NULL || host->focused == (uint8_t)focused) return;
    host->focused = focused ? 1u : 0u;
    host->pending_lifecycle = host->focused;
    if (host->clock_period_ms != 0)
        host->next_clock_tick_us =
            now_us(NULL) + (uint64_t)host->clock_period_ms * UINT64_C(1000);
    if (host->audio_device != 0)
        SDL_PauseAudioDevice(host->audio_device, !focused);
}

static void product_request_exit(void *context) {
    product_host_t *host = context;
    if (host != NULL) host->exit_requested = 1;
}

static void dispatch_lifecycle(product_host_t *host) {
    uint8_t state;
    if (host == NULL || host->pending_lifecycle > 1u ||
        host->active_component == PXA_COMPONENT_INVALID) return;
    state = host->pending_lifecycle;
    if (pxa_event_post_message(host->runtime, host->active_component,
                               PRODUCT_LIFECYCLE_SERVICE,
                               PRODUCT_LIFECYCLE_EVENT, 0,
                               (pxa_bytes_t){&state, sizeof(state)}, 0,
                               UINT64_C(0x001100008005)) == PXA_STATUS_OK) {
        host->pending_lifecycle = 2u;
        dispatch_component_events(host);
    }
}

/* Consume a short event/presentation chain in one UI turn. This avoids adding
 * a full simulator-loop delay between a Guest frame submission and scanout. */
static int drain_surface_updates(product_host_t *host) {
    uint8_t round;
    if (host == NULL || host->surfaces == NULL) return 0;
    for (round = 0; round < 4; ++round) {
        int32_t released;
        if (!surface_process_pending(host)) return 0;
        released = pxa_surface_service_flush_releases(host->surfaces);
        if (released < 0) {
            fprintf(stderr, "PXA surface release status=%d\n", (int)released);
            return -1;
        }
        if (released > 0) dispatch_component_events(host);
    }
    return 0;
}

static void dispatch_clock_tick(product_host_t *host) {
    uint64_t now;
    uint64_t period_us;
    uint8_t payload[8];
    pxa_status_t status;
    if (host == NULL || host->runtime == NULL ||
        host->active_component == PXA_COMPONENT_INVALID ||
        host->clock_period_ms == 0 || !host->focused)
        return;
    now = now_us(NULL);
    if (now < host->next_clock_tick_us) return;
    period_us = (uint64_t)host->clock_period_ms * UINT64_C(1000);
    /* Keep the cadence anchored to its original deadline. Re-basing on
     * `now` makes ordinary scheduler jitter accumulate into a slower clock. */
    do {
        host->next_clock_tick_us += period_us;
    } while (host->next_clock_tick_us <= now);
    pxa_write_u64(payload, now);
    status = pxa_event_post_message(host->runtime, host->active_component,
                                    PRODUCT_CLOCK_SERVICE, PRODUCT_CLOCK_TICK, 0,
                                    (pxa_bytes_t){payload, sizeof(payload)}, 0,
                                    UINT64_C(0x000400008001));
    if (status == PXA_STATUS_OK)
        dispatch_component_events(host);
}

static uint32_t limit_delay_to_clock_deadline(const product_host_t *host,
                                              uint32_t delay_ms) {
    uint64_t remaining_us;
    uint32_t clock_delay_ms;
    if (host == NULL || !host->focused || host->clock_period_ms == 0 ||
        host->next_clock_tick_us == 0)
        return delay_ms;
    {
        const uint64_t now = now_us(NULL);
        if (now >= host->next_clock_tick_us) return 1;
        remaining_us = host->next_clock_tick_us - now;
    }
    clock_delay_ms = (uint32_t)((remaining_us + 999u) / 1000u);
    if (clock_delay_ms == 0) clock_delay_ms = 1;
    return clock_delay_ms < delay_ms ? clock_delay_ms : delay_ms;
}

static void ui_event(uint32_t surface, uint32_t node, pxa_ui_event_kind_t kind,
                     uint16_t flags, const void *value, size_t value_size,
                     void *context) {
    product_host_t *host = context;
    if (host == NULL || host->ui == NULL ||
        host->active_component == PXA_COMPONENT_INVALID)
        return;
    if (pxa_ui_queue_event(host->ui, host->active_component, surface, node,
                           kind, flags, now_us(NULL), value, value_size) ==
        PXA_STATUS_OK)
        dispatch_component_events(host);
}

static pxa_status_t window_apply(void *context,
                                 const pxa_window_configuration_t *config) {
    product_host_t *host = context;
    if (host == NULL || config == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    if (host->active_component != PXA_COMPONENT_INVALID) {
        pxa_window_snapshot_t snapshot;
        if (pxa_window_get_snapshot(host->window, host->active_component,
                                    &snapshot) == PXA_STATUS_OK) {
            snapshot.system_bar_insets.top =
                config->status_bar_mode == PXA_WINDOW_BAR_HIDDEN
                    ? 0u : host->host_bar_insets.top;
            snapshot.system_bar_insets.bottom =
                config->navigation_bar_mode == PXA_WINDOW_BAR_HIDDEN
                    ? 0u : host->host_bar_insets.bottom;
            (void)pxa_window_update_snapshot(host->window,
                                              host->active_component, &snapshot);
        }
    }
    if (host->window_changed != NULL)
        host->window_changed(host->window_context, config);
    return PXA_STATUS_OK;
}

static pxa_status_t prepare_start(void *context, pxa_component_t component,
                                  uint64_t instance_id, uint8_t kind) {
    product_host_t *host = context;
    pxa_window_backend_t window_backend = {0};
    pxa_window_snapshot_t snapshot = {0};
    pxa_status_t status;
    (void)instance_id;
    if (host == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    if (kind == PXA_COMPONENT_KIND_SERVICE ||
        kind == PXA_COMPONENT_KIND_JOB) return PXA_STATUS_OK;
    if (kind != PXA_COMPONENT_KIND_UI) return PXA_STATUS_UNSUPPORTED;
    window_backend.struct_size = sizeof(window_backend);
    window_backend.context = host;
    window_backend.apply = window_apply;
    window_backend.show_toast = window_toast;
    status = pxa_window_bind(host->window, component, &window_backend);
    if (status != PXA_STATUS_OK) return status;
    status = pxa_ui_bind(host->ui, component, &host->ui_backend);
    if (status != PXA_STATUS_OK) return status;
    snapshot.logical_width = host->width;
    snapshot.logical_height = host->height;
    snapshot.pixel_width = host->width;
    snapshot.pixel_height = host->height;
    snapshot.density_numerator = 1;
    snapshot.density_denominator = 1;
    snapshot.safe_insets.left = host->safe_insets[3];
    snapshot.safe_insets.top = host->safe_insets[0];
    snapshot.safe_insets.right = host->safe_insets[1];
    snapshot.safe_insets.bottom = host->safe_insets[2];
    snapshot.system_bar_insets.left = host->host_bar_insets.left;
    snapshot.system_bar_insets.top = host->host_bar_insets.top;
    snapshot.system_bar_insets.right = host->host_bar_insets.right;
    snapshot.system_bar_insets.bottom = host->host_bar_insets.bottom;
    snapshot.focused = host->focused;
    status = pxa_window_update_snapshot(host->window, component, &snapshot);
    if (status != PXA_STATUS_OK) return status;
    host->active_component = component;
    return PXA_STATUS_OK;
}

static pxa_status_t read_artifact(void *context, pxa_bytes_t path,
                                  uint8_t *output, size_t capacity,
                                  size_t *size) {
    FILE *file;
    long file_size;
    char filename[1536];
    product_host_t *host = context;
    if (host == NULL || path.data == NULL || size == NULL ||
        path.size == 0 || path.size >= sizeof(filename))
        return PXA_STATUS_INVALID_ARGUMENT;
    /* The WAMR adapter already joins the package root and artifact path. */
    memcpy(filename, path.data, path.size);
    filename[path.size] = '\0';
    file = fopen(filename, "rb");
    if (file == NULL) return PXA_STATUS_NOT_FOUND;
    if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) <= 0) {
        fclose(file);
        return PXA_STATUS_IO_ERROR;
    }
    rewind(file);
    if (output == NULL) {
        *size = (size_t)file_size;
        fclose(file);
        return PXA_STATUS_OK;
    }
    if ((size_t)file_size > capacity ||
        fread(output, 1, (size_t)file_size, file) != (size_t)file_size) {
        fclose(file);
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    fclose(file);
    *size = (size_t)file_size;
    return PXA_STATUS_OK;
}

/* Visual integration tests can fix Clock NOW without changing frame pacing. */
#ifndef PXSYS_CLOCK_NOW_US
#define PXSYS_CLOCK_NOW_US() now_us(NULL)
#endif

static pxa_status_t clock_control(void *context, pxa_runtime_t *runtime,
                                  pxa_component_t component,
                                  const pxa_message_view_t *message) {
    product_host_t *host = context;
    uint16_t core_major = 0;
    if (host == NULL || message == NULL || component != host->active_component)
        return PXA_STATUS_BAD_STATE;
    if (message->opcode == PRODUCT_CLOCK_NOW) {
        uint8_t result[8];
        pxa_status_t status;
        if (message->request_id == 0 || message->payload.size != 0)
            return PXA_STATUS_INVALID_ARGUMENT;
        status = pxa_component_core_major(runtime, component, &core_major);
        if (status != PXA_STATUS_OK) return status;
        if (core_major == 1) {
            status = pxa_request_begin_reserved(
                runtime, component, message->request_id,
                PRODUCT_CLOCK_SERVICE, PRODUCT_CLOCK_NOW, 0,
                sizeof(result));
            if (status != PXA_STATUS_OK) return status;
            status = pxa_request_commit(runtime, component,
                                        message->request_id);
            if (status == PXA_STATUS_OK) {
                pxa_write_u64(result, PXSYS_CLOCK_NOW_US());
                status = pxa_request_complete(runtime, component,
                                              message->request_id,
                                              PXA_STATUS_OK, result,
                                              sizeof(result));
            }
            return status;
        }
        {
            uint8_t payload[12];
            pxa_write_u32(payload, PXA_STATUS_OK);
            pxa_write_u64(payload + 4, PXSYS_CLOCK_NOW_US());
            return pxa_event_post_message(
                runtime, component, PRODUCT_CLOCK_SERVICE,
                PRODUCT_CLOCK_NOW_RESULT, message->request_id,
                (pxa_bytes_t){payload, sizeof(payload)}, 1, 0);
        }
    }
    if (message->opcode != 1 || message->request_id != 0 ||
        message->payload.size != 2)
        return PXA_STATUS_UNSUPPORTED;
    host->clock_period_ms = (uint16_t)message->payload.data[0] |
                            (uint16_t)((uint16_t)message->payload.data[1] << 8);
    if (host->clock_period_ms != 0 &&
        (host->clock_period_ms < 16 || host->clock_period_ms > 1000))
        return PXA_STATUS_INVALID_ARGUMENT;
    host->next_clock_tick_us =
        now_us(NULL) + (uint64_t)host->clock_period_ms * UINT64_C(1000);
    return PXA_STATUS_OK;
}

/* The desktop Host can browse Store without an ESP package store. Lists are
 * empty; install, launch and removal complete as unsupported so the Guest
 * receives a real Core v1 completion instead of waiting indefinitely. */
static pxa_status_t store_installer_sim_control(
    void *context, pxa_runtime_t *runtime, pxa_component_t component,
    const pxa_message_view_t *message) {
    pxa_status_t status;
    uint8_t empty_count = 0;
    const int list = message != NULL &&
        (message->opcode == 4u || message->opcode == 5u);
    (void)context;
    if (message == NULL || message->request_id == 0 ||
        message->opcode < 1u || message->opcode > 8u ||
        (list && message->payload.size != 0))
        return PXA_STATUS_INVALID_ARGUMENT;
    status = pxa_request_begin_reserved(runtime, component, message->request_id,
                                        PXA_STORE_INSTALLER_SERVICE_ID,
                                        message->opcode, 0, list ? 1u : 0u);
    if (status != PXA_STATUS_OK) return status;
    status = pxa_request_commit(runtime, component, message->request_id);
    if (status != PXA_STATUS_OK) return status;
    return pxa_request_complete(runtime, component, message->request_id,
                                list ? PXA_STATUS_OK : PXA_STATUS_UNSUPPORTED,
                                list ? &empty_count : NULL, list ? 1u : 0u);
}

static void audio_release_sound(product_sound_voice_t *sound);
/* Called only while SDL serializes the callback and control/decoder updates. */
static void audio_finish_music_locked(product_host_t *host,pxa_status_t status) {
    if (!host->music_session) return;
    pxa_audio_playback_finish(&host->music_events,host->music_token,
        status ? PXA_AUDIO_PLAYBACK_ERROR : PXA_AUDIO_PLAYBACK_ENDED,status);
    host->music_session=0;
    if (status) host->music_count=0;
    SDL_AtomicSet(&host->asset_ready,1);
}
static void audio_callback(void *context, uint8_t *stream, int bytes) {
    product_host_t *host = context;
    int16_t *output = (int16_t *)stream;
    int samples = bytes / (int)sizeof(*output);
    pxa_audio_mixer_render(&host->audio_mixer, output, (size_t)samples);
    uint32_t music_available=0;
    if (host->music_session && !host->music_paused)
        music_available=pxa_audio_buffer_take(&host->music_buffer,host->music_count,(uint32_t)samples,host->music_finished);
    for (int index = 0; index < samples; ++index) {
        int32_t mixed = 0;
        int32_t target_gain = 0;
        int32_t source_target = 0;
        mixed = output[index];
        if (host->music_session && !host->music_paused) {
            if ((uint32_t)index < music_available) {
                host->music_last_sample = host->music_ring[host->music_read];
                host->music_read = (host->music_read + 1u) % 8192u;
                --host->music_count;
                source_target = 32768;
                target_gain = host->music_gain_q15;
            } else if (host->music_finished && !host->music_count) {
                audio_finish_music_locked(host,host->music_error);
            }
        }
        if (host->music_output_gain_q15 < target_gain) {
            host->music_output_gain_q15 += 256;
            if (host->music_output_gain_q15 > target_gain)
                host->music_output_gain_q15 = target_gain;
        } else if (host->music_output_gain_q15 > target_gain) {
            host->music_output_gain_q15 -= 256;
            if (host->music_output_gain_q15 < target_gain)
                host->music_output_gain_q15 = target_gain;
        }
        if (host->music_source_gain_q15 < source_target) {
            host->music_source_gain_q15 += 512;
            if (host->music_source_gain_q15 > source_target)
                host->music_source_gain_q15 = source_target;
        } else if (host->music_source_gain_q15 > source_target) {
            host->music_source_gain_q15 -= 512;
            if (host->music_source_gain_q15 < source_target)
                host->music_source_gain_q15 = source_target;
        }
        mixed += (((int32_t)host->music_last_sample *
                   host->music_source_gain_q15) >> 15) *
                 host->music_output_gain_q15 >> 15;
        for (size_t voice = 0; voice < PRODUCT_AUDIO_SOUND_VOICES; ++voice) {
            product_sound_voice_t *sound = &host->sound_voices[voice];
            if (sound->paused || sound->position >= sound->samples) continue;
            uint32_t remaining = sound->samples - sound->position;
            uint32_t attack = sound->position + 1u;
            uint32_t envelope = remaining < attack ? remaining : attack;
            if (envelope > 64u) envelope = 64u;
            int32_t sample = ((int32_t)sound->pcm[sound->position++] - 128)
                             * 256;
            mixed += ((sample * sound->gain_q15) >> 15) *
                     (int32_t)envelope / 64;
            if (sound->position == sound->samples) audio_release_sound(sound);
        }
        if (mixed > INT16_MAX) mixed = INT16_MAX;
        if (mixed < INT16_MIN) mixed = INT16_MIN;
        output[index] = (int16_t)mixed;
    }
}

static int audio_voice(product_host_t *host, uint64_t session) {
    if (!session) return -1;
    for (unsigned i = 0; i < PXA_AUDIO_MIXER_VOICES; ++i)
        if (host->audio_sessions[i] == session) return (int)i;
    return -1;
}

static pxa_status_t audio_open(void *context, uint16_t usage,
                               pxa_audio_format_t *format, uint64_t *session) {
    product_host_t *host = context;
    SDL_AudioSpec desired = {0};
    if (usage != PXA_AUDIO_USAGE_MEDIA || format == NULL || session == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (host->audio_device == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        return PXA_STATUS_INTERNAL;
    if (host->audio_device == 0) {
        desired.freq = 16000;
        desired.format = AUDIO_S16SYS;
        desired.channels = 1;
        desired.samples = 512;
        desired.callback = audio_callback;
        desired.userdata = host;
        host->audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, NULL, 0);
        if (host->audio_device == 0) {
            fprintf(stderr, "PXA audio output unavailable: %s\n", SDL_GetError());
            return PXA_STATUS_INTERNAL;
        }
        SDL_PauseAudioDevice(host->audio_device, !host->focused);
    }
    format->sample_rate = 16000;
    format->channels = 1;
    format->frame_ms = 20;
    SDL_LockAudioDevice(host->audio_device);
    for (unsigned i = 0; i < PXA_AUDIO_MIXER_VOICES; ++i) {
        if (host->audio_sessions[i]) continue;
        if (++host->audio_next_session == 0) ++host->audio_next_session;
        host->audio_sessions[i] = *session = host->audio_next_session;
        pxa_audio_mixer_reset(&host->audio_mixer.voices[i]);
        SDL_UnlockAudioDevice(host->audio_device);
        return PXA_STATUS_OK;
    }
    SDL_UnlockAudioDevice(host->audio_device);
    return PXA_STATUS_RESOURCE_LIMIT;
    return PXA_STATUS_OK;
}

static pxa_status_t audio_commit(void *context, uint64_t session,
                                  const pxa_audio_graph_t *graph) {
    product_host_t *host = context;
    int voice = audio_voice(host, session);
    if (voice < 0) return PXA_STATUS_NOT_FOUND;
    SDL_LockAudioDevice(host->audio_device);
    pxa_status_t status = pxa_audio_mixer_commit(&host->audio_mixer.voices[voice], graph);
    SDL_UnlockAudioDevice(host->audio_device);
    return status;
}
static pxa_status_t audio_submit(void *context, uint64_t session,
                                  const uint8_t *pcm, size_t size) {
    product_host_t *host = context;
    int voice = audio_voice(host, session);
    if (voice < 0) return PXA_STATUS_NOT_FOUND;
    SDL_LockAudioDevice(host->audio_device);
    pxa_status_t status = pxa_audio_mixer_write(&host->audio_mixer.voices[voice], pcm, size);
    SDL_UnlockAudioDevice(host->audio_device);
    return status;
}
static pxa_status_t audio_play_tone(void *context, uint64_t session,
                                     const pxa_audio_tone_t *tone) {
    product_host_t *host = context;
    int voice = audio_voice(host, session);
    if (voice < 0) return PXA_STATUS_NOT_FOUND;
    SDL_LockAudioDevice(host->audio_device);
    pxa_status_t status = pxa_audio_mixer_tone(&host->audio_mixer.voices[voice], tone);
    SDL_UnlockAudioDevice(host->audio_device);
    return status;
}

typedef struct {
    product_host_t *host;
    uint64_t token;
    pxa_asset_stream_t stream;
    pxa_posix_asset_input_t file;
    pxa_asset_input_t raw;
    uint64_t io_us; /* Worker-only: subtract callback I/O from codec wall time. */
} product_music_input_t;

static int music_input_cancelled(void *context) {
    product_music_input_t *input=context;
    if (SDL_AtomicGet(&input->host->music_stop)) return 1;
    SDL_LockAudioDevice(input->host->audio_device);
    int cancelled=input->token!=input->host->music_token;
    SDL_UnlockAudioDevice(input->host->audio_device);
    return cancelled;
}
static pxa_status_t music_input_load(void *context,const pxa_asset_block_t *block,
    uint8_t *buffer,size_t capacity) {
    product_music_input_t *input=context;
    uint64_t started=now_us(NULL);
    pxa_status_t status=pxa_posix_asset_select_range(&input->file,block->offset,block->bytes);
    if (!status) status=pxa_asset_load_block(block,&input->raw,buffer,capacity);
    input->io_us+=now_us(NULL)-started;
    return status;
}
static void music_input_close(product_music_input_t *input) {
    pxa_posix_asset_close(&input->file);
    if (input->host) {
        SDL_LockAudioDevice(input->host->audio_device);
        input->host->music_read_bytes+=input->stream.read_bytes;
        input->host->music_read_calls+=input->stream.read_calls;
        SDL_UnlockAudioDevice(input->host->audio_device);
    }
    memset(&input->stream,0,sizeof(input->stream));
}
static pxa_status_t music_input_open(product_music_input_t *input,product_host_t *host,
    uint64_t token,const pxa_asset_block_map_t *map) {
    input->host=host; input->token=token;
    pxa_status_t status=pxa_posix_asset_open(&input->file,host->package_root,
        map->info.path,map->info.stored_bytes,&input->raw);
    input->file.gate=host->storage_gate; input->file.storage_lane=PXA_STORAGE_MUSIC;
    input->file.cancel_context=input; input->file.cancelled=music_input_cancelled;
    if (!status) status=pxa_asset_stream_init(&input->stream,map,
        input,music_input_load,music_input_cancelled);
    return status;
}
static size_t music_vorbis_read(void *output,size_t size,size_t count,void *context) {
    product_music_input_t *input=context;
    if (!size || count>SIZE_MAX/size) return 0;
    size_t total=0, wanted=size*count;
    while (total<wanted) {
        size_t bytes=0;
        if (pxa_asset_stream_read(&input->stream,(uint8_t*)output+total,wanted-total,&bytes) || !bytes) break;
        total+=bytes;
    }
    return total/size;
}
static int music_input_seek(void *context,int64_t offset,int whence) {
    product_music_input_t *input=context;
    uint32_t base;
    if (whence==SEEK_SET) base=0;
    else if (whence==SEEK_CUR) base=input->stream.position;
    else if (whence==SEEK_END) base=input->stream.map.info.stored_bytes;
    else return -1;
    if (offset<-(int64_t)base || offset>(int64_t)input->stream.map.info.stored_bytes-base) return -1;
    return pxa_asset_stream_seek(&input->stream,(uint32_t)((int64_t)base+offset)) ? -1 : 0;
}
static int music_vorbis_seek(void *context,ogg_int64_t offset,int whence) {
    return music_input_seek(context,offset,whence);
}
static long music_vorbis_tell(void *context) {
    product_music_input_t *input=context;
#if LONG_MAX < UINT32_MAX
    if (input->stream.position>LONG_MAX) return -1;
#endif
    return (long)input->stream.position;
}
static int music_opus_read(void *context,unsigned char *output,int capacity) {
    product_music_input_t *input=context; size_t count=0;
    if (capacity<0 || pxa_asset_stream_read(&input->stream,output,(size_t)capacity,&count)) return -1;
    return (int)count;
}
static int music_opus_seek(void *context,opus_int64 offset,int whence) {
    return music_input_seek(context,offset,whence);
}
static opus_int64 music_opus_tell(void *context) {
    return ((product_music_input_t*)context)->stream.position;
}

/* All decoder/file I/O runs on this worker. SDL's callback only consumes a
 * bounded ring; a long music file does not increase resident PCM memory. */
static void audio_music_publish_locked(product_host_t *host,uint64_t token) {
    if (pxa_audio_buffer_publish(&host->music_buffer,host->music_count,host->music_finished)) {
        pxa_audio_playback_ready(&host->music_events,token);
        uint64_t elapsed=now_us(NULL)-host->music_accepted_us;
        if (elapsed>host->music_ready_max_us) host->music_ready_max_us=elapsed;
        SDL_AtomicSet(&host->asset_ready,1);
    }
}
static int audio_music_worker(void *context) {
    product_host_t *host = context;
    OggVorbis_File file;
    OggOpusFile *opus = NULL;
    SDL_AudioStream *converter = NULL;
    product_music_input_t source={0}; source.file.fd=-1;
    uint64_t token = 0;
    int opened = 0, eof = 0, loop = 0;
    int16_t input[2048];
    int16_t output[2048];
    while (!SDL_AtomicGet(&host->music_stop)) {
        pxa_asset_block_map_t map;
        int pending, paused;
        uint64_t requested;
        uint32_t room;
        SDL_LockAudioDevice(host->audio_device);
        requested = host->music_token;
        pending = host->music_pending;
        paused = host->music_paused;
        room = 8192u - host->music_count;
        if (pending) {
            map=host->music_map;
            loop = host->music_loop;
            host->music_pending = 0;
        }
        SDL_UnlockAudioDevice(host->audio_device);
        if (requested != token || pending) {
            if (opened) ov_clear(&file);
            if (opus) { op_free(opus); opus = NULL; }
            if (converter) SDL_FreeAudioStream(converter);
            music_input_close(&source);
            opened = 0; converter = NULL; eof = 0; token = requested;
        }
        if (pending) {
            pxa_status_t status=music_input_open(&source,host,token,&map);
            const ov_callbacks vorbis_callbacks={music_vorbis_read,music_vorbis_seek,NULL,music_vorbis_tell};
            const OpusFileCallbacks opus_callbacks={music_opus_read,music_opus_seek,music_opus_tell,NULL};
            if (!status && map.info.encoding==PXA_ASSET_ENCODING_OGG_VORBIS &&
                ov_open_callbacks(&source,&file,NULL,0,vorbis_callbacks)==0) {
                opened = 1;
                vorbis_info *info = ov_info(&file, -1);
                if (info && info->channels >= 1 && info->channels <= 2 &&
                    info->rate >= 8000 && info->rate <= 96000) {
                    converter = SDL_NewAudioStream(AUDIO_S16SYS, (uint8_t)info->channels,
                        (int)info->rate, AUDIO_S16SYS, 1, 16000);
                    if (!converter) status=PXA_STATUS_RESOURCE_LIMIT;
                } else status=PXA_STATUS_UNSUPPORTED;
            }
            if (!status && map.info.encoding==PXA_ASSET_ENCODING_OGG_OPUS) {
                int error = 0;
                opus = op_open_callbacks(&source,&opus_callbacks,NULL,0,&error);
                if (opus && op_channel_count(opus,-1)>=1 && op_channel_count(opus, -1) <= 2) {
                    converter = SDL_NewAudioStream(AUDIO_S16SYS, 2, 48000,
                                                   AUDIO_S16SYS, 1, 16000);
                    if (!converter) status=PXA_STATUS_RESOURCE_LIMIT;
                } else if (opus) status=PXA_STATUS_UNSUPPORTED;
            }
            if (!converter) {
                if (opened) { ov_clear(&file); opened = 0; }
                if (opus) { op_free(opus); opus = NULL; }
                if (!status) status=source.stream.status ? source.stream.status : PXA_STATUS_PROTOCOL_ERROR;
                music_input_close(&source);
                fprintf(stderr, "PXA audio: Ogg input/decode failed status=%d\n",(int)status);
                SDL_LockAudioDevice(host->audio_device);
                if (host->music_token == token) {
                    host->music_error=status;
                    host->music_finished = 1;
                    audio_finish_music_locked(host,host->music_error);
                }
                SDL_UnlockAudioDevice(host->audio_device);
            }
        }
        if (!converter || paused || room < 2048) { SDL_Delay(5); continue; }
        int available = SDL_AudioStreamAvailable(converter);
        if (available == 0 && !eof) {
            int bitstream = 0;
            long count;
            uint64_t decode_started=now_us(NULL), io_before=source.io_us;
            if (opus) {
                int frames = op_read_stereo(opus, (opus_int16 *)input,
                                             sizeof(input) / sizeof(opus_int16));
                count = frames > 0 ? frames * 4 : frames;
            } else {
                count = ov_read(&file, (char *)input, sizeof(input),
                    SDL_BYTEORDER == SDL_BIG_ENDIAN, 2, 1, &bitstream);
            }
            uint64_t decode_elapsed=now_us(NULL)-decode_started-(source.io_us-io_before);
            SDL_LockAudioDevice(host->audio_device);
            ++host->music_decode_calls; host->music_decode_us+=decode_elapsed;
            if (decode_elapsed>host->music_decode_max_us) host->music_decode_max_us=decode_elapsed;
            SDL_UnlockAudioDevice(host->audio_device);
            if (source.stream.status) count=-1;
            if (count==0 && loop && (opus ? op_pcm_total(opus,-1)>0 : ov_pcm_total(&file,-1)>0)) {
                if ((opus ? op_pcm_seek(opus,0) : ov_pcm_seek(&file,0))==0) continue;
                count=-1; /* A failed loop rewind is not natural completion. */
            }
            if (count > 0) {
                if (SDL_AudioStreamPut(converter, input, (int)count) != 0) {
                    SDL_LockAudioDevice(host->audio_device);
                    if (host->music_token==token) host->music_error=PXA_STATUS_RESOURCE_LIMIT;
                    SDL_UnlockAudioDevice(host->audio_device);
                    eof = 1;
                }
            } else {
                if (count < 0 || source.stream.status) {
                    SDL_LockAudioDevice(host->audio_device);
                    if (host->music_token==token) host->music_error=source.stream.status ? source.stream.status : PXA_STATUS_PROTOCOL_ERROR;
                    SDL_UnlockAudioDevice(host->audio_device);
                }
                SDL_AudioStreamFlush(converter);
                eof = 1;
            }
            available = SDL_AudioStreamAvailable(converter);
        }
        int count = SDL_AudioStreamGet(converter, output, sizeof(output));
        if (count > 0) {
            SDL_LockAudioDevice(host->audio_device);
            if (host->music_token == token) {
                for (int i = 0; i < count / 2; ++i) {
                    host->music_ring[(host->music_read + host->music_count) % 8192u] = output[i];
                    ++host->music_count;
                }
                audio_music_publish_locked(host,token);
            }
            SDL_UnlockAudioDevice(host->audio_device);
        } else if (eof || available < 0 || count < 0) {
            SDL_LockAudioDevice(host->audio_device);
            if (host->music_token == token) {
                if (available < 0 || count < 0) host->music_error=PXA_STATUS_IO_ERROR;
                host->music_finished = 1;
                audio_music_publish_locked(host,token);
                if (!host->music_count || host->music_error)
                    audio_finish_music_locked(host,host->music_error);
            }
            SDL_UnlockAudioDevice(host->audio_device);
            SDL_FreeAudioStream(converter); converter = NULL;
            if (opened) ov_clear(&file);
            if (opus) { op_free(opus); opus = NULL; }
            music_input_close(&source);
            opened = 0;
        }
    }
    if (converter) SDL_FreeAudioStream(converter);
    if (opened) ov_clear(&file);
    if (opus) op_free(opus);
    music_input_close(&source);
    return 0;
}

static void audio_stop_worker(product_host_t *host) {
    if (host->music_thread) {
        SDL_AtomicSet(&host->music_stop, 1);
        SDL_WaitThread(host->music_thread, NULL);
        host->music_thread = NULL;
    }
}

static pxa_status_t audio_play_sound(void *context,uint64_t session,pxa_asset_object_t *asset,int16_t gain) {
    product_host_t *host = context;
    int voice = audio_voice(host,session);
    if (voice < 0 || !host->audio_device) return PXA_STATUS_NOT_FOUND;
    if (!host->audio_mixer.voices[voice].committed) return PXA_STATUS_BAD_STATE;
    pxa_asset_object_view_t view; pxa_asset_object_view(asset,&view);
    if (view.kind != PXA_ASSET_AUDIO || !view.bytes || view.bytes > 16000 || gain > 0 || gain < -60*256)
        return PXA_STATUS_INVALID_ARGUMENT;
    int32_t gain_q15 = (int32_t)(32768.0 * pow(10.0,gain/5120.0));
    pxa_status_t status = PXA_STATUS_WOULD_BLOCK;
    SDL_LockAudioDevice(host->audio_device);
    for (unsigned i=0;i<PRODUCT_AUDIO_SOUND_VOICES;++i) if (!host->sound_voices[i].asset) {
        pxa_asset_object_retain(asset);
        host->sound_voices[i] = (product_sound_voice_t){asset,view.data,view.bytes,0,gain_q15,session,0};
        status = PXA_STATUS_OK; break;
    }
    SDL_UnlockAudioDevice(host->audio_device);
    return status;
}
static void audio_release_sound(product_sound_voice_t *sound) {
    pxa_asset_object_release_pinned(sound->asset);
    memset(sound,0,sizeof(*sound));
}
static void audio_release_all_sounds(product_host_t *host) {
    for (unsigned i=0;i<PRODUCT_AUDIO_SOUND_VOICES;++i) audio_release_sound(&host->sound_voices[i]);
}

static pxa_status_t audio_play_music(void *context, uint64_t session,
                                     const pxa_audio_asset_t *asset, uint64_t *instance) {
    product_host_t *host = context;
    if (!instance) return PXA_STATUS_INVALID_ARGUMENT;
    *instance=0;
    pxa_asset_block_map_t map;
    if (audio_voice(host, session) < 0 || host->audio_device == 0) return PXA_STATUS_NOT_FOUND;
    if (!host->audio_mixer.voices[audio_voice(host, session)].committed)
        return PXA_STATUS_BAD_STATE;
    if (asset == NULL || asset->path == NULL || asset->path_size < 12 ||
        !pxa_asset_path_valid((pxa_bytes_t){asset->path,asset->path_size}) ||
        asset->gain_db_q8>0 || asset->gain_db_q8 < -60*256 ||
        (asset->flags & ~PXA_AUDIO_ASSET_LOOP))
        return PXA_STATUS_INVALID_ARGUMENT;
    if (!host->asset_worker) return PXA_STATUS_UNSUPPORTED;
    pxa_status_t found=pxa_posix_asset_worker_block_map(host->asset_worker,
        (pxa_bytes_t){asset->path,asset->path_size},&map);
    if (found) return found;
    if (map.info.kind!=PXA_ASSET_AUDIO ||
        (map.info.encoding!=PXA_ASSET_ENCODING_OGG_VORBIS && map.info.encoding!=PXA_ASSET_ENCODING_OGG_OPUS))
        return PXA_STATUS_UNSUPPORTED;
    SDL_LockAudioDevice(host->audio_device);
    int busy = host->music_session && host->music_session != session;
    SDL_UnlockAudioDevice(host->audio_device);
    if (busy) return PXA_STATUS_WOULD_BLOCK;
    if (!host->music_thread) {
        SDL_AtomicSet(&host->music_stop, 0);
        host->music_thread = SDL_CreateThread(audio_music_worker, "pxa_music", host);
        if (!host->music_thread) return PXA_STATUS_RESOURCE_LIMIT;
    }
    SDL_LockAudioDevice(host->audio_device);
    pxa_status_t admission=pxa_audio_playback_begin(&host->music_events,session,instance);
    if (admission!=PXA_STATUS_OK) { SDL_UnlockAudioDevice(host->audio_device); return admission; }
    pxa_audio_playback_finish(&host->music_events,host->music_token,PXA_AUDIO_PLAYBACK_REPLACED,PXA_STATUS_OK);
    host->music_map=map;
    host->music_token=*instance;
    host->music_pending = 1;
    host->music_finished = 0;
    host->music_error=PXA_STATUS_OK;
    host->music_read = host->music_count = 0;
    pxa_audio_buffer_start(&host->music_buffer,4096);
    host->music_accepted_us=now_us(NULL);
    host->music_output_gain_q15 = host->music_source_gain_q15 = 0;
    host->music_last_sample = 0;
    host->music_session = session;
    host->music_loop = (asset->flags & PXA_AUDIO_ASSET_LOOP) != 0;
    host->music_paused = 0;
    host->music_gain_db_q8 = asset->gain_db_q8;
    host->music_gain_q15 = (int32_t)(32768.0 *
        pow(10.0, asset->gain_db_q8 / (20.0 * 256.0)));
    SDL_UnlockAudioDevice(host->audio_device);
    return PXA_STATUS_OK;
}

static pxa_status_t audio_play_asset(void *context, uint64_t session, const pxa_audio_asset_t *asset) {
    uint64_t instance;
    return audio_play_music(context,session,asset,&instance);
}
static pxa_status_t audio_playback_peek(void *context, pxa_audio_playback_event_t *event) {
    product_host_t *host=context;
    if (!host->audio_device) return PXA_STATUS_NOT_FOUND;
    SDL_LockAudioDevice(host->audio_device);
    pxa_status_t status=pxa_audio_playback_peek(&host->music_events,event);
    SDL_UnlockAudioDevice(host->audio_device);
    return status;
}
static pxa_status_t audio_playback_consume(void *context, const pxa_audio_playback_event_t *event) {
    product_host_t *host=context;
    if (!host->audio_device) return PXA_STATUS_NOT_FOUND;
    SDL_LockAudioDevice(host->audio_device);
    pxa_status_t status=pxa_audio_playback_consume(&host->music_events,event);
    SDL_UnlockAudioDevice(host->audio_device);
    return status;
}

static pxa_status_t audio_control_asset(void *context, uint64_t session,
                                        const pxa_audio_asset_control_t *control) {
    product_host_t *host = context;
    if (audio_voice(host, session) < 0 || host->audio_device == 0) return PXA_STATUS_NOT_FOUND;
    if (control == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    SDL_LockAudioDevice(host->audio_device);
    if (control->action < PXA_AUDIO_ASSET_PAUSE || control->action > PXA_AUDIO_ASSET_SET_GAIN) {
        SDL_UnlockAudioDevice(host->audio_device);
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < PRODUCT_AUDIO_SOUND_VOICES; ++i) {
        product_sound_voice_t *v = &host->sound_voices[i];
        if (v->session != session) continue;
        switch (control->action) {
            case PXA_AUDIO_ASSET_STOP: audio_release_sound(v); break;
            case PXA_AUDIO_ASSET_PAUSE: v->paused = 1; break;
            case PXA_AUDIO_ASSET_RESUME: v->paused = 0; break;
            case PXA_AUDIO_ASSET_SET_GAIN:
                v->gain_q15 = (int32_t)(32768.0 * pow(10.0, control->gain_db_q8 / 5120.0)); break;
        }
    }
    if (host->music_session != session) {
        SDL_UnlockAudioDevice(host->audio_device);
        return PXA_STATUS_OK;
    }
    switch (control->action) {
        case PXA_AUDIO_ASSET_STOP:
            pxa_audio_playback_finish(&host->music_events,host->music_token,PXA_AUDIO_PLAYBACK_STOPPED,PXA_STATUS_OK);
            host->music_token=0;
            host->music_pending = 0;
            host->music_read = host->music_count = 0;
            host->music_session = 0;
            host->music_output_gain_q15 = host->music_source_gain_q15 = 0;
            host->music_last_sample = 0;
            break;
        case PXA_AUDIO_ASSET_PAUSE: host->music_paused = 1; break;
        case PXA_AUDIO_ASSET_RESUME: host->music_paused = 0; break;
        case PXA_AUDIO_ASSET_SET_GAIN:
            host->music_gain_db_q8 = control->gain_db_q8;
            host->music_gain_q15 = (int32_t)(32768.0 *
                pow(10.0, control->gain_db_q8 / (20.0 * 256.0)));
            break;
        default:
            SDL_UnlockAudioDevice(host->audio_device);
            return PXA_STATUS_INVALID_ARGUMENT;
    }
    SDL_UnlockAudioDevice(host->audio_device);
    return PXA_STATUS_OK;
}

static pxa_status_t audio_flush(void *context, uint64_t session) {
    product_host_t *host = context;
    int voice = audio_voice(host, session);
    if (voice < 0) return PXA_STATUS_NOT_FOUND;
    pxa_audio_asset_control_t stop = {0, PXA_AUDIO_ASSET_STOP};
    (void)audio_control_asset(context, session, &stop);
    SDL_LockAudioDevice(host->audio_device);
    pxa_audio_mixer_flush(&host->audio_mixer.voices[voice]);
    SDL_UnlockAudioDevice(host->audio_device);
    return PXA_STATUS_OK;
}
static void audio_close(void *context, uint64_t session) {
    product_host_t *host = context;
    int voice = audio_voice(host, session);
    if (voice < 0) return;
    audio_flush(context, session);
    SDL_LockAudioDevice(host->audio_device);
    pxa_audio_mixer_reset(&host->audio_mixer.voices[voice]);
    host->audio_sessions[voice] = 0;
    pxa_audio_playback_close(&host->music_events,session);
    unsigned active = 0;
    for (unsigned i = 0; i < PXA_AUDIO_MIXER_VOICES; ++i)
        active += host->audio_sessions[i] != 0;
    SDL_UnlockAudioDevice(host->audio_device);
    if (!active) {
        audio_stop_worker(host);
        SDL_CloseAudioDevice(host->audio_device);
        host->audio_device = 0;
        audio_release_all_sounds(host);
    }
}
static pxa_status_t audio_query(void *context, uint64_t session,
                                 pxa_audio_state_t *state) {
    product_host_t *host = context;
    int voice = audio_voice(host, session);
    if (!state) return PXA_STATUS_INVALID_ARGUMENT;
    if (voice < 0) return PXA_STATUS_NOT_FOUND;
    SDL_LockAudioDevice(host->audio_device);
    *state = host->audio_mixer.voices[voice].state;
    SDL_UnlockAudioDevice(host->audio_device);
    return PXA_STATUS_OK;
}

static pxa_status_t permission_load(void *context, pxa_bytes_t identity,
                                    pxa_bytes_t name, pxa_bytes_t scope,
                                    pxa_permission_decision_t *decision) {
    /* Desktop development auto-grants declared permissions: the product
     * simulator has no permission UI and would otherwise deny every optional
     * request. Products configure permissions through their own store. */
    (void)context;
    (void)identity;
    (void)name;
    (void)scope;
    if (decision == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *decision = PXA_PERMISSION_ALLOW;
    return PXA_STATUS_OK;
}

static pxa_status_t permission_save(void *context, pxa_bytes_t identity,
                                    pxa_bytes_t name, pxa_bytes_t scope,
                                    pxa_permission_decision_t decision) {
    (void)context;
    (void)identity;
    (void)name;
    (void)scope;
    (void)decision;
    return PXA_STATUS_OK;
}

#ifndef PXSYS_PRODUCT_RUNNER_LIBRARY
static void print_usage(const char *program) {
    fprintf(stderr, "Usage: %s --package DIR --publisher-key DER [--state-root DIR] [--locale TAG] [--pxadb-control-socket PATH] [--width PX --height PX] [--safe-insets T,R,B,L] [--corner-radius PX|--round] [--shape-background matte|black]\n",
            program);
}

static int parse_options(int argc, char **argv, options_t *options) {
    int index;
    memset(options, 0, sizeof(*options));
    options->width = 296;
    options->height = 240;
    options->locale = "en-US";
    options->shape_background_matte = 1;
    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--package") == 0 && index + 1 < argc)
            options->package_path = argv[++index];
        else if (strcmp(argv[index], "--publisher-key") == 0 && index + 1 < argc)
            options->publisher_key = argv[++index];
        else if (strcmp(argv[index], "--state-root") == 0 && index + 1 < argc)
            options->state_root = argv[++index];
        else if (strcmp(argv[index], "--locale") == 0 && index + 1 < argc)
            options->locale = argv[++index];
        else if (strcmp(argv[index], "--pxadb-control-socket") == 0 && index + 1 < argc)
            options->pxadb_control_socket = argv[++index];
        else if (strcmp(argv[index], "--width") == 0 && index + 1 < argc)
            options->width = (uint32_t)strtoul(argv[++index], NULL, 10);
        else if (strcmp(argv[index], "--height") == 0 && index + 1 < argc)
            options->height = (uint32_t)strtoul(argv[++index], NULL, 10);
        else if (strcmp(argv[index], "--safe-insets") == 0 && index + 1 < argc) {
            if (sscanf(argv[++index], "%u,%u,%u,%u", &options->safe_insets[0],
                       &options->safe_insets[1], &options->safe_insets[2],
                       &options->safe_insets[3]) != 4)
                return 0;
        }
        else if (strcmp(argv[index], "--corner-radius") == 0 && index + 1 < argc) {
            char *end = NULL;
            const char *value = argv[++index];
            unsigned long radius;
            if (value[0] == '-' || value[0] == '\0') return 0;
            radius = strtoul(value, &end, 10);
            if (*end != '\0' || radius > UINT32_MAX) return 0;
            options->corner_radius = (uint32_t)radius;
            options->display_shape = options->corner_radius ? 1u : 0u;
        }
        else if (strcmp(argv[index], "--round") == 0) {
            options->display_shape = 2u;
        }
        else if (strcmp(argv[index], "--shape-background") == 0 && index + 1 < argc) {
            const char *background = argv[++index];
            if (strcmp(background, "matte") == 0)
                options->shape_background_matte = 1;
            else if (strcmp(background, "black") == 0)
                options->shape_background_matte = 0;
            else return 0;
        }
        else return 0;
    }
    return options->package_path != NULL && options->publisher_key != NULL &&
           options->width > 0 && options->height > 0 &&
           (options->display_shape != 1u ||
            options->corner_radius <= (options->width < options->height
                                           ? options->width : options->height) / 2u);
}
#endif

static pxa_status_t desktop_get_mac(void *context, uint16_t kind,
                                    uint8_t output[6], uint32_t *flags) {
    /* Locally administered identity so store clients see a stable device. */
    static const uint8_t hardware_mac[6] = {0x02, 0x50, 0x58, 0x41, 0x00, 0x01};
    (void)context;
    if (output == NULL || flags == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    switch (kind) {
        case PXA_DEVICE_MAC_KIND_WIFI_STATION_HARDWARE:
            memcpy(output, hardware_mac, sizeof(hardware_mac));
            *flags = PXA_DEVICE_MAC_FLAG_HARDWARE |
                     PXA_DEVICE_MAC_FLAG_LOCALLY_ADMINISTERED;
            return PXA_STATUS_OK;
        case PXA_DEVICE_MAC_KIND_WIFI_STATION_CURRENT:
            memcpy(output, hardware_mac, sizeof(hardware_mac));
            *flags = PXA_DEVICE_MAC_FLAG_CURRENT |
                     PXA_DEVICE_MAC_FLAG_LOCALLY_ADMINISTERED;
            return PXA_STATUS_OK;
        default:
            return PXA_STATUS_UNSUPPORTED;
    }
}

static void desktop_net_completion_ready(void *context) {
    product_host_t *host = (product_host_t *)context;
    if (host != NULL) host->net_completion_ready = 1;
}

static pxa_status_t desktop_sensor_subscribe(void *context, uint16_t id,
                                            uint32_t period_ms, void **subscription) {
    (void)period_ms;
    if (context == NULL || subscription == NULL || id != 1)
        return PXA_STATUS_NOT_FOUND;
    *subscription = context;
    return PXA_STATUS_OK;
}

static pxa_status_t desktop_sensor_read(void *context, void *subscription,
    uint16_t id, int32_t values[PXA_SENSOR_MAX_DIMENSIONS]) {
    product_host_t *host = context;
    if (host == NULL || subscription != context || id != 1)
        return PXA_STATUS_NOT_FOUND;
    values[0] = host->sensor_temperature_milli_celsius;
    return PXA_STATUS_OK;
}

static void desktop_sensor_unsubscribe(void *context, void *subscription, uint16_t id) {
    (void)context; (void)subscription; (void)id;
}

static void drain_net_completions(product_host_t *host) {
    pxa_component_t affected[4];
    size_t count = 0;
    if (host == NULL || host->net == NULL || !host->net_completion_ready)
        return;
    host->net_completion_ready = 0;
    (void)pxa_net_poll(host->net, affected,
                       sizeof(affected) / sizeof(affected[0]), &count);
    /* A completion is delivered as an event; wake the waiting Guest so its
     * response handler runs in this loop turn. */
    if (count != 0) dispatch_component_events(host);
}

static int run_product_simulator(const options_t *input,
                                 lv_display_t *embedded_display,
                                 lv_obj_t *parent,
                                 pxsys_product_simulator_pump_fn pump,
                                 void *pump_context,
                                 pxsys_product_simulator_window_fn window_changed,
                                 void *window_context,
                                 pxsys_product_simulator_control_bind_fn control_bind,
                                 const pxsys_display_profile_t *host_display,
                                 const pxsys_insets_t *host_bar_insets,
                                 const pxsys_theme_snapshot_t *host_theme) {
    const options_t options = *input;
    product_host_t host = {0};
    pxa_package_limits_t limits;
    pxa_posix_installer_config_t installer_config = {0};
    pxa_posix_installer_t *installer = NULL;
    pxa_posix_installer_result_t package_result = {0};
    pxa_package_manifest_t *manifest = NULL;
    pxa_openssl_publisher_key_t key = {0};
    pxa_runtime_limits_t runtime_limits;
    pxa_ui_config_t ui_config;
    pxa_lvgl_ui_config_t lvgl_config = {0};
    pxa_lvgl_ui_t *lvgl_ui = NULL;
    pxa_ui_backend_t ui_backend;
    pxa_audio_config_t audio_config = {0};
    pxa_audio_backend_t audio_backend = {0};
    pxa_permission_config_t permission_config = {0};
    pxa_posix_storage_config_t posix_storage_config = {0};
    pxa_storage_config_t storage_config = {0};
    pxa_storage_backend_t storage_backend = {0};
    pxa_posix_storage_t *posix_storage = NULL;
    pxa_storage_service_t *storage = NULL;
    pxa_posix_fs_config_t posix_fs_config = {0};
    pxa_fs_config_t fs_config = {0};
    pxa_fs_backend_t fs_backend = {0};
    pxa_posix_fs_t *posix_fs = NULL;
    pxa_fs_service_t *fs = NULL;
    pxa_ipc_limits_t ipc_limits;
    pxa_surface_config_t surface_config = {0};
    pxa_game_render_config_t game_render_config = {0};
    pxa_assets_config_t assets_config = {0};
    pxa_device_config_t device_config = {0};
    pxa_sensor_config_t sensor_config = {0};
    pxa_sensor_descriptor_t sensor_descriptor = {0};
    pxa_net_config_t net_config = {0};
    pxa_net_backend_t net_backend = {0};
    pxa_scheduler_config_t scheduler_config;
    pxa_bytes_t job_components[PRODUCT_COMPONENTS];
    uint16_t job_component_count = 0;
    pxa_log_config_t log_config = {0};
    pxa_service_ops_t clock_service = {0};
    pxa_service_ops_t store_installer_service = {0};
    pxa_wamr_engine_config_t engine_config = {0};
    pxa_package_service_capability_t capabilities[19] = {0};
    pxa_package_activation_profile_t activation = {0};
    pxa_package_host_profile_t profile = {0};
    pxa_activation_plan_t *plan = NULL;
    void *installer_workspace = NULL, *manifest_workspace = NULL;
    void *runtime_workspace = NULL, *window_workspace = NULL, *ui_workspace = NULL;
    void *permission_workspace = NULL, *audio_workspace = NULL, *storage_workspace = NULL;
    void *storage_service_workspace = NULL, *lvgl_workspace = NULL, *engine_workspace = NULL;
    void *fs_backend_workspace = NULL, *fs_service_workspace = NULL;
    void *ipc_workspace = NULL;
    void *scheduler_workspace = NULL;
    void *surface_workspace = NULL, *game_render_workspace = NULL;
    void *device_workspace = NULL, *net_workspace = NULL;
    void *sensor_workspace = NULL;
    void *log_workspace = NULL, *assets_workspace = NULL;
    void *plan_workspace = NULL, *coordinator_workspace = NULL;
    uint8_t *encoded = NULL, *public_key = NULL;
    size_t manifest_size = 0, public_key_size = 0;
    char root[1024] = {0};
    char *default_state_root = NULL;
    char *storage_parent = NULL;
    char *storage_path = NULL;
    char *fs_parent = NULL;
    char *fs_path = NULL;
    char publisher_hex[2u * PXA_PACKAGE_DIGEST_BYTES + 1u] = {0};
    lv_display_t *display = NULL;
    lv_indev_t *mouse = NULL;
    lv_indev_t *keyboard = NULL;
    pxsys_pxadb_control_t pxadb_control = {.listener = -1};
    shape_mask_image_t shape_mask = {0};
    pxa_component_t component;
    pxa_status_t status;
    const char *stage = "arguments";
    const int owns_display = embedded_display == NULL;
    int key_event_watch_added = 0;
    const char *loop_perf_env = getenv("PXA_SIMULATOR_PERF");
    const int trace_loop = owns_display && loop_perf_env != NULL &&
                           strcmp(loop_perf_env, "1") == 0;
    uint64_t perf_window_start_us = 0, previous_loop_start_us = 0;
    uint64_t loop_gap_total_us = 0, loop_gap_max_us = 0;
    uint64_t pre_total_us = 0, pre_max_us = 0;
    uint64_t lvgl_total_us = 0, lvgl_max_us = 0;
    uint64_t post_total_us = 0, post_max_us = 0;
    uint64_t sleep_total_us = 0, sleep_max_us = 0;
    uint32_t perf_loops = 0;
#ifdef PXSYS_PRODUCT_FRAME_OBSERVER
    /* Test-only CPU path: Guest tick + Surface execution, then the next
     * LVGL timer pass. Deliberately excludes sleep and device presentation. */
    uint64_t pending_frame_id = 0;
    uint64_t pending_tick_cpu_us = 0;
#endif
    int result = 1;

    host.window_changed = window_changed;
    host.window_context = window_context;
    host.host_theme = host_theme;
    host.focused = owns_display ? 1u : 0u;
    host.pending_lifecycle = owns_display ? 1u : 2u;
    host.next_instance_id = 1;
    host.running_work_component = PXA_COMPONENT_INVALID;

    {
        struct stat metadata;
        if (options.package_path == NULL || options.publisher_key == NULL ||
            options.width == 0 || options.height == 0) {
            return 2;
        }
        if (stat(options.package_path, &metadata) != 0 ||
            !S_ISDIR(metadata.st_mode)) {
            fprintf(stderr, "PXA product simulator requires an unpacked package directory: %s\n",
                    options.package_path);
            return 2;
        }
    }
    stage = "resource budget";
    if (resource_memory_init(&host,
        asset_setting("PXA_RESOURCE_INTERNAL_BYTES", 128u * 1024u, 1024u * 1024u),
        asset_setting("PXA_RESOURCE_EXTERNAL_BYTES", 2u * 1024u * 1024u, 16u * 1024u * 1024u),
        asset_setting("PXA_RESOURCE_TEMPORARY_INTERNAL_BYTES", 16u * 1024u, 1024u * 1024u),
        asset_setting("PXA_RESOURCE_TEMPORARY_EXTERNAL_BYTES", 512u * 1024u, 16u * 1024u * 1024u)) != PXA_STATUS_OK)
        goto done;
    stage = "publisher key";
    {
        FILE *file = fopen(options.publisher_key, "rb");
        long size;
        if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
            (size = ftell(file)) <= 0 || size > 4096)
            goto done;
        rewind(file);
        public_key = malloc((size_t)size);
        if (public_key == NULL || fread(public_key, 1, (size_t)size, file) != (size_t)size) {
            fclose(file); goto done;
        }
        fclose(file);
        public_key_size = (size_t)size;
    }
    key.spki = public_key; key.spki_size = public_key_size;
    stage = "package installer";
    pxa_package_limits_init(&limits);
    installer_config.struct_size = sizeof(installer_config);
    installer_config.storage_root = ".";
    installer_config.trust.struct_size = sizeof(installer_config.trust);
    installer_config.trust.keys = &key; installer_config.trust.key_count = 1;
    installer_config.limits = limits;
    installer_workspace = malloc(pxa_posix_installer_workspace_size(&installer_config));
    if (installer_workspace == NULL ||
        pxa_posix_installer_init(installer_workspace,
            pxa_posix_installer_workspace_size(&installer_config), &installer_config,
            &installer) != PXA_STATUS_OK ||
        pxa_posix_installer_source_manifest_size(installer, options.package_path,
                                                  &manifest_size) != PXA_STATUS_OK)
        goto done;
    encoded = malloc(manifest_size);
    manifest_workspace = malloc(pxa_package_manifest_workspace_size(&limits));
    if (encoded == NULL || manifest_workspace == NULL) goto done;
    package_result.struct_size = sizeof(package_result);
    package_result.manifest_workspace = manifest_workspace;
    package_result.manifest_workspace_size = pxa_package_manifest_workspace_size(&limits);
    package_result.encoded = encoded; package_result.encoded_capacity = manifest_size;
    package_result.manifest = &manifest; package_result.root = root;
    package_result.root_capacity = sizeof(root);
    stage = "installed package manifest";
    if (pxa_posix_installer_load_directory(installer, options.package_path,
                                          &package_result) != PXA_STATUS_OK)
        goto done;
    stage = "display";
    if (strlen(root) >= sizeof(host.package_root)) goto done;
    if (options.locale == NULL || strlen(options.locale) < 2u ||
        strlen(options.locale) > PRODUCT_LOCALE_MAX_BYTES)
        goto done;
    strcpy(host.package_root, root);
    strcpy(host.locale, options.locale);
    host.width = options.width;
    host.height = options.height;
    {
        const char *mode = getenv("PXA_SIM_NAV_MODE");
        host.navigation_mode = mode != NULL && strcmp(mode, "gesture-no-bar") == 0 ? 1u :
                               mode != NULL && strcmp(mode, "buttons") == 0 ? 2u : 0u;
    }
    memcpy(host.safe_insets, options.safe_insets, sizeof(host.safe_insets));
    host.display_shape = options.display_shape;
    host.corner_radius = options.display_shape == 2u
        ? (options.width < options.height ? options.width : options.height) / 2u
        : options.corner_radius;
    for (size_t index = 0; index < 4; ++index)
        host.corner_radii[index] = host.corner_radius;
    host.host_bar_insets.left = host.navigation_mode == 2u ? 0u :
                                PRODUCT_SYSTEM_GESTURE_EDGE_WIDTH;
    host.host_bar_insets.bottom = host.navigation_mode == 1u ? 0u :
        host.navigation_mode == 2u ? 36u : PRODUCT_SYSTEM_GESTURE_HOME_HEIGHT;
    if (host_display != NULL) {
        pxsys_insets_t safe;
        if (pxsys_display_effective_insets(host_display, &safe) ==
            PXSYS_STATUS_OK) {
            host.safe_insets[0] = safe.top;
            host.safe_insets[1] = safe.right;
            host.safe_insets[2] = safe.bottom;
            host.safe_insets[3] = safe.left;
        }
        host.display_shape = host_display->shape;
        host.corner_radii[0] = host_display->corner_radii.top_left;
        host.corner_radii[1] = host_display->corner_radii.top_right;
        host.corner_radii[2] = host_display->corner_radii.bottom_right;
        host.corner_radii[3] = host_display->corner_radii.bottom_left;
    }
    if (host_bar_insets != NULL) host.host_bar_insets = *host_bar_insets;
    if (owns_display) {
        lv_init();
        display = lv_sdl_window_create((int32_t)options.width,
                                       (int32_t)options.height);
        mouse = lv_sdl_mouse_create();
        keyboard = lv_sdl_keyboard_create();
        if (display == NULL || mouse == NULL || keyboard == NULL) goto done;
        lv_indev_set_display(mouse, display);
        lv_indev_set_display(keyboard, display);
        lv_sdl_window_set_title(display, "PXA Product Simulator");
    } else {
        display = embedded_display;
        if (display == NULL) goto done;
    }
    host.display = display;
    host.content_parent = parent != NULL ? parent : lv_screen_active();
    if (control_bind != NULL)
        control_bind(window_context, product_focus_changed,
                     product_request_exit, &host);
    pxa_runtime_limits_init(&runtime_limits); runtime_limits.max_components = PRODUCT_COMPONENTS;
    stage = "runtime";
    runtime_workspace = malloc(pxa_runtime_workspace_size(&runtime_limits));
    if (runtime_workspace == NULL || pxa_runtime_init(runtime_workspace,
        pxa_runtime_workspace_size(&runtime_limits), &runtime_limits, &host.runtime) != PXA_STATUS_OK)
        goto done;
    window_workspace = malloc(pxa_window_service_workspace_size(PRODUCT_COMPONENTS));
    stage = "window service";
    if (window_workspace == NULL || pxa_window_service_init(window_workspace,
        pxa_window_service_workspace_size(PRODUCT_COMPONENTS), host.runtime,
        PRODUCT_COMPONENTS, &host.window) != PXA_STATUS_OK ||
        pxa_window_service_register(host.window) != PXA_STATUS_OK) goto done;
    pxa_ui_config_init(&ui_config);
    stage = "ui service";
    if (host_theme != NULL)
        ui_config.color_scheme =
            host_theme->effective_scheme == PXSYS_COLOR_SCHEME_DARK
                ? PXA_UI_COLOR_SCHEME_DARK : PXA_UI_COLOR_SCHEME_LIGHT;
    ui_config.allocate = allocate_memory; ui_config.release = release_memory;
    ui_config.resize = reallocate_memory;
    ui_config.now_us = now_us; ui_config.features = PXA_UI_FEATURE_CANVAS |
        PXA_UI_FEATURE_VIRTUAL_LIST | PXA_UI_FEATURE_GRID |
        PXA_UI_FEATURE_RGB565_BITMAP |
        PXA_UI_FEATURE_CONTROLLER_INPUT | PXA_UI_FEATURE_MULTIPLE_SURFACES |
        PXA_UI_FEATURE_CANVAS_STREAM_IO;
    ui_config.primary_width = options.width; ui_config.primary_height = options.height;
    for (size_t index = 0; index < 4; ++index)
        ui_config.safe_insets[index] = host.safe_insets[index];
    ui_config.display_shape = host.display_shape;
    for (size_t index = 0; index < 4; ++index)
        ui_config.corner_radii[index] = host.corner_radii[index];
    ui_workspace = malloc(pxa_ui_service_workspace_size());
    if (ui_workspace == NULL || pxa_ui_service_init(ui_workspace,
        pxa_ui_service_workspace_size(), host.runtime, &ui_config, &host.ui) != PXA_STATUS_OK ||
        pxa_ui_service_register(host.ui) != PXA_STATUS_OK) goto done;
    stage = "permission service";
    permission_config.struct_size = sizeof(permission_config);
    permission_config.app_identity = manifest->app_id;
    permission_config.declarations =
        (const pxa_permission_declaration_t *)manifest->permissions;
    permission_config.declaration_count = manifest->permission_count;
    permission_config.max_authorities = 8;
    permission_config.max_pending_prompts = 0;
    permission_config.store.struct_size = sizeof(permission_config.store);
    permission_config.store.load = permission_load;
    permission_config.store.save = permission_save;
    permission_workspace = malloc(pxa_permission_service_workspace_size(&permission_config));
    if (permission_workspace == NULL || pxa_permission_service_init(permission_workspace,
        pxa_permission_service_workspace_size(&permission_config), host.runtime,
        &permission_config, &host.permissions) != PXA_STATUS_OK ||
        pxa_permission_policy_load(host.permissions) != PXA_STATUS_OK ||
        pxa_permission_service_register(host.permissions) != PXA_STATUS_OK) goto done;
    stage = "storage service";
    {
        static const char hex[] = "0123456789abcdef";
        const char *state_root = options.state_root;
        if (manifest->publisher_key_id == NULL) goto done;
        for (size_t index = 0; index < PXA_PACKAGE_DIGEST_BYTES; ++index) {
            publisher_hex[2u * index] =
                hex[manifest->publisher_key_id[index] >> 4];
            publisher_hex[2u * index + 1u] =
                hex[manifest->publisher_key_id[index] & 15u];
        }
        if (state_root == NULL) {
            const size_t package_path_size = strlen(options.package_path);
            default_state_root = malloc(package_path_size + sizeof(".state"));
            if (default_state_root == NULL) goto done;
            snprintf(default_state_root, package_path_size + sizeof(".state"),
                     "%s.state", options.package_path);
            if (mkdir(default_state_root, 0700) != 0 && errno != EEXIST)
                goto done;
            state_root = default_state_root;
        }
        size_t state_root_size = strlen(state_root);
        storage_parent = malloc(state_root_size + sizeof("/app-data"));
        if (storage_parent == NULL) goto done;
        snprintf(storage_parent, state_root_size + sizeof("/app-data"),
                 "%s/app-data", state_root);
        if (mkdir(storage_parent, 0700) != 0 && errno != EEXIST) goto done;
        storage_path = malloc(strlen(storage_parent) + 1u +
                              sizeof(publisher_hex) - 1u + 1u +
                              manifest->app_id.size + 1u);
        if (storage_path == NULL) goto done;
        snprintf(storage_path, strlen(storage_parent) + 1u +
                               sizeof(publisher_hex) - 1u + 1u +
                               manifest->app_id.size + 1u,
                 "%s/%s-%.*s", storage_parent, publisher_hex,
                 (int)manifest->app_id.size,
                 (const char *)manifest->app_id.data);
    }
    posix_storage_config.struct_size = sizeof(posix_storage_config);
    posix_storage_config.root_path = storage_path;
    posix_storage_config.max_keys = 128;
    posix_storage_config.max_value_bytes = PXA_STORAGE_MAX_VALUE_BYTES;
    posix_storage_config.quota_bytes = 256u * 1024u;
    storage_workspace = malloc(pxa_posix_storage_workspace_size(&posix_storage_config));
    if (storage_workspace == NULL ||
        pxa_posix_storage_init(storage_workspace,
            pxa_posix_storage_workspace_size(&posix_storage_config),
            &posix_storage_config, &posix_storage, &storage_backend) != PXA_STATUS_OK) goto done;
    storage_config.struct_size = sizeof(storage_config);
    storage_config.max_value_bytes = PXA_STORAGE_MAX_VALUE_BYTES;
    storage_config.backend = storage_backend;
    storage_service_workspace = malloc(pxa_storage_service_workspace_size(&storage_config));
    if (storage_service_workspace == NULL ||
        pxa_storage_service_init(storage_service_workspace,
            pxa_storage_service_workspace_size(&storage_config), host.runtime,
            &storage_config, &storage) != PXA_STATUS_OK ||
        pxa_storage_service_register(storage) != PXA_STATUS_OK) goto done;
    stage = "private FS service";
    {
        const char *state_root = options.state_root != NULL
                                     ? options.state_root : default_state_root;
        size_t parent_size;
        if (state_root == NULL) goto done;
        fs_parent = malloc(strlen(state_root) + sizeof("/private-files"));
        if (fs_parent == NULL) goto done;
        snprintf(fs_parent, strlen(state_root) + sizeof("/private-files"),
                 "%s/private-files", state_root);
        if (mkdir(fs_parent, 0700) != 0 && errno != EEXIST) goto done;
        parent_size = strlen(fs_parent);
        fs_path = malloc(parent_size + 1u + sizeof(publisher_hex) - 1u + 1u +
                         manifest->app_id.size + 1u);
        if (fs_path == NULL) goto done;
        snprintf(fs_path, parent_size + 1u + sizeof(publisher_hex) - 1u + 1u +
                          manifest->app_id.size + 1u,
                 "%s/%s-%.*s", fs_parent, publisher_hex,
                 (int)manifest->app_id.size,
                 (const char *)manifest->app_id.data);
    }
    posix_fs_config.struct_size = sizeof(posix_fs_config);
    posix_fs_config.root_path = fs_path;
    posix_fs_config.quota_bytes = 256u * 1024u;
    posix_fs_config.max_open_resources = 16;
    fs_backend_workspace = malloc(pxa_posix_fs_workspace_size(&posix_fs_config));
    if (fs_backend_workspace == NULL ||
        pxa_posix_fs_init(fs_backend_workspace,
                          pxa_posix_fs_workspace_size(&posix_fs_config),
                          &posix_fs_config, &posix_fs,
                          &fs_backend) != PXA_STATUS_OK) goto done;
    fs_config.struct_size = sizeof(fs_config);
    fs_config.max_open_resources = 16;
    fs_config.backend = fs_backend;
    fs_service_workspace = malloc(pxa_fs_service_workspace_size(&fs_config));
    if (fs_service_workspace == NULL ||
        pxa_fs_service_init(fs_service_workspace,
                            pxa_fs_service_workspace_size(&fs_config),
                            host.runtime, &fs_config, &fs) != PXA_STATUS_OK ||
        pxa_fs_service_register(fs) != PXA_STATUS_OK) goto done;
    stage = "IPC broker";
    pxa_ipc_limits_init(&ipc_limits);
    ipc_limits.max_endpoints = manifest->ipc_endpoint_count == 0
                                   ? 1 : manifest->ipc_endpoint_count;
    ipc_limits.max_pending_calls = 16;
    ipc_workspace = malloc(pxa_ipc_broker_workspace_size(&ipc_limits));
    if (ipc_workspace == NULL ||
        pxa_ipc_broker_init(ipc_workspace,
                            pxa_ipc_broker_workspace_size(&ipc_limits),
                            host.runtime, &ipc_limits, &host.ipc) !=
            PXA_STATUS_OK ||
        pxa_ipc_broker_register(host.ipc) != PXA_STATUS_OK ||
        pxa_ipc_broker_set_allocator(host.ipc, NULL, ipc_allocate,
                                      ipc_release) != PXA_STATUS_OK) goto done;
    audio_backend.struct_size = sizeof(audio_backend);
    audio_backend.context = &host;
    stage = "audio service";
    audio_backend.open = audio_open; audio_backend.commit = audio_commit;
    audio_backend.submit = audio_submit; audio_backend.close = audio_close;
    audio_backend.query = audio_query; audio_backend.flush = audio_flush;
    audio_backend.play_tone = audio_play_tone;
    audio_backend.play_asset = audio_play_asset;
    audio_backend.play_sound = audio_play_sound;
    audio_backend.play_music = audio_play_music;
    audio_backend.playback_peek = audio_playback_peek;
    audio_backend.playback_consume = audio_playback_consume;
    audio_backend.control_asset = audio_control_asset;
    audio_config.struct_size = sizeof(audio_config);
    audio_config.max_sessions = 3; audio_config.max_sessions_per_component = 3;
    audio_config.max_eq_bands = PXA_AUDIO_MAX_EQ_BANDS;
    audio_config.backend = audio_backend;
    audio_config.permissions = host.permissions;
    audio_workspace = malloc(pxa_audio_service_workspace_size(&audio_config));
    if (audio_workspace == NULL || pxa_audio_service_init(audio_workspace,
        pxa_audio_service_workspace_size(&audio_config), host.runtime,
        &audio_config, &host.audio) != PXA_STATUS_OK ||
        pxa_audio_service_register(host.audio) != PXA_STATUS_OK) goto done;
    clock_service.struct_size = sizeof(clock_service);
    clock_service.service_id = PRODUCT_CLOCK_SERVICE;
    clock_service.major = 0; clock_service.minor = 1;
    clock_service.context = &host;
    clock_service.control = clock_control;
    if (pxa_service_register(host.runtime, &clock_service) != PXA_STATUS_OK) goto done;
    store_installer_service.struct_size = sizeof(store_installer_service);
    store_installer_service.service_id = PXA_STORE_INSTALLER_SERVICE_ID;
    store_installer_service.major = 0;
    store_installer_service.minor = 5;
    store_installer_service.control = store_installer_sim_control;
    if (pxa_service_register(host.runtime, &store_installer_service) !=
        PXA_STATUS_OK) goto done;
    lvgl_config.struct_size = sizeof(lvgl_config);
    stage = "LVGL adapter";
    lvgl_config.allocate = allocate_memory; lvgl_config.release = release_memory;
    lvgl_config.execute = execute_inline; lvgl_config.resolve_asset = resolve_asset;
    lvgl_config.acquire_image = acquire_ui_image;
    lvgl_config.release_asset = release_asset; lvgl_config.event_callback = ui_event;
    lvgl_config.now_us = now_us; lvgl_config.callback_user_data = &host;
    lvgl_config.asset_user_data = &host;
    lvgl_config.primary_environment.surface = PXA_UI_PRIMARY_SURFACE;
    lvgl_config.parent_object = host.content_parent;
    lvgl_config.primary_environment.width = options.width;
    lvgl_config.primary_environment.height = options.height;
    lvgl_config.primary_environment.density_q16 = UINT32_C(1) << 16;
    lvgl_config.primary_environment.font_scale_q16 = UINT32_C(1) << 16;
    lvgl_config.primary_environment.display_shape = host.display_shape;
    for (size_t index = 0; index < 4; ++index)
        lvgl_config.primary_environment.corner_radii[index] = host.corner_radii[index];
    pxa_lvgl_ui_theme_init(&lvgl_config.theme);
    if (host_theme != NULL)
        map_host_theme(host_theme, lvgl_config.theme.rgba);
    host.caption_font = load_product_font(12, &lv_font_montserrat_14);
    host.label_font = load_product_font(14, &lv_font_montserrat_14);
    host.body_font = load_product_font(16, &lv_font_montserrat_16);
    host.title_font = load_product_font(20, &lv_font_montserrat_20);
    host.headline_font = load_product_font(24, &lv_font_montserrat_20);
    host.display_font = load_product_font(28, &lv_font_montserrat_20);
    lvgl_config.theme.caption_font = host.caption_font != NULL
                                        ? host.caption_font
                                        : &lv_font_montserrat_14;
    lvgl_config.theme.label_font = host.label_font != NULL
                                      ? host.label_font
                                      : lvgl_config.theme.caption_font;
    lvgl_config.theme.body_font = host.body_font != NULL
                                      ? host.body_font
                                      : &lv_font_montserrat_14;
    lvgl_config.theme.title_font = host.title_font != NULL
                                       ? host.title_font
                                       : &lv_font_montserrat_20;
    lvgl_config.theme.headline_font = host.headline_font != NULL
                                          ? host.headline_font
                                          : lvgl_config.theme.title_font;
    lvgl_config.theme.display_font = host.display_font != NULL
                                         ? host.display_font
                                         : lvgl_config.theme.title_font;
    lvgl_workspace = malloc(pxa_lvgl_ui_workspace_size());
    if (lvgl_workspace == NULL || pxa_lvgl_ui_init(lvgl_workspace,
        pxa_lvgl_ui_workspace_size(), &lvgl_config, &lvgl_ui, &ui_backend) != PXA_STATUS_OK)
        goto done;
    /* The UI backend is bound during prepare_start. */
    host.ui_backend = ui_backend;
    host.ui_adapter = lvgl_ui;
    sync_host_theme(&host, lvgl_ui, &lvgl_config.theme);
    stage = "surface service";
    surface_config.struct_size = sizeof(surface_config);
    surface_config.max_surfaces = 1;
    surface_config.max_surfaces_per_component = 1;
    surface_config.max_width = options.width;
    surface_config.max_height = options.height;
    surface_config.max_frame_bytes = options.width * options.height * 2u;
    surface_config.min_buffer_count = 2;
    surface_config.max_buffer_count = 3;
    surface_config.backend.struct_size = sizeof(surface_config.backend);
    surface_config.backend.context = &host;
    surface_config.backend.create = surface_create;
    surface_config.backend.write = surface_write;
    surface_config.backend.queue = surface_queue;
    surface_config.backend.configure = surface_configure;
    surface_config.backend.query = surface_query;
    surface_config.backend.close = surface_close;
    surface_config.backend.register_buffers = surface_register_buffers;
    surface_config.backend.acquire_buffer = surface_acquire_buffer;
    surface_config.backend.present_buffer = surface_present_buffer;
    surface_config.backend.peek_release = surface_peek_release;
    surface_config.backend.consume_release = surface_consume_release;
    surface_workspace = malloc(pxa_surface_service_workspace_size(&surface_config));
    if (surface_workspace == NULL || pxa_surface_service_init(surface_workspace,
        pxa_surface_service_workspace_size(&surface_config), host.runtime,
        &surface_config, &host.surfaces) != PXA_STATUS_OK ||
        pxa_surface_service_register(host.surfaces) != PXA_STATUS_OK) goto done;
    stage = "game render service";
    game_render_config.struct_size = sizeof(game_render_config);
    game_render_config.max_contexts = 1;
    game_render_config.max_contexts_per_component = 1;
    game_render_config.min_buffer_count = 2;
    game_render_config.max_buffer_count = 3;
    game_render_config.backend.struct_size = sizeof(game_render_config.backend);
    game_render_config.backend.context = &host;
    game_render_config.backend.create = game_render_create;
    game_render_config.backend.upload = surface_raster_upload;
    game_render_config.backend.submit = surface_raster_submit;
    game_render_config.backend.query = surface_raster_query;
    game_render_config.backend.close = surface_close;
    game_render_config.backend.bind_assets = surface_raster_bind_assets;
    game_render_workspace = malloc(
        pxa_game_render_service_workspace_size(&game_render_config));
    if (game_render_workspace == NULL ||
        pxa_game_render_service_init(
            game_render_workspace,
            pxa_game_render_service_workspace_size(&game_render_config),
            host.runtime, &game_render_config, &host.game_render) !=
            PXA_STATUS_OK ||
        pxa_game_render_service_register(host.game_render) != PXA_STATUS_OK)
        goto done;
    stage = "asset worker";
    if (manifest_uses_service(manifest,PXA_ASSETS_SERVICE_ID) ||
        (manifest_uses_service(manifest,PXA_AUDIO_SERVICE_ID) && manifest_has_music(manifest))) {
        if (pxa_package_file_find(manifest, (pxa_bytes_t){(const uint8_t *)PXA_ASSET_INDEX_PATH, sizeof(PXA_ASSET_INDEX_PATH) - 1})) {
            pxa_posix_asset_worker_config_t wc = {0};
            wc.package_root = host.package_root;
            wc.manifest = manifest;
            wc.cache.max_entries = 64;
            wc.cache.max_requests = 48;
            wc.cache.max_requests_per_owner = 32;
            wc.cache.max_pending = 16;
            if (!manifest_uses_service(manifest,PXA_ASSETS_SERVICE_ID)) {
                // Music borrows catalog metadata and uses its decoder task;
                // no Guest Assets requests can consume a resident cache here.
                wc.cache.max_entries=wc.cache.max_requests=wc.cache.max_requests_per_owner=wc.cache.max_pending=1;
            }
            wc.cache.resident_limit[0] = asset_setting("PXA_ASSET_INTERNAL_BYTES", 64u * 1024u, 1024u * 1024u);
            wc.cache.resident_limit[1] = asset_setting("PXA_ASSET_EXTERNAL_BYTES", 512u * 1024u, 16u * 1024u * 1024u);
            wc.cache.owner_limit[0] = wc.cache.resident_limit[0];
            wc.cache.owner_limit[1] = wc.cache.resident_limit[1];
            wc.max_catalog_bytes = 64u * 1024u;
            wc.metadata_allocator = &host.resource_allocators[PXA_MEMORY_EXTERNAL][PXA_MEMORY_METADATA];
            wc.temporary_allocator = &host.resource_allocators[PXA_MEMORY_EXTERNAL][PXA_MEMORY_TEMPORARY];
            wc.allocator_context = &host;
            wc.allocate = asset_allocate;
            wc.release = raster_asset_free;
            wc.notify_context = &host;
            wc.notify = asset_notify;
            wc.read_delay_us = (uint32_t)asset_setting("PXA_ASSET_READ_DELAY_US", 0, 1000000);
            pxa_posix_storage_gate_config_t io_config={
                asset_setting("PXA_STORAGE_BYTES_PER_SECOND",0,1000000000),
                (uint32_t)asset_setting("PXA_STORAGE_LATENCY_US",0,1000000)};
            fprintf(stderr,"PXA STORAGE CONFIG bytes_per_second=%llu latency_us=%u max_read=4096\n",
                (unsigned long long)io_config.bytes_per_second,io_config.latency_us);
            status=pxa_posix_storage_gate_create(&io_config,wc.metadata_allocator,&host.storage_gate);
            if(status) goto done;
            wc.storage_gate=host.storage_gate;
            status = pxa_posix_asset_worker_create(&wc, &host.asset_worker);
            if (status != PXA_STATUS_OK) {
                fprintf(stderr, "PXA asset worker status=%d\n", (int)status);
                goto done;
            }
        }
    }
    if (manifest_uses_service(manifest,PXA_ASSETS_SERVICE_ID)) {
        stage = "assets service";
        assets_config.struct_size = sizeof(assets_config);
        assets_config.max_pending = assets_config.max_pending_per_component = 16;
        assets_config.max_resources = assets_config.max_resources_per_component = 32;
        assets_config.backend = (pxa_assets_backend_t){&host, asset_find, asset_request, asset_query, asset_acquire, asset_release, asset_prefetch, asset_inspect,
            asset_read,asset_read_result,asset_read_release};
        assets_workspace = malloc(pxa_assets_service_workspace_size(&assets_config));
        if (!assets_workspace || pxa_assets_service_init(assets_workspace,
            pxa_assets_service_workspace_size(&assets_config), host.runtime, &assets_config, &host.assets) != PXA_STATUS_OK ||
            pxa_assets_service_register(host.assets) != PXA_STATUS_OK) goto done;
    }
    stage = "log service";
    log_config.struct_size = sizeof(log_config);
    log_config.write = simulator_log_write;
    log_config.app_id = manifest->app_id;
    log_config.max_message_bytes = PXA_LOG_MAX_MESSAGE_BYTES;
    log_workspace = malloc(pxa_log_service_workspace_size(&log_config));
    if (log_workspace == NULL ||
        pxa_log_service_init(log_workspace,
                             pxa_log_service_workspace_size(&log_config),
                             host.runtime, &log_config, &host.log) !=
            PXA_STATUS_OK ||
        pxa_log_service_register(host.log) != PXA_STATUS_OK)
        goto done;
    stage = "device service";
    device_config.struct_size = sizeof(device_config);
    device_config.get_mac = desktop_get_mac;
    device_config.permissions = host.permissions;
    device_config.target = "linux-x86_64";
    device_config.architecture = "x86_64";
    device_config.engine = "wamr";
    device_config.engine_abi = PXSYS_WAMR_ENGINE_ABI;
    device_config.formats = PXA_DEVICE_FORMAT_WASM | PXA_DEVICE_FORMAT_AOT;
    device_workspace = malloc(pxa_device_service_workspace_size(&device_config));
    if (device_workspace == NULL ||
        pxa_device_service_init(device_workspace,
                                pxa_device_service_workspace_size(
                                    &device_config),
                                host.runtime, &device_config, &host.device) !=
            PXA_STATUS_OK ||
        pxa_device_service_register(host.device) != PXA_STATUS_OK)
        goto done;
    if (manifest_uses_service(manifest, PXA_SENSOR_SERVICE_ID)) {
        stage = "sensor service";
        const char *temperature = getenv("PXA_SIM_SENSOR_TEMPERATURE_MILLI_CELSIUS");
        if (temperature != NULL) {
            char *end;
            errno = 0;
            long value = strtol(temperature, &end, 10);
            if (errno || end == temperature || *end || value < INT32_MIN || value > INT32_MAX) {
                fprintf(stderr, "Invalid PXA_SIM_SENSOR_TEMPERATURE_MILLI_CELSIUS\n");
                goto done;
            }
            host.sensor_temperature_milli_celsius = (int32_t)value;
            sensor_descriptor.id = 1;
            static const uint8_t temperature_semantic[] = "ambient.temperature";
            sensor_descriptor.semantic = (pxa_bytes_t){temperature_semantic,
                sizeof(temperature_semantic) - 1};
            sensor_descriptor.unit = PXA_SENSOR_UNIT_MILLI_CELSIUS;
            sensor_descriptor.dimensions = 1;
            sensor_descriptor.min_period_ms = 100;
            sensor_descriptor.max_period_ms = 60000;
            sensor_config.descriptors = &sensor_descriptor;
            sensor_config.descriptor_count = 1;
            fprintf(stderr, "PXA SIM SENSOR ambient.temperature=%ld milli-celsius\n", value);
        }
        sensor_config.struct_size = sizeof(sensor_config);
        sensor_config.max_subscriptions = PRODUCT_COMPONENTS * 2;
        sensor_config.max_subscriptions_per_component = 2;
        sensor_config.provider_context = &host;
        sensor_config.subscribe = desktop_sensor_subscribe;
        sensor_config.read = desktop_sensor_read;
        sensor_config.unsubscribe = desktop_sensor_unsubscribe;
        sensor_config.permissions = host.permissions;
        size_t workspace_size = pxa_sensor_service_workspace_size(&sensor_config);
        sensor_workspace = malloc(workspace_size);
        if (sensor_workspace == NULL || !workspace_size ||
            pxa_sensor_service_init(sensor_workspace, workspace_size, host.runtime,
                                    &sensor_config, &host.sensor) != PXA_STATUS_OK ||
            pxa_sensor_service_register(host.sensor) != PXA_STATUS_OK) goto done;
    }
    stage = "net service";
    if (!pxsys_desktop_net_backend(&net_backend,
                                   desktop_net_completion_ready, &host))
        goto done;
    net_config.struct_size = sizeof(net_config);
    net_config.max_pending_requests = 4;
    net_config.max_requests_per_component = 2;
    net_config.max_response_streams = 4;
    net_config.max_headers = PXA_NET_MAX_HEADERS;
    net_config.max_response_bytes = 262144;
    net_config.max_inline_body_bytes = 65536;
    net_config.max_request_header_bytes = 1024;
    net_config.max_response_header_bytes = 1024;
    net_config.min_timeout_ms = 100;
    net_config.default_timeout_ms = 15000;
    net_config.max_timeout_ms = 60000;
    net_config.backend = net_backend;
    net_config.permissions = host.permissions;
    net_workspace = malloc(pxa_net_service_workspace_size(&net_config));
    if (net_workspace == NULL ||
        pxa_net_service_init(net_workspace,
                             pxa_net_service_workspace_size(&net_config),
                             host.runtime, &net_config, &host.net) !=
            PXA_STATUS_OK ||
        pxa_net_service_register(host.net) != PXA_STATUS_OK)
        goto done;
    stage = "work service";
    for (uint16_t index = 0; index < manifest->component_count; ++index) {
        if (manifest->components[index].kind != PXA_COMPONENT_KIND_JOB)
            continue;
        if (job_component_count == PRODUCT_COMPONENTS) goto done;
        job_components[job_component_count++] = manifest->components[index].id;
    }
    pxa_scheduler_config_init(&scheduler_config);
    scheduler_config.job_components = job_components;
    scheduler_config.job_component_count = job_component_count;
    scheduler_config.clock = work_now_ms;
    scheduler_config.store.context = &host;
    scheduler_config.store.load = work_store_load;
    scheduler_config.store.save = work_store_save;
    scheduler_config.work_context = &host;
    scheduler_config.complete_work = work_complete;
    scheduler_config.cancel_work = work_cancel;
    scheduler_workspace = malloc(
        pxa_scheduler_service_workspace_size(&scheduler_config));
    if (scheduler_workspace == NULL ||
        pxa_scheduler_service_init(scheduler_workspace,
            pxa_scheduler_service_workspace_size(&scheduler_config),
            host.runtime, &scheduler_config, &host.scheduler) != PXA_STATUS_OK ||
        pxa_scheduler_load(host.scheduler) != PXA_STATUS_OK ||
        pxa_scheduler_service_register(host.scheduler) != PXA_STATUS_OK)
        goto done;
    engine_config.struct_size = sizeof(engine_config); engine_config.host_context = &host;
    stage = "WAMR engine";
    engine_config.read_artifact = read_artifact; engine_config.now_us = now_us;
    engine_config.prepare_start = prepare_start; engine_config.max_components = PRODUCT_COMPONENTS;
    engine_config.wasi_enabled = 1; engine_config.allocate_artifact = allocate_memory;
    engine_config.release_artifact = release_memory; engine_config.allocate_runtime = allocate_memory;
    engine_config.reallocate_runtime = reallocate_memory;
    engine_config.release_runtime = release_memory;
    engine_workspace = malloc(pxa_wamr_engine_workspace_size(&engine_config));
    if (engine_workspace == NULL || pxa_wamr_engine_init(engine_workspace,
        pxa_wamr_engine_workspace_size(&engine_config), &engine_config, &host.engine,
        &host.engine_ops) != PXA_STATUS_OK) goto done;
    pxa_wamr_engine_set_runtime(host.engine, host.runtime);
    stage = "start configuration";
    if (configure_start_locale(&host, 1, &ui_config) != PXA_STATUS_OK) goto done;
    capabilities[0].service = PXA_SERVICE_CORE; capabilities[0].version.major = 0; capabilities[0].version.minor = 1;
    capabilities[1].service = PXA_WINDOW_SERVICE_ID; capabilities[1].version.major = PXA_WINDOW_SERVICE_MAJOR; capabilities[1].version.minor = PXA_WINDOW_SERVICE_MINOR;
    capabilities[2].service = PXA_UI_SERVICE_ID; capabilities[2].version.major = PXA_UI_SERVICE_MAJOR; capabilities[2].version.minor = PXA_UI_SERVICE_MINOR;
    capabilities[2].features = PXA_UI_FEATURE_CANVAS |
                              PXA_UI_FEATURE_CANVAS_STREAM_IO |
                              PXA_UI_FEATURE_GRID;
    capabilities[3].service = PRODUCT_CLOCK_SERVICE; capabilities[3].version.major = 0; capabilities[3].version.minor = 1;
    capabilities[4].service = PXA_AUDIO_SERVICE_ID; capabilities[4].version.major = PXA_AUDIO_SERVICE_MAJOR; capabilities[4].version.minor = PXA_AUDIO_SERVICE_MINOR;
    capabilities[5].service = PXA_PERMISSION_SERVICE_ID; capabilities[5].version.major = 0; capabilities[5].version.minor = 1;
    capabilities[6].service = PXA_STORAGE_SERVICE_ID; capabilities[6].version.major = 0; capabilities[6].version.minor = 1;
    capabilities[7].service = PXA_SURFACE_SERVICE_ID; capabilities[7].version.major = 0; capabilities[7].version.minor = 2;
    capabilities[8].service = PXA_GAME_RENDER_SERVICE_ID; capabilities[8].version.major = PXA_GAME_RENDER_SERVICE_MAJOR; capabilities[8].version.minor = PXA_GAME_RENDER_SERVICE_MINOR;
    capabilities[9].service = PXA_LOG_SERVICE_ID; capabilities[9].version.major = PXA_LOG_SERVICE_MAJOR; capabilities[9].version.minor = PXA_LOG_SERVICE_MINOR;
    capabilities[10].service = PXA_DEVICE_SERVICE_ID; capabilities[10].version.major = PXA_DEVICE_SERVICE_MAJOR; capabilities[10].version.minor = PXA_DEVICE_SERVICE_MINOR;
    capabilities[11].service = PXA_NET_SERVICE_ID; capabilities[11].version.major = PXA_NET_SERVICE_MAJOR; capabilities[11].version.minor = PXA_NET_SERVICE_MINOR;
    capabilities[12].service = PXA_FS_SERVICE_ID; capabilities[12].version.major = PXA_FS_SERVICE_MAJOR; capabilities[12].version.minor = PXA_FS_SERVICE_MINOR;
    capabilities[13].service = PXA_IPC_SERVICE_ID; capabilities[13].version.major = PXA_IPC_SERVICE_MAJOR; capabilities[13].version.minor = PXA_IPC_SERVICE_MINOR;
    capabilities[14].service = PXA_WASI_SERVICE_ID; capabilities[14].version.major = PXA_WASI_SERVICE_MAJOR; capabilities[14].version.minor = PXA_WASI_SERVICE_MINOR;
    capabilities[14].features = PXA_WASI_FEATURE_CLOCKS | PXA_WASI_FEATURE_RANDOM;
    capabilities[15].service = PXA_STORE_INSTALLER_SERVICE_ID;
    capabilities[15].version.major = 0;
    capabilities[15].version.minor = 5;
    activation.core_version.major = PXA_CORE_VERSION_MAJOR;
    activation.core_version.minor = PXA_CORE_VERSION_MINOR;
    capabilities[8].features |= PXA_GAME_RENDER_FEATURE_ASSET_BINDINGS;
    capabilities[16].service = PXA_ASSETS_SERVICE_ID;
    capabilities[16].version.major = PXA_ASSETS_SERVICE_MAJOR;
    capabilities[16].version.minor = PXA_ASSETS_SERVICE_MINOR;
    capabilities[17].service = PXA_SENSOR_SERVICE_ID;
    capabilities[17].version.major = PXA_SENSOR_SERVICE_MAJOR;
    capabilities[17].version.minor = PXA_SENSOR_SERVICE_MINOR;
    capabilities[18].service = PXA_WORK_SERVICE_ID;
    capabilities[18].version.major = PXA_WORK_SERVICE_MAJOR;
    capabilities[18].version.minor = PXA_WORK_SERVICE_MINOR;
    activation.services = capabilities; activation.service_count = 19;
    profile.target = (pxa_bytes_t){(const uint8_t *)"linux-x86_64", sizeof("linux-x86_64") - 1u};
    profile.engine = (pxa_bytes_t){(const uint8_t *)"wamr", 4};
    profile.engine_abi = (pxa_bytes_t){(const uint8_t *)PXSYS_WAMR_ENGINE_ABI,
                                       sizeof(PXSYS_WAMR_ENGINE_ABI) - 1u};
    profile.memory_model = PXA_MEMORY_WASM32;
    plan_workspace = malloc(pxa_activation_plan_workspace_size(manifest->component_count));
    stage = "activation plan";
    status = plan_workspace == NULL ? PXA_STATUS_RESOURCE_LIMIT :
        pxa_activation_plan_prepare(plan_workspace,
            pxa_activation_plan_workspace_size(manifest->component_count), manifest, &activation,
            &profile, (pxa_bytes_t){(const uint8_t *)host.package_root, strlen(host.package_root)}, &plan);
    if (status != PXA_STATUS_OK) {
        /* A stale package built against an older engine/service profile is the
         * common cause; name it so the fix (rebuild + reinstall) is obvious. */
        fprintf(stderr,
                "PXA activation plan status=%d for %.*s %.*s "
                "(rebuild and reinstall the package if the ABI changed)\n",
                (int)status, (int)manifest->app_id.size,
                (const char *)manifest->app_id.data,
                (int)manifest->version.size,
                (const char *)manifest->version.data);
        goto done;
    }
    coordinator_workspace = malloc(pxa_activation_coordinator_workspace_size(plan));
    stage = "activation coordinator";
    if (coordinator_workspace == NULL || pxa_activation_coordinator_init(coordinator_workspace,
        pxa_activation_coordinator_workspace_size(plan), host.runtime, plan, &host.engine_ops,
        &host.coordinator) != PXA_STATUS_OK) goto done;
    host.manifest = manifest;
    if (pxa_ipc_broker_set_endpoint_resolver(host.ipc, &host,
                                              resolve_ipc_endpoint) !=
        PXA_STATUS_OK) goto done;
    for (uint16_t index = 0; index < manifest->ipc_endpoint_count; ++index)
        if (pxa_ipc_endpoint_declare(
                host.ipc, manifest->ipc_endpoints[index].name) !=
            PXA_STATUS_OK) goto done;
    stage = "main component start";
    status = pxa_activation_activate(host.coordinator,
        (pxa_bytes_t){(const uint8_t *)"main", 4}, 1, &component);
    if (status != PXA_STATUS_OK) {
        fprintf(stderr, "PXA main component start status=%d\n", (int)status);
        goto done;
    }
    /* Guest startup may fill the mailbox before initial window metrics fit.
     * Drain completions regardless of the metrics result, then retry. */
    status = pxa_window_flush_metrics(host.window, component);
    dispatch_component_events(&host);
    if (status == PXA_STATUS_RESOURCE_LIMIT) {
        (void)pxa_window_flush_metrics(host.window, component);
        dispatch_component_events(&host);
    }
    /* An embedded system UI owns Home and Back; installing the standalone
     * strips would shadow its gesture handling. */
    if (owns_display) {
        install_system_gestures(&host);
        host.input_window_id = SDL_GetWindowID(lv_sdl_window_get_window(display));
        SDL_AddEventWatch(product_key_event_watch, &host);
        key_event_watch_added = 1;
    }
    stage = "shape mask";
    if (owns_display && !install_shape_mask(display, &options, &shape_mask))
        goto done;
    stage = "event loop";
    if (owns_display && options.pxadb_control_socket != NULL &&
        !pxsys_pxadb_control_start(&pxadb_control,
                                   options.pxadb_control_socket, display,
                                   NULL, NULL))
        goto done;
    while (lv_display_get_default() != NULL) {
        const uint64_t loop_start_us = trace_loop ? now_us(NULL) : 0;
        uint64_t pre_end_us = 0, lvgl_end_us = 0, post_end_us = 0;
        if (trace_loop) {
            if (perf_window_start_us == 0) perf_window_start_us = loop_start_us;
            if (previous_loop_start_us != 0) {
                const uint64_t gap = loop_start_us - previous_loop_start_us;
                loop_gap_total_us += gap;
                if (gap > loop_gap_max_us) loop_gap_max_us = gap;
            }
            previous_loop_start_us = loop_start_us;
        }
        pxsys_pxadb_control_poll(&pxadb_control);
        if (pump != NULL) pump(pump_context);
        if (SDL_AtomicGet(&host.home_requested)) host.exit_requested = 1;
        if (host.exit_requested) break;
        if (SDL_AtomicGet(&host.back_requested) &&
            pxa_window_queue_back(host.window, host.active_component) == PXA_STATUS_OK)
            SDL_AtomicSet(&host.back_requested, 0);
        sync_host_theme(&host, lvgl_ui, &lvgl_config.theme);
        dispatch_lifecycle(&host);
        /* Services can complete without producing a UI or clock event. */
        dispatch_component_events(&host);
        if (pump_work(&host) != PXA_STATUS_OK) {
            stage = "work dispatch";
            goto done;
        }
        if (trace_loop) pre_end_us = now_us(NULL);
#ifdef PXSYS_PRODUCT_FRAME_OBSERVER
        const uint64_t lvgl_start_us = now_us(NULL);
#endif
        uint32_t delay = lv_timer_handler();
        if (lv_display_get_default() == NULL) break;
#ifdef PXSYS_PRODUCT_FRAME_OBSERVER
        if (pending_frame_id != 0) {
            PXSYS_PRODUCT_FRAME_OBSERVER(pump_context, &host,
                pending_frame_id, pending_tick_cpu_us +
                now_us(NULL) - lvgl_start_us);
            pending_frame_id = 0;
        }
#endif
        if (trace_loop) lvgl_end_us = now_us(NULL);
        if (drain_surface_updates(&host) < 0) goto done;
#ifdef PXSYS_PRODUCT_FRAME_OBSERVER
        const uint64_t rendered_before_tick = host.raster_telemetry.rendered_frames;
        const uint64_t tick_start_us = now_us(NULL);
#endif
        drain_net_completions(&host);
        dispatch_clock_tick(&host);
        if (drain_surface_updates(&host) < 0) goto done;
#ifdef PXSYS_PRODUCT_FRAME_OBSERVER
        if (host.raster_telemetry.rendered_frames > rendered_before_tick) {
            pending_frame_id = host.raster_telemetry.rendered_frames;
            pending_tick_cpu_us = now_us(NULL) - tick_start_us;
        }
#endif
        if (trace_loop) post_end_us = now_us(NULL);
        if (delay < 1) delay = 1;
        if (delay > 16) delay = 16;
        delay = limit_delay_to_clock_deadline(&host, delay);
        if (SDL_AtomicGet(&host.asset_ready)) delay = 1;
        SDL_Delay(delay);
        if (trace_loop) {
            const uint64_t sleep_end_us = now_us(NULL);
            const uint64_t pre_us = pre_end_us - loop_start_us;
            const uint64_t lvgl_us = lvgl_end_us - pre_end_us;
            const uint64_t post_us = post_end_us - lvgl_end_us;
            const uint64_t sleep_us = sleep_end_us - post_end_us;
            pre_total_us += pre_us;
            lvgl_total_us += lvgl_us;
            post_total_us += post_us;
            sleep_total_us += sleep_us;
            if (pre_us > pre_max_us) pre_max_us = pre_us;
            if (lvgl_us > lvgl_max_us) lvgl_max_us = lvgl_us;
            if (post_us > post_max_us) post_max_us = post_us;
            if (sleep_us > sleep_max_us) sleep_max_us = sleep_us;
            ++perf_loops;
            if (sleep_end_us - perf_window_start_us >= UINT64_C(2000000)) {
                fprintf(stderr,
                        "PXA SIM LOOP samples=%u gap_avg_us=%llu gap_max_us=%llu "
                        "pre_avg_us=%llu pre_max_us=%llu "
                        "lvgl_avg_us=%llu lvgl_max_us=%llu "
                        "post_avg_us=%llu post_max_us=%llu "
                        "sleep_avg_us=%llu sleep_max_us=%llu\n",
                        perf_loops,
                        (unsigned long long)(loop_gap_total_us / perf_loops),
                        (unsigned long long)loop_gap_max_us,
                        (unsigned long long)(pre_total_us / perf_loops),
                        (unsigned long long)pre_max_us,
                        (unsigned long long)(lvgl_total_us / perf_loops),
                        (unsigned long long)lvgl_max_us,
                        (unsigned long long)(post_total_us / perf_loops),
                        (unsigned long long)post_max_us,
                        (unsigned long long)(sleep_total_us / perf_loops),
                        (unsigned long long)sleep_max_us);
                perf_window_start_us = sleep_end_us;
                loop_gap_total_us = loop_gap_max_us = 0;
                pre_total_us = pre_max_us = 0;
                lvgl_total_us = lvgl_max_us = 0;
                post_total_us = post_max_us = 0;
                sleep_total_us = sleep_max_us = 0;
                perf_loops = 0;
            }
        }
    }
    result = 0;
done:
    if (key_event_watch_added) SDL_DelEventWatch(product_key_event_watch, &host);
    if (control_bind != NULL) control_bind(window_context, NULL, NULL, NULL);
    pxsys_pxadb_control_stop(&pxadb_control);
    if (result != 0) fprintf(stderr, "PXA product simulator failed at %s\n", stage);
    if (host.coordinator != NULL && lv_display_get_default() != NULL)
        pxa_activation_deactivate_all(host.coordinator, PXA_STOP_SHUTDOWN);
    audio_stop_worker(&host);
    fprintf(stderr,"PXA MUSIC INPUT read_bytes=%llu read_calls=%llu\n",
        (unsigned long long)host.music_read_bytes,
        (unsigned long long)host.music_read_calls);
    if (host.audio_device != 0) SDL_CloseAudioDevice(host.audio_device);
    audio_release_all_sounds(&host);
    if (host.engine != NULL) pxa_wamr_engine_deinit(host.engine);
    if (lv_display_get_default() != NULL) {
        if (lvgl_ui != NULL) pxa_lvgl_ui_deinit(lvgl_ui);
        if (host.ui != NULL) pxa_ui_service_deinit(host.ui);
    }
    if (host.display_font != NULL) lv_freetype_font_delete(host.display_font);
    if (host.headline_font != NULL) lv_freetype_font_delete(host.headline_font);
    if (host.title_font != NULL) lv_freetype_font_delete(host.title_font);
    if (host.body_font != NULL) lv_freetype_font_delete(host.body_font);
    if (host.label_font != NULL) lv_freetype_font_delete(host.label_font);
    if (host.caption_font != NULL) lv_freetype_font_delete(host.caption_font);
    if (host.runtime != NULL) pxa_runtime_deinit(host.runtime);
    if (host.asset_worker != NULL) {
        pxa_asset_cache_stats_t stats;
        size_t metadata;
        pxa_posix_asset_worker_stats(host.asset_worker, &stats, &metadata);
        fprintf(stderr, "PXA ASSETS peak_internal=%zu peak_external=%zu metadata=%zu native_stack=%zu hits=%llu evictions=%llu failures=%llu pressure_retries=%llu prefetch_requests=%llu prefetch_failures=%llu\n",
            stats.peak_charged[0], stats.peak_charged[1], metadata,
            pxa_posix_asset_worker_stack_bytes(host.asset_worker),
            (unsigned long long)stats.cache_hits, (unsigned long long)stats.evictions,
            (unsigned long long)stats.load_failures, (unsigned long long)stats.pressure_retries,
            (unsigned long long)stats.prefetch_requests, (unsigned long long)stats.prefetch_failures);
        while (pxa_posix_asset_worker_destroy(host.asset_worker) == PXA_STATUS_WOULD_BLOCK) SDL_Delay(1);
        host.asset_worker = NULL;
    }
    pxa_posix_storage_stats_t io_stats;
    pxa_posix_storage_gate_stats(host.storage_gate,&io_stats);
    for (unsigned lane=0;lane<2;++lane) {
        pxa_posix_storage_lane_t *s=&io_stats.lanes[lane];
        fprintf(stderr,"PXA STORAGE lane=%u reads=%llu bytes=%llu wait_us=%llu max_wait_us=%llu service_us=%llu max_service_us=%llu max_read=%u errors=%llu elapsed_us=%llu stalls=%llu stall_us=%llu\n",
            lane,(unsigned long long)s->reads,(unsigned long long)s->bytes,(unsigned long long)s->wait_us,
            (unsigned long long)s->max_wait_us,(unsigned long long)s->service_us,(unsigned long long)s->max_service_us,
            s->max_read_bytes,(unsigned long long)s->errors,(unsigned long long)io_stats.elapsed_us,
            (unsigned long long)io_stats.stalls,(unsigned long long)io_stats.stall_requested_us);
    }
    fprintf(stderr,"PXA MUSIC BUFFER underruns=%llu recoveries=%llu missing_samples=%llu consumed=%llu high_water=%u low_water=%u low_water_valid=%u ready_max_us=%llu\n",
        (unsigned long long)host.music_buffer.underruns,(unsigned long long)host.music_buffer.recoveries,
        (unsigned long long)host.music_buffer.missing_samples,(unsigned long long)host.music_buffer.consumed_samples,
        host.music_buffer.high_water,host.music_buffer.low_water,host.music_buffer.low_water_valid,
        (unsigned long long)host.music_ready_max_us);
    fprintf(stderr,"PXA MUSIC DECODE calls=%llu total_us=%llu max_us=%llu excludes_input_io=1\n",
        (unsigned long long)host.music_decode_calls,(unsigned long long)host.music_decode_us,
        (unsigned long long)host.music_decode_max_us);
    pxa_posix_storage_gate_destroy(host.storage_gate); host.storage_gate=NULL;
    if (host.resource_mutex) {
        pxa_memory_stats_t stats;
        pxa_memory_budget_stats(&host.resource_budget, 0, &stats);
        fprintf(stderr, "PXA RESOURCE BUDGET peak_internal=%zu peak_external=%zu remaining_internal=%zu remaining_external=%zu denied=%llu counter_storage=%zu allocator_storage=%zu\n",
            stats.peak[0], stats.peak[1], stats.charged[0], stats.charged[1],
            (unsigned long long)stats.denied, sizeof(host.resource_budget), sizeof(host.resource_allocators));
        fprintf(stderr, "PXA TEMPORARY BUDGET peak_internal=%zu peak_external=%zu limit_internal=%zu limit_external=%zu\n",
            stats.temporary_peak[0], stats.temporary_peak[1],
            host.resource_budget.config.temporary_limit[0], host.resource_budget.config.temporary_limit[1]);
        if (resource_memory_end(&host) != PXA_STATUS_OK) {
            fprintf(stderr, "PXA resource allocations remain at shutdown\n");
            result = 1;
        }
    }
    free(assets_workspace);
    if (posix_fs != NULL) pxa_posix_fs_deinit(posix_fs);
    if (posix_storage != NULL) pxa_posix_storage_deinit(posix_storage);
    if (installer != NULL) pxa_posix_installer_deinit(installer);
    free(coordinator_workspace); free(plan_workspace); free(engine_workspace); free(lvgl_workspace);
    free(log_workspace); free(game_render_workspace); free(surface_workspace);
    free(device_workspace); free(net_workspace);
    free(sensor_workspace);
    free(ui_workspace); free(window_workspace); free(permission_workspace); free(runtime_workspace); free(manifest_workspace);
    free(fs_service_workspace); free(fs_backend_workspace); free(fs_path); free(fs_parent);
    free(ipc_workspace);
    free(scheduler_workspace);
    free(storage_service_workspace); free(storage_workspace); free(storage_path); free(storage_parent); free(default_state_root);
    free(encoded); free(installer_workspace); free(public_key);
    if (owns_display && display != NULL && lv_display_get_default() != NULL) {
        lv_display_delete(display);
        lv_deinit();
    }
    free(shape_mask.pixels);
    return result;
}

int pxsys_product_simulator_run_embedded(const char *package_path,
                                         const char *publisher_key,
                                         const char *state_root,
                                         const char *locale,
                                         uint32_t width, uint32_t height,
                                         lv_display_t *display, lv_obj_t *parent,
                                         pxsys_product_simulator_pump_fn pump,
                                         void *pump_context,
                                         pxsys_product_simulator_window_fn window_changed,
                                         void *window_context,
                                         pxsys_product_simulator_control_bind_fn control_bind,
                                         const pxsys_display_profile_t *host_display,
                                         const pxsys_insets_t *host_bar_insets,
                                         const pxsys_theme_snapshot_t *host_theme) {
    const options_t options = {
        .package_path = package_path,
        .publisher_key = publisher_key,
        .state_root = state_root,
        .locale = locale,
        .width = width,
        .height = height,
    };
    return run_product_simulator(&options, display, parent, pump, pump_context,
                                 window_changed, window_context, control_bind,
                                 host_display, host_bar_insets, host_theme);
}

#ifndef PXSYS_PRODUCT_RUNNER_LIBRARY
int main(int argc, char **argv) {
    options_t options;
    if (!parse_options(argc, argv, &options)) {
        print_usage(argv[0]);
        return 2;
    }
    return run_product_simulator(&options, NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL);
}
#endif
