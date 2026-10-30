/* SPDX-License-Identifier: MIT */
#include "pxa_event_port.h"
#include "pxa_canvas.h"
#include "pxa_window.h"

static uint8_t events[PXA_MAX_CONTROL_BYTES];
static uint8_t draw[128], commands[256], packet[512], initialized;
static uint32_t generation;

int32_t pxa_app_start(const uint8_t *config, uint32_t size) {
    (void)config;
    (void)size;
    pxa_canvas_frame_t frame;
    pxa_canvas_begin(&frame, draw, sizeof(draw));
    pxa_canvas_rect(&frame, 0, 0, 64, 64, 0x207060, 0);
    if (!pxa_canvas_present(2, &frame, &generation, &initialized,
            commands, sizeof(commands), packet, sizeof(packet)))
        return PXA_STATUS_INTERNAL;
    return pxa_event_port_open(1);
}

int32_t pxa_app_on_event(const uint8_t *bytes, uint32_t size) {
    pxa_event_t event;
    pxa_event_port_result_t opened;
    if (!pxa_parse_event(bytes, size, &event)) return PXA_STATUS_INTERNAL;
    if (!pxa_event_port_parse_open(&event, 1, &opened)) return PXA_EVENT_HANDLED;
    if (opened.status) return opened.status;
    pxa_log_write(2, "Main loop: entered");
    for (;;) {
        int32_t count = pxa_event_port_wait(opened.handle, events, sizeof(events), 1000000);
        if (count == PXA_STATUS_TIMED_OUT || count == PXA_STATUS_WOULD_BLOCK) continue;
        if (count < 0) return count;
        if (!pxa_parse_event(events, (uint32_t)count, &event)) return PXA_STATUS_INTERNAL;
        if (event.service == 17 && event.opcode == 0x8005 && event.payload_size == 1)
            pxa_log_write(2, event.payload[0] ? "Main loop: foreground" : "Main loop: background");
        if (pxa_window_is_back_requested(&event)) pxa_log_write(2, "Main loop: back");
    }
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
