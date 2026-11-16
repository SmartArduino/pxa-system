#undef NDEBUG
#include "pxa/assets.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static size_t native_free, native_largest;
static void *allocate(void *c, size_t n) { (void)c; return malloc(n); }
static void release(void *c, void *p) { (void)c; free(p); }
static pxa_status_t heap(void *c, size_t *free_bytes, size_t *largest) {
    (void)c; *free_bytes=native_free; *largest=native_largest; return PXA_STATUS_OK;
}
static pxa_status_t open_codec(pxa_bytes_t b, pxa_image_target_t *target,
    const pxa_memory_allocator_t *temporary, void **out) {
    (void)b; (void)temporary; *out=target; return PXA_STATUS_OK;
}
static pxa_status_t write_codec(void *c, pxa_bytes_t b) { (void)c; (void)b; return PXA_STATUS_OK; }
static pxa_status_t finish_codec(void *c) { return pxa_image_target_begin(c,320,200,0); }
static void close_codec(void *c) { (void)c; }
static pxa_status_t next_codec(void *c, pxa_image_frame_info_t *f) {
    (void)c; memset(f,0,sizeof(*f));f->last=1;return PXA_STATUS_OK;
}
static uint32_t query(pxa_runtime_t *r, pxa_component_t c, uint64_t token,
                       uint32_t pixels, uint32_t expected_max) {
    uint8_t request[4], event[64];size_t size;pxa_message_view_t reply;
    pxa_write_u32(request,pixels);
    pxa_message_view_t message={21,8,token,{request,sizeof(request)}};
    assert(!pxa_component_begin_event(r,c));
    assert(!pxa_runtime_control_view(r,c,&message));
    assert(!pxa_component_finish_event(r,c,1));
    assert(!pxa_event_pop(r,c,event,sizeof(event),&size));
    assert(!pxa_message_decode(event,size,sizeof(event),&reply));
    assert(reply.service==21 && reply.opcode==8 && reply.request_token==token);
    assert(reply.payload.size==16 && !pxa_read_u32(reply.payload.data));
    assert(pxa_read_u32(reply.payload.data+4)==expected_max);
    assert(pxa_read_u32(reply.payload.data+12)==524288);
    return pxa_read_u32(reply.payload.data+8);
}
static uint32_t run(size_t quota, size_t temporary_quota, int legacy) {
    pxa_memory_budget_t budget;pxa_memory_budget_config_t bc={0};
    bc.limit[1]=quota;bc.temporary_limit[1]=temporary_quota;
    assert(!pxa_memory_budget_init(&budget,&bc));
    pxa_memory_owner_t owner;assert(!pxa_memory_owner_open(&budget,bc.limit,&owner));
    pxa_memory_allocator_t pixels={&budget,owner,1,PXA_MEMORY_IMAGE,NULL,allocate,release};
    pxa_memory_allocator_t temporary=pixels;temporary.kind=PXA_MEMORY_TEMPORARY;
    pxa_image_codec_t codec={open_codec,write_codec,finish_codec,close_codec,NULL};
    const pxa_image_codec_t *codecs[]={&codec};pxa_image_backend_t backend={codecs,1};
    pxa_runtime_limits_t limits;pxa_runtime_limits_init(&limits);
    limits.max_components=1;limits.max_services=1;limits.max_requests=2;limits.max_requests_per_component=2;
    size_t n=pxa_runtime_workspace_size(&limits);void *rw=malloc(n);pxa_runtime_t *r;
    assert(rw && !pxa_runtime_init(rw,n,&limits,&r));
    pxa_assets_config_t cfg={0};cfg.struct_size=legacy ?
        offsetof(pxa_assets_config_t,image_max_encoded_bytes):sizeof(cfg);
    cfg.max_pending=cfg.max_pending_per_component=cfg.max_resources=cfg.max_resources_per_component=2;
    cfg.image_allocator=&pixels;cfg.image_temporary_allocator=&temporary;cfg.image_backend=&backend;
    cfg.image_max_encoded_bytes=PXA_IMAGE_ENCODED_MAX_BYTES;
    cfg.image_memory_available=heap;
    n=pxa_assets_service_workspace_size(&cfg);void *sw=malloc(n);pxa_assets_service_t *s;
    assert(n && sw && !pxa_assets_service_init(sw,n,r,&cfg,&s));assert(!pxa_assets_service_register(s));
    pxa_component_t c;assert(!pxa_component_create(r,1,&c));
    assert(!pxa_component_set_wire_profile(r,c,1));
    assert(!pxa_component_begin_start(r,c));assert(!pxa_component_finish_start(r,c,0));
    const uint32_t maximum=legacy?524288:PXA_IMAGE_ENCODED_MAX_BYTES;
    native_free=quota;native_largest=quota;
    uint32_t normal=query(r,c,1,32768,maximum);
    uint32_t detail=query(r,c,2,262144,maximum);assert(detail<=normal);
    pxa_memory_stats_t stats;assert(!pxa_memory_budget_stats(&budget,owner,&stats));
    assert(!stats.charged[1] && !stats.peak[1]); /* Queries reserve/allocate nothing. */
    void *held=pxa_memory_allocate(&temporary,temporary_quota/2);assert(held);
    uint32_t pressured=query(r,c,3,32768,maximum);
    assert(legacy ? pressured<=normal : pressured<normal);
    pxa_memory_release(held);assert(query(r,c,4,32768,maximum)==normal);
    if (!legacy) {
        native_free=64*1024;assert(!query(r,c,5,32768,maximum));
        native_free=quota;native_largest=4096;assert(!query(r,c,6,32768,maximum));
        native_largest=quota;
        pxa_image_decode_options_t options={.max_width=320,.max_height=200,
            .max_pixels=32768,.max_encoded_bytes=2*1024*1024};
        pxa_image_decoder_t *decoder;assert(!pxa_image_decoder_create(&options,&pixels,&temporary,&backend,&decoder));
        uint8_t bytes[4096]={0};
        for (unsigned i=0;i<384;++i)assert(!pxa_image_decoder_write(decoder,(pxa_bytes_t){bytes,sizeof(bytes)}));
        pxa_decoded_image_t image;assert(!pxa_image_decoder_finish(decoder,&image));
        pxa_asset_object_release(image.image);
        pxa_image_decoder_destroy(decoder);
        /* An ordinary large request never widens default GIF/sequence input. */
        codec.next=next_codec;
        assert(!pxa_image_decoder_create(&options,&pixels,&temporary,&backend,&decoder));
        for(unsigned i=0;i<128;++i)assert(!pxa_image_decoder_write(decoder,(pxa_bytes_t){bytes,sizeof(bytes)}));
        assert(pxa_image_decoder_write(decoder,(pxa_bytes_t){bytes,1})==PXA_STATUS_LIMIT_EXCEEDED);
        pxa_image_decoder_destroy(decoder);
    }
    assert(!pxa_component_request_stop(r,c,0));assert(!pxa_component_begin_stop(r,c));
    assert(!pxa_component_finish_stop(r,c));
    assert(!pxa_memory_budget_stats(&budget,owner,&stats));assert(!stats.charged[1]);
    assert(!pxa_memory_owner_close(&budget,owner));free(sw);free(rw);return normal;
}
int main(void) {
    uint32_t small=run(4*1024*1024,2*1024*1024,0);
    uint32_t large=run(16*1024*1024,8*1024*1024,0);
    assert(small>524288 && large>small && large<=PXA_IMAGE_ENCODED_MAX_BYTES);
    assert(run(4*1024*1024,2*1024*1024,1)==524288);
    printf("image input limits PASS: 8MiB=%u, 32MiB=%u; pressure, release, legacy, GIF ceiling\n",small,large);
}
