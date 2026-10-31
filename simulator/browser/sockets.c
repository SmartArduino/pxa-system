/* SPDX-License-Identifier: MIT */
#include <emscripten.h>
#include <stdlib.h>
#include <string.h>
#include "services.h"
#include "pxa/socket_stream.h"
static struct socket {uint32_t id;pxa_socket_stream_t stream;void *storage;} sockets[4];
static uint32_t sequence;
static struct socket *find_socket(uint32_t id){for(unsigned i=0;i<4;i++)if(sockets[i].id==id&&id)return &sockets[i];return NULL;}
EM_JS(int,relay_available,(),{return !!Module.browserSocketAvailable;});
EM_JS(void,socket_start_js,(uint32_t id,const uint8_t *url,size_t n,uint32_t timeout),{Module.browserSocketStart({id,endpoint:UTF8ToString(url,n),timeout});});
EM_JS(void,socket_wake_js,(uint32_t id),{Module.browserSocketWake?.(id);});
EM_JS(void,socket_close_js,(uint32_t id),{Module.browserSocketClose?.(id);});
pxa_status_t browser_socket_open(void *c,const pxa_socket_request_t *r,void **out){
    (void)c;if(!relay_available())return PXA_STATUS_UNSUPPORTED;
    for(unsigned i=0;i<4;i++)if(!sockets[i].id){
        struct socket *s=&sockets[i];s->storage=calloc(1,r->buffer_bytes);if(!s->storage)return PXA_STATUS_RESOURCE_LIMIT;
        pxa_socket_stream_init(&s->stream,s->storage,r->buffer_bytes);
        do{s->id=++sequence;}while(!s->id);*out=s;
        socket_start_js(s->id,r->endpoint.data,r->endpoint.size,r->connect_timeout_ms);return 0;
    }
    return PXA_STATUS_RESOURCE_LIMIT;
}
int32_t browser_socket_io(void *c,void *value,uint32_t op,uint8_t *data,size_t n){
    (void)c;struct socket *s=value;int32_t result=pxa_socket_stream_io(&s->stream,op,data,n);socket_wake_js(s->id);return result;
}
void browser_socket_close(void *c,void *value){(void)c;struct socket *s=value;if(s&&s->id){socket_close_js(s->id);free(s->storage);memset(s,0,sizeof(*s));}}
EMSCRIPTEN_KEEPALIVE int browser_socket_receive(uint32_t id,const uint8_t *data,size_t n){
    struct socket *s=find_socket(id);if(!s)return PXA_STATUS_CANCELLED;
    size_t at=0;while(at<n){uint8_t *out;size_t next=pxa_http_ring_peek(&s->stream.receive,&out,1);if(next>n-at)next=n-at;if(!next)break;memcpy(out,data+at,next);pxa_http_ring_advance(&s->stream.receive,next,1);at+=next;}return at;
}
EMSCRIPTEN_KEEPALIVE int browser_socket_upload(uint32_t id,uint8_t *out,size_t n){
    struct socket *s=find_socket(id);if(!s)return PXA_STATUS_CANCELLED;
    uint8_t *data;size_t next=pxa_http_ring_peek(&s->stream.send,&data,0);if(next>n)next=n;
    if(next){memcpy(out,data,next);pxa_http_ring_advance(&s->stream.send,next,0);}return next;
}
EMSCRIPTEN_KEEPALIVE void browser_socket_state(uint32_t id,int state,int error){struct socket *s=find_socket(id);if(s){if(state==1)s->stream.state|=PXA_SOCKET_CONNECTED;else pxa_socket_stream_finish(&s->stream,error);}}
