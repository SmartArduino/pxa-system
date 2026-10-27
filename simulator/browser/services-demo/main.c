/* SPDX-License-Identifier: MIT
 * The ordinary PXA C SDK, no browser imports. */
#include "pxa_ui_components.h"
#include "pxa_storage.h"
#include "pxa_permission.h"
#include "pxa_audio.h"
#include "pxa_clock.h"
#include "pxa_fs.h"
#include <stdio.h>
#include <string.h>
static uint8_t scratch[1024],packet[2048];
static uint32_t ancestors[8],generation,input,save_button,held;
static uint64_t audio;
static char note[129]="你好，PXA！",message[96]="输入中文，保存后重新打开。";
static int audio_ready;
static int render(void) {
    pxa_ui_builder_t ui={0};
    if(!pxa_ui_builder_begin(&ui,&generation,1,0,PXA_UI_TRANSACTION_REPLACE_SURFACE,
                            scratch,sizeof(scratch),ancestors,8))return -1;
    uint32_t root=pxa_ui_builder_auto_enter(&ui,PXA_UI_NODE_ROOT);
    pxa_ui_transaction_t *tx=pxa_ui_builder_transaction(&ui);
    if(!root || !pxa_ui_set_event_mask(tx,root,PXA_UI_EVENT_MASK_CONTROLLER_STATE) ||
       !pxa_ui_set_length(tx,root,PXA_UI_PROPERTY_WIDTH,PXA_UI_LENGTH_PERCENT,100) ||
       !pxa_ui_set_length(tx,root,PXA_UI_PROPERTY_HEIGHT,PXA_UI_LENGTH_PERCENT,100) ||
       !pxa_ui_set_u8(tx,root,PXA_UI_PROPERTY_LAYOUT,PXA_UI_LAYOUT_COLUMN) ||
       !pxa_ui_set_padding(tx,root,16,12,16,12) || !pxa_ui_set_dp(tx,root,PXA_UI_PROPERTY_GAP,10) ||
       !pxa_ui_text(&ui,"PXA 浏览器便签",PXA_UI_FONT_ROLE_TITLE,PXA_UI_THEME_TEXT))goto fail;
    input=pxa_ui_builder_auto_node_typed(&ui,PXA_UI_NODE_CONTROL,PXA_UI_CONTROL_TEXT_INPUT);
    if(!input || !pxa_ui_set_length(tx,input,PXA_UI_PROPERTY_WIDTH,PXA_UI_LENGTH_PERCENT,100) ||
       !pxa_ui_set_length(tx,input,PXA_UI_PROPERTY_HEIGHT,PXA_UI_LENGTH_LOGICAL_PX,46) ||
       !pxa_ui_set_u8(tx,input,PXA_UI_PROPERTY_TEXT_SINGLE_LINE,1) ||
       !pxa_ui_set_u32(tx,input,PXA_UI_PROPERTY_TEXT_MAX_BYTES,sizeof(note)-1) ||
       !pxa_ui_set_text(tx,input,note,strlen(note)) ||
       !pxa_ui_set_event_mask(tx,input,PXA_UI_EVENT_MASK_TEXT|PXA_UI_EVENT_MASK_CLICK))goto fail;
    save_button=pxa_ui_button(&ui,"保存并播放提示音",PXA_UI_THEME_PRIMARY,PXA_UI_THEME_ON_PRIMARY);
    if(!save_button || !pxa_ui_set_padding(tx,save_button,14,10,14,10) ||
       !pxa_ui_text(&ui,message,1,PXA_UI_THEME_TEXT) || !pxa_ui_builder_leave(&ui))goto fail;
    return pxa_ui_builder_end(&ui)?0:-1;
fail:pxa_ui_builder_abort(&ui);return -1;
}
static int save(void){
    if(audio_ready)pxa_audio_play_tone(audio,PXA_AUDIO_TONE_SINE,660,160,-12*256,10,30,0);
    int status=pxa_storage_request_set(packet,sizeof(packet),6,"note",4,(uint8_t*)note,strlen(note));
    if(!status)status=pxa_fs_request_open(7,"note.txt",8,PXA_FS_OPEN_WRITE|PXA_FS_OPEN_CREATE|PXA_FS_OPEN_TRUNCATE);
    return status<0?status:1;
}
int32_t pxa_app_start(const uint8_t *config,uint32_t size){
    (void)config;(void)size;uint32_t written;
    if(render())return -1;
    if(pxa_storage_request_get(1,"note",4)<0)return -1;
    if(!pxa_permission_build(packet,sizeof(packet),PXA_PERMISSION_ACQUIRE,2,
        "audio.playback",14,(const uint8_t*)"media",5,&written))return -1;
    if(pxa_control_packet(packet,written)<0)return -1;
    return pxa_clock_wall_time(5);
}
int32_t pxa_app_on_event(const uint8_t *bytes,uint32_t size){
    pxa_event_t event;if(!pxa_parse_event(bytes,size,&event))return -1;
    if(event.service==PXA_PERMISSION_SERVICE){
        pxa_permission_acquire_result_t r;
        if(pxa_permission_parse_acquire(&event,2,&r)&&!r.status)return pxa_audio_open_media(3,r.handle)<0?-1:1;
    }
    if(event.service==PXA_AUDIO_SERVICE){
        pxa_audio_open_result_t r;int32_t status;
        if(pxa_audio_parse_open(&event,3,&r)){
            if(r.status)return r.status;audio=r.handle;
            pxa_audio_graph_t graph={0};return pxa_audio_commit_graph(4,audio,&graph)<0?-1:1;
        }
        if(pxa_audio_parse_status(&event,4,PXA_AUDIO_COMMIT_GRAPH,&status)){
            audio_ready=!status;printf("Audio ready: %d\n",audio_ready);return 1;
        }
    }
    if(event.service==PXA_CLOCK_SERVICE){
        pxa_clock_wall_time_result_t wall;
        if(pxa_clock_parse_wall_time(&event,5,&wall)){printf("Wall clock status: %d\n",wall.status);return 1;}
    }
    if(event.service==PXA_FS_SERVICE){
        pxa_fs_open_result_t r;
        if(pxa_fs_parse_open(&event,7,&r)&&!r.status){
            int n=pxa_io(r.handle,PXA_FS_IO_WRITE,(uint8_t*)note,strlen(note));
            pxa_fs_close(r.handle);printf("File bytes written: %d\n",n);return n<0?n:1;
        }
    }
    if(event.service==PXA_STORAGE_SERVICE){
        pxa_storage_get_result_t r;int32_t status;
        if(pxa_storage_parse_get(&event,1,&r)){
            if(!r.status && r.value.size<sizeof(note)){memcpy(note,r.value.data,r.value.size);note[r.value.size]=0;return render()<0?-1:1;}
            return 1;
        }
        if(pxa_storage_parse_status(&event,6,PXA_STORAGE_SET,&status)){
            snprintf(message,sizeof(message),"%s",status?"保存失败":"已保存，下次打开可继续编辑。");
            printf("Storage status: %d\n",status);return render()<0?-1:1;
        }
    }
    pxa_ui_event_data_t ui;
    if(pxa_ui_parse_event(&event,&ui)&&pxa_ui_event_is_current(&ui,1,generation)){
        if(ui.node==input&&ui.kind==PXA_UI_EVENT_TEXT_KIND){pxa_ui_event_text(&ui,note,sizeof(note));return 1;}
        if(ui.kind==PXA_UI_EVENT_CLICK_KIND&&(ui.node==save_button||ui.node==input))return save();
        pxa_ui_controller_data_t controller;
        if(pxa_ui_parse_controller(&event,&controller)){uint32_t pressed=controller.buttons&~held;held=controller.buttons;return pressed&PXA_CONTROLLER_A?save():1;}
    }
    return 0;
}
void pxa_app_stop(uint32_t reason){(void)reason;}
