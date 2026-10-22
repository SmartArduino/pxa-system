#include "pxa/audio_mixer.h"
#include <math.h>
#include <string.h>
#define PI 3.14159265358979323846f
void pxa_audio_mixer_reset(pxa_audio_mixer_voice_t *v) {
    memset(v, 0, sizeof(*v));
    v->read = PXA_AUDIO_MIXER_PACKETS;
    v->gain = 1.0f;
    v->source_rate = v->output_rate = PXA_AUDIO_MIXER_RATE;
    v->channels = 1;
    v->state.flags = PXA_AUDIO_STATE_ACCEPTED_IS_SINK_SUBMITTED;
}
static uint32_t frames_ms(uint32_t rate, uint32_t ms) {
    return rate * ms / 1000u;
}
pxa_status_t pxa_audio_mixer_configure(pxa_audio_mixer_voice_t *v,
    uint32_t source_rate, uint8_t channels, uint32_t output_rate) {
    if (!v || source_rate < 8000 || source_rate > PXA_AUDIO_MIXER_MAX_RATE ||
        output_rate < 8000 || output_rate > PXA_AUDIO_MIXER_MAX_RATE || (channels != 1 && channels != 2))
        return PXA_STATUS_INVALID_ARGUMENT;
    if (v->committed || v->count) return PXA_STATUS_BAD_STATE;
    v->source_rate = source_rate; v->output_rate = output_rate;
    v->channels = channels; v->resample_phase = 0; v->pending_source_frames = 0; v->previous_sample = 0;
    return PXA_STATUS_OK;
}
void pxa_audio_mixer_flush(pxa_audio_mixer_voice_t *v) {
    v->read = PXA_AUDIO_MIXER_PACKETS;
    v->count = 0;
    v->resample_phase = 0; v->pending_source_frames = 0; v->previous_sample = 0;
    for (unsigned i = 0; i < PXA_AUDIO_MIXER_PACKETS; ++i)
        v->packets[i].position = v->packets[i].length;
    v->state.queued_samples = 0;
    for (unsigned i = 0; i < v->eq_count; ++i)
        v->eq[i].z1 = v->eq[i].z2 = 0;
}
pxa_status_t pxa_audio_mixer_commit(pxa_audio_mixer_voice_t *v,
                                   const pxa_audio_graph_t *g) {
    if (!v || !g || g->route != PXA_AUDIO_ROUTE_SPEAKER ||
        g->eq_band_count > PXA_AUDIO_MAX_EQ_BANDS ||
        g->gain_db_q8 < -48 * 256 || g->gain_db_q8 > 12 * 256)
        return PXA_STATUS_INVALID_ARGUMENT;
    pxa_audio_biquad_t filters[PXA_AUDIO_MAX_EQ_BANDS] = {{0}};
    for (unsigned i = 0; i < g->eq_band_count; ++i) {
        const pxa_audio_eq_band_t *b = &g->eq_bands[i];
        if (b->frequency_hz < 20 || b->frequency_hz * 2u >= (v->source_rate<v->output_rate?v->source_rate:v->output_rate) ||
            b->q_q8 < 64 || b->q_q8 > 4096 ||
            b->gain_db_q8 < -12 * 256 || b->gain_db_q8 > 12 * 256)
            return PXA_STATUS_INVALID_ARGUMENT;
        const float a = powf(10.0f, b->gain_db_q8 / 10240.0f);
        const float w = 2 * PI * b->frequency_hz / v->output_rate;
        const float alpha = sinf(w) / (2 * (b->q_q8 / 256.0f));
        const float a0 = 1 + alpha / a;
        filters[i].b0 = (1 + alpha * a) / a0;
        filters[i].b1 = -2 * cosf(w) / a0;
        filters[i].b2 = (1 - alpha * a) / a0;
        filters[i].a1 = filters[i].b1;
        filters[i].a2 = (1 - alpha / a) / a0;
    }
    memcpy(v->eq, filters, sizeof(filters));
    v->eq_count = g->eq_band_count;
    v->gain = powf(10.0f, g->gain_db_q8 / 5120.0f);
    v->committed = 1;
    return PXA_STATUS_OK;
}
static pxa_audio_mixer_packet_t *next_packet(pxa_audio_mixer_voice_t *v) {
    for (unsigned i = 0; i < PXA_AUDIO_MIXER_PACKETS; ++i)
        if (v->packets[i].position == v->packets[i].length) return &v->packets[i];
    return NULL;
}
static void publish(pxa_audio_mixer_voice_t *v, pxa_audio_mixer_packet_t *p, uint32_t credited_frames) {
    ++v->count;
    p->accounted_frames = p->source_phase = 0;
    v->state.submitted_samples += credited_frames;
    v->state.queued_samples += credited_frames;
    if (p->kind == PXA_AUDIO_PACKET_EFFECT) return;
    const uint8_t index = (uint8_t)(p - v->packets);
    p->next = PXA_AUDIO_MIXER_PACKETS;
    if (v->read == PXA_AUDIO_MIXER_PACKETS) v->read = index;
    else {
        unsigned tail = v->read;
        while (v->packets[tail].next != PXA_AUDIO_MIXER_PACKETS)
            tail = v->packets[tail].next;
        v->packets[tail].next = index;
    }
}
static int valid_tone(const pxa_audio_tone_t *t) {
    return t && t->waveform <= PXA_AUDIO_TONE_NOISE &&
        t->frequency_hz >= 40 && t->frequency_hz <= 8000 &&
        t->duration_ms >= 10 && t->duration_ms <= 1000 && t->delay_ms <= 1000 &&
        t->attack_ms <= t->duration_ms && t->release_ms <= t->duration_ms &&
        t->gain_db_q8 >= -60 * 256 && t->gain_db_q8 <= 0;
}
static int16_t pcm_sample(const uint8_t *pcm) {
    /* Signed little endian, including on big endian hosts. */
    const uint16_t bits = (uint16_t)pcm[0] | (uint16_t)pcm[1] << 8;
    return bits <= INT16_MAX ? (int16_t)bits : (int16_t)((int32_t)bits - 65536);
}
pxa_status_t pxa_audio_mixer_write(pxa_audio_mixer_voice_t *v,
                                  const uint8_t *pcm, size_t bytes) {
    if (!v || !pcm || !bytes || !v->channels ||
        bytes % (v->channels * 2u)) return PXA_STATUS_INVALID_ARGUMENT;
    const size_t frames = bytes / (v->channels * 2u);
    if (frames > frames_ms(v->source_rate, 20)) return PXA_STATUS_INVALID_ARGUMENT;
    if (!v->committed) return PXA_STATUS_BAD_STATE;
    if (v->count == PXA_AUDIO_MIXER_PACKETS) return PXA_STATUS_WOULD_BLOCK;
    if(v->source_rate>v->output_rate){
        uint32_t queued=v->pending_source_frames;
        for(unsigned i=0;i<PXA_AUDIO_MIXER_PACKETS;++i){
            const pxa_audio_mixer_packet_t *p=&v->packets[i];
            if(p->kind==PXA_AUDIO_PACKET_PCM && p->position<p->length)
                queued+=p->source_frames-p->accounted_frames;
        }
        if(queued+frames>PXA_AUDIO_MIXER_PACKETS*frames_ms(v->source_rate,20))
            return PXA_STATUS_WOULD_BLOCK;
    }
    const uint32_t length = (frames * v->output_rate + v->resample_phase) / v->source_rate;
    if (length > PXA_AUDIO_MIXER_MAX_FRAME) return PXA_STATUS_INVALID_ARGUMENT;
    if (!length) {
        /* A short downsampling write need not produce an output frame. Carry
         * its fractional clock and last sample without publishing an empty
         * packet; the next real packet accounts these source frames. */
        int32_t last=pcm_sample(pcm+(frames-1)*v->channels*2u);
        if(v->channels==2)last=(last+pcm_sample(pcm+(frames-1)*4u+2))/2;
        v->previous_sample=(int16_t)last;
        v->resample_phase+=frames*v->output_rate;
        v->pending_source_frames+=(uint32_t)frames;
        v->state.submitted_samples+=frames;v->state.queued_samples+=(uint32_t)frames;
        return PXA_STATUS_OK;
    }
    pxa_audio_mixer_packet_t *p = next_packet(v);
    p->kind = PXA_AUDIO_PACKET_PCM; p->length = length; p->position = 0;
    p->source_frames = (uint32_t)frames+v->pending_source_frames;
    v->pending_source_frames=0;
    /* Keep compact source-rate PCM in the existing packet storage. Expansion
     * belongs to the audio consumer, not the caller's game/render thread.
     * phase/noise reuse the oscillator state, including its existing
     * sink-retry checkpoint; the immutable previous sample bridges packets. */
    p->phase = v->resample_phase + v->output_rate;
    p->noise = 0;
    p->meta.pcm.previous = v->previous_sample;
    for (size_t i = 0; i < frames; ++i) {
        int32_t current = pcm_sample(pcm + i * v->channels * 2u);
        if (v->channels == 2)
            current = (current + pcm_sample(pcm + i * 4u + 2)) / 2;
        p->data.pcm[i] = (int16_t)current;
    }
    v->resample_phase = (frames * v->output_rate + v->resample_phase) % v->source_rate;
    v->previous_sample = p->data.pcm[frames - 1];
    publish(v, p, (uint32_t)frames);
    return PXA_STATUS_OK;
}
static int16_t interpolate_pcm(int32_t previous, int32_t current,
                                uint32_t phase, uint32_t output_rate) {
    const int32_t delta = current - previous;
    /* Same interpolation and signed truncation as eager expansion. The
     * full-scale product fits unsigned 32 bits for the supported rates. */
    const uint32_t magnitude = (uint32_t)(delta < 0 ? -delta : delta);
    const int32_t offset = (int32_t)(magnitude * (output_rate - phase) / output_rate);
    return (int16_t)(previous + (delta < 0 ? -offset : offset));
}
static int16_t render_pcm(pxa_audio_mixer_packet_t *p, uint32_t source_rate,
                         uint32_t output_rate) {
    if (source_rate == output_rate) return p->data.pcm[p->position];
    uint32_t source_index = p->noise;
    while (p->phase < source_rate) {
        p->phase += output_rate;
        p->noise = ++source_index;
    }
    p->phase -= source_rate;
    return interpolate_pcm(source_index ? p->data.pcm[source_index - 1] :
        p->meta.pcm.previous, p->data.pcm[source_index], p->phase, output_rate);
}
pxa_status_t pxa_audio_mixer_tone(pxa_audio_mixer_voice_t *v,
                                 const pxa_audio_tone_t *t) {
    if (!v || !valid_tone(t)) return PXA_STATUS_INVALID_ARGUMENT;
    if (!v->committed) return PXA_STATUS_BAD_STATE;
    if (v->count == PXA_AUDIO_MIXER_PACKETS) return PXA_STATUS_WOULD_BLOCK;
    pxa_audio_mixer_packet_t *p = next_packet(v);
    p->kind = PXA_AUDIO_PACKET_TONE; p->meta.tone = *t; p->position = p->phase = 0;
    p->noise = 0x9e3779b9u;
    p->data.oscillator.step = (uint32_t)(((uint64_t)t->frequency_hz << 32) / v->output_rate);
    p->length = frames_ms(v->output_rate, t->duration_ms) + frames_ms(v->output_rate, t->delay_ms);
    p->source_frames = frames_ms(v->source_rate, t->duration_ms + t->delay_ms);
    p->data.oscillator.gain = powf(10.0f, t->gain_db_q8 / 5120.0f);
    publish(v, p, p->source_frames);
    return PXA_STATUS_OK;
}
pxa_status_t pxa_audio_mixer_effect(pxa_audio_mixer_voice_t *v,
    const pxa_audio_tone_t *tones, size_t count, uint8_t track) {
    if (!v || !tones || !count || count > PXA_AUDIO_EFFECT_LAYERS ||
        (track != PXA_AUDIO_SOUND_APPEND && track >= PXA_AUDIO_SOUND_TRACKS))
        return PXA_STATUS_INVALID_ARGUMENT;
    for (size_t i = 0; i < count; ++i)
        if (!valid_tone(&tones[i])) return PXA_STATUS_INVALID_ARGUMENT;
    if (!v->committed) return PXA_STATUS_BAD_STATE;
    pxa_audio_mixer_packet_t *p = NULL;
    if (track != PXA_AUDIO_SOUND_APPEND)
        for (unsigned i = 0; i < PXA_AUDIO_MIXER_PACKETS; ++i)
            if (v->packets[i].position < v->packets[i].length &&
                v->packets[i].kind == PXA_AUDIO_PACKET_EFFECT && v->packets[i].meta.effect.track == track)
                p = &v->packets[i];
    if (p) {
        v->state.queued_samples -= p->source_frames - p->accounted_frames;
        --v->count;
    } else p = next_packet(v);
    if (!p) return PXA_STATUS_WOULD_BLOCK;
    p->kind = PXA_AUDIO_PACKET_EFFECT; p->meta.effect.track = track; p->meta.effect.count = (uint8_t)count;
    p->length = p->position = 0;
    for (size_t i = 0; i < count; ++i) {
        const pxa_audio_tone_t *t = &tones[i];
        pxa_audio_mixer_layer_t *layer = &p->data.layers[i];
        layer->tone = *t;
        layer->step = (uint32_t)(((uint64_t)t->frequency_hz << 32) / v->output_rate);
        layer->gain = powf(10.f, t->gain_db_q8 / 5120.f);
        layer->attack_inverse = t->attack_ms ? 1.f / frames_ms(v->output_rate, t->attack_ms) : 0;
        layer->release_inverse = t->release_ms ? 1.f / frames_ms(v->output_rate, t->release_ms) : 0;
        const uint32_t length = frames_ms(v->output_rate, t->duration_ms) + frames_ms(v->output_rate, t->delay_ms);
        if (length > p->length) {
            p->length = length;
            p->source_frames = frames_ms(v->source_rate, t->duration_ms + t->delay_ms);
        }
    }
    publish(v, p, p->source_frames);
    return PXA_STATUS_OK;
}
pxa_status_t pxa_audio_mixer_stop_effect(pxa_audio_mixer_voice_t *v, uint8_t track) {
    if (!v || track >= PXA_AUDIO_SOUND_TRACKS) return PXA_STATUS_INVALID_ARGUMENT;
    if (!v->committed) return PXA_STATUS_BAD_STATE;
    for (unsigned i = 0; i < PXA_AUDIO_MIXER_PACKETS; ++i) {
        pxa_audio_mixer_packet_t *p = &v->packets[i];
        if (p->position < p->length && p->kind == PXA_AUDIO_PACKET_EFFECT && p->meta.effect.track == track) {
            v->state.queued_samples -= p->source_frames - p->accounted_frames;
            p->position = p->length;
            --v->count;
        }
    }
    return PXA_STATUS_OK;
}
/* Caller places this bounded PCM workspace in external memory on ESP. */
typedef char mixer_memory_is_bounded[sizeof(pxa_audio_mixer_t) <= 24576 ? 1 : -1];
typedef char effects_reuse_pcm_storage[sizeof(((pxa_audio_mixer_packet_t *)0)->data.layers) <=
    sizeof(((pxa_audio_mixer_packet_t *)0)->data.pcm) ? 1 : -1];
/* Linear interpolation avoids a transcendental call for every sample. */
static const int16_t sine_table[257] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739, 9512, 10278, 11039, 11793,
    12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530, 18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
    32767, 32757, 32728, 32678, 32609, 32521, 32412, 32285, 32137, 31971, 31785, 31580, 31356, 31113, 30852, 30571,
    30273, 29956, 29621, 29268, 28898, 28510, 28105, 27683, 27245, 26790, 26319, 25832, 25329, 24811, 24279, 23731,
    23170, 22594, 22005, 21403, 20787, 20159, 19519, 18868, 18204, 17530, 16846, 16151, 15446, 14732, 14010, 13279,
    12539, 11793, 11039, 10278, 9512, 8739, 7962, 7179, 6393, 5602, 4808, 4011, 3212, 2410, 1608, 804,
    0, -804, -1608, -2410, -3212, -4011, -4808, -5602, -6393, -7179, -7962, -8739, -9512, -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530, -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
    -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790, -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
    -30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971, -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
    -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285, -32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
    -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683, -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
    -23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868, -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
    -12539, -11793, -11039, -10278, -9512, -8739, -7962, -7179, -6393, -5602, -4808, -4011, -3212, -2410, -1608, -804,
    0
};
static float tone_sample(pxa_audio_mixer_packet_t *p, uint32_t rate) {
    const pxa_audio_tone_t *t = &p->meta.tone;
    const uint32_t delay = frames_ms(rate, t->delay_ms);
    if (p->position < delay) return 0;
    const uint32_t pos = p->position - delay;
    const uint32_t remaining = frames_ms(rate, t->duration_ms) - pos - 1;
    float envelope = 1, value;
    if (t->attack_ms && pos < frames_ms(rate, t->attack_ms))
        envelope = (float)pos / (frames_ms(rate, t->attack_ms));
    if (t->release_ms && remaining < frames_ms(rate, t->release_ms)) {
        const float release = (float)remaining / (frames_ms(rate, t->release_ms));
        if (release < envelope) envelope = release;
    }
    const float phase = (p->phase >> 8) / 16777216.0f;
    switch (t->waveform) {
        case PXA_AUDIO_TONE_SQUARE: value = phase < .5f ? -1.f : 1.f; break;
        case PXA_AUDIO_TONE_TRIANGLE: value = 1.f - 4.f * fabsf(phase - .5f); break;
        case PXA_AUDIO_TONE_NOISE:
            p->noise = p->noise * 1664525u + 1013904223u;
            value = (int16_t)(p->noise >> 16) / 32768.f; break;
        default: {
            const unsigned index = p->phase >> 24;
            const float fraction = (p->phase & 0xffffffu) / 16777216.f;
            value = (sine_table[index] +
                (sine_table[index + 1] - sine_table[index]) * fraction) / 32767.f;
            break;
        }
    }
    p->phase += p->data.oscillator.step;
    return value * 32767.f * envelope * p->data.oscillator.gain;
}
/* Position-derived oscillators need no per-layer mutable state or rollback
 * buffer. Integer hash noise is repeatable across render block boundaries. */
static float effect_sample(const pxa_audio_mixer_packet_t *p, uint32_t rate) {
    float mixed = 0;
    for (unsigned i = 0; i < p->meta.effect.count; ++i) {
        const pxa_audio_mixer_layer_t *layer = &p->data.layers[i];
        const pxa_audio_tone_t *t = &layer->tone;
        const uint32_t start = frames_ms(rate, t->delay_ms), duration = frames_ms(rate, t->duration_ms);
        if (p->position < start || p->position - start >= duration) continue;
        const uint32_t pos = p->position - start, remaining = duration - pos - 1;
        float envelope = 1, value;
        if (t->attack_ms && pos < frames_ms(rate, t->attack_ms))
            envelope = pos * layer->attack_inverse;
        if (t->release_ms && remaining < frames_ms(rate, t->release_ms)) {
            const float release = remaining * layer->release_inverse;
            if (release < envelope) envelope = release;
        }
        const uint32_t phase = pos * layer->step;
        switch (t->waveform) {
            case PXA_AUDIO_TONE_SQUARE: value = phase < 0x80000000u ? -1.f : 1.f; break;
            case PXA_AUDIO_TONE_TRIANGLE:
                value = 1.f - 4.f * fabsf((phase >> 8) / 16777216.f - .5f); break;
            case PXA_AUDIO_TONE_NOISE: {
                uint32_t noise = pos + 0x9e3779b9u + i * 0x85ebca6bu;
                noise = (noise ^ (noise >> 16)) * 0x7feb352du;
                noise = (noise ^ (noise >> 15)) * 0x846ca68bu;
                value = (int16_t)((noise ^ (noise >> 16)) >> 16) / 32768.f;
                break;
            }
            default: {
                const unsigned index = phase >> 24;
                const float fraction = (phase & 0xffffffu) / 16777216.f;
                value = (sine_table[index] +
                    (sine_table[index + 1] - sine_table[index]) * fraction) / 32767.f;
                break;
            }
        }
        mixed += value * 32767.f * envelope * layer->gain;
    }
    return mixed;
}
static void account_frame(pxa_audio_mixer_voice_t *v, pxa_audio_mixer_packet_t *p) {
    p->source_phase += p->source_frames;
    while (p->source_phase >= p->length) {
        p->source_phase -= p->length;
        ++p->accounted_frames;
        --v->state.queued_samples;
        ++v->state.accepted_samples;
    }
}
/* Games normally submit one PCM voice without EQ/effects. Account source
 * frames once per packet fragment instead of updating 64-bit counters for
 * every device sample. Tone/EQ/multi-voice cases keep the general mixer. */
static size_t render_pcm_prefix(pxa_audio_mixer_voice_t *v, int16_t *out, size_t n) {
    size_t written = 0;
    const uint32_t source_rate = v->source_rate, output_rate = v->output_rate;
    const float gain = v->gain;
    while (written < n && v->read != PXA_AUDIO_MIXER_PACKETS) {
        pxa_audio_mixer_packet_t *p = &v->packets[v->read];
        if (p->kind != PXA_AUDIO_PACKET_PCM) break;
        size_t count = p->length - p->position;
        if (count > n - written) count = n - written;
        if (source_rate == output_rate) {
            for (size_t i = 0; i < count; ++i) {
                const float sample = p->data.pcm[p->position + i] * gain;
                out[written++] = sample > 32767 ? 32767 :
                                 sample < -32768 ? -32768 : (int16_t)sample;
            }
        } else {
            /* Packet storage is PSRAM on ESP. Keep the hot cursor and the
             * current source pair local for this block; touch source PCM only
             * when advancing, and publish the mutable cursor once at the end. */
            uint32_t phase = p->phase, index = p->noise;
            int32_t current = p->data.pcm[index];
            int32_t previous = index ? p->data.pcm[index - 1] : p->meta.pcm.previous;
            for (size_t i = 0; i < count; ++i) {
                while (phase < source_rate) {
                    phase += output_rate;
                    previous = current;
                    current = p->data.pcm[++index];
                }
                phase -= source_rate;
                const float sample = interpolate_pcm(previous, current, phase, output_rate) * gain;
                out[written++] = sample > 32767 ? 32767 :
                                 sample < -32768 ? -32768 : (int16_t)sample;
            }
            p->phase = phase;
            p->noise = index;
        }
        p->position += (uint32_t)count;
        const uint32_t phase = p->source_phase + (uint32_t)count * p->source_frames;
        const uint32_t frames = phase / p->length;
        p->source_phase = phase % p->length;
        p->accounted_frames += frames;
        v->state.queued_samples -= frames;
        v->state.accepted_samples += frames;
        if (p->position == p->length) { v->read = p->next; --v->count; }
    }
    return written;
}
size_t pxa_audio_mixer_render(pxa_audio_mixer_t *m, int16_t *out, size_t n) {
    size_t extent = 0;
    uint8_t effects[PXA_AUDIO_MIXER_VOICES] = {0};
    pxa_audio_mixer_voice_t *active[PXA_AUDIO_MIXER_VOICES];
    unsigned active_count = 0;
    for (unsigned j = 0; j < PXA_AUDIO_MIXER_VOICES; ++j) {
        pxa_audio_mixer_voice_t *v = &m->voices[j];
        int live = v->count != 0;
        for (unsigned k = 0; k < v->eq_count; ++k)
            live |= v->eq[k].z1 != 0 || v->eq[k].z2 != 0;
        if (!live) continue;
        active[active_count] = v;
        for (unsigned slot = 0; slot < PXA_AUDIO_MIXER_PACKETS; ++slot) {
            const pxa_audio_mixer_packet_t *p = &v->packets[slot];
            if (p->kind == PXA_AUDIO_PACKET_EFFECT && p->position < p->length)
                effects[active_count] |= 1u << slot;
        }
        ++active_count;
    }
    if (!active_count) { memset(out, 0, n * sizeof(*out)); return 0; }
    size_t start = 0;
    if (active_count == 1 && !effects[0] && !active[0]->eq_count) {
        start = render_pcm_prefix(active[0], out, n);
        extent = start;
        if (!active[0]->count) {
            memset(out + start, 0, (n - start) * sizeof(*out));
            return extent;
        }
    }
    for (size_t i = start; i < n; ++i) {
        float mixed = 0;
        for (unsigned j = 0; j < active_count; ++j) {
            pxa_audio_mixer_voice_t *v = active[j];
            float sample = 0;
            if (v->count && v->read != PXA_AUDIO_MIXER_PACKETS) {
                pxa_audio_mixer_packet_t *p = &v->packets[v->read];
                sample = p->kind ? tone_sample(p, v->output_rate) :
                    render_pcm(p, v->source_rate, v->output_rate);
                if (++p->position == p->length) {
                    v->read = p->next;
                    --v->count;
                }
                account_frame(v, p);
                extent = i + 1;
            }
            if (effects[j]) for (unsigned slot = 0; slot < PXA_AUDIO_MIXER_PACKETS; ++slot) {
                pxa_audio_mixer_packet_t *p = &v->packets[slot];
                if (!(effects[j] & (1u << slot))) continue;
                sample += effect_sample(p, v->output_rate);
                if (++p->position == p->length) {
                    --v->count;
                    effects[j] &= ~(1u << slot);
                }
                account_frame(v, p);
                extent = i + 1;
            }
            for (unsigned k = 0; k < v->eq_count; ++k) {
                pxa_audio_biquad_t *b = &v->eq[k];
                float y = b->b0 * sample + b->z1;
                b->z1 = b->b1 * sample - b->a1 * y + b->z2;
                b->z2 = b->b2 * sample - b->a2 * y;
                /* Bound denormal filter tails on targets without FTZ. */
                if (fabsf(b->z1) < 1e-12f) b->z1 = 0;
                if (fabsf(b->z2) < 1e-12f) b->z2 = 0;
                sample = y;
            }
            mixed += sample * v->gain;
        }
        out[i] = mixed > 32767 ? 32767 : mixed < -32768 ? -32768 : (int16_t)mixed;
        if (out[i]) extent = i + 1;
    }
    return extent;
}
