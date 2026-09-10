#include <stdint.h>

#include "pxa_canvas.h"

#define LAB_NODE UINT32_C(2)
#define WASI_CLOCK_REALTIME UINT32_C(0)
#define WASI_CLOCK_MONOTONIC UINT32_C(1)

__attribute__((import_module("wasi_snapshot_preview1"),
               import_name("clock_time_get"))) uint32_t
wasi_clock_time_get(uint32_t clock_id, uint64_t precision,
                    uint64_t *timestamp);
__attribute__((import_module("wasi_snapshot_preview1"),
               import_name("random_get"))) uint32_t
wasi_random_get(uint8_t *buffer, uint32_t length);

static uint8_t draw_data[2048];
static uint8_t commands[512];
static uint8_t packet[512];
static uint32_t generation;
static uint8_t initialized;

static uint32_t run_checks(void) {
    uint8_t entropy[16] = {0};
    uint64_t monotonic_ns = 0;
    uint64_t wall_ns = 0;
    uint32_t passed = 0;
    uint32_t index;
    uint8_t entropy_nonzero = 0;

    passed += wasi_clock_time_get(WASI_CLOCK_MONOTONIC, 1,
                                  &monotonic_ns) == 0;
    passed += monotonic_ns != 0;
    passed += wasi_clock_time_get(WASI_CLOCK_REALTIME, 1, &wall_ns) == 0;
    if (wasi_random_get(entropy, sizeof(entropy)) == 0) {
        for (index = 0; index < sizeof(entropy); ++index)
            entropy_nonzero |= entropy[index];
    }
    passed += entropy_nonzero != 0;
    return passed;
}

static int render(uint32_t passed) {
    static const char title[] = "WASI System Lab";
    static const char detail[] = "monotonic  wall clock  random";
    static const char policy[] = "Signed capabilities, no files or host stdio";
    static const char success[] = "4/4 checks passed";
    static const char failure[] = "WASI checks failed";
    const char *summary = passed == 4 ? success : failure;
    const size_t summary_length =
        passed == 4 ? sizeof(success) - 1u : sizeof(failure) - 1u;
    const uint32_t accent =
        passed == 4 ? UINT32_C(0x3ac77a) : UINT32_C(0xe05d68);
    pxa_canvas_frame_t frame;

    pxa_canvas_begin(&frame, draw_data, sizeof(draw_data));
    pxa_canvas_rect(&frame, 0, 0, 296, 240, UINT32_C(0x111820), 0);
    pxa_canvas_rect(&frame, 14, 14, 268, 42, UINT32_C(0x243442), 6);
    pxa_canvas_text_role(&frame, 24, 25, 248, UINT32_C(0xf4f7fa),
                         PXA_CANVAS_FONT_TITLE, PXA_CANVAS_ALIGN_LEFT, title,
                         sizeof(title) - 1u);
    pxa_canvas_rect(&frame, 14, 72, 268, 70, UINT32_C(0x1b2731), 6);
    pxa_canvas_text_role(&frame, 24, 86, 248, accent,
                         PXA_CANVAS_FONT_TITLE, PXA_CANVAS_ALIGN_CENTER,
                         summary, summary_length);
    pxa_canvas_text(&frame, 24, 118, 248, UINT32_C(0xaec0cc),
                    PXA_CANVAS_ALIGN_CENTER, detail, sizeof(detail) - 1u);
    pxa_canvas_text(&frame, 20, 170, 256, UINT32_C(0x8fa5b3),
                    PXA_CANVAS_ALIGN_CENTER, policy, sizeof(policy) - 1u);
    return pxa_canvas_present(LAB_NODE, &frame, &generation, &initialized,
                              commands, sizeof(commands), packet,
                              sizeof(packet));
}


int32_t pxa_app_start(const uint8_t *config, uint32_t config_length) {
    uint32_t passed;
    (void)config;
    (void)config_length;
    passed = run_checks();
    return pxa_window_fullscreen() && render(passed) && passed == 4
               ? PXA_STATUS_OK
               : PXA_STATUS_INTERNAL;
}

int32_t pxa_app_on_event(const uint8_t *event, uint32_t length) {
    (void)event;
    (void)length;
    return PXA_EVENT_UNHANDLED;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
