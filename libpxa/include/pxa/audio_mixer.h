#ifndef PXA_AUDIO_MIXER_H
#define PXA_AUDIO_MIXER_H
#include "pxa/audio.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Host-side, allocation-free renderer. Caller serializes access. PCM and
 * tones share a FIFO per voice; different voices advance on one sample clock. */
#define PXA_AUDIO_MIXER_VOICES 3
#define PXA_AUDIO_MIXER_PACKETS 4
#define PXA_AUDIO_MIXER_FRAME 320
#define PXA_AUDIO_MIXER_RATE 16000

typedef struct {
    uint32_t length, position, phase, noise;
    float gain;
    pxa_audio_tone_t tone;
    uint8_t is_tone;
    int16_t pcm[PXA_AUDIO_MIXER_FRAME];
} pxa_audio_mixer_packet_t;
typedef struct {
    float b0, b1, b2, a1, a2, z1, z2;
} pxa_audio_biquad_t;
typedef struct {
    pxa_audio_mixer_packet_t packets[PXA_AUDIO_MIXER_PACKETS];
    pxa_audio_biquad_t eq[PXA_AUDIO_MAX_EQ_BANDS];
    pxa_audio_state_t state;
    float gain;
    uint8_t read, count, committed, eq_count;
} pxa_audio_mixer_voice_t;
typedef struct {
    pxa_audio_mixer_voice_t voices[PXA_AUDIO_MIXER_VOICES];
} pxa_audio_mixer_t;
void pxa_audio_mixer_reset(pxa_audio_mixer_voice_t *voice);
void pxa_audio_mixer_flush(pxa_audio_mixer_voice_t *voice);
pxa_status_t pxa_audio_mixer_commit(pxa_audio_mixer_voice_t *voice,
                                   const pxa_audio_graph_t *graph);
pxa_status_t pxa_audio_mixer_write(pxa_audio_mixer_voice_t *voice,
                                  const uint8_t *pcm, size_t bytes);
pxa_status_t pxa_audio_mixer_tone(pxa_audio_mixer_voice_t *voice,
                                 const pxa_audio_tone_t *tone);
/* Always writes capacity samples (zero padded); returns non-padding extent.
 * accepted_samples counts renderer consumption, not physical DAC playback. */
size_t pxa_audio_mixer_render(pxa_audio_mixer_t *mixer, int16_t *out,
                             size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
