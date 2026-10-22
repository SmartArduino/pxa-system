#ifndef PXA_AUDIO_MIXER_H
#define PXA_AUDIO_MIXER_H
#include "pxa/audio.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Host-side, allocation-free renderer. Caller serializes access. PCM and
 * legacy tones share a FIFO per voice. Effects run in parallel on the same
 * sample clock, using free FIFO slots rather than allocating another pool. */
#define PXA_AUDIO_MIXER_VOICES 3
#define PXA_AUDIO_MIXER_PACKETS 4
#define PXA_AUDIO_MIXER_FRAME 320
#define PXA_AUDIO_MIXER_RATE 16000
#define PXA_AUDIO_MIXER_MAX_RATE 48000
#define PXA_AUDIO_MIXER_MAX_FRAME 960

typedef struct {
    pxa_audio_tone_t tone;
    uint32_t step;
    float gain, attack_inverse, release_inverse;
} pxa_audio_mixer_layer_t;

enum { PXA_AUDIO_PACKET_PCM, PXA_AUDIO_PACKET_TONE, PXA_AUDIO_PACKET_EFFECT };

typedef struct {
    uint32_t length, position, phase;
    uint32_t noise; /* Noise state, or PCM source index; included in sink rollback. */
    uint32_t source_frames, accounted_frames, source_phase;
    union {
        pxa_audio_tone_t tone;
        struct { uint8_t track, count; } effect;
        struct { int16_t previous; } pcm;
    } meta;
    uint8_t kind;
    uint8_t next; /* FIFO link, PACKETS is the end sentinel. */
    union {
        int16_t pcm[PXA_AUDIO_MIXER_MAX_FRAME];
        struct { uint32_t step; float gain; } oscillator;
        pxa_audio_mixer_layer_t layers[PXA_AUDIO_EFFECT_LAYERS];
    } data;
} pxa_audio_mixer_packet_t;
typedef struct {
    float b0, b1, b2, a1, a2, z1, z2;
} pxa_audio_biquad_t;
typedef struct {
    pxa_audio_mixer_packet_t packets[PXA_AUDIO_MIXER_PACKETS];
    pxa_audio_biquad_t eq[PXA_AUDIO_MAX_EQ_BANDS];
    pxa_audio_state_t state;
    uint32_t source_rate, output_rate, resample_phase;
    uint32_t pending_source_frames; /* Sub-output-period PCM, credited by the next packet. */
    int16_t previous_sample;
    uint8_t channels;
    float gain;
    uint8_t read, count, committed, eq_count;
} pxa_audio_mixer_voice_t;
typedef struct {
    pxa_audio_mixer_voice_t voices[PXA_AUDIO_MIXER_VOICES];
} pxa_audio_mixer_t;
void pxa_audio_mixer_reset(pxa_audio_mixer_voice_t *voice);
/* Configure an idle, uncommitted voice. Submission copies/downmixes S16LE at
 * source rate; the renderer upsamples into the advertised mono sink.
 * No submitted guest pointer survives the call. Only upsampling is supported.
 * State counters measure source frames, independent of channel count. */
pxa_status_t pxa_audio_mixer_configure(pxa_audio_mixer_voice_t *voice,
    uint32_t source_rate, uint8_t channels, uint32_t output_rate);
void pxa_audio_mixer_flush(pxa_audio_mixer_voice_t *voice);
pxa_status_t pxa_audio_mixer_commit(pxa_audio_mixer_voice_t *voice,
                                   const pxa_audio_graph_t *graph);
pxa_status_t pxa_audio_mixer_write(pxa_audio_mixer_voice_t *voice,
                                  const uint8_t *pcm, size_t bytes);
pxa_status_t pxa_audio_mixer_tone(pxa_audio_mixer_voice_t *voice,
                                 const pxa_audio_tone_t *tone);
pxa_status_t pxa_audio_mixer_effect(pxa_audio_mixer_voice_t *voice,
    const pxa_audio_tone_t *tones, size_t count, uint8_t track);
pxa_status_t pxa_audio_mixer_stop_effect(pxa_audio_mixer_voice_t *voice, uint8_t track);
/* Always writes capacity samples (zero padded); returns non-padding extent.
 * accepted_samples counts renderer consumption, not physical DAC playback. */
size_t pxa_audio_mixer_render(pxa_audio_mixer_t *mixer, int16_t *out,
                             size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
