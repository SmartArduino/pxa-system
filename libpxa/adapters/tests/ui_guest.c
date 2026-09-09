/* Freestanding PXA guest exercising the UI integration path: window
 * fullscreen configuration, a Canvas frame (rect + text) and a Storage SET.
 * No libc dependency; compiles with clang --target=wasm32 -nostdlib. */

#include "pxa.h"
#include "pxa_canvas.h"

#define PXA_STORAGE_GET 1u
#define PXA_STORAGE_SET 2u

static uint32_t s_request_id = 1;
static uint8_t s_canvas_initialized;
static uint32_t s_canvas_generation;
static uint8_t s_commands[512];
static uint8_t s_packet[768];
static uint8_t s_canvas_frame[512];

static uint32_t pxa_length(const uint8_t *value) {
    uint32_t length = 0;
    while (value[length] != 0) ++length;
    return length;
}

static int32_t storage_set(const char *key, const char *value) {
    uint8_t payload[80];
    uint8_t packet[96];
    pxa_writer_t writer;
    pxa_writer_t message;
    pxa_writer_init(&writer, payload, sizeof(payload));
    if (!pxa_record(&writer, 1, (const uint8_t *)key,
                    (uint32_t)pxa_length((const uint8_t *)key)) ||
        !pxa_record(&writer, 2, (const uint8_t *)value,
                    (uint32_t)pxa_length((const uint8_t *)value))) {
        return PXA_STATUS_INTERNAL;
    }
    pxa_writer_init(&message, packet, sizeof(packet));
    if (!pxa_message(&message, PXA_SERVICE_STORAGE, PXA_STORAGE_SET,
                     s_request_id++, writer.data,
                     (uint32_t)writer.length)) {
        return PXA_STATUS_INTERNAL;
    }
    return pxa_control(message.data, (uint32_t)message.length);
}

static int present_canvas(void) {
    pxa_canvas_frame_t frame;
    char text[8];
    pxa_canvas_begin(&frame, s_canvas_frame, sizeof(s_canvas_frame));
    if (!pxa_canvas_rect(&frame, 0, 0, 296, 240, 0xff0000, 0) ||
        !pxa_canvas_text(&frame, 8, 8, 200, 0xffffff, PXA_CANVAS_ALIGN_LEFT,
                         "count", 5)) {
        return PXA_STATUS_INTERNAL;
    }
    {
        const size_t text_length = pxa_canvas_u32_text(text, 0);
        if (!pxa_canvas_text(&frame, 8, 30, 200, 0xffffff,
                             PXA_CANVAS_ALIGN_LEFT, text, text_length)) {
            return PXA_STATUS_INTERNAL;
        }
    }
    if (!pxa_canvas_present(2, &frame, &s_canvas_generation,
                            &s_canvas_initialized, s_commands,
                            sizeof(s_commands), s_packet, sizeof(s_packet))) {
        return PXA_STATUS_INTERNAL;
    }
    return PXA_STATUS_OK;
}


int32_t pxa_app_start(const uint8_t *config, uint32_t config_length) {
    (void)config;
    (void)config_length;
    if (!pxa_window_fullscreen()) return PXA_STATUS_INTERNAL;
    if (present_canvas() != PXA_STATUS_OK) return PXA_STATUS_INTERNAL;
    return storage_set("hits", "1");
}

int32_t pxa_app_on_event(const uint8_t *event, uint32_t length) {
    pxa_event_t parsed;
    if (!pxa_parse_event(event, length, &parsed)) return PXA_EVENT_UNHANDLED;
    if (parsed.service == PXA_SERVICE_STORAGE && parsed.request_id == 1) {
        return PXA_EVENT_HANDLED;
    }
    return PXA_EVENT_UNHANDLED;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
