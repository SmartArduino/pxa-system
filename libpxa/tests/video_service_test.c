// SPDX-License-Identifier: Apache-2.0
#include "pxa/video.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static pxa_runtime_t *runtime;
static pxa_request_token_t token=1;
static pxa_message_view_t reply;
static uint8_t response[256], pixels[24];
static unsigned opened, closed, decoded;
static int fail_open, padded_frame;
static pxa_status_t open_decoder(void *ctx, uint16_t index, uint16_t w, uint16_t h, void **out) {
    (void)ctx; assert(!index && w==4 && h==3); *out=NULL;
    if (fail_open) return PXA_STATUS_RESOURCE_LIMIT;
    ++opened; *out=pixels; return 0;
}
static pxa_status_t decode(void *ctx,const uint8_t *p,size_t n,uint64_t pts,uint32_t flags,pxa_video_frame_t *f) {
    assert(ctx==pixels); ++decoded;
    if (flags) { assert(!n); return 0; }
    assert(n==1 && *p==42);
    *f=(pxa_video_frame_t){4,3,padded_frame ? 10u : 8u,pts,99,pixels,padded_frame ? 30u : sizeof(pixels)}; return 0;
}
static pxa_status_t reset(void *ctx) { assert(ctx==pixels); return 0; }
static void close_decoder(void *ctx) { assert(ctx==pixels); ++closed; }
static pxa_component_t create(unsigned id) {
    pxa_component_t c; assert(!pxa_component_create(runtime,id,&c));
    assert(!pxa_component_begin_start(runtime,c)); assert(!pxa_component_set_wire_profile(runtime,c,1));
    assert(!pxa_component_finish_start(runtime,c,0)); return c;
}
static pxa_status_t call(pxa_component_t c,uint16_t op,const void *data,size_t size) {
    size_t n; pxa_message_view_t m={24,op,token++,{data,size}};
    assert(!pxa_component_begin_event(runtime,c)); assert(!pxa_runtime_control_view(runtime,c,&m));
    assert(!pxa_component_finish_event(runtime,c,1));
    assert(!pxa_event_pop(runtime,c,response,sizeof(response),&n));
    assert(!pxa_message_decode(response,n,sizeof(response),&reply));
    assert(reply.service==24 && reply.opcode==op && reply.request_token==token-1);
    return (pxa_status_t)pxa_read_u32(reply.payload.data);
}
static int32_t io(pxa_component_t component,pxa_handle64_t handle,uint32_t op,uint8_t *data,size_t bytes) {
    assert(!pxa_component_begin_event(runtime,component));
    int32_t status=pxa_runtime_io64(runtime,component,handle,op,data,bytes);
    assert(!pxa_component_finish_event(runtime,component,1));
    return status;
}
int main(void) {
    pxa_runtime_limits_t limits; pxa_runtime_limits_init(&limits);
    limits.max_services=2; limits.max_components=2;
    size_t n=pxa_runtime_workspace_size(&limits); void *rw=malloc(n); assert(rw);
    assert(!pxa_runtime_init(rw,n,&limits,&runtime));
    pxa_component_t c=create(1), other=create(2);
    n=pxa_video_service_workspace_size(); void *sw=malloc(n); assert(sw);
    pxa_video_service_t *s;
    for (unsigned i=0;i<sizeof(pixels);++i) pixels[i]=(uint8_t)i;
    assert(!pxa_video_service_init(sw,n,runtime,NULL,&s)); assert(!pxa_video_service_register(s));
    uint8_t body[32]={0};
    assert(!call(c,1,body,2)); assert(reply.payload.size==44 && !pxa_read_u16(reply.payload.data+4));
    pxa_write_u16(body,1); assert(call(c,1,body,2)==PXA_STATUS_NOT_FOUND);
    const pxa_video_capability_t caps={1,1,7,4,3,8,8,32,16,24,40,1,0};
    pxa_video_backend_t backend={sizeof(backend),NULL,1,&caps,open_decoder,decode,reset,close_decoder,NULL};
    assert(!pxa_video_service_init(sw,n,runtime,&backend,&s));
    memset(body,0,sizeof(body)); assert(!call(c,1,body,2));
    assert(pxa_read_u16(reply.payload.data+4)==1 && pxa_read_u32(reply.payload.data+28)==16);
    pxa_write_u16(body+2,4); pxa_write_u16(body+4,3);
    assert(call(c,2,body,8)==PXA_STATUS_DENIED);
    pxa_video_service_set_foreground(s,c);
    pxa_write_u16(body+2,9); assert(call(c,2,body,8)==PXA_STATUS_LIMIT_EXCEEDED && !opened);
    pxa_write_u16(body+2,4); fail_open=1;
    assert(call(c,2,body,8)==PXA_STATUS_RESOURCE_LIMIT && !opened);
    fail_open=0; assert(!call(c,2,body,8));
    pxa_handle64_t handle=pxa_read_u64(reply.payload.data+4); assert(handle>>32);
    assert(call(c,2,body,8)==PXA_STATUS_BUSY && opened==1);
    assert(io(other,handle,PXA_VIDEO_IO_RESET,NULL,0)<0);
    assert(io(c,handle,257,body,32)==PXA_STATUS_WOULD_BLOCK);
    assert(io(c,handle,PXA_VIDEO_IO_READ_SCALED_PIXELS,body,8)==PXA_STATUS_BAD_STATE);
    pxa_write_u16(body,2); pxa_write_u16(body+2,2);
    assert(io(other,handle,PXA_VIDEO_IO_CONFIGURE_SCALED,body,4)<0);
    assert(io(c,handle,PXA_VIDEO_IO_CONFIGURE_SCALED,body,3)==PXA_STATUS_INVALID_ARGUMENT);
    assert(!io(c,handle,PXA_VIDEO_IO_CONFIGURE_SCALED,body,4));
    assert(io(c,handle,PXA_VIDEO_IO_READ_SCALED_PIXELS,body,8)==PXA_STATUS_WOULD_BLOCK);
    uint8_t packet[33]={0}; pxa_write_u64(packet,12345); packet[16]=42;
    assert(io(c,handle,256,packet,33)==PXA_STATUS_LIMIT_EXCEEDED && !decoded);
    packet[12]=1; assert(io(c,handle,256,packet,17)==PXA_STATUS_INVALID_ARGUMENT);
    packet[12]=0; assert(!io(c,handle,256,packet,17));
    assert(!io(c,handle,257,body,32));
    assert(pxa_read_u64(body+8)==12345 && pxa_read_u32(body+24)==24);
    uint8_t scaled[144]; memset(scaled,0xa5,sizeof(scaled));
    pxa_write_u16(scaled,2); pxa_write_u16(scaled+2,2); pxa_write_u32(scaled+4,0);
    assert(!io(c,handle,PXA_VIDEO_IO_READ_SCALED,scaled,16));
    const uint8_t expected[]={0,1,4,5,8,9,12,13};
    assert(!memcmp(scaled+8,expected,sizeof(expected)) && scaled[16]==0xa5);
    memset(body,0xa5,sizeof(body));
    assert(!io(c,handle,PXA_VIDEO_IO_READ_SCALED_PIXELS,body+1,8));
    assert(body[0]==0xa5 && body[9]==0xa5 && !memcmp(body+1,expected,8));
    assert(io(c,handle,PXA_VIDEO_IO_READ_SCALED_PIXELS,body,7)==PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u16(body,4097); pxa_write_u16(body+2,2);
    assert(io(c,handle,PXA_VIDEO_IO_CONFIGURE_SCALED,body,4)==PXA_STATUS_LIMIT_EXCEEDED);
    assert(!io(c,handle,PXA_VIDEO_IO_READ_SCALED_PIXELS,body,8) && !memcmp(body,expected,8));
    assert(io(other,handle,PXA_VIDEO_IO_READ_SCALED,scaled,16)<0);
    assert(io(c,handle,PXA_VIDEO_IO_READ_SCALED,scaled,15)==PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u16(scaled,8); pxa_write_u16(scaled+2,6);
    assert(!io(c,handle,PXA_VIDEO_IO_READ_SCALED,scaled,104));
    for(unsigned y=0;y<6;++y)for(unsigned x=0;x<8;++x) {
        assert(scaled[8+(y*8+x)*2]==pixels[((y/2)*4+x/2)*2]);
    }
    pxa_write_u16(scaled,4097);
    assert(io(c,handle,PXA_VIDEO_IO_READ_SCALED,scaled,28)==PXA_STATUS_LIMIT_EXCEEDED);
    pxa_write_u16(scaled,4); pxa_write_u16(scaled+2,3);
    assert(!io(c,handle,PXA_VIDEO_IO_READ_SCALED,scaled,32) && !memcmp(scaled+8,pixels,24));
    for (unsigned offset=0;offset<2;++offset) {
        uint8_t *out=scaled+offset; memset(out,0xa5,130);
        pxa_write_u16(out,7); pxa_write_u16(out+2,5); pxa_write_u32(out+4,0);
        assert(!io(c,handle,PXA_VIDEO_IO_READ_SCALED,out,78));
        for(unsigned y=0;y<5;++y)for(unsigned x=0;x<7;++x)
            assert(!memcmp(out+8+(y*7+x)*2,pixels+((y*3/5)*4+x*4/7)*2,2));
        assert(out[78]==0xa5);
    }
    pxa_write_u16(scaled,4); pxa_write_u16(scaled+2,3);
    pxa_write_u32(scaled+4,1);
    assert(io(c,handle,PXA_VIDEO_IO_READ_SCALED,scaled,32)==PXA_STATUS_INVALID_ARGUMENT);
    assert(io(c,handle,1,body,10)==10);
    assert(!memcmp(body,pixels,10)); /* scaled reads leave the sequential cursor unchanged. */
    assert(io(c,handle,1,body,32)==14);
    assert(!io(c,handle,1,body,32));
    padded_frame=1;
    assert(io(c,handle,256,packet,17)==PXA_STATUS_PROTOCOL_ERROR);
    assert(io(c,handle,257,body,32)==PXA_STATUS_WOULD_BLOCK);
    padded_frame=0;
    assert(!io(c,handle,258,NULL,0));
    assert(io(c,handle,257,body,32)==PXA_STATUS_WOULD_BLOCK);
    assert(!io(c,handle,256,packet,17));
    assert(!io(c,handle,PXA_VIDEO_IO_READ_SCALED_PIXELS,body,8) && !memcmp(body,expected,8));
    assert(!io(c,handle,258,NULL,0));
    pxa_write_u32(packet+8,1); assert(!io(c,handle,256,packet,16));
    assert(io(c,handle,257,body,32)==PXA_STATUS_WOULD_BLOCK);
    pxa_video_service_set_foreground(s,other); assert(closed==1);
    assert(io(c,handle,258,NULL,0)<0);
    memset(body,0,8); pxa_write_u16(body+2,4); pxa_write_u16(body+4,3);
    assert(!call(other,2,body,8)); handle=pxa_read_u64(reply.payload.data+4);
    assert(io(other,handle,PXA_VIDEO_IO_READ_SCALED_PIXELS,body,8)==PXA_STATUS_BAD_STATE);
    pxa_video_service_deinit(s); pxa_runtime_deinit(runtime); free(sw); free(rw);
    assert(opened==closed); return 0;
}
