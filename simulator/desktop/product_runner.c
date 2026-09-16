#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <SDL2/SDL.h>

#include "lvgl.h"
#include "pxa/activation.h"
#include "pxa/audio.h"
#include "pxa/game_render.h"
#include "pxa/lvgl/pxa_lvgl_ui.h"
#include "pxa/openssl/pxa_openssl.h"
#include "pxa/package.h"
#include "pxa/permission.h"
#include "pxa/posix/pxa_posix_installer.h"
#include "pxa/posix/pxa_posix_storage.h"
#include "pxa/runtime.h"
#include "pxa/service.h"
#include "pxa/storage.h"
#include "pxa/surface.h"
#include "pxa/ui.h"
#include "pxa/wamr/pxa_wamr_engine.h"
#include "pxa/wire.h"
#include "pxa/window.h"
#include "pxadb_control.h"
#include "src/drivers/sdl/lv_sdl_keyboard.h"

#define PRODUCT_CLOCK_SERVICE UINT16_C(4)
#define PRODUCT_CLOCK_TICK UINT16_C(0x8001)
#define PRODUCT_COMPONENTS UINT16_C(8)
#define PRODUCT_SYSTEM_GESTURE_EDGE_WIDTH 16
#define PRODUCT_SYSTEM_GESTURE_HOME_HEIGHT 20
#define PRODUCT_SYSTEM_GESTURE_COMMIT_DISTANCE 32
#define PRODUCT_SURFACE_FLAG_GAME_RENDER UINT8_C(8)

typedef struct {
    const char *package_path;
    const char *publisher_key;
    const char *state_root;
    const char *pxadb_control_socket;
    uint32_t width;
    uint32_t height;
} options_t;

typedef struct {
    lv_display_t *display;
    pxa_runtime_t *runtime;
    pxa_window_service_t *window;
    pxa_ui_service_t *ui;
    pxa_ui_backend_t ui_backend;
    pxa_audio_service_t *audio;
    pxa_permission_service_t *permissions;
    pxa_surface_service_t *surfaces;
    pxa_game_render_service_t *game_render;
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
    uint16_t *raster_depth_buffers[2];
    uint8_t *raster_draw_lists[2];
    uint32_t raster_draw_sizes[2];
    int8_t raster_current_buffer;
    int8_t raster_draw_pending;
    uint64_t raster_last_frame_id;
    uint16_t *raster_palette;
    uint8_t *raster_textures[PXA_RASTER_MAX_TEXTURES];
    uint16_t raster_texture_width[PXA_RASTER_MAX_TEXTURES];
    uint16_t raster_texture_height[PXA_RASTER_MAX_TEXTURES];
    pxa_raster_telemetry_t raster_telemetry;
    pxa_wamr_engine_t *engine;
    pxa_component_engine_t engine_ops;
    pxa_activation_coordinator_t *coordinator;
    pxa_component_t active_component;
    char package_root[1024];
    uint32_t width;
    uint32_t height;
    uint16_t clock_period_ms;
    uint64_t next_clock_tick_us;
    lv_obj_t *system_back_gesture;
    lv_obj_t *system_home_gesture;
    lv_obj_t *system_back_indicator;
    int32_t system_gesture_press_x;
    int32_t system_gesture_press_y;
    uint8_t exit_requested;
} product_host_t;

static uint64_t now_us(void *context) {
    struct timespec now;
    (void)context;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000);
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
        host->exit_requested = 1;
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

static void install_system_gestures(product_host_t *host) {
    lv_obj_t *layer;
    if (host == NULL || host->display == NULL) return;
    layer = lv_display_get_layer_top(host->display);
    host->system_back_gesture = lv_obj_create(layer);
    lv_obj_set_pos(host->system_back_gesture, 0, 0);
    lv_obj_set_size(host->system_back_gesture,
                    PRODUCT_SYSTEM_GESTURE_EDGE_WIDTH,
                    (int32_t)host->height - PRODUCT_SYSTEM_GESTURE_HOME_HEIGHT);
    lv_obj_set_style_bg_opa(host->system_back_gesture, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(host->system_back_gesture, 0, 0);
    lv_obj_set_style_pad_all(host->system_back_gesture, 0, 0);
    lv_obj_clear_flag(host->system_back_gesture, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(host->system_back_gesture,
                    LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
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
                   (int32_t)host->height - PRODUCT_SYSTEM_GESTURE_HOME_HEIGHT);
    lv_obj_set_size(host->system_home_gesture, (int32_t)host->width,
                    PRODUCT_SYSTEM_GESTURE_HOME_HEIGHT);
    lv_obj_set_style_bg_opa(host->system_home_gesture, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(host->system_home_gesture, 0, 0);
    lv_obj_set_style_pad_all(host->system_home_gesture, 0, 0);
    lv_obj_clear_flag(host->system_home_gesture, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(host->system_home_gesture,
                    LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(host->system_home_gesture, system_home_gesture_event,
                        LV_EVENT_PRESSED, host);
    lv_obj_add_event_cb(host->system_home_gesture, system_home_gesture_event,
                        LV_EVENT_RELEASED, host);
    lv_obj_add_event_cb(host->system_home_gesture, system_home_gesture_event,
                        LV_EVENT_PRESS_LOST, host);
}

static void *allocate_memory(void *context, size_t size) {
    (void)context;
    return malloc(size);
}

static void release_memory(void *context, void *memory) {
    (void)context;
    free(memory);
}

static pxa_status_t surface_create(void *context, const pxa_surface_desc_t *desc,
                                   uint64_t *surface, uint32_t *stride) {
    product_host_t *host = context;
    uint8_t scale;
    const int mapped = desc != NULL &&
        (desc->flags & PXA_SURFACE_FLAG_GUEST_MAPPED) != 0;
    const int raster = desc != NULL &&
        (desc->flags & PRODUCT_SURFACE_FLAG_GAME_RENDER) != 0;
    if (host == NULL || desc == NULL || surface == NULL || stride == NULL ||
        desc->format != PXA_SURFACE_FORMAT_RGB565 ||
        (desc->flags & ~(PXA_SURFACE_FLAG_KNOWN_MASK |
                         PRODUCT_SURFACE_FLAG_GAME_RENDER)) != 0 ||
        mapped == raster ||
        desc->width == 0 || desc->height == 0 ||
        desc->buffer_count < 2 || desc->buffer_count > 3 ||
        host->surface_frame_bytes != 0)
        return PXA_STATUS_UNSUPPORTED;
    if (host->width % desc->width != 0 || host->height % desc->height != 0 ||
        host->width / desc->width != host->height / desc->height)
        return PXA_STATUS_UNSUPPORTED;
    scale = (uint8_t)(host->width / desc->width);
    if (scale != 1 && scale != 2 && scale != 4)
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
        host->surface_display_buffer = malloc(host->surface_display_frame_bytes);
        if (host->surface_display_buffer == NULL) goto failed;
        for (index = 0; index < 2; ++index) {
            host->raster_buffers[index] = malloc(host->surface_frame_bytes);
            host->raster_depth_buffers[index] = malloc(
                (size_t)host->surface_width * host->surface_height *
                sizeof(*host->raster_depth_buffers[index]));
            host->raster_draw_lists[index] = malloc(PXA_RASTER_MAX_DRAW_BYTES);
            if (host->raster_buffers[index] == NULL ||
                host->raster_depth_buffers[index] == NULL ||
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
        host->surface_image = lv_image_create(lv_screen_active());
        if (host->surface_image == NULL) goto failed;
        lv_image_set_src(host->surface_image, &host->surface_bitmap);
        lv_image_set_inner_align(host->surface_image, LV_IMAGE_ALIGN_TOP_LEFT);
        lv_obj_clear_flag(host->surface_image, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(host->surface_image, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(host->surface_image, host->surface_display_width,
                        host->surface_display_height);
        lv_obj_move_foreground(host->surface_image);
    }
    return PXA_STATUS_OK;
failed:
    for (uint8_t index = 0; index < 2; ++index) {
        free(host->raster_buffers[index]);
        free(host->raster_depth_buffers[index]);
        free(host->raster_draw_lists[index]);
        host->raster_buffers[index] = NULL;
        host->raster_depth_buffers[index] = NULL;
        host->raster_draw_lists[index] = NULL;
    }
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
    host->surface_image = lv_image_create(lv_screen_active());
    if (host->surface_image == NULL) {
        free(host->surface_display_buffer);
        host->surface_display_buffer = NULL;
        host->surface_buffers = NULL;
        host->surface_registered = 0;
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    lv_image_set_src(host->surface_image, &host->surface_bitmap);
    lv_image_set_inner_align(host->surface_image, LV_IMAGE_ALIGN_TOP_LEFT);
    lv_obj_clear_flag(host->surface_image, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(host->surface_image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(host->surface_image, host->surface_display_width,
                    host->surface_display_height);
    lv_obj_set_pos(host->surface_image,
                   host->surface_layer.x * host->surface_scale,
                   host->surface_layer.y * host->surface_scale);
    lv_obj_move_foreground(host->surface_image);
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
        lv_obj_set_pos(host->surface_image, layer->x * host->surface_scale,
                       layer->y * host->surface_scale);
        if (layer->visible)
            lv_obj_remove_flag(host->surface_image, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(host->surface_image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(host->surface_image);
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

static void raster_resources(const product_host_t *host,
                             pxa_raster_resources_t *resources) {
    memset(resources, 0, sizeof(*resources));
    resources->palette = host->raster_palette;
    resources->capabilities = PXA_RASTER_CAP_FLAT_QUAD |
                              PXA_RASTER_CAP_TEXTURED_QUAD |
                              PXA_RASTER_CAP_ADDITIVE_SPRITE |
                              PXA_RASTER_CAP_SPRITE_BATCH |
                              PXA_RASTER_CAP_TRIANGLE_BATCH;
    for (uint8_t index = 0; index < PXA_RASTER_MAX_TEXTURES; ++index) {
        resources->textures[index].pixels = host->raster_textures[index];
        resources->textures[index].width = host->raster_texture_width[index];
        resources->textures[index].height = host->raster_texture_height[index];
    }
}

static pxa_status_t surface_raster_upload(void *context, uint64_t surface,
                                          const uint8_t *bytes, size_t size) {
    product_host_t *host = context;
    pxa_raster_upload_view_t upload;
    uint8_t *replacement;
    uint8_t *previous;
    pxa_status_t status;
    if (host == NULL || surface != 1 || bytes == NULL ||
        (host->surface_flags & PRODUCT_SURFACE_FLAG_GAME_RENDER) == 0 ||
        host->raster_last_frame_id != 0 || host->raster_draw_pending >= 0)
        return PXA_STATUS_BAD_STATE;
    status = pxa_raster_decode_upload(bytes, size, &upload);
    if (status != PXA_STATUS_OK) return status;
    replacement = malloc(upload.payload_bytes);
    if (replacement == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    if (upload.kind == PXA_RASTER_UPLOAD_PALETTE_RGB565) {
        for (uint16_t index = 0; index < PXA_RASTER_PALETTE_COLORS; ++index)
            ((uint16_t *)replacement)[index] =
                pxa_read_u16(upload.payload + (size_t)index * 2u);
        previous = (uint8_t *)host->raster_palette;
        host->raster_palette = (uint16_t *)replacement;
    } else {
        memcpy(replacement, upload.payload, upload.payload_bytes);
        previous = host->raster_textures[upload.slot];
        host->raster_textures[upload.slot] = replacement;
        host->raster_texture_width[upload.slot] = upload.width;
        host->raster_texture_height[upload.slot] = upload.height;
    }
    free(previous);
    return PXA_STATUS_OK;
}

static pxa_status_t surface_raster_submit(void *context, uint64_t surface,
                                          const uint8_t *bytes, size_t size) {
    product_host_t *host = context;
    pxa_raster_resources_t resources;
    pxa_raster_target_t target;
    pxa_raster_draw_list_view_t list;
    pxa_status_t status;
    uint8_t mailbox;
    if (host == NULL || surface != 1 || bytes == NULL ||
        host->raster_palette == NULL ||
        (host->surface_flags & PRODUCT_SURFACE_FLAG_GAME_RENDER) == 0)
        return PXA_STATUS_BAD_STATE;
    raster_resources(host, &resources);
    target.pixels = (uint16_t *)host->raster_buffers[0];
    target.depth_pixels = host->raster_depth_buffers[0];
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
        free(host->raster_depth_buffers[index]);
        free(host->raster_draw_lists[index]);
        host->raster_buffers[index] = NULL;
        host->raster_depth_buffers[index] = NULL;
        host->raster_draw_lists[index] = NULL;
    }
    for (uint8_t index = 0; index < PXA_RASTER_MAX_TEXTURES; ++index) {
        free(host->raster_textures[index]);
        host->raster_textures[index] = NULL;
    }
    free(host->raster_palette);
    host->raster_palette = NULL;
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
    if ((desc->flags & PXA_GAME_RENDER_FLAG_PREFER_DIRECT_SCANOUT) != 0)
        surface_desc.flags |= PXA_SURFACE_FLAG_PREFER_DIRECT_SCANOUT;
    status = surface_create(context, &surface_desc, provider_context, &stride);
    if (status != PXA_STATUS_OK) return status;
    *capabilities = PXA_RASTER_CAP_FLAT_QUAD |
                    PXA_RASTER_CAP_TEXTURED_QUAD |
                    PXA_RASTER_CAP_ADDITIVE_SPRITE |
                    PXA_RASTER_CAP_SPRITE_BATCH |
                    PXA_RASTER_CAP_TRIANGLE_BATCH;
    return PXA_STATUS_OK;
}

static int surface_process_pending(product_host_t *host) {
    uint8_t buffer_index = 0;
    uint64_t frame_id;
    const uint16_t *source;
    if (host == NULL || !host->surface_registered ||
        host->surface_image == NULL)
        return 0;
    if ((host->surface_flags & PRODUCT_SURFACE_FLAG_GAME_RENDER) != 0) {
        pxa_raster_resources_t resources;
        pxa_raster_target_t target;
        pxa_raster_draw_list_view_t list;
        pxa_raster_telemetry_t frame_telemetry = {0};
        pxa_status_t status;
        const uint8_t draw_index = (uint8_t)host->raster_draw_pending;
        if (host->raster_draw_pending < 0) return 0;
        buffer_index = host->raster_current_buffer == 0 ? 1 : 0;
        raster_resources(host, &resources);
        target.pixels = (uint16_t *)host->raster_buffers[buffer_index];
        target.depth_pixels = host->raster_depth_buffers[buffer_index];
        target.stride_pixels = host->surface_stride_bytes / 2u;
        target.depth_stride_pixels = host->surface_width;
        target.width = host->surface_width;
        target.height = host->surface_height;
        status = pxa_raster_validate_draw_list(host->raster_draw_lists[draw_index],
                                               host->raster_draw_sizes[draw_index],
                                               &target, &resources, &list);
        host->raster_draw_pending = -1;
        if (status != PXA_STATUS_OK) {
            ++host->raster_telemetry.rejected_lists;
            return 0;
        }
        pxa_raster_execute_draw_list(host->raster_draw_lists[draw_index], &list,
                                     &target, &resources, &frame_telemetry);
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
        const uint32_t destination_stride = host->surface_display_stride_bytes / 2u;
        for (uint16_t y = 0; y < host->surface_display_height; ++y) {
            const uint16_t *source_row = source +
                (uint32_t)(y / host->surface_scale) * source_stride;
            uint16_t *destination_row = destination + (uint32_t)y * destination_stride;
            for (uint16_t x = 0; x < host->surface_display_width; ++x)
                destination_row[x] = source_row[x / host->surface_scale];
        }
    }
    if ((host->surface_flags & PXA_SURFACE_FLAG_GUEST_MAPPED) != 0) {
        host->surface_pending_buffer = -1;
        host->surface_pending_frame_id = 0;
    }
    ++host->surface_presented_frames;
    if ((host->surface_flags & PXA_SURFACE_FLAG_GUEST_MAPPED) != 0 &&
        !surface_enqueue_release(host, buffer_index, frame_id)) return 0;
    lv_image_set_src(host->surface_image, &host->surface_bitmap);
    lv_obj_move_foreground(host->surface_image);
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

static const void *resolve_asset(const uint8_t *path, size_t path_size,
                                 void *context) {
    product_host_t *host = context;
    char *full_path;
    if (host == NULL || path == NULL || path_size == 0 ||
        path_size > 512 || strlen(host->package_root) + path_size + 2 >= 1024)
        return NULL;
    full_path = malloc(strlen(host->package_root) + path_size + 2);
    if (full_path == NULL) return NULL;
    sprintf(full_path, "%s/%.*s", host->package_root, (int)path_size, path);
    return full_path;
}

static void release_asset(const void *asset, void *context) {
    (void)context;
    free((void *)asset);
}

static void dispatch_component_events(product_host_t *host) {
    pxa_wamr_event_result_t result;
    if (host == NULL || host->engine == NULL || host->runtime == NULL ||
        host->active_component == PXA_COMPONENT_INVALID)
        return;
    while (pxa_wamr_engine_deliver_event_result(host->engine, host->runtime,
                                                 host->active_component,
                                                 &result) == PXA_STATUS_OK) {
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
        host->clock_period_ms == 0)
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
    if (host == NULL || host->clock_period_ms == 0 ||
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
    (void)context;
    (void)config;
    return PXA_STATUS_OK;
}

static pxa_status_t prepare_start(void *context, pxa_component_t component,
                                  uint64_t instance_id, uint8_t kind) {
    product_host_t *host = context;
    pxa_window_backend_t window_backend = {0};
    pxa_window_snapshot_t snapshot = {0};
    pxa_status_t status;
    (void)instance_id;
    if (host == NULL || kind != PXA_COMPONENT_KIND_UI) return PXA_STATUS_UNSUPPORTED;
    window_backend.struct_size = sizeof(window_backend);
    window_backend.context = host;
    window_backend.apply = window_apply;
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
    snapshot.focused = 1;
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

static pxa_status_t clock_control(void *context, pxa_runtime_t *runtime,
                                  pxa_component_t component,
                                  const pxa_message_view_t *message) {
    product_host_t *host = context;
    (void)runtime;
    if (host == NULL || message == NULL || component != host->active_component)
        return PXA_STATUS_BAD_STATE;
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

static pxa_status_t audio_open(void *context, uint16_t usage,
                               pxa_audio_format_t *format, uint64_t *session) {
    (void)context;
    if (usage != PXA_AUDIO_USAGE_MEDIA || format == NULL || session == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    format->sample_rate = 16000;
    format->channels = 1;
    format->frame_ms = 20;
    *session = 1;
    return PXA_STATUS_OK;
}

static pxa_status_t audio_commit(void *context, uint64_t session,
                                 const pxa_audio_graph_t *graph) {
    (void)context;
    (void)graph;
    return session == 1 ? PXA_STATUS_OK : PXA_STATUS_NOT_FOUND;
}

static pxa_status_t audio_submit(void *context, uint64_t session,
                                 const uint8_t *pcm, size_t size) {
    (void)context;
    (void)pcm;
    (void)size;
    return session == 1 ? PXA_STATUS_OK : PXA_STATUS_NOT_FOUND;
}

static void audio_close(void *context, uint64_t session) {
    (void)context;
    (void)session;
}

static pxa_status_t audio_query(void *context, uint64_t session,
                                pxa_audio_state_t *state) {
    (void)context;
    if (session != 1 || state == NULL) return PXA_STATUS_NOT_FOUND;
    memset(state, 0, sizeof(*state));
    state->flags = PXA_AUDIO_STATE_ACCEPTED_IS_SINK_SUBMITTED;
    return PXA_STATUS_OK;
}

static pxa_status_t audio_flush(void *context, uint64_t session) {
    (void)context;
    return session == 1 ? PXA_STATUS_OK : PXA_STATUS_NOT_FOUND;
}

static pxa_status_t permission_load(void *context, pxa_bytes_t identity,
                                    pxa_bytes_t name, pxa_bytes_t scope,
                                    pxa_permission_decision_t *decision) {
    (void)context;
    (void)identity;
    (void)name;
    (void)scope;
    (void)decision;
    return PXA_STATUS_NOT_FOUND;
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

static void print_usage(const char *program) {
    fprintf(stderr, "Usage: %s --package DIR --publisher-key DER [--state-root DIR] [--pxadb-control-socket PATH] [--width PX --height PX]\n",
            program);
}

static int parse_options(int argc, char **argv, options_t *options) {
    int index;
    memset(options, 0, sizeof(*options));
    options->width = 296;
    options->height = 240;
    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--package") == 0 && index + 1 < argc)
            options->package_path = argv[++index];
        else if (strcmp(argv[index], "--publisher-key") == 0 && index + 1 < argc)
            options->publisher_key = argv[++index];
        else if (strcmp(argv[index], "--state-root") == 0 && index + 1 < argc)
            options->state_root = argv[++index];
        else if (strcmp(argv[index], "--pxadb-control-socket") == 0 && index + 1 < argc)
            options->pxadb_control_socket = argv[++index];
        else if (strcmp(argv[index], "--width") == 0 && index + 1 < argc)
            options->width = (uint32_t)strtoul(argv[++index], NULL, 10);
        else if (strcmp(argv[index], "--height") == 0 && index + 1 < argc)
            options->height = (uint32_t)strtoul(argv[++index], NULL, 10);
        else return 0;
    }
    return options->package_path != NULL && options->publisher_key != NULL &&
           options->width > 0 && options->height > 0;
}

int main(int argc, char **argv) {
    options_t options;
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
    pxa_surface_config_t surface_config = {0};
    pxa_game_render_config_t game_render_config = {0};
    pxa_service_ops_t clock_service = {0};
    pxa_wamr_engine_config_t engine_config = {0};
    pxa_package_service_capability_t capabilities[9] = {0};
    pxa_package_activation_profile_t activation = {0};
    pxa_package_host_profile_t profile = {0};
    pxa_activation_plan_t *plan = NULL;
    void *installer_workspace = NULL, *manifest_workspace = NULL;
    void *runtime_workspace = NULL, *window_workspace = NULL, *ui_workspace = NULL;
    void *permission_workspace = NULL, *audio_workspace = NULL, *storage_workspace = NULL;
    void *storage_service_workspace = NULL, *lvgl_workspace = NULL, *engine_workspace = NULL;
    void *surface_workspace = NULL, *game_render_workspace = NULL;
    void *plan_workspace = NULL, *coordinator_workspace = NULL;
    uint8_t *encoded = NULL, *public_key = NULL;
    size_t manifest_size = 0, public_key_size = 0;
    char root[1024] = {0};
    char *storage_parent = NULL;
    char *storage_path = NULL;
    lv_display_t *display = NULL;
    lv_indev_t *mouse = NULL;
    lv_indev_t *keyboard = NULL;
    pxsys_pxadb_control_t pxadb_control = {.listener = -1};
    pxa_component_t component;
    pxa_status_t status;
    const char *stage = "arguments";
    int result = 1;

    {
        struct stat metadata;
        if (!parse_options(argc, argv, &options)) {
            print_usage(argv[0]);
            return 2;
        }
        if (stat(options.package_path, &metadata) != 0 ||
            !S_ISDIR(metadata.st_mode)) {
            fprintf(stderr, "PXA product simulator requires an unpacked package directory: %s\n",
                    options.package_path);
            return 2;
        }
    }
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
    stage = "package verification";
    if (pxa_posix_installer_verify_source(installer, options.package_path,
                                          &package_result) != PXA_STATUS_OK)
        goto done;
    stage = "display";
    if (strlen(root) >= sizeof(host.package_root)) goto done;
    strcpy(host.package_root, root); host.width = options.width; host.height = options.height;
    lv_init();
    display = lv_sdl_window_create((int32_t)options.width, (int32_t)options.height);
    mouse = lv_sdl_mouse_create();
    keyboard = lv_sdl_keyboard_create();
    if (display == NULL || mouse == NULL || keyboard == NULL) goto done;
    host.display = display;
    lv_indev_set_display(mouse, display);
    lv_indev_set_display(keyboard, display);
    lv_sdl_window_set_title(display, "PXA Product Simulator");
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
    ui_config.allocate = allocate_memory; ui_config.release = release_memory;
    ui_config.now_us = now_us; ui_config.features = PXA_UI_FEATURE_CANVAS |
        PXA_UI_FEATURE_VIRTUAL_LIST | PXA_UI_FEATURE_RGB565_BITMAP |
        PXA_UI_FEATURE_CONTROLLER_INPUT | PXA_UI_FEATURE_MULTIPLE_SURFACES |
        PXA_UI_FEATURE_CANVAS_STREAM_IO;
    ui_config.primary_width = options.width; ui_config.primary_height = options.height;
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
        const char *state_root = options.state_root != NULL ? options.state_root : options.package_path;
        size_t state_root_size = strlen(state_root);
        storage_parent = malloc(state_root_size + sizeof("/app-data"));
        if (storage_parent == NULL) goto done;
        snprintf(storage_parent, state_root_size + sizeof("/app-data"),
                 "%s/app-data", state_root);
        if (mkdir(storage_parent, 0700) != 0 && errno != EEXIST) goto done;
        storage_path = malloc(strlen(storage_parent) + 1 + manifest->app_id.size + 1);
        if (storage_path == NULL) goto done;
        snprintf(storage_path, strlen(storage_parent) + 1 + manifest->app_id.size + 1,
                 "%s/%.*s", storage_parent, (int)manifest->app_id.size,
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
    audio_backend.struct_size = sizeof(audio_backend);
    stage = "audio service";
    audio_backend.open = audio_open; audio_backend.commit = audio_commit;
    audio_backend.submit = audio_submit; audio_backend.close = audio_close;
    audio_backend.query = audio_query; audio_backend.flush = audio_flush;
    audio_config.struct_size = sizeof(audio_config);
    audio_config.max_sessions = 2; audio_config.max_sessions_per_component = 2;
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
    lvgl_config.struct_size = sizeof(lvgl_config);
    stage = "LVGL adapter";
    lvgl_config.allocate = allocate_memory; lvgl_config.release = release_memory;
    lvgl_config.execute = execute_inline; lvgl_config.resolve_asset = resolve_asset;
    lvgl_config.release_asset = release_asset; lvgl_config.event_callback = ui_event;
    lvgl_config.now_us = now_us; lvgl_config.callback_user_data = &host;
    lvgl_config.asset_user_data = &host;
    lvgl_config.primary_environment.surface = PXA_UI_PRIMARY_SURFACE;
    lvgl_config.primary_environment.width = options.width;
    lvgl_config.primary_environment.height = options.height;
    lvgl_config.primary_environment.density_q16 = UINT32_C(1) << 16;
    lvgl_config.primary_environment.font_scale_q16 = UINT32_C(1) << 16;
    pxa_lvgl_ui_theme_init(&lvgl_config.theme);
    lvgl_config.theme.body_font = &lv_font_montserrat_14;
    lvgl_config.theme.title_font = &lv_font_montserrat_20;
    lvgl_workspace = malloc(pxa_lvgl_ui_workspace_size());
    if (lvgl_workspace == NULL || pxa_lvgl_ui_init(lvgl_workspace,
        pxa_lvgl_ui_workspace_size(), &lvgl_config, &lvgl_ui, &ui_backend) != PXA_STATUS_OK)
        goto done;
    /* The UI backend is bound during prepare_start. */
    host.ui_backend = ui_backend;
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
    game_render_config.max_width = options.width;
    game_render_config.max_height = options.height;
    game_render_config.min_buffer_count = 2;
    game_render_config.max_buffer_count = 3;
    game_render_config.backend.struct_size = sizeof(game_render_config.backend);
    game_render_config.backend.context = &host;
    game_render_config.backend.create = game_render_create;
    game_render_config.backend.upload = surface_raster_upload;
    game_render_config.backend.submit = surface_raster_submit;
    game_render_config.backend.query = surface_raster_query;
    game_render_config.backend.close = surface_close;
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
    capabilities[0].service = PXA_SERVICE_CORE; capabilities[0].version.major = 0; capabilities[0].version.minor = 1;
    capabilities[1].service = PXA_WINDOW_SERVICE_ID; capabilities[1].version.major = 0; capabilities[1].version.minor = 1;
    capabilities[2].service = PXA_UI_SERVICE_ID; capabilities[2].version.major = 0; capabilities[2].version.minor = 3;
    capabilities[3].service = PRODUCT_CLOCK_SERVICE; capabilities[3].version.major = 0; capabilities[3].version.minor = 1;
    capabilities[4].service = PXA_AUDIO_SERVICE_ID; capabilities[4].version.major = 0; capabilities[4].version.minor = 5;
    capabilities[5].service = PXA_PERMISSION_SERVICE_ID; capabilities[5].version.major = 0; capabilities[5].version.minor = 1;
    capabilities[6].service = PXA_STORAGE_SERVICE_ID; capabilities[6].version.major = 0; capabilities[6].version.minor = 1;
    capabilities[7].service = PXA_SURFACE_SERVICE_ID; capabilities[7].version.major = 0; capabilities[7].version.minor = 2;
    capabilities[8].service = PXA_GAME_RENDER_SERVICE_ID; capabilities[8].version.major = 0; capabilities[8].version.minor = 1;
    activation.core_version.major = 0; activation.core_version.minor = 1;
    activation.services = capabilities; activation.service_count = 9;
    profile.target = (pxa_bytes_t){(const uint8_t *)"linux-x86_64", 13};
    profile.engine = (pxa_bytes_t){(const uint8_t *)"wamr", 4};
    profile.engine_abi = (pxa_bytes_t){(const uint8_t *)"wasm32", 6};
    profile.memory_model = PXA_MEMORY_WASM32;
    plan_workspace = malloc(pxa_activation_plan_workspace_size(manifest->component_count));
    stage = "activation plan";
    status = plan_workspace == NULL ? PXA_STATUS_RESOURCE_LIMIT :
        pxa_activation_plan_prepare(plan_workspace,
            pxa_activation_plan_workspace_size(manifest->component_count), manifest, &activation,
            &profile, (pxa_bytes_t){(const uint8_t *)host.package_root, strlen(host.package_root)}, &plan);
    if (status != PXA_STATUS_OK) {
        fprintf(stderr, "PXA activation plan status=%d\n", (int)status);
        goto done;
    }
    coordinator_workspace = malloc(pxa_activation_coordinator_workspace_size(plan));
    stage = "activation coordinator";
    if (coordinator_workspace == NULL || pxa_activation_coordinator_init(coordinator_workspace,
        pxa_activation_coordinator_workspace_size(plan), host.runtime, plan, &host.engine_ops,
        &host.coordinator) != PXA_STATUS_OK) goto done;
    stage = "main component start";
    status = pxa_activation_activate(host.coordinator,
        (pxa_bytes_t){(const uint8_t *)"main", 4}, 1, &component);
    if (status != PXA_STATUS_OK) {
        fprintf(stderr, "PXA main component start status=%d\n", (int)status);
        goto done;
    }
    if (pxa_window_flush_metrics(host.window, component) == PXA_STATUS_OK)
        dispatch_component_events(&host);
    install_system_gestures(&host);
    if (options.pxadb_control_socket != NULL &&
        !pxsys_pxadb_control_start(&pxadb_control,
                                   options.pxadb_control_socket, display,
                                   NULL, NULL))
        goto done;
    while (lv_display_get_default() != NULL) {
        pxsys_pxadb_control_poll(&pxadb_control);
        if (host.exit_requested) break;
        uint32_t delay = lv_timer_handler();
        if (drain_surface_updates(&host) < 0) goto done;
        dispatch_clock_tick(&host);
        if (drain_surface_updates(&host) < 0) goto done;
        if (delay < 1) delay = 1;
        if (delay > 16) delay = 16;
        delay = limit_delay_to_clock_deadline(&host, delay);
        SDL_Delay(delay);
    }
    result = 0;
done:
    pxsys_pxadb_control_stop(&pxadb_control);
    if (result != 0) fprintf(stderr, "PXA product simulator failed at %s\n", stage);
    if (host.coordinator != NULL && lv_display_get_default() != NULL)
        pxa_activation_deactivate_all(host.coordinator, PXA_STOP_SHUTDOWN);
    if (host.engine != NULL) pxa_wamr_engine_deinit(host.engine);
    if (lv_display_get_default() != NULL) {
        if (lvgl_ui != NULL) pxa_lvgl_ui_deinit(lvgl_ui);
        if (host.ui != NULL) pxa_ui_service_deinit(host.ui);
    }
    if (host.runtime != NULL) pxa_runtime_deinit(host.runtime);
    if (posix_storage != NULL) pxa_posix_storage_deinit(posix_storage);
    if (installer != NULL) pxa_posix_installer_deinit(installer);
    free(coordinator_workspace); free(plan_workspace); free(engine_workspace); free(lvgl_workspace);
    free(game_render_workspace); free(surface_workspace);
    free(ui_workspace); free(window_workspace); free(permission_workspace); free(runtime_workspace); free(manifest_workspace);
    free(storage_service_workspace); free(storage_workspace); free(storage_path); free(storage_parent);
    free(encoded); free(installer_workspace); free(public_key);
    return result;
}
