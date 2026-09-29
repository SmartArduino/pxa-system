#include "pxa/audio_playback.h"
#include <string.h>

static pxa_audio_playback_record_t *find(pxa_audio_playback_queue_t *q, uint64_t id) {
    for (unsigned i=0; i<q->count; ++i)
        if (q->records[i].instance == id) return &q->records[i];
    return NULL;
}
static void remove_record(pxa_audio_playback_queue_t *q, unsigned i) {
    --q->count;
    memmove(q->records+i, q->records+i+1, (q->count-i)*sizeof(q->records[0]));
    memset(q->records+q->count,0,sizeof(q->records[0]));
}
pxa_status_t pxa_audio_playback_begin(pxa_audio_playback_queue_t *q, uint64_t session, uint64_t *id) {
    if (!q || !session || !id) return PXA_STATUS_INVALID_ARGUMENT;
    *id=0;
    if (q->next_instance == UINT64_MAX) return PXA_STATUS_RESOURCE_LIMIT;
    if (q->count == PXA_AUDIO_PLAYBACK_CAPACITY) return PXA_STATUS_WOULD_BLOCK;
    pxa_audio_playback_record_t *r=&q->records[q->count++];
    memset(r,0,sizeof(*r));
    r->instance=++q->next_instance; r->provider_session=session;
    *id=r->instance;
    return PXA_STATUS_OK;
}
void pxa_audio_playback_ready(pxa_audio_playback_queue_t *q, uint64_t id) {
    pxa_audio_playback_record_t *r=find(q,id);
    if (r && !r->terminal) r->ready=1;
}
void pxa_audio_playback_finish(pxa_audio_playback_queue_t *q, uint64_t id, uint8_t reason, pxa_status_t status) {
    pxa_audio_playback_record_t *r=find(q,id);
    if (!r || r->terminal || reason<PXA_AUDIO_PLAYBACK_ENDED || reason>PXA_AUDIO_PLAYBACK_ERROR) return;
    if ((reason == PXA_AUDIO_PLAYBACK_ERROR) != (status != PXA_STATUS_OK)) return;
    r->terminal=reason; r->status=status;
}
pxa_status_t pxa_audio_playback_peek(const pxa_audio_playback_queue_t *q, pxa_audio_playback_event_t *event) {
    if (!q || !event) return PXA_STATUS_INVALID_ARGUMENT;
    memset(event,0,sizeof(*event));
    for (unsigned i=0; i<q->count; ++i) {
        const pxa_audio_playback_record_t *r=&q->records[i];
        uint8_t state=r->ready && !r->ready_sent ? PXA_AUDIO_PLAYBACK_READY : r->terminal;
        if (state) {
            event->instance=r->instance; event->provider_session=r->provider_session;
            event->state=state; event->status=state==PXA_AUDIO_PLAYBACK_READY ? PXA_STATUS_OK : r->status;
            return PXA_STATUS_OK;
        }
    }
    return PXA_STATUS_NOT_FOUND;
}
pxa_status_t pxa_audio_playback_consume(pxa_audio_playback_queue_t *q, const pxa_audio_playback_event_t *event) {
    if (!q || !event) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_audio_playback_record_t *r=find(q,event->instance);
    if (!r || r->provider_session!=event->provider_session) return PXA_STATUS_NOT_FOUND;
    uint8_t state=r->ready && !r->ready_sent ? PXA_AUDIO_PLAYBACK_READY : r->terminal;
    pxa_status_t status=state==PXA_AUDIO_PLAYBACK_READY ? PXA_STATUS_OK : r->status;
    if (!state || state!=event->state || status!=event->status) return PXA_STATUS_BAD_STATE;
    if (state==PXA_AUDIO_PLAYBACK_READY) r->ready_sent=1;
    else remove_record(q,(unsigned)(r-q->records));
    return PXA_STATUS_OK;
}
void pxa_audio_playback_discard(pxa_audio_playback_queue_t *q, uint64_t id) {
    pxa_audio_playback_record_t *r=find(q,id);
    if (r) remove_record(q,(unsigned)(r-q->records));
}
void pxa_audio_playback_close(pxa_audio_playback_queue_t *q, uint64_t session) {
    for (unsigned i=0; i<q->count;)
        if (q->records[i].provider_session==session) remove_record(q,i);
        else ++i;
}
