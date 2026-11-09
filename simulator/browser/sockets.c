/* SPDX-License-Identifier: MIT */
#include <emscripten.h>
#include "services.h"
#include "pxa/lan.h"
static uint32_t slots[8], sequence;
EM_JS(int, ws_open, (uint32_t id,const char *url,size_t n,uint32_t bytes,uint32_t limit,uint32_t timeout), {
    return Module.browserWebSocketOpen?.(id,UTF8ToString(url,n),bytes,limit,timeout) ?? (-3);
});
EM_JS(int, ws_io, (uint32_t id,uint32_t op,uint8_t *p,size_t n), {
    return Module.browserWebSocketIO?.(id,op,p,n) ?? (-3);
});
EM_JS(void, ws_close, (uint32_t id), {Module.browserWebSocketClose?.(id);});
EM_JS(int, ws_poll, (), {return Module.browserWebSocketPoll?.()??0;});
static pxa_status_t open_ws(void *c,const pxa_lan_request_t *r,void **out){
    (void)c;
    if(r->kind!=PXA_NETWORK_WEBSOCKET)return PXA_STATUS_UNSUPPORTED;
    for(unsigned i=0;i<8;++i)if(!slots[i]){
        do{slots[i]=++sequence;}while(!slots[i]);
        int status=ws_open(slots[i],(const char *)r->url.data,r->url.size,
            r->buffer_bytes,r->max_message_bytes,r->connect_timeout_ms);
        if(status){slots[i]=0;return status;}
        *out=&slots[i];return PXA_STATUS_OK;
    }
    return PXA_STATUS_RESOURCE_LIMIT;
}
static int32_t io_ws(void *c,void *value,uint32_t op,uint8_t *p,size_t n){
    (void)c;return ws_io(*(uint32_t *)value,op,p,n);
}
static void close_ws(void *c,void *value){(void)c;uint32_t *id=value;ws_close(*id);*id=0;}
static int poll_ws(void *c){(void)c;return ws_poll();}
const pxa_lan_backend_t *browser_websocket_backend(void){
    static const pxa_lan_backend_t backend={.struct_size=sizeof(pxa_lan_backend_t),
        .features=PXA_NET_FEATURE_WEBSOCKET,.open=open_ws,.io=io_ws,
        .close=close_ws,.poll=poll_ws,.max_resources=8,.max_buffer_bytes=65536};
    return &backend;
}
pxa_status_t browser_socket_open(void *c,const pxa_socket_request_t *r,void **out){
    (void)c;(void)r;(void)out;return PXA_STATUS_UNSUPPORTED;
}
int32_t browser_socket_io(void *c,void *value,uint32_t op,uint8_t *data,size_t n){
    (void)c;(void)value;(void)op;(void)data;(void)n;return PXA_STATUS_UNSUPPORTED;
}
void browser_socket_close(void *c,void *value){(void)c;(void)value;}
