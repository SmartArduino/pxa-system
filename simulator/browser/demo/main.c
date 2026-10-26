/* SPDX-License-Identifier: MIT
 * Ordinary PXA C SDK game, no browser APIs. */
#include "pxa_canvas.h"
#include "pxa_clock.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static uint8_t draw[4096], packet[512], commands[128], initialized;
static uint32_t generation, held, score;
static float x=160,y=120,vx=95,vy=100,paddle=130;
static uint64_t last_tick;
static int paused;
static void reset(void){x=160;y=100;vx=95;vy=100;score=0;}
static int render(void){
    pxa_canvas_frame_t f; pxa_canvas_begin(&f,draw,sizeof(draw));
    pxa_canvas_rect(&f,0,0,320,240,0x0b1420,0);
    pxa_canvas_rect(&f,8,35,304,2,0x28475c,0);
    char title[40];int n=snprintf(title,sizeof(title),"PXA PADDLE   %lu",(unsigned long)score);
    pxa_canvas_text(&f,12,10,296,0xc3f4ff,0,title,n);
    for(int i=0;i<7;i++)pxa_canvas_rect(&f,12+i*43,47,38,9,0x375566,3);
    pxa_canvas_circle(&f,(int16_t)x,(int16_t)y,5,0x81f7d3);
    pxa_canvas_rect(&f,(int16_t)paddle,211,60,8,0x6de7ff,4);
    const char *hint=paused?"PAUSED - PRESS A":"ARROWS / DRAG - A TO PAUSE";
    pxa_canvas_text(&f,0,224,320,0xa2b7c9,1,hint,strlen(hint));
    return pxa_canvas_present_with_root_event_mask(2,&f,&generation,&initialized,
        PXA_UI_EVENT_MASK_CONTROLLER_STATE,commands,sizeof(commands),packet,sizeof(packet))?0:-1;
}
int32_t pxa_app_start(const uint8_t *config,uint32_t size){
    (void)config;(void)size;
    int status=pxa_clock_set_period(16);return status<0?status:render();
}
int32_t pxa_app_on_event(const uint8_t *bytes,uint32_t size){
    pxa_event_t event;if(!pxa_parse_event(bytes,size,&event))return -1;
    pxa_ui_controller_data_t keys;
    pxa_ui_pointer_data_t point;
    if(pxa_ui_parse_controller(&event,&keys)){
        if((keys.buttons&~held)&PXA_CONTROLLER_A)paused=!paused;
        if((keys.buttons&~held)&PXA_CONTROLLER_B)reset();
        held=keys.buttons;return render()<0?-1:1;
    }
    if(pxa_ui_parse_pointer(&event,&point)){
        if(point.buttons){paddle=point.x-30;if(paddle<8)paddle=8;if(paddle>252)paddle=252;}
        return 1;
    }
    if(event.service!=PXA_CLOCK_SERVICE||event.opcode!=PXA_CLOCK_TICK)return 0;
    uint64_t now=pxa_load_u64(event.payload);
    float dt=last_tick?(now-last_tick)/1000000.0f:0;last_tick=now;
    if(dt>0.05f)dt=0.05f;
    if(!paused){
        if(held&PXA_CONTROLLER_LEFT)paddle-=220*dt;
        if(held&PXA_CONTROLLER_RIGHT)paddle+=220*dt;
        if(paddle<8)paddle=8;if(paddle>252)paddle=252;
        float previous=y;x+=vx*dt;y+=vy*dt;
        if(x<13){x=13;vx=fabsf(vx);}if(x>307){x=307;vx=-fabsf(vx);}
        if(y<64){y=64;vy=fabsf(vy);}
        if(vy>0&&previous<206&&y>=206&&x>=paddle-4&&x<=paddle+64){
            y=206;vy=-fminf(220,fabsf(vy)+8);vx=(x-paddle-30)*5;score++;
        }
        if(y>240)reset();
    }
    return render()<0?-1:1;
}
void pxa_app_stop(uint32_t reason){(void)reason;}
