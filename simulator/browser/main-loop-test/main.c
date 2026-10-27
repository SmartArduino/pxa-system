/* SPDX-License-Identifier: MIT
 * Regression fixture: no allocator exports, asynchronous event-port loop. */
#include "pxa_event_port.h"
#include "pxa_canvas.h"
#include "pxa_clock.h"
#include <stdio.h>
static uint8_t draw[256],commands[128],packet[512],events[4096],initialized;
static uint32_t generation,color=0x102030;
static int foreground;
static int render(void){
    pxa_canvas_frame_t f;pxa_canvas_begin(&f,draw,sizeof(draw));
    pxa_canvas_rect(&f,0,0,320,240,color,0);
    return pxa_canvas_present_with_root_event_mask(2,&f,&generation,&initialized,
        PXA_UI_EVENT_MASK_CONTROLLER_STATE,commands,sizeof(commands),packet,sizeof(packet))?0:-1;
}
int32_t pxa_app_start(const uint8_t *config,uint32_t size){
    (void)config;(void)size;
    int status=pxa_event_port_open(1);if(status)return status;
    return render();
}
static void dispatch(const pxa_event_t *event){
    if(event->service==17&&event->opcode==0x8005&&event->payload_size==1){
        foreground=event->payload[0];printf("focus:%d\n",foreground);
    }
    pxa_ui_controller_data_t keys;
    if(foreground&&pxa_ui_parse_controller(event,&keys)&&keys.buttons){color+=0x103040;render();}
}
int32_t pxa_app_on_event(const uint8_t *bytes,uint32_t size){
    pxa_event_t event;if(!pxa_parse_event(bytes,size,&event))return -1;
    pxa_event_port_result_t opened;
    if(!pxa_event_port_parse_open(&event,1,&opened)){dispatch(&event);return 0;}
    if(opened.status)return opened.status;
    for(;;){
        int32_t n=pxa_event_port_wait(opened.handle,events,sizeof(events),1000000);
        if(n==PXA_STATUS_TIMED_OUT||n==PXA_STATUS_WOULD_BLOCK)continue;
        if(n<0)return n;
        if(!pxa_parse_event(events,n,&event))return -1;
        dispatch(&event);
    }
}
void pxa_app_stop(uint32_t reason){(void)reason;puts("stopped");}
