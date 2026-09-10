#include "libc_lab.h"

#include <string.h>

#include "pxa_canvas.h"

#define LAB_NODE UINT32_C(2)

static uint8_t draw_data[2048];
static uint8_t commands[512];
static uint8_t packet[512];
static uint32_t generation;
static uint8_t initialized;

static int render(const libc_lab_result_t* result, const char* summary) {
    static const char title[] = "WASI libc Lab";
    static const char detail[] = "strings  memory  malloc  snprintf";
    static const char policy[] = "No files, env, clock, random or host stdio";
    const uint32_t accent =
        result->passed == result->total ? UINT32_C(0x3ac77a) : UINT32_C(0xe05d68);
    pxa_canvas_frame_t frame;

    pxa_canvas_begin(&frame, draw_data, sizeof(draw_data));
    pxa_canvas_rect(&frame, 0, 0, 296, 240, UINT32_C(0x111820), 0);
    pxa_canvas_rect(&frame, 14, 14, 268, 42, UINT32_C(0x243442), 6);
    pxa_canvas_text_role(&frame, 24, 25, 248, UINT32_C(0xf4f7fa), PXA_CANVAS_FONT_TITLE,
                         PXA_CANVAS_ALIGN_LEFT, title, sizeof(title) - 1u);
    pxa_canvas_rect(&frame, 14, 72, 268, 70, UINT32_C(0x1b2731), 6);
    pxa_canvas_text_role(&frame, 24, 86, 248, accent, PXA_CANVAS_FONT_TITLE,
                         PXA_CANVAS_ALIGN_CENTER, summary, strlen(summary));
    pxa_canvas_text(&frame, 24, 118, 248, UINT32_C(0xaec0cc), PXA_CANVAS_ALIGN_CENTER, detail,
                    sizeof(detail) - 1u);
    pxa_canvas_text(&frame, 20, 170, 256, UINT32_C(0x8fa5b3), PXA_CANVAS_ALIGN_CENTER, policy,
                    sizeof(policy) - 1u);
    return pxa_canvas_present(LAB_NODE, &frame, &generation, &initialized, commands,
                              sizeof(commands), packet, sizeof(packet));
}


int32_t pxa_app_start(const uint8_t* config, uint32_t config_length) {
    libc_lab_result_t result;
    char summary[48];
    (void)config;
    (void)config_length;
    result = libc_lab_run();
    if (!libc_lab_format(summary, sizeof(summary), &result))
        return PXA_STATUS_INTERNAL;
    return pxa_window_fullscreen() && render(&result, summary) ? PXA_STATUS_OK
                                                               : PXA_STATUS_INTERNAL;
}

int32_t pxa_app_on_event(const uint8_t* event, uint32_t length) {
    pxa_event_t parsed;
    return pxa_parse_event(event, length, &parsed) ? PXA_EVENT_UNHANDLED : PXA_EVENT_UNHANDLED;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
