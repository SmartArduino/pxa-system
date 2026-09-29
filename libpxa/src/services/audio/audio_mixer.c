#include "pxa/audio_mixer.h"
#include <math.h>
#include <string.h>
#define PI 3.14159265358979323846f
void pxa_audio_mixer_reset(pxa_audio_mixer_voice_t *v) {
    memset(v, 0, sizeof(*v));
    v->gain = 1.0f;
    v->state.flags = PXA_AUDIO_STATE_ACCEPTED_IS_SINK_SUBMITTED;
}
void pxa_audio_mixer_flush(pxa_audio_mixer_voice_t *v) {
    v->read = v->count = 0;
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
        if (b->frequency_hz < 20 || b->frequency_hz >= PXA_AUDIO_MIXER_RATE / 2 ||
            b->q_q8 < 64 || b->q_q8 > 4096 ||
            b->gain_db_q8 < -12 * 256 || b->gain_db_q8 > 12 * 256)
            return PXA_STATUS_INVALID_ARGUMENT;
        const float a = powf(10.0f, b->gain_db_q8 / 10240.0f);
        const float w = 2 * PI * b->frequency_hz / PXA_AUDIO_MIXER_RATE;
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
    return &v->packets[(v->read + v->count) % PXA_AUDIO_MIXER_PACKETS];
}
static void publish(pxa_audio_mixer_voice_t *v, uint32_t samples) {
    ++v->count;
    v->state.submitted_samples += samples;
    v->state.queued_samples += samples;
}
pxa_status_t pxa_audio_mixer_write(pxa_audio_mixer_voice_t *v,
                                  const uint8_t *pcm, size_t bytes) {
    if (!v || !pcm || !bytes || bytes % 2 || bytes > PXA_AUDIO_MIXER_FRAME * 2)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (!v->committed) return PXA_STATUS_BAD_STATE;
    if (v->count == PXA_AUDIO_MIXER_PACKETS) return PXA_STATUS_WOULD_BLOCK;
    pxa_audio_mixer_packet_t *p = next_packet(v);
    p->is_tone = 0; p->length = (uint32_t)bytes / 2; p->position = 0;
    /* Wire PCM is signed little endian, including on big endian hosts. */
    for (size_t i = 0; i < bytes / 2; ++i)
        p->pcm[i] = (int16_t)((uint16_t)pcm[i * 2] | (uint16_t)pcm[i * 2 + 1] << 8);
    publish(v, p->length);
    return PXA_STATUS_OK;
}
pxa_status_t pxa_audio_mixer_tone(pxa_audio_mixer_voice_t *v,
                                 const pxa_audio_tone_t *t) {
    if (!v || !t || t->waveform > PXA_AUDIO_TONE_NOISE ||
        t->frequency_hz < 40 || t->frequency_hz > 8000 ||
        t->duration_ms < 10 || t->duration_ms > 1000 || t->delay_ms > 1000 ||
        t->attack_ms > t->duration_ms || t->release_ms > t->duration_ms ||
        t->gain_db_q8 < -60 * 256 || t->gain_db_q8 > 0)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (!v->committed) return PXA_STATUS_BAD_STATE;
    if (v->count == PXA_AUDIO_MIXER_PACKETS) return PXA_STATUS_WOULD_BLOCK;
    pxa_audio_mixer_packet_t *p = next_packet(v);
    p->is_tone = 1; p->tone = *t; p->position = p->phase = 0;
    p->noise = 0x9e3779b9u;
    p->length = (t->duration_ms + t->delay_ms) * 16u;
    p->gain = powf(10.0f, t->gain_db_q8 / 5120.0f);
    publish(v, p->length);
    return PXA_STATUS_OK;
}
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
static float tone_sample(pxa_audio_mixer_packet_t *p) {
    const pxa_audio_tone_t *t = &p->tone;
    const uint32_t delay = t->delay_ms * 16u;
    if (p->position < delay) return 0;
    const uint32_t pos = p->position - delay;
    const uint32_t remaining = t->duration_ms * 16u - pos - 1;
    float envelope = 1, value;
    if (t->attack_ms && pos < t->attack_ms * 16u)
        envelope = (float)pos / (t->attack_ms * 16u);
    if (t->release_ms && remaining < t->release_ms * 16u) {
        const float release = (float)remaining / (t->release_ms * 16u);
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
    p->phase += (uint32_t)(((uint64_t)t->frequency_hz << 32) / PXA_AUDIO_MIXER_RATE);
    return value * 32767.f * envelope * p->gain;
}
size_t pxa_audio_mixer_render(pxa_audio_mixer_t *m, int16_t *out, size_t n) {
    size_t extent = 0;
    for (size_t i = 0; i < n; ++i) {
        float mixed = 0;
        for (unsigned j = 0; j < PXA_AUDIO_MIXER_VOICES; ++j) {
            pxa_audio_mixer_voice_t *v = &m->voices[j];
            float sample = 0;
            if (v->count) {
                pxa_audio_mixer_packet_t *p = &v->packets[v->read];
                sample = p->is_tone ? tone_sample(p) : p->pcm[p->position];
                if (++p->position == p->length) {
                    v->read = (v->read + 1) % PXA_AUDIO_MIXER_PACKETS;
                    --v->count;
                }
                --v->state.queued_samples;
                ++v->state.accepted_samples;
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
