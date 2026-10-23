#ifndef PXA_AUDIO_GB_PSG_H
#define PXA_AUDIO_GB_PSG_H
#include "pxa/runtime.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Audio 0.13 GB PSG profile 1. Explicit LE state, no native emulator structs.
 * Frame sequencer/registers stay with the caller. See spec/draft/audio.md. */
#define PXA_AUDIO_GB_PSG_HEADER_BYTES 16u
#define PXA_AUDIO_GB_PSG_STATE_BYTES 128u
#define PXA_AUDIO_GB_PSG_MAX_FRAMES 128u
#define PXA_AUDIO_GB_PSG_MAX_NOISE_STEPS 262144u
pxa_status_t pxa_audio_gb_psg_render(const uint8_t *packet, size_t packet_size,
    uint8_t *memory, size_t memory_size);
#ifdef __cplusplus
}
#endif
#endif
