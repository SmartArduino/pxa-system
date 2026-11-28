// SPDX-License-Identifier: Apache-2.0
#include "../../sdk/testing/pxa_control_capture.h"
#include "pxa_video.h"
#include "pxa_http.h"
#include "pxa_audio.h"
#include <assert.h>
static uint16_t last_service,last_opcode;
static uint64_t last_token;
static int corrupt;
static int32_t poll_bytes = 32;
static uint8_t last_payload[64];
int32_t pxa_test_control(const uint8_t *data,uint32_t size) {
    assert(size>=20);last_service=pxa_load_u16(data);last_opcode=pxa_load_u16(data+2);last_token=pxa_load_u64(data+4);
    memcpy(last_payload,data+20,size-20<sizeof(last_payload) ? size-20 : sizeof(last_payload));return 0;
}
int32_t pxa_io(uint64_t handle,uint32_t operation,uint8_t *data,uint32_t size) {
    assert(handle==UINT64_C(0x100000001));
    if(operation==PXA_VIDEO_IO_PREFERRED_SIZE) {
        assert(size==4 && pxa_load_u16(data)==854 && pxa_load_u16(data+2)==480);
        pxa_store_u16(data,corrupt ? 855 : 840); pxa_store_u16(data+2,472);
        return 4;
    }
    if(operation==PXA_VIDEO_IO_BIND_SURFACE) {
        assert(size==16 && pxa_load_u64(data)==UINT64_C(0x200000003));
        assert(pxa_load_u16(data+8)==2 && pxa_load_u16(data+10)==3);
        assert(pxa_load_u16(data+12)==320 && pxa_load_u16(data+14)==180);
        return 0;
    }
    if(operation==PXA_VIDEO_IO_PREPARE_SURFACE) { assert(!data && !size); return 0; }
    if(operation==PXA_VIDEO_IO_PRESENT_SURFACE || operation==PXA_VIDEO_IO_PRESENT_SURFACE_ORDERED) {
        assert(size==8 && pxa_load_u64(data)==UINT64_C(0x123456789));
        return 0;
    }
    if(operation==256 || operation==PXA_VIDEO_IO_PREPARE_AND_QUEUE) {
        assert(size==17 && pxa_load_u64(data)==12345 && !pxa_load_u32(data+8) && !pxa_load_u32(data+12) && data[16]==42);return 0;
    }
    if(operation==259) {
        assert(size==16 && pxa_load_u16(data)==2 && pxa_load_u16(data+2)==2 && !pxa_load_u32(data+4));
        memset(data+8,42,8);return 0;
    }
    if(operation==260) {
        assert((size==4 || size==8) && pxa_load_u16(data)==2 && pxa_load_u16(data+2)==2);
        if(size==8) assert(pxa_load_u32(data+4)==8);
        return 0;
    }
    if(operation==261) { assert(size==8); memset(data,42,8); return 0; }
    assert((operation==PXA_VIDEO_IO_FRAME_INFO || operation==PXA_VIDEO_IO_POLL_FRAME) && size==32);
    if (operation==PXA_VIDEO_IO_POLL_FRAME && poll_bytes!=32) return poll_bytes;
    memset(data,0,32);
    pxa_store_u16(data,4);pxa_store_u16(data+2,3);pxa_store_u32(data+4,8);pxa_store_u32(data+24,24);
    if(corrupt)pxa_store_u32(data+28,1);
    return operation==PXA_VIDEO_IO_POLL_FRAME ? poll_bytes : 0;
}
int main(void) {
    assert(!pxa_video_request_capability(7,0));assert(last_service==24 && last_opcode==1 && last_token==7);
    assert(!pxa_video_request_open(8,0,640,360));assert(last_opcode==2);
    uint8_t body[44]={0};pxa_store_u16(body+4,1);pxa_store_u16(body+8,1);pxa_store_u16(body+10,1);
    pxa_store_u16(body+20,1280);pxa_store_u16(body+22,1280);pxa_store_u32(body+24,1280*720);
    pxa_store_u32(body+28,512*1024);pxa_store_u16(body+40,1);
    pxa_event_t e={0};e.service=24;e.opcode=1;e.token=7;e.payload=body;e.payload_size=44;
    pxa_video_capability_t cap;assert(pxa_video_parse_capability(&e,7,&cap) && cap.max_pixels==1280*720);
    e.payload_size=43;assert(!pxa_video_parse_capability(&e,7,&cap));e.payload_size=44;
    e.token=8;assert(!pxa_video_parse_capability(&e,7,&cap));e.token=7;
    pxa_store_u16(body+40,2);assert(!pxa_video_parse_capability(&e,7,&cap));
    const uint64_t decoder=UINT64_C(0x100000001);
    uint16_t width=854,height=480;
    corrupt=1;
    assert(pxa_video_preferred_size(decoder,&width,&height)==PXA_STATUS_PROTOCOL_ERROR && width==854 && height==480);
    corrupt=0;
    assert(!pxa_video_preferred_size(decoder,&width,&height) && width==840 && height==472);
    assert(!pxa_video_bind_surface(decoder,UINT64_C(0x200000003),2,3,320,180));
    assert(!pxa_video_prepare_surface(decoder));
    assert(!pxa_video_present_surface(decoder,UINT64_C(0x123456789)));
    assert(!pxa_video_present_surface_ordered(decoder,UINT64_C(0x123456789)));
    uint8_t packet[4096]={0};packet[16]=42;
    assert(!pxa_video_decode(UINT64_C(0x100000001),packet,17,12345,0));
    assert(!pxa_video_prepare_and_queue(decoder,packet,17,12345,0));
    assert(!pxa_video_read_scaled(UINT64_C(0x100000001),packet,16,2,2) && packet[8]==42);
    assert(pxa_video_read_scaled(UINT64_C(0x100000001),packet,15,2,2)==PXA_STATUS_INVALID_ARGUMENT);
    assert(!pxa_video_configure_scaled(UINT64_C(0x100000001),2,2));
    assert(pxa_video_configure_scaled(UINT64_C(0x100000001),0,2)==PXA_STATUS_INVALID_ARGUMENT);
    assert(!pxa_video_configure_scaled_stride(UINT64_C(0x100000001),2,2,8));
    assert(pxa_video_configure_scaled_stride(UINT64_C(0x100000001),2,2,5)==PXA_STATUS_INVALID_ARGUMENT);
    assert(!pxa_video_read_scaled_pixels(UINT64_C(0x100000001),packet,8) && packet[0]==42);
    assert(pxa_video_read_scaled_pixels(UINT64_C(0x100000001),NULL,8)==PXA_STATUS_INVALID_ARGUMENT);
    pxa_video_frame_t frame;assert(!pxa_video_frame_info(UINT64_C(0x100000001),&frame) && frame.frame_bytes==24);
    assert(!pxa_video_poll_frame(UINT64_C(0x100000001),&frame) && frame.frame_bytes==24);
    poll_bytes=0;
    assert(!pxa_video_poll_frame(UINT64_C(0x100000001),&frame) && !frame.frame_bytes);
    poll_bytes=PXA_STATUS_WOULD_BLOCK;
    assert(pxa_video_poll_frame(UINT64_C(0x100000001),&frame)==PXA_STATUS_WOULD_BLOCK);
    poll_bytes=31;
    assert(pxa_video_poll_frame(UINT64_C(0x100000001),&frame)==PXA_STATUS_PROTOCOL_ERROR);
    corrupt=1;assert(pxa_video_frame_info(UINT64_C(0x100000001),&frame)==PXA_STATUS_PROTOCOL_ERROR);
    char url[2050];memset(url,'a',sizeof(url));memcpy(url,"https://example.com/",20);url[2048]=0;
    pxa_http_request_t request=pxa_http_request_defaults();request.url=url;request.url_size=2048;
    request.permission_handle=UINT64_C(0x100000001);uint32_t written;
    assert(pxa_http_build_open(packet,sizeof(packet),9,&request,&written));
    request.url_size=2049;assert(!pxa_http_build_open(packet,sizeof(packet),9,&request,&written));
    assert(!pxa_audio_open_pcm_buffered(10,UINT64_C(0x100000001),44100,2));
    assert(last_service==10 && last_opcode==5 && last_payload[13]==1);
    uint8_t audio_body[56]={0},scalar[8];size_t at=4,n=0;
    pxa_store_u64(scalar,UINT64_C(0x100000001));
    assert(pxa_wire_record_encode(audio_body+at,56-at,3,scalar,8,&n));at+=n;
    pxa_store_u32(scalar,44100);assert(pxa_wire_record_encode(audio_body+at,56-at,4,scalar,4,&n));at+=n;
    scalar[0]=2;assert(pxa_wire_record_encode(audio_body+at,56-at,5,scalar,1,&n));at+=n;
    pxa_store_u16(scalar,20);assert(pxa_wire_record_encode(audio_body+at,56-at,6,scalar,2,&n));at+=n;
    pxa_store_u32(scalar,16000);assert(pxa_wire_record_encode(audio_body+at,56-at,7,scalar,4,&n));at+=n;
    scalar[0]=2;assert(pxa_wire_record_encode(audio_body+at,56-at,8,scalar,1,&n));at+=n;
    pxa_store_u32(scalar,44100*480/1000);assert(pxa_wire_record_encode(audio_body+at,56-at,9,scalar,4,&n));at+=n;
    assert(at==56);e.service=10;e.opcode=5;e.token=10;e.payload=audio_body;e.payload_size=56;
    pxa_audio_pcm_result_t audio;
    assert(!pxa_audio_parse_pcm(&e,10,&audio));
    assert(pxa_audio_parse_pcm_buffered(&e,10,&audio) && audio.queue_frames==21168);
    pxa_store_u32(audio_body+52,21169);assert(!pxa_audio_parse_pcm_buffered(&e,10,&audio));
    return 0;
}
