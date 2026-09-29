#ifndef PXA_AUDIO_PLAYBACK_H
#define PXA_AUDIO_PLAYBACK_H
#include "pxa/status.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define PXA_AUDIO_PLAYBACK_READY UINT8_C(1)
#define PXA_AUDIO_PLAYBACK_ENDED UINT8_C(2)
#define PXA_AUDIO_PLAYBACK_STOPPED UINT8_C(3)
#define PXA_AUDIO_PLAYBACK_REPLACED UINT8_C(4)
#define PXA_AUDIO_PLAYBACK_ERROR UINT8_C(5)
#define PXA_AUDIO_PLAYBACK_CAPACITY 8u

typedef struct {
    uint64_t instance;
    uint64_t provider_session;
    pxa_status_t status;
    uint8_t state;
} pxa_audio_playback_event_t;

typedef struct {
    uint64_t instance, provider_session;
    pxa_status_t status;
    uint8_t ready, ready_sent, terminal;
} pxa_audio_playback_record_t;

/* Backend-owned, zero-initialize once for its entire lifetime. Caller holds
 * the audio state mutex for every operation; no allocation, I/O or callback.
 * Each accepted play reserves space for READY plus one terminal notification.
 * READY is optional on failure/stop before preparation. Pending notifications
 * survive runtime event-pool backpressure. Tokens never wrap or reset on exit. */
typedef struct {
    pxa_audio_playback_record_t records[PXA_AUDIO_PLAYBACK_CAPACITY];
    uint64_t next_instance;
    uint8_t count;
} pxa_audio_playback_queue_t;

pxa_status_t pxa_audio_playback_begin(pxa_audio_playback_queue_t *, uint64_t session, uint64_t *instance);
void pxa_audio_playback_ready(pxa_audio_playback_queue_t *, uint64_t instance);
void pxa_audio_playback_finish(pxa_audio_playback_queue_t *, uint64_t instance, uint8_t reason, pxa_status_t status);
pxa_status_t pxa_audio_playback_peek(const pxa_audio_playback_queue_t *, pxa_audio_playback_event_t *);
pxa_status_t pxa_audio_playback_consume(pxa_audio_playback_queue_t *, const pxa_audio_playback_event_t *);
/* Discard only for a rejected command or a session whose authority is gone. */
void pxa_audio_playback_discard(pxa_audio_playback_queue_t *, uint64_t instance);
void pxa_audio_playback_close(pxa_audio_playback_queue_t *, uint64_t session);

#ifdef __cplusplus
}
#endif
#endif
