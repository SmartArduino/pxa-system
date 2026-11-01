/* SPDX-License-Identifier: MIT
 * Retained transparent Canvas over independently changing Surface pixels. */
#include "pxa_canvas.h"
#include <string.h>
static uint8_t draw[512],packet[2048],initialized=1;
static uint32_t generation;
static int render(int mode) {
    pxa_canvas_frame_t f;pxa_canvas_begin(&f,draw,sizeof(draw));
    if (mode) {
        pxa_canvas_rect(&f,10,10,20,20,0xff0000,0);
        pxa_canvas_rect_rgba(&f,40,10,20,20,0x00ff0080,0,0,0);
        pxa_canvas_rect(&f,-10,40,20,20,0xffffff,0);
        pxa_canvas_rect(&f,315,40,20,20,0xffffff,0);
        pxa_canvas_text(&f,10,80,240,0xffffff,0,"Alpha overlay",13);
    }
    return pxa_canvas_present_regions(2,&f,&generation,&initialized,0,NULL,0,
        packet,sizeof(packet),NULL,0)?0:-1;
}
int32_t pxa_app_start(const uint8_t *config,uint32_t bytes) {
    (void)config;(void)bytes;pxa_ui_transaction_t tx;
    if (!pxa_ui_transaction_begin(&tx,1,PXA_UI_TRANSACTION_REPLACE_SURFACE,packet,sizeof(packet)) ||
        !pxa_ui_create(&tx,1,0,0,PXA_UI_NODE_ROOT) ||
        !pxa_ui_set_rgba(&tx,1,PXA_UI_PROPERTY_BACKGROUND,0) ||
        !pxa_ui_set_event_mask(&tx,1,PXA_UI_EVENT_MASK_CONTROLLER_STATE) ||
        !pxa_ui_create(&tx,2,1,0,PXA_UI_NODE_CANVAS) ||
        !pxa_ui_set_length(&tx,2,PXA_UI_PROPERTY_WIDTH,PXA_UI_LENGTH_FILL,0) ||
        !pxa_ui_set_length(&tx,2,PXA_UI_PROPERTY_HEIGHT,PXA_UI_LENGTH_FILL,0) ||
        !pxa_ui_set_u8(&tx,2,PXA_UI_PROPERTY_COMPOSITION,PXA_UI_COMPOSITION_ALPHA_OVERLAY) ||
        !pxa_ui_set_rgba(&tx,2,PXA_UI_PROPERTY_BACKGROUND,0) ||
        !pxa_ui_transaction_commit(&tx)) return -1;
    return render(1);
}
int32_t pxa_app_on_event(const uint8_t *bytes,uint32_t size) {
    pxa_event_t e;pxa_ui_controller_data_t keys;
    if (!pxa_parse_event(bytes,size,&e))return -1;
    if (pxa_ui_parse_controller(&e,&keys)&&keys.buttons)
        return render((keys.buttons&PXA_CONTROLLER_B)?0:1);
    return 0;
}
void pxa_app_stop(uint32_t reason){(void)reason;}
