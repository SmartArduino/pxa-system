#define _POSIX_C_SOURCE 200809L

#include "pxadb_control.h"

#include <errno.h>
#include <fcntl.h>
#include <png.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <SDL2/SDL.h>

static int set_nonblocking(int descriptor) {
    const int flags = fcntl(descriptor, F_GETFL, 0);
    return flags >= 0 && fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0;
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
    if (info == NULL || setjmp(png_jmpbuf(png))) goto done;
    png_init_io(png, stream);
    png_set_IHDR(png, info, (png_uint_32)width, (png_uint_32)height, 8,
                 PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (y = 0; y < height; ++y)
        rows[y] = pixels + (size_t)y * (size_t)width * 4u;
    png_write_image(png, rows);
    png_write_end(png, NULL);
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

static SDL_Keycode keycode_from_name(const char *name) {
    if (strcmp(name, "BACK") == 0) return SDLK_ESCAPE;
    if (strcmp(name, "HOME") == 0) return SDLK_HOME;
    if (strcmp(name, "VOLUME_UP") == 0) return SDLK_VOLUMEUP;
    if (strcmp(name, "VOLUME_DOWN") == 0) return SDLK_VOLUMEDOWN;
    if (strcmp(name, "ENTER") == 0) return SDLK_RETURN;
    if (strcmp(name, "UP") == 0) return SDLK_UP;
    if (strcmp(name, "DOWN") == 0) return SDLK_DOWN;
    if (strcmp(name, "LEFT") == 0) return SDLK_LEFT;
    if (strcmp(name, "RIGHT") == 0) return SDLK_RIGHT;
    return SDLK_UNKNOWN;
}

static int push_pointer(const char *action, int x, int y) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    if (strcmp(action, "MOVE") == 0) {
        event.type = SDL_MOUSEMOTION;
        event.motion.x = x;
        event.motion.y = y;
    } else if (strcmp(action, "DOWN") == 0 || strcmp(action, "UP") == 0) {
        SDL_Event motion;
        memset(&motion, 0, sizeof(motion));
        motion.type = SDL_MOUSEMOTION;
        motion.motion.x = x;
        motion.motion.y = y;
        if (SDL_PushEvent(&motion) != 1) return 0;
        event.type = strcmp(action, "DOWN") == 0 ? SDL_MOUSEBUTTONDOWN
                                                   : SDL_MOUSEBUTTONUP;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.state = event.type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED
                                                                 : SDL_RELEASED;
        event.button.x = x;
        event.button.y = y;
    } else {
        return 0;
    }
    return SDL_PushEvent(&event) == 1;
}

static int push_key(const char *name) {
    SDL_Event event;
    const SDL_Keycode keycode = keycode_from_name(name);
    if (keycode == SDLK_UNKNOWN) return 0;
    memset(&event, 0, sizeof(event));
    event.type = SDL_KEYDOWN;
    event.key.state = SDL_PRESSED;
    event.key.keysym.sym = keycode;
    event.key.keysym.scancode = SDL_GetScancodeFromKey(keycode);
    if (SDL_PushEvent(&event) != 1) return 0;
    event.type = SDL_KEYUP;
    event.key.state = SDL_RELEASED;
    return SDL_PushEvent(&event) == 1;
}

static void handle_client(pxsys_pxadb_control_t *control, int client) {
    char command[128] = {0};
    char action[16] = {0};
    char key[24] = {0};
    int x;
    int y;
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
    } else if (sscanf(command, "POINTER %15s %d %d", action, &x, &y) == 3) {
        (void)send_text(client, push_pointer(action, x, y) ? "OK\n"
                                                            : "ERR invalid_pointer\n");
    } else if (sscanf(command, "TAP %d %d", &x, &y) == 2) {
        const int ok = push_pointer("DOWN", x, y) && push_pointer("UP", x, y);
        (void)send_text(client, ok ? "OK\n" : "ERR input_queue_full\n");
    } else if (sscanf(command, "KEY %23s", key) == 1) {
        (void)send_text(client, push_key(key) ? "OK\n" : "ERR unsupported_key\n");
    } else if (strcmp(command, "SYNC\n") == 0) {
        (void)send_text(client, "OK\n");
    } else if (strcmp(command, "CAPABILITIES\n") == 0) {
        (void)send_text(client, "OK\n");
    } else {
        (void)send_text(client, "ERR invalid_control_command\n");
    }
}

int pxsys_pxadb_control_start(pxsys_pxadb_control_t *control,
                              const char *socket_path,
                              lv_display_t *display) {
    if (control == NULL || socket_path == NULL || display == NULL ||
        strlen(socket_path) >= sizeof(control->path))
        return 0;
    memset(control, 0, sizeof(*control));
    control->listener = -1;
    control->display = display;
    strcpy(control->path, socket_path);
    return bind_listener(control);
}

void pxsys_pxadb_control_poll(pxsys_pxadb_control_t *control) {
    int client;
    if (control == NULL || control->listener < 0 ||
        (control->path[0] != '\0' && access(control->path, F_OK) != 0 &&
         !bind_listener(control)))
        return;
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
    if (control->listener >= 0) close(control->listener);
    if (control->path[0] != '\0') unlink(control->path);
    control->listener = -1;
    control->path[0] = '\0';
    control->display = NULL;
}
