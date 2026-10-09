#ifndef PXA_AUDIO_SOUND_H
#define PXA_AUDIO_SOUND_H

#include "pxa/audio.h"
#include "pxa/wire.h"
#include <string.h>

/* All operations are serialized by the physical sink. Resident PCM only:
 * no allocation, decoding, file access or Guest callback on the audio task. */
typedef struct {
    pxa_asset_object_t *asset;
    const uint8_t *pcm;
    uint32_t samples, position;
    int32_t gain_q15, target_gain_q15;
    int32_t last_output, replacement_tail;
    uint64_t voice;
    uint16_t attack_remaining, replacement_remaining;
    uint8_t encoding, track, loop, paused, stopping;
} pxa_audio_sound_voice_t;

/* Leave ordinary levels unchanged; overlapping effects approach full scale
 * smoothly instead of creating flat-topped peaks through hard clipping. */
static inline int16_t pxa_audio_output_limit(int32_t mixed) {
    const int32_t knee=28800, range=32767-knee;
    int64_t magnitude=mixed<0 ? -(int64_t)mixed : mixed;
    if (magnitude>knee)
        magnitude=knee+range-(int64_t)range*range/(magnitude-knee+range);
    return (int16_t)(mixed<0 ? -magnitude : magnitude);
}

static inline void pxa_audio_sound_release(pxa_audio_sound_voice_t *sound) {
    if (sound->asset) pxa_asset_object_release_pinned(sound->asset);
    memset(sound, 0, sizeof(*sound));
}

static inline int pxa_audio_sound_start(pxa_audio_sound_voice_t *sound,
    pxa_asset_object_t *asset, uint64_t voice, uint8_t track, int loop,
    int32_t gain_q15) {
    pxa_asset_object_view_t view;
    pxa_asset_object_view(asset, &view);
    if (view.kind != PXA_ASSET_AUDIO || !view.data || !view.bytes ||
        (view.encoding != PXA_ASSET_ENCODING_PCM_U8_16K_MONO &&
         view.encoding != PXA_ASSET_ENCODING_PCM_S16LE_16K_MONO)) return 0;
    int32_t tail = sound->last_output;
    int occupied = sound->asset != NULL;
    pxa_asset_object_retain(asset);
    pxa_audio_sound_release(sound);
    sound->asset = asset; sound->pcm = view.data;
    sound->samples = view.bytes / (view.encoding == PXA_ASSET_ENCODING_PCM_S16LE_16K_MONO ? 2u : 1u);
    sound->encoding = view.encoding;
    sound->voice = voice; sound->track = track; sound->loop = loop != 0;
    sound->gain_q15 = sound->target_gain_q15 = gain_q15;
    sound->attack_remaining = 64;
    if (occupied) { sound->replacement_tail = tail; sound->replacement_remaining = 64; }
    return 1;
}

static inline int32_t pxa_audio_sound_render(pxa_audio_sound_voice_t *sound) {
    if (!sound->asset || sound->paused) return 0;
    int32_t delta = sound->target_gain_q15 - sound->gain_q15;
    if (delta > 512) delta = 512;
    if (delta < -512) delta = -512;
    sound->gain_q15 += delta;
    if (sound->stopping && sound->gain_q15 == 0) {
        pxa_audio_sound_release(sound);
        return 0;
    }
    int32_t sample = sound->encoding == PXA_ASSET_ENCODING_PCM_S16LE_16K_MONO
        ? (int16_t)pxa_read_u16(sound->pcm + sound->position * 2u)
        : ((int32_t)sound->pcm[sound->position] - 128) * 256;
    uint32_t envelope = 64;
    if (sound->attack_remaining) envelope = 65u - sound->attack_remaining--;
    if (!sound->loop && sound->samples - sound->position < envelope)
        envelope = sound->samples - sound->position;
    int32_t mixed = (int32_t)(((int64_t)sample * sound->gain_q15) >> 15) * (int32_t)envelope / 64;
    if (sound->replacement_remaining) {
        mixed += sound->replacement_tail * sound->replacement_remaining-- / 64;
    }
    sound->last_output = mixed;
    if (++sound->position == sound->samples) {
        if (sound->loop) sound->position = 0;
        else pxa_audio_sound_release(sound);
    }
    return mixed;
}

static inline void pxa_audio_sound_control(pxa_audio_sound_voice_t *sound,
    uint8_t action, int32_t gain_q15) {
    if (action == PXA_AUDIO_ASSET_PAUSE) sound->paused = 1;
    if (action == PXA_AUDIO_ASSET_RESUME) sound->paused = 0;
    if (action == PXA_AUDIO_ASSET_STOP) {
        sound->stopping = 1; sound->target_gain_q15 = 0;
        // Paused clips have no in-flight waveform to fade.
        if (sound->paused) pxa_audio_sound_release(sound);
    }
    if (action == PXA_AUDIO_ASSET_SET_GAIN) sound->target_gain_q15 = gain_q15;
}

#endif
