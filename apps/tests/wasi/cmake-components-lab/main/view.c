#include "view.h"

#include <stdio.h>
#include <string.h>

#include "pxa_canvas.h"

#define VIEW_NODE UINT32_C(2)

static uint8_t draw_data[1536];
static uint8_t commands[512];
static uint8_t packet[512];
static uint32_t generation;
static uint8_t initialized;

int component_view_render(const char* state, int passed) {
    static const char title[] = "CMake Components";
    static const char topology[] = "main.wasm  -> IPC ->  responder.wasm";
    static const char isolation[] = "Independent memory and WASI contexts";
    char summary[48];
    pxa_canvas_frame_t frame;
    int written;

    written = snprintf(summary, sizeof(summary), "IPC status: %s", state);
    if (written <= 0 || (size_t)written >= sizeof(summary))
        return 0;
    pxa_canvas_begin(&frame, draw_data, sizeof(draw_data));
    pxa_canvas_rect(&frame, 0, 0, 296, 240, UINT32_C(0x101820), 0);
    pxa_canvas_rect(&frame, 14, 14, 268, 42, UINT32_C(0x30475a), 6);
    pxa_canvas_text_role(&frame, 24, 25, 248, UINT32_C(0xf6f8fa), PXA_CANVAS_FONT_TITLE,
                         PXA_CANVAS_ALIGN_LEFT, title, sizeof(title) - 1u);
    pxa_canvas_rect(&frame, 14, 75, 268, 62, UINT32_C(0x1c2a35), 6);
    pxa_canvas_text(&frame, 22, 91, 252, UINT32_C(0xb8cad5), PXA_CANVAS_ALIGN_CENTER, topology,
                    sizeof(topology) - 1u);
    pxa_canvas_text_role(&frame, 22, 156, 252, passed ? UINT32_C(0x43cf86) : UINT32_C(0xf2bd57),
                         PXA_CANVAS_FONT_TITLE, PXA_CANVAS_ALIGN_CENTER, summary, strlen(summary));
    pxa_canvas_text(&frame, 22, 195, 252, UINT32_C(0x8ea4b2), PXA_CANVAS_ALIGN_CENTER, isolation,
                    sizeof(isolation) - 1u);
    return pxa_canvas_present(VIEW_NODE, &frame, &generation, &initialized, commands,
                              sizeof(commands), packet, sizeof(packet));
}
