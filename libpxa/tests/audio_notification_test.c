/* Isolate the real service's routing/pool behavior, with a backend mailbox.
 * The product AOT test covers permission/session creation and WAMR delivery. */
#undef NDEBUG
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "../src/services/audio/audio_service.c"

static pxa_status_t peek(void *c,pxa_audio_playback_event_t *e) { return pxa_audio_playback_peek(c,e); }
static pxa_status_t consume(void *c,const pxa_audio_playback_event_t *e) { return pxa_audio_playback_consume(c,e); }
static void pop(pxa_runtime_t *runtime,pxa_component_t component,uint16_t opcode,uint64_t session,uint64_t instance,uint8_t state) {
    uint8_t bytes[128]; size_t n; pxa_message_view_t event;
    assert(!pxa_event_pop(runtime,component,bytes,sizeof(bytes),&n));
    assert(!pxa_message_decode(bytes,n,sizeof(bytes),&event));
    assert(event.opcode==opcode && !event.request_id);
    if (opcode==PXA_AUDIO_PLAYBACK_EVENT) {
        assert(event.service==PXA_AUDIO_SERVICE_ID && event.payload.size==24);
        assert(pxa_read_u64(event.payload.data)==session);
        assert(pxa_read_u64(event.payload.data+8)==instance && event.payload.data[16]==state);
    }
}
int main(void) {
    pxa_runtime_limits_t limits; pxa_runtime_limits_init(&limits);
    limits.max_components=1; limits.max_events=4; limits.mailbox_capacity=2;
    limits.reliable_event_reserve=1;
    size_t bytes=pxa_runtime_workspace_size(&limits);
    void *workspace=malloc(bytes); pxa_runtime_t *runtime;
    assert(workspace && !pxa_runtime_init(workspace,bytes,&limits,&runtime));
    pxa_component_t component;
    assert(!pxa_component_create(runtime,1,&component));
    assert(!pxa_component_set_core_major(runtime,component,1));
    assert(!pxa_component_begin_start(runtime,component));
    assert(!pxa_component_finish_start(runtime,component,0));
    pxa_audio_playback_queue_t queue={0};
    pxa_audio_session_t session={0};
    pxa_audio_service_t service={0};
    service.magic=PXA_AUDIO_MAGIC; service.runtime=runtime;
    service.sessions=&session; service.max_sessions=1; service.active_head=0;
    service.backend.context=&queue; service.backend.playback_peek=peek; service.backend.playback_consume=consume;
    session.component=component; session.provider_session=17;
    session.handle=UINT64_C(0x1234567800000001); session.next=PXA_AUDIO_SLOT_NONE;
    const pxa_bytes_t empty={NULL,0};
    for(unsigned i=0;i<2;++i) assert(!pxa_event_post_message(runtime,component,99,1,0,empty,1,0));
    uint64_t id;
    assert(!pxa_audio_playback_begin(&queue,17,&id));
    pxa_audio_playback_ready(&queue,id);
    pxa_audio_playback_finish(&queue,id,PXA_AUDIO_PLAYBACK_ENDED,0);
    for(unsigned i=0;i<10;++i) pxa_audio_service_poll(&service);
    assert(queue.count==1 && !queue.records[0].ready_sent);
    pop(runtime,component,1,0,0,0);
    pxa_audio_service_poll(&service);
    assert(queue.count==1 && queue.records[0].ready_sent);
    pop(runtime,component,1,0,0,0);
    pop(runtime,component,PXA_AUDIO_PLAYBACK_EVENT,session.handle,id,PXA_AUDIO_PLAYBACK_READY);
    pxa_audio_service_poll(&service);
    assert(!queue.count);
    pop(runtime,component,PXA_AUDIO_PLAYBACK_EVENT,session.handle,id,PXA_AUDIO_PLAYBACK_ENDED);
    /* A provider slot reused by another session cannot inherit old events. */
    assert(!pxa_audio_playback_begin(&queue,17,&id));
    pxa_audio_playback_finish(&queue,id,PXA_AUDIO_PLAYBACK_ERROR,PXA_STATUS_IO_ERROR);
    session.provider_session=18; session.handle+=UINT64_C(0x100000000);
    pxa_audio_service_poll(&service);
    assert(!queue.count);
    pxa_event_view_t event;
    assert(pxa_event_peek(runtime,component,&event)==PXA_STATUS_WOULD_BLOCK);
    pxa_runtime_deinit(runtime); free(workspace);
    puts("audio service: real event-pool backpressure, ordered reliable READY/terminal and retired provider routing passed");
    return 0;
}
