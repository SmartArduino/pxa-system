#undef NDEBUG
#include "pxa/audio_playback.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    pxa_audio_playback_queue_t q={0};
    uint64_t id, newer;
    pxa_audio_playback_event_t event, saved;
    assert(!pxa_audio_playback_begin(&q,17,&id) && id);
    assert(pxa_audio_playback_peek(&q,&event)==PXA_STATUS_NOT_FOUND);
    pxa_audio_playback_ready(&q,id);
    assert(!pxa_audio_playback_peek(&q,&saved) && saved.state==PXA_AUDIO_PLAYBACK_READY);
    /* Producer completes while consumer has peeked but has not queued READY. */
    pxa_audio_playback_finish(&q,id,PXA_AUDIO_PLAYBACK_ENDED,0);
    for (unsigned i=0;i<20;++i) {
        assert(!pxa_audio_playback_peek(&q,&event));
        assert(event.instance==saved.instance && event.state==saved.state);
    }
    assert(!pxa_audio_playback_consume(&q,&saved));
    assert(pxa_audio_playback_consume(&q,&saved)==PXA_STATUS_BAD_STATE);
    assert(!pxa_audio_playback_peek(&q,&event) && event.state==PXA_AUDIO_PLAYBACK_ENDED);
    assert(!pxa_audio_playback_consume(&q,&event) && !q.count);
    for (unsigned i=0;i<PXA_AUDIO_PLAYBACK_CAPACITY;++i) {
        assert(!pxa_audio_playback_begin(&q,17,&id));
        pxa_audio_playback_finish(&q,id,PXA_AUDIO_PLAYBACK_REPLACED,0);
    }
    assert(pxa_audio_playback_begin(&q,17,&newer)==PXA_STATUS_WOULD_BLOCK && !newer);
    assert(!pxa_audio_playback_peek(&q,&event));
    assert(!pxa_audio_playback_consume(&q,&event));
    assert(!pxa_audio_playback_begin(&q,19,&newer) && newer>id);
    pxa_audio_playback_close(&q,17);
    assert(q.count==1);
    pxa_audio_playback_ready(&q,id); /* retired producer */
    pxa_audio_playback_finish(&q,id,PXA_AUDIO_PLAYBACK_ERROR,PXA_STATUS_IO_ERROR);
    assert(pxa_audio_playback_peek(&q,&event)==PXA_STATUS_NOT_FOUND);
    pxa_audio_playback_finish(&q,newer,PXA_AUDIO_PLAYBACK_ERROR,PXA_STATUS_PROTOCOL_ERROR);
    pxa_audio_playback_ready(&q,newer); /* cannot publish READY after terminal */
    pxa_audio_playback_finish(&q,newer,PXA_AUDIO_PLAYBACK_STOPPED,0);
    assert(!pxa_audio_playback_peek(&q,&event) && event.instance==newer);
    assert(event.state==PXA_AUDIO_PLAYBACK_ERROR && event.status==PXA_STATUS_PROTOCOL_ERROR);
    saved=event;
    pxa_audio_playback_close(&q,19);
    assert(pxa_audio_playback_consume(&q,&saved)==PXA_STATUS_NOT_FOUND);
    assert(!pxa_audio_playback_begin(&q,19,&id) && id>newer);
    pxa_audio_playback_discard(&q,id); assert(!q.count);
    q.next_instance=UINT64_MAX;
    assert(pxa_audio_playback_begin(&q,19,&id)==PXA_STATUS_RESOURCE_LIMIT && !id);
    puts("audio playback: bounded admission, retained READY/terminal, stale completion, close and no token wrap passed");
    return 0;
}
