#undef NDEBUG
#include "pxa/audio_mixer.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
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
int main(void) {
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
