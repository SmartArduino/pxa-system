// SPDX-License-Identifier: Apache-2.0
#include "pxa/video.h"
#include "pxa/surface.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#define PXA_VIDEO_PACKED_SCALE 1
#include "../src/services/video/video_scale.h"
static pxa_runtime_t *runtime;
static pxa_request_token_t token=1;
static pxa_message_view_t reply;
static uint8_t response[256];
static _Alignas(uint16_t) uint8_t pixels[24];
static unsigned opened, closed, decoded;
static int fail_open, padded_frame;
static bool defer_frame;
static bool async_pending, async_complete;
static uint8_t async_byte;
static uint64_t async_pts;
static pxa_status_t open_decoder(void *ctx, uint16_t index, uint16_t w, uint16_t h, void **out) {
    (void)ctx; assert(!index && w==4 && h==3); *out=NULL;
    if (fail_open) return PXA_STATUS_RESOURCE_LIMIT;
    ++opened; *out=pixels; return 0;
}
static pxa_status_t decode(void *ctx,const uint8_t *p,size_t n,uint64_t pts,uint32_t flags,pxa_video_frame_t *f) {
    assert(ctx==pixels); ++decoded;
    if (flags) { assert(!n); return 0; }
    if (n==1 && *p==44) return PXA_STATUS_OK;
    assert(n==1 && (*p==42 || *p==43));
    if (*p==43) {
        *f=(pxa_video_frame_t){3,3,6,pts,99,pixels,18}; return 0;
    }
    *f=(pxa_video_frame_t){4,3,padded_frame ? 10u : 8u,pts,99,defer_frame ? NULL : pixels,padded_frame ? 30u : sizeof(pixels)}; return 0;
}
static void render_deferred(void *ctx,uint8_t *out,uint32_t w,uint32_t h,uint32_t stride) {
    assert(ctx==pixels && defer_frame);
    for (uint32_t y=0;y<h;++y) for (uint32_t x=0;x<w;++x)
        memcpy(out+y*stride+x*2,pixels+((y*3/h)*4+x*4/w)*2,2);
}
static pxa_status_t materialize(void *ctx,pxa_video_frame_t *frame) {
    assert(ctx==pixels && defer_frame && !frame->rgb565);
    frame->rgb565=pixels;
    return PXA_STATUS_OK;
}
static pxa_status_t queue_decode(void *ctx, const uint8_t *p, size_t n, uint64_t pts, uint32_t flags) {
    assert(ctx==pixels && n==1 && !flags && !async_pending);
    async_byte=*p; async_pts=pts;
    async_pending=true; async_complete=false;
    return PXA_STATUS_OK;
}
static pxa_status_t poll_decode(void *ctx, pxa_video_frame_t *frame) {
    assert(async_pending);
    if (!async_complete) return PXA_STATUS_WOULD_BLOCK;
    async_pending=false;
    return decode(ctx,&async_byte,1,async_pts,0,frame);
}
static pxa_status_t reset(void *ctx) { assert(ctx==pixels); async_pending=false; return 0; }
static void close_decoder(void *ctx) { assert(ctx==pixels); async_pending=false; ++closed; }
static pxa_component_t create(unsigned id) {
    pxa_component_t c; assert(!pxa_component_create(runtime,id,&c));
    assert(!pxa_component_begin_start(runtime,c)); assert(!pxa_component_set_wire_profile(runtime,c,1));
    assert(!pxa_component_finish_start(runtime,c,0)); return c;
}
static pxa_status_t service_call(pxa_component_t c,uint16_t service,uint16_t op,const void *data,size_t size) {
    size_t n; pxa_message_view_t m={service,op,token++,{data,size}};
    assert(!pxa_component_begin_event(runtime,c)); assert(!pxa_runtime_control_view(runtime,c,&m));
    assert(!pxa_component_finish_event(runtime,c,1));
    assert(!pxa_event_pop(runtime,c,response,sizeof(response),&n));
    assert(!pxa_message_decode(response,n,sizeof(response),&reply));
    assert(reply.service==service && reply.opcode==op && reply.request_token==token-1);
    return (pxa_status_t)pxa_read_u32(reply.payload.data);
}
static pxa_status_t call(pxa_component_t c,uint16_t op,const void *data,size_t size) {
    return service_call(c,PXA_VIDEO_SERVICE_ID,op,data,size);
}
static int32_t io(pxa_component_t component,pxa_handle64_t handle,uint32_t op,uint8_t *data,size_t bytes) {
    assert(!pxa_component_begin_event(runtime,component));
    int32_t status=pxa_runtime_io64(runtime,component,handle,op,data,bytes);
    assert(!pxa_component_finish_event(runtime,component,1));
    return status;
}
static bool preferred_size(void *ctx,uint16_t *w,uint16_t *h) {
    assert(ctx==pixels);
    if (*w < 2) return false;
    *w = 2;
    *h = 2;
    return true;
}
static uint8_t display[40];
static bool staged, native_surface;
static bool render_native(void *ctx,uint8_t *base,uint32_t stride,uint16_t rows,
                          uint16_t x,uint16_t y,uint16_t w,uint16_t h) {
    assert(ctx==pixels && base==display && stride==10 && rows==4 && x==1 && y==1 && w==2 && h==2);
    if (!native_surface) return false;
    for(unsigned row=0;row<h;++row) memset(base+(y+row)*stride+2*x,0x77,2*w);
    return true;
}

static pxa_status_t prepare_next(void *ctx,const uint8_t *p,size_t n,uint64_t pts,uint32_t flags,
    uint8_t *base,uint32_t stride,uint16_t rows,uint16_t x,uint16_t y,uint16_t w,uint16_t h) {
    if (!native_surface) return PXA_STATUS_WOULD_BLOCK;
    assert(render_native(ctx,base,stride,rows,x,y,w,h));
    return queue_decode(ctx,p,n,pts,flags);
}
static pxa_status_t surface_create(void *ctx,const pxa_surface_desc_t *desc,uint64_t *handle,uint32_t *stride) {
    (void)ctx;
    assert(desc->width==4 && desc->height==4);
    *handle=1; *stride=10; staged=false; memset(display,0xcc,sizeof(display));
    return PXA_STATUS_OK;
}
static pxa_status_t surface_stage(void *ctx,uint64_t handle,pxa_surface_fill_fn fill,void *source) {
    (void)ctx; assert(handle==1);
    if (fill && staged) return PXA_STATUS_WOULD_BLOCK;
    if (fill) fill(source,display,10,0);
    staged=fill!=NULL;
    return PXA_STATUS_OK;
}
static pxa_status_t surface_queue(void *ctx,uint64_t handle,uint64_t id,const pxa_surface_damage_rect_t *rect,uint8_t n) {
    (void)ctx; assert(handle==1 && id && !rect && !n);
    if (!staged) return PXA_STATUS_BAD_STATE;
    staged=false; return PXA_STATUS_OK;
}
static pxa_status_t surface_write(void *ctx,uint64_t h,const uint8_t *data,size_t n) {
    (void)ctx; (void)h; (void)data; (void)n; return PXA_STATUS_UNSUPPORTED;
}
static pxa_status_t surface_configure(void *ctx,uint64_t h,const pxa_surface_layer_t *layer) {
    (void)ctx; (void)h; (void)layer; return PXA_STATUS_OK;
}
static pxa_status_t surface_query(void *ctx,uint64_t h,pxa_surface_state_t *state) {
    (void)ctx; (void)h; memset(state,0,sizeof(*state)); return PXA_STATUS_OK;
}
static void surface_close(void *ctx,uint64_t h) { (void)ctx; (void)h; staged=false; }

static void *test_surface_output(pxa_component_t owner,pxa_component_t other,pxa_handle64_t decoder) {
    pxa_surface_config_t config = {
        .struct_size=sizeof(config), .max_surfaces=2, .max_surfaces_per_component=1,
        .max_width=4, .max_height=4, .max_frame_bytes=40, .min_buffer_count=2, .max_buffer_count=2,
        .backend={.struct_size=sizeof(pxa_surface_backend_t), .create=surface_create, .write=surface_write,
            .queue=surface_queue, .configure=surface_configure, .query=surface_query,
            .close=surface_close, .stage=surface_stage}
    };
    /* A provider compiled with the previous ABI includes stage and ends
     * immediately before queue_ordered. Only the advertised bytes count. */
    config.struct_size=offsetof(pxa_surface_config_t,backend)+offsetof(pxa_surface_backend_t,queue_ordered);
    config.backend.struct_size=offsetof(pxa_surface_backend_t,queue_ordered);
    config.backend.queue_ordered=surface_queue;
    size_t size=pxa_surface_service_workspace_size(&config);
    void *workspace=malloc(size); pxa_surface_service_t *service;
    assert(!pxa_surface_service_init(workspace,size,runtime,&config,&service));
    assert(!pxa_surface_service_register(service));
    uint8_t desc[8]={4,0,4,0,1,0,2,PXA_SURFACE_FLAG_HOST_PRODUCED};
    uint8_t bind[16]={0}, id[8]={1};
    pxa_write_u16(bind+8,1); pxa_write_u16(bind+10,1);
    pxa_write_u16(bind+12,2); pxa_write_u16(bind+14,2);
    assert(!service_call(other,PXA_SURFACE_SERVICE_ID,PXA_SURFACE_CREATE,desc,sizeof(desc)));
    pxa_handle64_t foreign=pxa_read_u64(reply.payload.data+4);
    pxa_write_u64(bind,foreign);
    assert(io(owner,decoder,PXA_VIDEO_IO_BIND_SURFACE,bind,sizeof(bind))<0);
    assert(!pxa_handle_close64(runtime,other,foreign));
    assert(!service_call(owner,PXA_SURFACE_SERVICE_ID,PXA_SURFACE_CREATE,desc,sizeof(desc)));
    const pxa_handle64_t target=pxa_read_u64(reply.payload.data+4);
    pxa_write_u64(bind,target);
    assert(!io(owner,decoder,PXA_VIDEO_IO_BIND_SURFACE,bind,sizeof(bind)));
    assert(io(owner,target,PXA_IO_WRITE,display,sizeof(display))==PXA_STATUS_DENIED);
    assert(!io(owner,decoder,PXA_VIDEO_IO_PREPARE_SURFACE,NULL,0));
    assert(io(owner,decoder,PXA_VIDEO_IO_PREPARE_SURFACE,NULL,0)==PXA_STATUS_WOULD_BLOCK);
    assert(display[12]!=0x77);
    pxa_write_u16(bind+12,4);
    assert(io(owner,decoder,PXA_VIDEO_IO_BIND_SURFACE,bind,sizeof(bind))==PXA_STATUS_INVALID_ARGUMENT);
    assert(io(owner,decoder,PXA_VIDEO_IO_PRESENT_SURFACE_ORDERED,id,7)==PXA_STATUS_INVALID_ARGUMENT);
    assert(io(owner,decoder,PXA_VIDEO_IO_PRESENT_SURFACE_ORDERED,id,sizeof(id))==PXA_STATUS_UNSUPPORTED && staged);
    assert(!io(owner,decoder,PXA_VIDEO_IO_PRESENT_SURFACE,id,sizeof(id)));
    uint8_t expected[40]={0};
    memcpy(expected+12,pixels,2); memcpy(expected+14,pixels+4,2);
    memcpy(expected+22,pixels+8,2); memcpy(expected+24,pixels+12,2);
    assert(!memcmp(expected,display,sizeof(display)));
    uint8_t next[17] = {0};
    next[16] = 42;
    pxa_write_u64(next,98765);
    assert(io(owner,decoder,PXA_VIDEO_IO_PREPARE_AND_QUEUE,next,sizeof(next))==PXA_STATUS_WOULD_BLOCK);
    assert(!async_pending && !staged);
    native_surface=true;
    assert(!io(owner,decoder,PXA_VIDEO_IO_PREPARE_AND_QUEUE,next,sizeof(next)) && staged && async_pending);
    assert(io(owner,decoder,PXA_VIDEO_IO_PREPARE_AND_QUEUE,next,sizeof(next))==PXA_STATUS_BUSY);
    assert(io(owner,decoder,PXA_VIDEO_IO_POLL_DECODE,NULL,0)==PXA_STATUS_WOULD_BLOCK);
    assert(!io(owner,decoder,PXA_VIDEO_IO_PRESENT_SURFACE,id,sizeof(id)) && !staged);
    async_complete=true;
    assert(!io(owner,decoder,PXA_VIDEO_IO_POLL_DECODE,NULL,0));
    uint8_t metadata[32];
    assert(!io(owner,decoder,PXA_VIDEO_IO_FRAME_INFO,metadata,sizeof(metadata)) && pxa_read_u64(metadata+8)==98765);
    assert(!io(owner,decoder,PXA_VIDEO_IO_PREPARE_SURFACE,NULL,0));
    assert(display[12]==0x77 && display[25]==0x77 && display[11]==0 && display[26]==0);
    native_surface=false;
    assert(io(owner,decoder,PXA_VIDEO_IO_PREPARE_SURFACE,NULL,0)==PXA_STATUS_WOULD_BLOCK);
    assert(!io(owner,decoder,PXA_VIDEO_IO_RESET,NULL,0));
    assert(io(owner,decoder,PXA_VIDEO_IO_PRESENT_SURFACE,id,sizeof(id))==PXA_STATUS_BAD_STATE);
    assert(!pxa_handle_close64(runtime,owner,target));
    assert(!service_call(owner,PXA_SURFACE_SERVICE_ID,PXA_SURFACE_CREATE,desc,sizeof(desc)));
    const pxa_handle64_t replacement=pxa_read_u64(reply.payload.data+4);
    assert(replacement!=target && io(owner,decoder,PXA_VIDEO_IO_PRESENT_SURFACE,id,sizeof(id))<0);
    assert(!pxa_handle_close64(runtime,owner,replacement));
    return workspace;
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
    pxa_video_backend_t backend={sizeof(backend),NULL,1,&caps,open_decoder,decode,reset,close_decoder,NULL,
        queue_decode,poll_decode,render_deferred,materialize,NULL,render_native,preferred_size,prepare_next};
    backend.struct_size = PXA_VIDEO_BACKEND_SYNC_SIZE;
    assert(!pxa_video_service_init(sw,n,runtime,&backend,&s));
    backend.struct_size = PXA_VIDEO_BACKEND_DEFERRED_SIZE;
    assert(!pxa_video_service_init(sw,n,runtime,&backend,&s));
    backend.struct_size = sizeof(backend);
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
    uint8_t fit[4] = {5,0,3,0};
    assert(io(c,handle,PXA_VIDEO_IO_PREFERRED_SIZE,fit,4)==4 && fit[0]==2 && fit[2]==2);
    fit[2]=1;
    assert(io(c,handle,PXA_VIDEO_IO_PREFERRED_SIZE,fit,4)==PXA_STATUS_IO_ERROR && fit[2]==1);
    fit[0]=1;
    assert(io(c,handle,PXA_VIDEO_IO_PREFERRED_SIZE,fit,4)==PXA_STATUS_UNSUPPORTED);
    fit[0]=0;
    assert(io(c,handle,PXA_VIDEO_IO_PREFERRED_SIZE,fit,4)==PXA_STATUS_INVALID_ARGUMENT);
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
    uint8_t strided[16];
    pxa_write_u16(body,2); pxa_write_u16(body+2,2); pxa_write_u32(body+4,8);
    assert(!io(c,handle,PXA_VIDEO_IO_CONFIGURE_SCALED,body,8));
    memset(strided,0xa5,sizeof(strided));
    assert(!io(c,handle,PXA_VIDEO_IO_READ_SCALED_PIXELS,strided+1,12));
    assert(!memcmp(strided+1,expected,4) && !memcmp(strided+9,expected+4,4));
    assert(strided[0]==0xa5 && strided[5]==0xa5 && strided[8]==0xa5 && strided[13]==0xa5);
    _Alignas(uint16_t) uint8_t three_quarters[16];
    pxa_write_u16(body,3); pxa_write_u16(body+2,2); pxa_write_u32(body+4,8);
    assert(!io(c,handle,PXA_VIDEO_IO_CONFIGURE_SCALED,body,8));
    for (unsigned offset=0; offset<2; ++offset) {
        memset(three_quarters,0xa5,sizeof(three_quarters));
        uint8_t *out=three_quarters+offset;
        assert(!io(c,handle,PXA_VIDEO_IO_READ_SCALED_PIXELS,out,14));
        assert(!memcmp(out,pixels,6) && !memcmp(out+8,pixels+8,6));
        assert(out[6]==0xa5 && out[7]==0xa5 && out[14]==0xa5);
    }
    pxa_write_u16(body,2);
    pxa_write_u32(body+4,3);
    assert(io(c,handle,PXA_VIDEO_IO_CONFIGURE_SCALED,body,8)==PXA_STATUS_INVALID_ARGUMENT);
    assert(io(c,handle,PXA_VIDEO_IO_READ_SCALED_PIXELS,body,8)==PXA_STATUS_INVALID_ARGUMENT);
    assert(!io(c,handle,PXA_VIDEO_IO_CONFIGURE_SCALED,body,4));
    assert(io(other,handle,PXA_VIDEO_IO_READ_SCALED,scaled,16)<0);
    assert(io(c,handle,PXA_VIDEO_IO_READ_SCALED,scaled,15)==PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u16(scaled,8); pxa_write_u16(scaled+2,6);
    assert(!io(c,handle,PXA_VIDEO_IO_READ_SCALED,scaled,104));
    for(unsigned y=0;y<6;++y)for(unsigned x=0;x<8;++x) {
        assert(scaled[8+(y*8+x)*2]==pixels[((y/2)*4+x/2)*2]);
    }
#if defined(__GNUC__) || defined(__clang__)
    for (unsigned src_offset=0;src_offset<2;++src_offset)
        for (unsigned dst_offset=0;dst_offset<2;++dst_offset)
            for (unsigned groups=0;groups<32;++groups) {
                uint16_t *in=malloc((1+src_offset+3*groups)*sizeof(*in));
                uint16_t out[130];
                assert(in);
                for (unsigned x=0;x<3*groups;++x) in[1+src_offset+x]=(uint16_t)(113u*x+7);
                memset(out,0xa5,sizeof(out));
                pxa_scale_expand_groups((uint8_t*)(out+1+dst_offset),
                    (const uint8_t*)(in+1+src_offset),groups);
                for (unsigned x=0;x<4*groups;++x)
                    assert(out[1+dst_offset+x]==in[1+src_offset+3*x/4]);
                assert(out[dst_offset]==0xa5a5 && out[1+dst_offset+4*groups]==0xa5a5);
                free(in);
            }
    uint16_t source_row[1024], dest_row[1368];
    for (unsigned x=0;x<1024;++x) source_row[x]=(uint16_t)(x*233u);
    for (unsigned sw=36;sw<=1024;++sw) {
        for (unsigned dw=sw-4;dw<=sw+4;++dw) {
            pxa_scale_copy_span spans[PXA_SCALE_MAX_DELTA + 1];
            const unsigned count=pxa_scale_copy_spans(sw,dw,spans);
            if (sw==dw) { assert(!count); continue; }
            assert(count && count<=PXA_SCALE_MAX_DELTA+1);
            dest_row[dw]=0x55aa;
            pxa_scale_copy_row((uint8_t*)dest_row,(const uint8_t*)source_row,spans,count);
            for (unsigned x=0;x<dw;++x) assert(dest_row[x]==source_row[x*sw/dw]);
            assert(dest_row[dw]==0x55aa);
        }
    }
    for (unsigned sw=24;sw<1024;++sw) for (unsigned dw=(4*sw-4)/3;dw<=(4*sw+4)/3;++dw) {
        pxa_scale_run runs[PXA_SCALE_MAX_RUNS];
        unsigned count=pxa_scale_four_thirds_runs(sw,dw,runs);
        if (dw<32 || 3*dw+4<4*sw) continue;
        assert(count);
        dest_row[0]=dest_row[dw+1]=0xa55a;
        pxa_scale_run_row((uint8_t*)(dest_row+1),(const uint8_t*)source_row,sw,dw,runs,count);
        for (unsigned x=0;x<dw;++x) assert(dest_row[x+1]==source_row[x*sw/dw]);
        assert(dest_row[0]==0xa55a && dest_row[dw+1]==0xa55a);
    }
#endif
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
    defer_frame=true;
    assert(!io(c,handle,PXA_VIDEO_IO_DECODE,packet,17));
    assert(!io(c,handle,PXA_VIDEO_IO_FRAME_INFO,body,32));
    void *surface_workspace=test_surface_output(c,other,handle);
    assert(!io(c,handle,PXA_VIDEO_IO_DECODE,packet,17));
    assert(io(c,handle,PXA_IO_READ,body,3)==3 && !memcmp(body,pixels,3));
    assert(io(c,handle,PXA_IO_READ,body,5)==5 && !memcmp(body,pixels+3,5));
    defer_frame=false;
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
    pxa_write_u32(packet+8,0); packet[16]=43;
    assert(!io(other,handle,256,packet,17));
    pxa_write_u16(body,4); pxa_write_u16(body+2,4); pxa_write_u32(body+4,10);
    assert(!io(other,handle,PXA_VIDEO_IO_CONFIGURE_SCALED,body,8));
    _Alignas(uint16_t) uint8_t four_thirds[40];
    for (unsigned offset=0; offset<2; ++offset) {
        memset(four_thirds,0xa5,sizeof(four_thirds));
        uint8_t *out=four_thirds+offset;
        assert(!io(other,handle,PXA_VIDEO_IO_READ_SCALED_PIXELS,out,38));
        for (unsigned y=0;y<4;++y) {
            for (unsigned x=0;x<4;++x)
                assert(!memcmp(out+y*10+x*2,pixels+((y*3/4)*3+x*3/4)*2,2));
            assert(out[y*10+8]==0xa5);
        }
        if (offset) assert(four_thirds[0]==0xa5);
    }
    assert(io(other,handle,PXA_VIDEO_IO_POLL_DECODE,NULL,0)==PXA_STATUS_NOT_FOUND);
    pxa_write_u64(packet,54321);
    packet[16]=42;
    assert(io(other,handle,PXA_VIDEO_IO_QUEUE_DECODE,packet,33)==PXA_STATUS_LIMIT_EXCEEDED);
    assert(!io(other,handle,PXA_VIDEO_IO_QUEUE_DECODE,packet,17));
    packet[16]=0; /* The queued job owns an independent packet copy. */
    assert(io(other,handle,PXA_VIDEO_IO_FRAME_INFO,body,32)==PXA_STATUS_WOULD_BLOCK);
    assert(io(other,handle,PXA_VIDEO_IO_DECODE,packet,17)==PXA_STATUS_BUSY);
    assert(io(other,handle,PXA_VIDEO_IO_QUEUE_DECODE,packet,17)==PXA_STATUS_BUSY);
    assert(io(other,handle,PXA_VIDEO_IO_POLL_FRAME,body,32)==PXA_STATUS_WOULD_BLOCK);
    assert(io(other,handle,PXA_VIDEO_IO_POLL_DECODE,NULL,0)==PXA_STATUS_WOULD_BLOCK);
    async_complete=true;
    pxa_video_service_dispatch(s);
    pxa_video_service_dispatch(s);
    assert(io(other,handle,PXA_VIDEO_IO_FRAME_INFO,body,32)==PXA_STATUS_WOULD_BLOCK);
    size_t event_size=0;
    assert(!pxa_event_pop(runtime,other,response,sizeof(response),&event_size));
    assert(!pxa_message_decode(response,event_size,sizeof(response),&reply));
    assert(reply.service==PXA_VIDEO_SERVICE_ID && reply.opcode==PXA_VIDEO_DECODE_READY &&
        reply.payload.size==8 && pxa_read_u64(reply.payload.data)==handle);
    assert(io(other,handle,PXA_VIDEO_IO_POLL_FRAME,body,31)==PXA_STATUS_INVALID_ARGUMENT);
    assert(io(other,handle,PXA_VIDEO_IO_POLL_FRAME,body,32)==32);
    assert(pxa_read_u64(body+8)==54321 && pxa_read_u32(body+24)==24 && !pxa_read_u32(body+28));
    assert(!io(other,handle,PXA_VIDEO_IO_FRAME_INFO,body,32));
    assert(pxa_read_u64(body+8)==54321 && pxa_read_u32(body+24)==24);
    assert(io(other,handle,PXA_VIDEO_IO_POLL_DECODE,NULL,0)==PXA_STATUS_NOT_FOUND);
    packet[16]=44;
    assert(!io(other,handle,PXA_VIDEO_IO_QUEUE_DECODE,packet,17));
    async_complete=true;
    assert(!io(other,handle,PXA_VIDEO_IO_POLL_FRAME,body,32));
    assert(io(other,handle,PXA_VIDEO_IO_POLL_FRAME,body,32)==PXA_STATUS_NOT_FOUND);
    assert(io(other,handle,PXA_VIDEO_IO_FRAME_INFO,body,32)==PXA_STATUS_WOULD_BLOCK);
    packet[16]=42;
    assert(!io(other,handle,PXA_VIDEO_IO_QUEUE_DECODE,packet,17));
    assert(!io(other,handle,PXA_VIDEO_IO_RESET,NULL,0) && !async_pending);
    assert(io(other,handle,PXA_VIDEO_IO_POLL_DECODE,NULL,0)==PXA_STATUS_NOT_FOUND);
    assert(io(other,handle,PXA_IO_READ,body,1)==PXA_STATUS_WOULD_BLOCK);
    assert(!io(other,handle,PXA_VIDEO_IO_QUEUE_DECODE,packet,17));
    pxa_video_service_deinit(s); pxa_runtime_deinit(runtime); free(surface_workspace); free(sw); free(rw);
    assert(opened==closed && !async_pending); return 0;
}
