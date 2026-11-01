/* SPDX-License-Identifier: MIT */
#include <emscripten.h>
#include "services.h"
EM_JS(void, unavailable, (), {
    Module.browserNotify?.({type:'log',level:2,message:'Browser direct mode: raw TCP/TLS sockets are unsupported; no server tunnel is used'});
});
pxa_status_t browser_socket_open(void *c,const pxa_socket_request_t *r,void **out){
    (void)c;(void)r;(void)out;unavailable();return PXA_STATUS_UNSUPPORTED;
}
int32_t browser_socket_io(void *c,void *value,uint32_t op,uint8_t *data,size_t n){
    (void)c;(void)value;(void)op;(void)data;(void)n;return PXA_STATUS_UNSUPPORTED;
}
void browser_socket_close(void *c,void *value){(void)c;(void)value;}
