#ifndef PXA_AUDIO_PSG_H
#define PXA_AUDIO_PSG_H

#include "pxa/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Audio 0.12. Synchronous, integer-only PSG computation in borrowed memory.
 * All records are LE wire bytes, never native structs. See spec/draft/audio.md
 * for the clock, state, waveform and atomic validation contract. */
#define PXA_AUDIO_PSG_HEADER_BYTES 20u
#define PXA_AUDIO_PSG_VOICE_BYTES 40u
#define PXA_AUDIO_PSG_MAX_VOICES 8u
#define PXA_AUDIO_PSG_MAX_FRAMES 4096u
#define PXA_AUDIO_PSG_MAX_WORK 16384u
#define PXA_AUDIO_PSG_MAX_NOISE_STEPS 262144u
#define PXA_AUDIO_PSG_MAX_TICKS 1048576u
#define PXA_AUDIO_PSG_PULSE 1u
#define PXA_AUDIO_PSG_WAVE 2u
#define PXA_AUDIO_PSG_NOISE 3u

pxa_status_t pxa_audio_psg_render(const uint8_t *packet, size_t packet_size,
    uint8_t *memory, size_t memory_size);

#ifdef __cplusplus
}
#endif
#endif
