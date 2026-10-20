#undef NDEBUG
#include "pxa/audio_mixer.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static pxa_audio_mixer_t mixer;
static int16_t output[320];
static void reset(void) {
    pxa_audio_graph_t g = {.route = PXA_AUDIO_ROUTE_SPEAKER};
    for (unsigned i = 0; i < 3; ++i) {
        pxa_audio_mixer_reset(&mixer.voices[i]);
        assert(pxa_audio_mixer_commit(&mixer.voices[i], &g) == PXA_STATUS_OK);
    }
}
static void fill(unsigned voice, int16_t value, unsigned n) {
    uint8_t pcm[640];
    for (unsigned i = 0; i < n; ++i) {
        pcm[2*i] = (uint8_t)value; pcm[2*i+1] = (uint16_t)value >> 8;
    }
    assert(pxa_audio_mixer_write(&mixer.voices[voice], pcm, n*2) == PXA_STATUS_OK);
}
static void effects_test(void) {
    memset(&mixer, 0, sizeof(mixer));
    pxa_audio_mixer_reset(&mixer.voices[0]);
    pxa_audio_graph_t graph = {.route=PXA_AUDIO_ROUTE_SPEAKER};
    assert(pxa_audio_mixer_commit(&mixer.voices[0], &graph) == PXA_STATUS_OK);
    fill(0, 11, 320);
    assert(pxa_audio_mixer_render(&mixer, output, 320) == 320);
    assert(!mixer.voices[1].state.queued_samples && !mixer.voices[2].state.queued_samples);
    assert(pxa_audio_mixer_render(&mixer, output, 320) == 0);
    reset();
    pxa_audio_tone_t tones[2] = {
        {.frequency_hz=1000, .duration_ms=100, .gain_db_q8=-12*256},
        {.frequency_hz=1000, .duration_ms=100, .delay_ms=40, .gain_db_q8=-12*256}};
    pxa_audio_mixer_voice_t *v = &mixer.voices[0];
    assert(pxa_audio_mixer_effect(v, tones, 2, 0) == PXA_STATUS_OK);
    assert(v->count == 1 && v->state.queued_samples == 2240);
    pxa_audio_mixer_render(&mixer, output, 320);
    const int16_t single = output[4];
    assert(single > 8200 && single < 8250);
    pxa_audio_mixer_render(&mixer, output, 320);
    pxa_audio_mixer_render(&mixer, output, 320);
    assert(abs(output[4] - 2 * single) <= 1); /* exact 40ms relative onset */
    assert(v->state.queued_samples == 1280);
    tones[1].frequency_hz = 0;
    pxa_audio_mixer_voice_t before = *v;
    assert(pxa_audio_mixer_effect(v, tones, 2, 0) == PXA_STATUS_INVALID_ARGUMENT);
    assert(!memcmp(v, &before, sizeof(before))); /* invalid replacement is atomic */
    tones[1].frequency_hz = 1000;
    assert(pxa_audio_mixer_effect(v, tones, 2, 0) == PXA_STATUS_OK);
    assert(v->count == 1 && v->state.queued_samples == 2240);
    for (unsigned track = 1; track < 4; ++track)
        assert(pxa_audio_mixer_effect(v, tones, 2, track) == PXA_STATUS_OK);
    before = *v;
    assert(pxa_audio_mixer_effect(v, tones, 2, 255) == PXA_STATUS_WOULD_BLOCK);
    assert(!memcmp(v, &before, sizeof(before)));
    assert(pxa_audio_mixer_effect(v, tones, 2, 0) == PXA_STATUS_OK); /* replace while full */
    for (unsigned track = 0; track < 4; ++track)
        assert(pxa_audio_mixer_stop_effect(v, track) == PXA_STATUS_OK);
    assert(!v->count && !v->state.queued_samples);
    assert(pxa_audio_mixer_render(&mixer, output, 320) == 0);
    /* PCM remains FIFO through holes occupied/released by effects. */
    reset();
    fill(0, 11, 1);
    assert(pxa_audio_mixer_effect(v, tones, 2, 0) == PXA_STATUS_OK);
    fill(0, 22, 1);
    assert(pxa_audio_mixer_stop_effect(v, 0) == PXA_STATUS_OK);
    fill(0, 33, 1);
    assert(pxa_audio_mixer_render(&mixer, output, 320) == 3);
    assert(output[0] == 11 && output[1] == 22 && output[2] == 33);
    /* Noise, delay, phase and envelope must not depend on block size. */
    reset();
    tones[0].waveform = PXA_AUDIO_TONE_NOISE;
    tones[0].attack_ms = 5; tones[0].release_ms = 10;
    assert(pxa_audio_mixer_effect(v, tones, 2, 0) == PXA_STATUS_OK);
    pxa_audio_mixer_t saved = mixer;
    int16_t whole[320], chunked[320];
    pxa_audio_mixer_render(&mixer, whole, 320);
    mixer = saved;
    for (unsigned i = 0; i < 320; ++i)
        pxa_audio_mixer_render(&mixer, chunked + i, 1);
    assert(!memcmp(whole, chunked, sizeof(whole)));
    pxa_audio_mixer_flush(v);
    assert(pxa_audio_mixer_render(&mixer, output, 320) == 0);
}
static void configure(uint32_t rate, uint8_t channels, uint32_t sink) {
    memset(&mixer, 0, sizeof(mixer));
    pxa_audio_mixer_voice_t *v = &mixer.voices[0];
    pxa_audio_mixer_reset(v);
    assert(pxa_audio_mixer_configure(v, rate, channels, sink) == PXA_STATUS_OK);
    pxa_audio_graph_t graph = {.route=PXA_AUDIO_ROUTE_SPEAKER};
    assert(pxa_audio_mixer_commit(v, &graph) == PXA_STATUS_OK);
}
static void format_test(void) {
    static const uint32_t rates[] = {8000, 11025, 16000, 22050, 44100, 48000};
    uint8_t input[960 * 4];
    int16_t expected[6000], actual[6000];
    for (unsigned r = 0; r < sizeof(rates)/sizeof(rates[0]); ++r) {
        const uint32_t rate = rates[r];
        for (unsigned channels = 1; channels <= 2; ++channels) {
            const unsigned frames = rate / 50;
            /* Full-scale transitions exercise overflow, endian and both channels. */
            for (unsigned i = 0; i < frames; ++i)
                for (unsigned c = 0; c < channels; ++c) {
                    const int16_t sample = (int16_t)(i * 13753u + c * 37219u);
                    input[(i*channels+c)*2] = (uint8_t)sample;
                    input[(i*channels+c)*2+1] = (uint16_t)sample >> 8;
                }
            configure(rate, (uint8_t)channels, 48000);
            pxa_audio_mixer_voice_t *v = &mixer.voices[0];
            uint32_t phase = 0; int16_t previous = 0; size_t want = 0;
            for (unsigned repeat = 0; repeat < 5; ++repeat)
                for (unsigned i = 0; i < frames; ++i) {
                    int32_t current = 0;
                    for (unsigned c = 0; c < channels; ++c) {
                        const unsigned k = (i*channels+c)*2;
                        uint16_t bits = input[k] | (uint16_t)input[k+1] << 8;
                        current += bits <= INT16_MAX ? bits : (int32_t)bits - 65536;
                    }
                    current /= (int32_t)channels;
                    phase += 48000;
                    while (phase >= rate) {
                        phase -= rate;
                        expected[want++] = (int16_t)(previous +
                            (int64_t)(current - previous) * (48000 - phase) / 48000);
                    }
                    previous = (int16_t)current;
                }
            size_t got = 0;
            /* Uneven packets and arbitrary render sizes must produce the same clock. */
            for (unsigned repeat = 0; repeat < 5; ++repeat) {
                for (unsigned start = 0; start < frames;) {
                    unsigned count = (start % 31) + 1;
                    if (count > frames - start) count = frames - start;
                    assert(pxa_audio_mixer_write(v, input + start*channels*2,
                        count*channels*2) == PXA_STATUS_OK);
                    while (v->count) {
                        size_t n = pxa_audio_mixer_render(&mixer, output, 17);
                        assert(n && got+n <= sizeof(actual)/sizeof(actual[0]));
                        memcpy(actual+got, output, n*sizeof(*output)); got += n;
                    }
                    start += count;
                }
            }
            assert(got == want && !memcmp(expected, actual, want*sizeof(*actual)));
            assert(got == (uint64_t)frames*5*48000/rate);
            assert(v->state.submitted_samples == frames*5);
            assert(v->state.accepted_samples == frames*5 && !v->state.queued_samples);
            assert(v->resample_phase == phase);
            /* Reconfiguration and queue-full failure preserve all state. */
            pxa_audio_mixer_voice_t saved = *v;
            assert(pxa_audio_mixer_configure(v, 16000, 1, 48000) == PXA_STATUS_BAD_STATE);
            assert(!memcmp(&saved, v, sizeof(saved)));
            for (unsigned slot = 0; slot < PXA_AUDIO_MIXER_PACKETS; ++slot)
                assert(pxa_audio_mixer_write(v, input, channels*2) == PXA_STATUS_OK);
            saved = *v;
            assert(pxa_audio_mixer_write(v, input, channels*2) == PXA_STATUS_WOULD_BLOCK);
            assert(!memcmp(&saved, v, sizeof(saved)));
            pxa_audio_mixer_flush(v);
            assert(!v->resample_phase && !v->previous_sample && !v->count);
            assert(pxa_audio_mixer_render(&mixer, output, 320) == 0);
        }
    }
    configure(48000, 2, 48000);
    const uint8_t opposed[] = {0xff,0x7f,1,0x80, 0,0x80,0xff,0x7f};
    assert(pxa_audio_mixer_write(&mixer.voices[0], opposed, sizeof(opposed)) == PXA_STATUS_OK);
    assert(pxa_audio_mixer_render(&mixer, output, 2) == 2);
    assert(output[0] == 0 && output[1] == 0); /* symmetric truncation toward zero */
    configure(16000, 1, 48000);
    pxa_audio_tone_t tone = {.frequency_hz=1000, .duration_ms=100,
        .delay_ms=40, .gain_db_q8=-12*256, .attack_ms=5, .release_ms=10};
    assert(pxa_audio_mixer_tone(&mixer.voices[0], &tone) == PXA_STATUS_OK);
    size_t rendered = 0;
    while (mixer.voices[0].count) {
        size_t n = pxa_audio_mixer_render(&mixer, output, 320);
        if (rendered < 1920) for (unsigned i = 0; i < 320; ++i) assert(!output[i]);
        rendered += n;
    }
    assert(rendered == 6720 && mixer.voices[0].state.accepted_samples == 2240);
    assert(!mixer.voices[0].state.queued_samples);
    pxa_audio_mixer_reset(&mixer.voices[0]);
    pxa_audio_mixer_voice_t saved = mixer.voices[0];
    assert(pxa_audio_mixer_configure(&mixer.voices[0], 44100, 2, 16000) == PXA_STATUS_INVALID_ARGUMENT);
    assert(!memcmp(&saved, &mixer.voices[0], sizeof(saved)));
}
int main(void) {
    format_test();
    effects_test();
    reset(); fill(0, 20000, 320); fill(1, 20000, 320); fill(2, -10000, 160);
    assert(pxa_audio_mixer_render(&mixer, output, 320) == 320);
    assert(output[0] == 30000 && output[159] == 30000 && output[160] == 32767);
    assert(mixer.voices[2].state.accepted_samples == 160);
    assert(pxa_audio_mixer_render(&mixer, output, 320) == 0);
    for (unsigned i = 0; i < 320; ++i) assert(!output[i]);
    reset();
    for (unsigned i = 0; i < 4; ++i) fill(0, (int16_t)(i + 1), 1);
    uint8_t pcm[2] = {1,0};
    assert(pxa_audio_mixer_write(&mixer.voices[0], pcm, 2) == PXA_STATUS_WOULD_BLOCK);
    assert(pxa_audio_mixer_render(&mixer, output, 2) == 2 && output[0] == 1 && output[1] == 2);
    fill(0, 5, 1);
    assert(pxa_audio_mixer_render(&mixer, output, 320) == 3);
    assert(output[0] == 3 && output[1] == 4 && output[2] == 5);
    fill(0, -32768, 320); fill(1, -32768, 320);
    pxa_audio_mixer_flush(&mixer.voices[0]);
    pxa_audio_mixer_render(&mixer, output, 320); assert(output[0] == -32768);
    reset();
    pxa_audio_tone_t t = {.frequency_hz=1000, .duration_ms=1000, .delay_ms=40,
        .attack_ms=5, .release_ms=20, .gain_db_q8=-12*256};
    assert(pxa_audio_mixer_tone(&mixer.voices[0], &t) == PXA_STATUS_OK);
    for (unsigned frame = 0; frame < 52; ++frame) {
        assert(pxa_audio_mixer_render(&mixer, output, 320) == 320);
        if (frame < 2) for (unsigned i = 0; i < 320; ++i) assert(!output[i]);
        if (frame == 10) assert(output[4] > 8200 && output[4] < 8250);
        if (frame == 51) assert(output[319] == 0);
    }
    assert(mixer.voices[0].state.submitted_samples == 16640);
    assert(mixer.voices[0].state.accepted_samples == 16640 && !mixer.voices[0].state.queued_samples);
    /* A peaking filter must actually change amplitude around its center. */
    reset();
    pxa_audio_graph_t g = {.route=PXA_AUDIO_ROUTE_SPEAKER, .eq_band_count=1,
        .eq_bands={{1000, 6*256, 256}}};
    assert(pxa_audio_mixer_commit(&mixer.voices[0], &g) == PXA_STATUS_OK);
    t.delay_ms = 0; t.attack_ms = t.release_ms = 0;
    assert(pxa_audio_mixer_tone(&mixer.voices[0], &t) == PXA_STATUS_OK);
    for (unsigned i = 0; i < 5; ++i) pxa_audio_mixer_render(&mixer, output, 320);
    assert(output[4] > 16300 && output[4] < 16500);
    g.eq_bands[0].frequency_hz = 8000;
    assert(pxa_audio_mixer_commit(&mixer.voices[0], &g) == PXA_STATUS_INVALID_ARGUMENT);
    assert(mixer.voices[0].eq_count == 1); /* failed commit is transactional */
    pxa_audio_mixer_flush(&mixer.voices[0]);
    assert(pxa_audio_mixer_render(&mixer, output, 320) == 0);
    printf("audio mixer passed; fixed workspace=%zu bytes\n", sizeof(mixer));
    return 0;
}
