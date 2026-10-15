#define _POSIX_C_SOURCE 200809L

#include "pxadb_control.h"

#include <errno.h>
#include <fcntl.h>
#include "png_writer.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <SDL2/SDL.h>

#include "include/lvgl/drivers/sdl/lv_sdl_window.h"

static int set_nonblocking(int descriptor) {
    const int flags = fcntl(descriptor, F_GETFL, 0);
    return flags >= 0 && fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0 &&
           fcntl(descriptor, F_SETFD, FD_CLOEXEC) == 0;
}

static int bind_listener(pxsys_pxadb_control_t *control) {
    struct sockaddr_un address;
    if (control == NULL || control->path[0] == '\0') return 0;
    if (control->listener >= 0) close(control->listener);
    control->listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (control->listener < 0) return 0;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, control->path);
    unlink(control->path);
    if (bind(control->listener, (const struct sockaddr *)&address,
             sizeof(address)) != 0 || listen(control->listener, 4) != 0 ||
        !set_nonblocking(control->listener)) {
        close(control->listener);
        control->listener = -1;
        return 0;
    }
    return 1;
}

static int send_all(int descriptor, const void *data, size_t size) {
    const uint8_t *cursor = data;
    while (size != 0) {
        const ssize_t sent = send(descriptor, cursor, size, MSG_NOSIGNAL);
        if (sent <= 0) return 0;
        cursor += (size_t)sent;
        size -= (size_t)sent;
    }
    return 1;
}

static int send_text(int descriptor, const char *text) {
    return send_all(descriptor, text, strlen(text));
}

static int capture_png(lv_display_t *display, uint8_t **output,
                       size_t *output_size) {
    lv_draw_buf_t *draw_buffer;
    FILE *stream = NULL;
    png_structp png = NULL;
    png_infop info = NULL;
    uint8_t *pixels = NULL;
    png_bytep *rows = NULL;
    char *encoded = NULL;
    size_t encoded_size = 0;
    uint32_t source_stride;
    int width;
    int height;
    int y;
    int ok = 0;
    if (display == NULL || output == NULL || output_size == NULL) return 0;
    *output = NULL;
    *output_size = 0;
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
    if (pixels == NULL || rows == NULL ||
        SDL_ConvertPixels(width, height, SDL_PIXELFORMAT_RGB888,
                          draw_buffer->data, (int)source_stride,
                          SDL_PIXELFORMAT_RGBA32, pixels, width * 4) != 0)
        goto done;
    stream = open_memstream(&encoded, &encoded_size);
    if (stream == NULL) goto done;
    png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png == NULL) goto done;
    info = png_create_info_struct(png);
    if (info == NULL) goto done;
    for (y = 0; y < height; ++y)
        rows[y] = pixels + (size_t)y * (size_t)width * 4u;
    if (!pxsys_png_write(png,info,stream,(png_uint_32)width,(png_uint_32)height,rows)) goto done;
    if (fclose(stream) != 0) {
        stream = NULL;
        goto done;
    }
    stream = NULL;
    *output = (uint8_t *)encoded;
    *output_size = encoded_size;
    encoded = NULL;
    ok = 1;
done:
    if (stream != NULL) fclose(stream);
    if (png != NULL) png_destroy_write_struct(&png, &info);
    free(encoded);
    free(rows);
    free(pixels);
    return ok;
}

/* Debug text input uses the same LVGL edit/value-changed path as the IME.
 * Guests do not need a diagnostic ABI or access to their private state. */
static lv_obj_t *focused_text_input(lv_obj_t *root) {
    if (root == NULL || !lv_obj_is_visible(root) ||
        lv_obj_has_state(root, LV_STATE_DISABLED)) return NULL;
    if (lv_obj_check_type(root, &lv_textarea_class) &&
        lv_obj_has_state(root, LV_STATE_FOCUSED)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        lv_obj_t *found = focused_text_input(lv_obj_get_child(root, (int32_t)i));
        if (found != NULL) return found;
    }
    return NULL;
}

static SDL_Keycode keycode_from_name(const char *name) {
    if (strcmp(name, "POWER") == 0) return SDLK_POWER;
    if (strcmp(name, "BACK") == 0) return SDLK_ESCAPE;
    if (strcmp(name, "HOME") == 0) return SDLK_HOME;
    if (strcmp(name, "VOLUME_UP") == 0) return SDLK_VOLUMEUP;
    if (strcmp(name, "VOLUME_DOWN") == 0) return SDLK_VOLUMEDOWN;
    if (strcmp(name, "ENTER") == 0) return SDLK_RETURN;
    if (strcmp(name, "UP") == 0) return SDLK_UP;
    if (strcmp(name, "DOWN") == 0) return SDLK_DOWN;
    if (strcmp(name, "LEFT") == 0) return SDLK_LEFT;
    if (strcmp(name, "RIGHT") == 0) return SDLK_RIGHT;
    if (strcmp(name, "BACKSPACE") == 0) return SDLK_BACKSPACE;
    return SDLK_UNKNOWN;
}

static int push_pointer(uint32_t window_id, const char *action, int x, int y) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    if (strcmp(action, "MOVE") == 0) {
        event.type = SDL_MOUSEMOTION;
        event.motion.windowID = window_id;
        event.motion.x = x;
        event.motion.y = y;
    } else if (strcmp(action, "DOWN") == 0 || strcmp(action, "UP") == 0) {
        SDL_Event motion;
        memset(&motion, 0, sizeof(motion));
        motion.type = SDL_MOUSEMOTION;
        motion.motion.windowID = window_id;
        motion.motion.x = x;
        motion.motion.y = y;
        if (SDL_PushEvent(&motion) != 1) return 0;
        event.type = strcmp(action, "DOWN") == 0 ? SDL_MOUSEBUTTONDOWN
                                                   : SDL_MOUSEBUTTONUP;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.windowID = window_id;
        event.button.state = event.type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED
                                                                 : SDL_RELEASED;
        event.button.x = x;
        event.button.y = y;
    } else {
        return 0;
    }
    return SDL_PushEvent(&event) == 1;
}

static int push_key_phase(uint32_t window_id, const char *name, int pressed) {
    SDL_Event event;
    const SDL_Keycode keycode = keycode_from_name(name);
    if (keycode == SDLK_UNKNOWN) return 0;
    memset(&event, 0, sizeof(event));
    event.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.windowID = window_id;
    event.key.state = pressed ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.sym = keycode;
    event.key.keysym.scancode = SDL_GetScancodeFromKey(keycode);
    return SDL_PushEvent(&event) == 1;
}

static int push_key(uint32_t window_id, const char *name) {
    return push_key_phase(window_id, name, 1) &&
           push_key_phase(window_id, name, 0);
}

static void read_touch(lv_indev_t *indev, lv_indev_data_t *data) {
    const pxsys_pxadb_touch_t *touch = lv_indev_get_user_data(indev);
    data->point = touch->point;
    data->state = touch->pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->timestamp = lv_tick_get();
}

/* These are independent LVGL indevs, matching the physical board path. SDL's
 * mouse driver represents only one pointer, even for SDL finger events. */
static int inject_touch(pxsys_pxadb_control_t *control, const char *action,
                        unsigned id, int x, int y) {
    pxsys_pxadb_touch_t *touch;
    int down = strcmp(action, "DOWN") == 0;
    int move = strcmp(action, "MOVE") == 0;
    int up = strcmp(action, "UP") == 0;
    int cancel = strcmp(action, "CANCEL") == 0;
    if (id >= 2 || x < 0 || y < 0 ||
        x >= lv_display_get_horizontal_resolution(control->display) ||
        y >= lv_display_get_vertical_resolution(control->display) ||
        (!down && !move && !up && !cancel)) return 0;
    touch = &control->touches[id];
    if (down ? touch->pressed : !touch->pressed) return 0;
    if (touch->indev == NULL) {
        touch->indev = lv_indev_create();
        if (touch->indev == NULL) return 0;
        lv_indev_set_type(touch->indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(touch->indev, control->display);
        lv_indev_set_user_data(touch->indev, touch);
        lv_indev_set_read_cb(touch->indev, read_touch);
        lv_indev_set_mode(touch->indev, LV_INDEV_MODE_EVENT);
    }
    if (cancel) lv_indev_wait_release(touch->indev);
    else { touch->point.x = x; touch->point.y = y; }
    touch->pressed = down || move;
    lv_indev_read(touch->indev);
    return 1;
}

static void handle_client(pxsys_pxadb_control_t *control, int client) {
    char command[128] = {0};
    char action[16] = {0};
    char key[24] = {0};
    char package_id[130] = {0};
    char extra;
    int x;
    int y;
    unsigned pointer_id;
    ssize_t size;
    uint8_t *png = NULL;
    size_t png_size = 0;
    char header[64];
    if (control == NULL || control->display == NULL) return;
    size = recv(client, command, sizeof(command) - 1u, 0);
    if (size <= 0) return;
    command[size] = '\0';
    if (strcmp(command, "SCREENSHOT\n") == 0 ||
        strcmp(command, "SCREENSHOT AFTER_PRESENT\n") == 0) {
        if (!capture_png(control->display, &png, &png_size) ||
            snprintf(header, sizeof(header), "PNG %zu\n", png_size) >=
                (int)sizeof(header) ||
            !send_text(client, header) || !send_all(client, png, png_size))
            (void)send_text(client, "ERR screenshot_failed\n");
        free(png);
    } else if (strcmp(command, "TOUCH CANCEL_ALL\n") == 0) {
        for (unsigned id = 0; id < 2; ++id)
            if (control->touches[id].pressed)
                (void)inject_touch(control, "CANCEL", id, 0, 0);
        (void)send_text(client, "OK\n");
    } else if (sscanf(command, "TOUCH %15s %u %d %d %c", action, &pointer_id,
                      &x, &y, &extra) == 4) {
        (void)send_text(client, inject_touch(control, action, pointer_id, x, y)
            ? "OK\n" : "ERR invalid_touch\n");
    } else if (sscanf(command, "POINTER %15s %d %d", action, &x, &y) == 3) {
        if (strcmp(action, "UP") == 0) control->tap_pending = 0;
        (void)send_text(client, push_pointer(control->window_id, action, x, y) ? "OK\n"
                                                            : "ERR invalid_pointer\n");
    } else if (sscanf(command, "TAP %d %d", &x, &y) == 2) {
        const int ok = !control->tap_pending &&
                       push_pointer(control->window_id, "DOWN", x, y);
        if (ok) {
            control->tap_pending = 1;
            control->tap_x = (int16_t)x;
            control->tap_y = (int16_t)y;
            control->tap_release_at = lv_tick_get() + 16u;
        }
        (void)send_text(client, ok ? "OK\n" : "ERR input_queue_full\n");
    } else if (strncmp(command, "TEXT ", 5) == 0) {
        size_t length = strcspn(command + 5, "\r\n");
        lv_obj_t *target = focused_text_input(lv_display_get_layer_top(control->display));
        if (target == NULL)
            target = focused_text_input(lv_display_get_screen_active(control->display));
        if (length == 0) {
            (void)send_text(client, "ERR text_length\n");
        } else if (target == NULL) {
            (void)send_text(client, "ERR no_focused_text_input\n");
        } else {
            command[5 + length] = '\0';
            lv_textarea_add_text(target, command + 5);
            (void)send_text(client, "OK\n");
        }
    } else if (sscanf(command, "KEY_DOWN %23s", key) == 1) {
        (void)send_text(client, push_key_phase(control->window_id, key, 1)
                                    ? "OK\n"
                                    : "ERR unsupported_key\n");
    } else if (sscanf(command, "KEY_UP %23s", key) == 1) {
        (void)send_text(client, push_key_phase(control->window_id, key, 0)
                                    ? "OK\n"
                                    : "ERR unsupported_key\n");
    } else if (sscanf(command, "KEY %23s", key) == 1) {
        (void)send_text(client, push_key(control->window_id, key) ? "OK\n" : "ERR unsupported_key\n");
    } else if (strcmp(command, "SYNC\n") == 0) {
        (void)send_text(client, "OK\n");
    } else if (strcmp(command, "CATALOG REFRESH\n") == 0) {
        (void)send_text(
            client,
            (control->refresh_catalog == NULL ||
             control->refresh_catalog(control->catalog_context))
                ? "OK\n"
                : "ERR catalog_refresh_failed\n");
    } else if (sscanf(command, "PACKAGE %15s %129s %c", action, package_id, &extra) == 2) {
        const char* actions[] = {"enable", "disable", "clear-data", "uninstall", "run", "stop", "active"};
        int selected = -1;
        for (unsigned i = 0; i < 7; ++i) if (!strcmp(action, actions[i])) selected = (int)i;
        int result = selected >= 0 && control->package_action != NULL &&
            control->package_action(control->catalog_context, package_id, (unsigned)selected);
        (void)send_text(client, selected == PXSYS_PXADB_PACKAGE_ACTIVE ? (result ? "OK 1\n" : "OK 0\n") :
                                (result ? "OK\n" : "ERR package_action_failed\n"));
    } else if (strcmp(command, "CAPABILITIES\n") == 0) {
        (void)send_text(client, "OK\n");
    } else {
        (void)send_text(client, "ERR invalid_control_command\n");
    }
}

int pxsys_pxadb_control_start(pxsys_pxadb_control_t *control,
                              const char *socket_path,
                              lv_display_t *display, void *catalog_context,
                              pxsys_pxadb_catalog_refresh_fn refresh_catalog) {
    if (control == NULL || socket_path == NULL || display == NULL ||
        strlen(socket_path) >= sizeof(control->path))
        return 0;
    memset(control, 0, sizeof(*control));
    control->listener = -1;
    control->display = display;
    control->catalog_context = catalog_context;
    control->refresh_catalog = refresh_catalog;
    control->window_id = SDL_GetWindowID(lv_sdl_window_get_window(display));
    if (control->window_id == 0) return 0;
    strcpy(control->path, socket_path);
    return bind_listener(control);
}

void pxsys_pxadb_control_poll(pxsys_pxadb_control_t *control) {
    int client;
    if (control == NULL || control->listener < 0 ||
        (control->path[0] != '\0' && access(control->path, F_OK) != 0 &&
         !bind_listener(control)))
        return;
    if (control->tap_pending &&
        (int32_t)(lv_tick_get() - control->tap_release_at) >= 0) {
        (void)push_pointer(control->window_id, "UP", control->tap_x,
                           control->tap_y);
        control->tap_pending = 0;
    }
    for (;;) {
        client = accept(control->listener, NULL, NULL);
        if (client < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            return;
        }
        handle_client(control, client);
        close(client);
    }
}

void pxsys_pxadb_control_stop(pxsys_pxadb_control_t *control) {
    if (control == NULL) return;
    for (unsigned id = 0; id < 2; ++id) {
        if (control->touches[id].indev != NULL) {
            if (control->touches[id].pressed)
                (void)inject_touch(control, "CANCEL", id, 0, 0);
            lv_indev_delete(control->touches[id].indev);
            control->touches[id].indev = NULL;
        }
    }
    if (control->listener >= 0) close(control->listener);
    if (control->path[0] != '\0') unlink(control->path);
    control->listener = -1;
    control->path[0] = '\0';
    control->display = NULL;
    control->catalog_context = NULL;
    control->refresh_catalog = NULL;
}
