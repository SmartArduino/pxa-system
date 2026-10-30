/* SPDX-License-Identifier: MIT */
#include <emscripten.h>
#include <stdlib.h>
#include <string.h>
#include "services.h"
#include "pxa/http_stream.h"

#define SLOTS 8
typedef struct {
    uint32_t id;
    uint8_t wanted_content_type;
    pxa_http_stream_t stream;
    void *storage;
    pxa_net_header_t headers[PXA_NET_MAX_HEADERS];
} exchange_t;
static exchange_t exchanges[SLOTS];
static uint32_t sequence;
static pxa_net_service_t *net;
EM_JS(int,connection_state,(),{return typeof navigator==='undefined'||typeof navigator.onLine!=='boolean'?0:navigator.onLine?2:1;});
static pxa_status_t connectivity(void *c,uint32_t *state){(void)c;*state=connection_state();return PXA_STATUS_OK;}
static exchange_t *find(uint32_t id){
    for(unsigned i=0;i<SLOTS;i++)if(exchanges[i].id==id&&id)return &exchanges[i];
    return NULL;
}
EM_JS(void, js_wake, (uint32_t id), {Module.browserNetWake?.(id);});
EM_JS(void, js_close, (uint32_t id), {Module.browserNetClose?.(id);});
EM_JS(int, streaming_upload, (), {return !!Module.browserStreamingUpload;});
_Static_assert(sizeof(pxa_bytes_t)==8 && sizeof(pxa_net_header_t)==16,"Wasm32 network records");
EM_JS(void, js_start, (uint32_t id,const char *url,size_t url_size,int method,
    const pxa_net_header_t *headers,int header_count,const uint8_t *body,size_t body_size,
    const pxa_bytes_t *wanted,int wanted_count,uint32_t timeout,int upload), {
    const text=(p,n)=>UTF8ToString(p,n),h=[],w=[];
    for(let i=0;i<header_count;i++){
        const p=(headers+i*16)>>>2;h.push([text(HEAPU32[p],HEAPU32[p+1]),text(HEAPU32[p+2],HEAPU32[p+3])]);
    }
    for(let i=0;i<wanted_count;i++){const p=(wanted+i*8)>>>2;w.push(text(HEAPU32[p],HEAPU32[p+1]));}
    Module.browserNetStart({id,url:text(url,url_size),method:["",'GET','HEAD','POST','PUT','PATCH','DELETE'][method],
        headers:h,body:HEAPU8.slice(body,body+body_size),wanted:w,timeout,upload:!!upload});
});
static pxa_status_t create(const pxa_net_request_t *r,exchange_t **out,int streaming){
    exchange_t *e=NULL;for(unsigned i=0;i<SLOTS;i++)if(!exchanges[i].id){e=&exchanges[i];break;}
    if(!e)return PXA_STATUS_RESOURCE_LIMIT;
    uint32_t buffer=streaming?r->buffer_bytes:16384,head=streaming?r->response_header_bytes:2048;
    size_t size=pxa_http_stream_storage_size(buffer,head);if(!size)return PXA_STATUS_INVALID_ARGUMENT;
    memset(e,0,sizeof(*e));e->storage=calloc(1,size);if(!e->storage)return PXA_STATUS_RESOURCE_LIMIT;
    do{e->id=++sequence;}while(!e->id);
    pxa_http_stream_init(&e->stream,e->storage,buffer,head,streaming?r->max_body_bytes:r->max_response_bytes,
        streaming?r->upload_bytes:0,r->method);
    for(unsigned i=0;i<r->wanted_response_header_count;i++) {
        pxa_bytes_t name=r->wanted_response_headers[i];
        if(name.size==12&&!memcmp(name.data,"content-type",12))e->wanted_content_type=1;
    }
    if(streaming&&r->body.size){
        int32_t accepted=pxa_http_stream_io(&e->stream,PXA_IO_WRITE,(uint8_t *)r->body.data,r->body.size);
        if(accepted!=(int32_t)r->body.size){free(e->storage);memset(e,0,sizeof(*e));return PXA_STATUS_INVALID_ARGUMENT;}
        pxa_http_stream_io(&e->stream,PXA_HTTP_IO_FINISH_UPLOAD,NULL,0);
    }
    js_start(e->id,(const char *)r->url.data,r->url.size,r->method,r->headers,r->header_count,
        r->body.data,r->body.size,r->wanted_response_headers,r->wanted_response_header_count,r->timeout_ms,
        streaming&&r->upload_bytes);
    *out=e;return PXA_STATUS_OK;
}
static pxa_status_t start(void *c,const pxa_net_request_t *r,uint64_t *operation){
    (void)c;exchange_t *e;pxa_status_t s=create(r,&e,0);if(!s)*operation=e->id;return s;
}
static void close_exchange(void *c,void *value){
    (void)c;exchange_t *e=value;if(!e||!e->id)return;js_close(e->id);free(e->storage);memset(e,0,sizeof(*e));
}
static void cancel(void *c,uint64_t id){exchange_t *e=find((uint32_t)id);if(e)close_exchange(c,e);}
static pxa_status_t poll(void *c,uint64_t id,pxa_net_response_t *out){
    (void)c;exchange_t *e=find((uint32_t)id);if(!e)return PXA_STATUS_CANCELLED;
    pxa_http_stream_t *s=&e->stream;uint32_t state=pxa_http_stream_state(s);
    if(!(state&PXA_HTTP_STREAM_HEADERS))return state&PXA_HTTP_STREAM_DONE?s->error:PXA_STATUS_WOULD_BLOCK;
    out->status_code=pxa_read_u16(s->head);out->body_stream=e;
    out->flags=s->method==PXA_NET_METHOD_HEAD?0:PXA_NET_RESPONSE_BODY_PRESENT;
    out->headers=e->headers;out->header_count=0;
    for(size_t i=16;i<s->head_size;){
        size_t n=pxa_read_u16(s->head+i),v=pxa_read_u16(s->head+i+2);
        pxa_bytes_t name={s->head+i+4,n},value={s->head+i+4+n,v};
        int content_type=n==12&&!memcmp(name.data,"content-type",12);
        if(content_type)out->content_type=value;
        if((!content_type||e->wanted_content_type)&&out->header_count<PXA_NET_MAX_HEADERS)
            e->headers[out->header_count++]=(pxa_net_header_t){name,value};
        i+=4+n+v;
    }
    return PXA_STATUS_OK;
}
static pxa_status_t read_body(void *c,void *value,uint8_t *out,size_t capacity,size_t *size){
    (void)c;exchange_t *e=value;int32_t n=pxa_http_stream_io(&e->stream,PXA_IO_READ,out,capacity);
    if(n>=0){*size=(size_t)n;js_wake(e->id);return PXA_STATUS_OK;}return n;
}
static pxa_status_t open_exchange(void *c,const pxa_net_request_t *r,void **out){
    if(r->upload_bytes&&!streaming_upload())return PXA_STATUS_UNSUPPORTED;
    (void)c;exchange_t *e;pxa_status_t s=create(r,&e,1);if(!s)*out=e;return s;
}
static int32_t exchange_io(void *c,void *value,uint32_t op,uint8_t *data,size_t size){
    (void)c;exchange_t *e=value;int32_t n=pxa_http_stream_io(&e->stream,op,data,size);
    if(n>=0)js_wake(e->id);return n;
}
EMSCRIPTEN_KEEPALIVE int browser_net_header(uint32_t id,const uint8_t *n,size_t ns,const uint8_t *v,size_t vs){
    exchange_t *e=find(id);return e&&pxa_http_stream_header(&e->stream,n,ns,v,vs);
}
EMSCRIPTEN_KEEPALIVE void browser_net_headers(uint32_t id,uint16_t status){
    exchange_t *e=find(id);if(e)pxa_http_stream_headers(&e->stream,status,UINT64_MAX);
}
EMSCRIPTEN_KEEPALIVE void browser_net_done(uint32_t id,int status){
    exchange_t *e=find(id);if(e&&!(pxa_http_stream_state(&e->stream)&PXA_HTTP_STREAM_DONE))pxa_http_stream_done(&e->stream,status);
}
EMSCRIPTEN_KEEPALIVE int browser_net_receive(uint32_t id,const uint8_t *data,size_t size){
    exchange_t *e=find(id);if(!e)return PXA_STATUS_CANCELLED;pxa_http_stream_t *s=&e->stream;
    if(size>s->max_body_bytes-s->received_bytes){browser_net_done(id,PXA_STATUS_LIMIT_EXCEEDED);return PXA_STATUS_LIMIT_EXCEEDED;}
    size_t total=0;while(total<size){uint8_t *block;size_t n=pxa_http_ring_peek(&s->receive,&block,1);
        if(n>size-total)n=size-total;if(!n)break;memcpy(block,data+total,n);pxa_http_ring_advance(&s->receive,n,1);total+=n;}
    s->received_bytes+=total;return (int)total;
}
EMSCRIPTEN_KEEPALIVE int browser_net_upload(uint32_t id,uint8_t *out,size_t capacity){
    exchange_t *e=find(id);if(!e)return PXA_STATUS_CANCELLED;pxa_http_stream_t *s=&e->stream;
    uint8_t *block;size_t n=pxa_http_ring_peek(&s->send,&block,0);if(n>capacity)n=capacity;
    if(n){memcpy(out,block,n);pxa_http_ring_advance(&s->send,n,0);s->sent_bytes+=n;return (int)n;}
    return pxa_http_stream_state(s)&PXA_HTTP_STREAM_UPLOAD_DONE?0:PXA_STATUS_WOULD_BLOCK;
}
pxa_status_t browser_net_init(pxa_runtime_t *runtime,pxa_permission_service_t *permissions){
    pxa_net_config_t c={.struct_size=sizeof(c),.max_pending_requests=4,.max_requests_per_component=4,
        .max_response_streams=4,.max_headers=8,.max_response_bytes=16*1024*1024,.max_inline_body_bytes=2048,
        .max_request_header_bytes=2048,.max_response_header_bytes=2048,.min_timeout_ms=100,
        .default_timeout_ms=30000,.max_timeout_ms=86400000,.permissions=permissions,
        .backend={.struct_size=sizeof(pxa_net_backend_t),.start=start,.poll=poll,.cancel=cancel,
            .read_body=read_body,.close_body=close_exchange,.connectivity=connectivity}};
    c.backend.open_socket=browser_socket_open;c.backend.socket_io=browser_socket_io;c.backend.close_socket=browser_socket_close;
    c.backend.open_exchange=open_exchange;c.backend.exchange_io=exchange_io;c.backend.close_exchange=close_exchange;
    size_t size=pxa_net_service_workspace_size(&c);
    pxa_status_t s=pxa_net_service_init(calloc(1,size),size,runtime,&c,&net);
    return s?s:pxa_net_service_register(net);
}
void browser_net_tick(void){pxa_component_t affected[4];size_t count;if(net)pxa_net_poll(net,affected,4,&count);}
