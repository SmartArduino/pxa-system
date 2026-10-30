/* SPDX-License-Identifier: MIT */
#include <emscripten.h>
#include <string.h>
#include "services.h"
#include "pxa/audio.h"
static uint32_t sequence,session;
static uint16_t sizes[16];
static uint8_t packets[16][PXA_AUDIO_CAPTURE_MAX_PACKET],head,count;
static pxa_status_t failure;
EM_JS(int,available,(),{return !!Module.browserCaptureAvailable;});
EM_JS(void,start,(uint32_t id),{Module.browserCaptureStart?.(id);});
EM_JS(void,stop,(uint32_t id),{Module.browserCaptureStop?.(id);});
pxa_status_t browser_capture_open(void *c,uint64_t *id){
    (void)c;if(!available())return PXA_STATUS_UNSUPPORTED;
    if(session)return PXA_STATUS_BUSY;
    do{session=++sequence;}while(!session);head=count=0;failure=0;*id=session;start(session);return PXA_STATUS_OK;
}
int32_t browser_capture_io(void *c,uint64_t id,uint32_t op,uint8_t *out,size_t bytes){
    (void)c;if(id!=session||!session)return PXA_STATUS_NOT_FOUND;
    if(op!=PXA_IO_READ)return PXA_STATUS_UNSUPPORTED;
    if(failure)return failure;
    if(!count)return PXA_STATUS_WOULD_BLOCK;
    if(bytes<sizes[head])return PXA_STATUS_RESOURCE_LIMIT;
    uint16_t n=sizes[head];memcpy(out,packets[head],n);head=(head+1)%16;count--;return n;
}
void browser_capture_close(void *c,uint64_t id){
    (void)c;if(id==session&&session){uint32_t old=session;session=0;head=count=0;stop(old);}
}
EMSCRIPTEN_KEEPALIVE int browser_capture_packet(uint32_t id,const uint8_t *data,uint32_t bytes){
    if(!session||session!=id)return PXA_STATUS_CANCELLED;
    if(!bytes||bytes>PXA_AUDIO_CAPTURE_MAX_PACKET)return PXA_STATUS_INVALID_ARGUMENT;
    if(count==16){head=(head+1)%16;count--;}
    unsigned at=(head+count)%16;memcpy(packets[at],data,bytes);sizes[at]=bytes;count++;return 0;
}
EMSCRIPTEN_KEEPALIVE void browser_capture_error(uint32_t id,int status){if(session==id)failure=status<0?status:PXA_STATUS_IO_ERROR;}
