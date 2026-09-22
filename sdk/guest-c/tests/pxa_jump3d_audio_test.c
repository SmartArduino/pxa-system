/* Audio bank and mixer test for Jump Jump 3D.
 *
 * The app plays the original mini game's sounds from an embedded 8 kHz IMA
 * ADPCM bank through its own 16 kHz mixer. This test stubs the Host PCM sink
 * and checks that clips decode to real audio, that channels replace each
 * other, that loops wrap, and that the background music ducks under effects.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "jump3d_audio.h"
#include "jump3d_audio_data.h"
#include "pxa.h"

static int16_t g_captured[4096];
static uint32_t g_captured_samples;
static uint32_t g_submit_calls;
static uint32_t g_running_peak; /* peak of everything written since the reset */

int32_t pxa_control(const uint8_t *data, uint32_t length) {
    (void)data;
    (void)length;
    return PXA_STATUS_OK;
}

/* Captures the PCM the mixer writes through the audio session. */
int32_t pxa_io(uint32_t handle, uint32_t operation, uint8_t *data,
               uint32_t length) {
    assert(handle != 0);
    if (operation == PXA_IO_WRITE) {
        assert(length == J3_AUDIO_FRAME_SAMPLES * 2u);
        {
            uint32_t index;
            for (index = 0; index + 2u <= length; index += 2u) {
                const int32_t value = (int16_t)(data[index] |
                                                ((uint16_t)data[index + 1] << 8));
                const uint32_t magnitude =
                    (uint32_t)(value < 0 ? -value : value);
                if (magnitude > g_running_peak) g_running_peak = magnitude;
            }
        }
        if (g_captured_samples + J3_AUDIO_FRAME_SAMPLES <=
            sizeof(g_captured) / sizeof(g_captured[0])) {
            memcpy(&g_captured[g_captured_samples], data, length);
            g_captured_samples += J3_AUDIO_FRAME_SAMPLES;
        }
        ++g_submit_calls;
        return (int32_t)length;
    }
    return PXA_STATUS_UNSUPPORTED;
}

static void clear_capture(void) {
    memset(g_captured, 0, sizeof(g_captured));
    g_captured_samples = 0;
    g_submit_calls = 0;
    g_running_peak = 0;
}

/* A ready session with a plausible handle, as the permission/audio handshake
 * would leave it. */
static void arm(j3_audio_t *audio) {
    memset(audio, 0, sizeof(*audio));
    audio->state = J3_AUDIO_READY;
    audio->session_handle = 7u;
}

static uint32_t peak(const int16_t *samples, uint32_t count) {
    uint32_t maximum = 0;
    uint32_t index;
    for (index = 0; index < count; ++index) {
        const int32_t value = samples[index] < 0 ? -samples[index]
                                                 : samples[index];
        if ((uint32_t)value > maximum) maximum = (uint32_t)value;
    }
    return maximum;
}

/* Drives `frames` 20 ms mixer frames through the clock path. The clock keeps
 * advancing between calls, like the Host's periodic tick. */
static void pump(j3_audio_t *audio, uint32_t frames) {
    static uint8_t payload[8];
    static uint64_t now_us = 1000000u;
    pxa_event_t event;
    uint32_t index;
    memset(&event, 0, sizeof(event));
    event.service = PXA_SERVICE_CLOCK;
    event.opcode = PXA_CLOCK_TICK;
    event.payload = payload;
    event.payload_length = 8u;
    for (index = 0; index < frames; ++index) {
        const uint32_t low = (uint32_t)now_us;
        const uint32_t high = (uint32_t)(now_us >> 32);
        payload[0] = (uint8_t)low;
        payload[1] = (uint8_t)(low >> 8);
        payload[2] = (uint8_t)(low >> 16);
        payload[3] = (uint8_t)(low >> 24);
        payload[4] = (uint8_t)high;
        payload[5] = (uint8_t)(high >> 8);
        payload[6] = (uint8_t)(high >> 16);
        payload[7] = (uint8_t)(high >> 24);
        j3_audio_tick(audio, &event);
        now_us += J3_AUDIO_FRAME_US;
    }
}

static void test_bank_is_intact(void) {
    uint32_t index;
    assert(J3_AUDIO_CLIP_COUNT == 19u);
    for (index = 0; index < J3_AUDIO_CLIP_COUNT; ++index) {
        assert(j3_audio_bank[index].words != NULL);
        assert(j3_audio_bank[index].samples > 800u); /* at least 100 ms */
    }
    /* The charge sustain and the background loop repeat by design. */
    assert(j3_audio_bank[J3_CLIP_SCALE_LOOP].looping == 1u);
    assert(j3_audio_bank[J3_CLIP_ICON].looping == 1u);
    assert(j3_audio_bank[J3_CLIP_SUCCESS].looping == 0u);
    printf("ok: audio bank has %u clips\n", (unsigned)J3_AUDIO_CLIP_COUNT);
}

static void test_clip_plays_audio(void) {
    j3_audio_t audio;
    arm(&audio);
    clear_capture();
    j3_audio_play(&audio, J3_CHANNEL_LAND, J3_CLIP_SUCCESS, J3_GAIN_FULL, 0);
    assert(j3_audio_channel_active(&audio, J3_CHANNEL_LAND));
    pump(&audio, 4);
    assert(g_submit_calls >= 4u);
    assert(g_captured_samples >= J3_AUDIO_FRAME_SAMPLES * 4u);
    assert(peak(g_captured, g_captured_samples) > 2000u);
    /* A one shot stops by itself once the clip is exhausted. */
    pump(&audio, 40);
    assert(!j3_audio_channel_active(&audio, J3_CHANNEL_LAND));
    printf("ok: one-shot clips decode and stop\n");
}

static void test_channels_replace_each_other(void) {
    j3_audio_t audio;
    arm(&audio);
    j3_audio_play(&audio, J3_CHANNEL_COMBO, J3_CLIP_COMBO1, J3_GAIN_LOUD, 0);
    pump(&audio, 2);
    j3_audio_play(&audio, J3_CHANNEL_COMBO, J3_CLIP_COMBO5, J3_GAIN_LOUD, 0);
    assert(j3_audio_channel_clip(&audio, J3_CHANNEL_COMBO) == J3_CLIP_COMBO5);
    assert(audio.voices[J3_CHANNEL_COMBO].position < 8u);
    /* Stopping ramps down instead of cutting, then releases the voice. */
    j3_audio_stop(&audio, J3_CHANNEL_COMBO);
    assert(j3_audio_channel_active(&audio, J3_CHANNEL_COMBO));
    clear_capture();
    pump(&audio, 4);
    assert(!j3_audio_channel_active(&audio, J3_CHANNEL_COMBO));
    assert(peak(&g_captured[g_captured_samples - J3_AUDIO_FRAME_SAMPLES],
                J3_AUDIO_FRAME_SAMPLES) == 0u);
    printf("ok: channels replace and stop with a fade\n");
}

/* The stored clips are 11.025 kHz and the session is 16 kHz, so a one shot has
 * to last its source duration, not its sample count. */
static void test_rate_conversion(void) {
    j3_audio_t audio;
    /* Source samples per 20 ms session frame, rounded up. */
    const uint32_t per_frame = J3_AUDIO_CLIP_RATE_HZ * J3_AUDIO_FRAME_US / 1000000u;
    const uint32_t frames =
        (j3_audio_bank[J3_CLIP_SUCCESS].samples + per_frame - 1u) / per_frame;
    uint32_t pumped = 2u; /* the first tick pre-fills two frames */
    arm(&audio);
    j3_audio_play(&audio, J3_CHANNEL_LAND, J3_CLIP_SUCCESS, J3_GAIN_FULL, 0);
    pump(&audio, 2);
    assert(j3_audio_channel_active(&audio, J3_CHANNEL_LAND));
    while (j3_audio_channel_active(&audio, J3_CHANNEL_LAND) && pumped < 40u) {
        pump(&audio, 1);
        ++pumped;
    }
    /* Allow the gain ramp, the tick granularity and the pre-fill to shift the
     * count by a frame. */
    printf("   source %.3f s -> %u expected frames, %u pumped\n",
           (double)j3_audio_bank[J3_CLIP_SUCCESS].samples /
               (double)J3_AUDIO_CLIP_RATE_HZ,
           (unsigned)frames, (unsigned)pumped);
    assert(pumped + 2u >= frames && pumped <= frames + 4u);
    printf("ok: clip rate converts to the session (%.3f s of source, %u "
           "expected frames, %u pumped)\n",
           (double)j3_audio_bank[J3_CLIP_SUCCESS].samples /
               (double)J3_AUDIO_CLIP_RATE_HZ,
           (unsigned)frames, (unsigned)pumped);
}

static void test_loops_wrap(void) {
    j3_audio_t audio;
    arm(&audio);
    j3_audio_play(&audio, J3_CHANNEL_CHARGE, J3_CLIP_SCALE_LOOP, J3_GAIN_LOUD, 1);
    clear_capture();
    /* Two seconds: the 1.3 s loop must wrap several times without a dropout
     * larger than one 20 ms frame. */
    pump(&audio, 100);
    assert(j3_audio_channel_active(&audio, J3_CHANNEL_CHARGE));
    {
        uint32_t index;
        uint32_t silent_frames = 0;
        uint32_t longest = 0;
        for (index = 0; index + J3_AUDIO_FRAME_SAMPLES <= g_captured_samples;
             index += J3_AUDIO_FRAME_SAMPLES) {
            if (peak(&g_captured[index], J3_AUDIO_FRAME_SAMPLES) < 60u) {
                ++silent_frames;
                if (silent_frames > longest) longest = silent_frames;
            } else {
                silent_frames = 0;
            }
        }
        assert(longest <= 1u);
    }
    printf("ok: the charge loop wraps seamlessly\n");
}

static void test_background_music_ducks(void) {
    j3_audio_t audio;
    arm(&audio);
    j3_audio_play(&audio, J3_CHANNEL_BGM, J3_CLIP_ICON, J3_GAIN_BGM, 1);
    pump(&audio, 2);
    assert(audio.voices[J3_CHANNEL_BGM].gain_q12 == J3_GAIN_BGM);
    j3_audio_play(&audio, J3_CHANNEL_LAND, J3_CLIP_SUCCESS, J3_GAIN_FULL, 0);
    pump(&audio, 1);
    assert(audio.voices[J3_CHANNEL_BGM].gain_q12 == J3_GAIN_BGM_DUCKED);
    pump(&audio, 30);
    assert(audio.voices[J3_CHANNEL_BGM].gain_q12 == J3_GAIN_BGM);
    printf("ok: the background loop ducks under effects\n");
}

/* The charge swell is released while it is still loud; the per-sample gain ramp
 * has to fade it instead of cutting. */
static void test_release_fades(void) {
    j3_audio_t audio;
    uint32_t index;
    int32_t biggest_step = 0;
    arm(&audio);
    j3_audio_play(&audio, J3_CHANNEL_CHARGE, J3_CLIP_SCALE_INTRO, J3_GAIN_FULL, 0);
    clear_capture();
    pump(&audio, 8);
    assert(g_captured_samples > J3_AUDIO_FRAME_SAMPLES * 7u);
    assert(j3_audio_channel_active(&audio, J3_CHANNEL_CHARGE));
    j3_audio_stop(&audio, J3_CHANNEL_CHARGE);
    clear_capture();
    pump(&audio, 4);
    assert(g_captured_samples >= J3_AUDIO_FRAME_SAMPLES * 3u);
    /* Still audible right after the release, silent by the end of the ramp. */
    assert(peak(&g_captured[0], J3_AUDIO_FRAME_SAMPLES) > 0u);
    assert(peak(&g_captured[g_captured_samples - J3_AUDIO_FRAME_SAMPLES],
                J3_AUDIO_FRAME_SAMPLES) == 0u);
    assert(!j3_audio_channel_active(&audio, J3_CHANNEL_CHARGE));
    for (index = 1; index < g_captured_samples; ++index) {
        const int32_t step = (int32_t)g_captured[index] -
                             (int32_t)g_captured[index - 1];
        if (step > biggest_step) biggest_step = step;
        if (-step > biggest_step) biggest_step = -step;
    }
    /* A cut would step from full level to zero within one sample. */
    assert(biggest_step < 12000);
    printf("ok: the charge release fades (max step %d)\n", (int)biggest_step);
}

/* A centre hit stacks success + combo + pop; the sum must stay inside full
 * scale without flat topping. */
static void test_overlap_never_clips(void) {
    j3_audio_t audio;
    uint32_t index;
    uint32_t flat = 0;
    arm(&audio);
    j3_audio_play(&audio, J3_CHANNEL_LAND, J3_CLIP_SUCCESS, J3_GAIN_FULL, 0);
    j3_audio_play(&audio, J3_CHANNEL_COMBO, J3_CLIP_COMBO4, J3_GAIN_LOUD, 0);
    j3_audio_play(&audio, J3_CHANNEL_POP, J3_CLIP_POP, J3_GAIN_SOFT, 0);
    clear_capture();
    pump(&audio, 20);
    for (index = 0; index < g_captured_samples; ++index) {
        const int32_t value = g_captured[index];
        if (value >= 32700 || value <= -32700) ++flat;
    }
    assert(flat == 0u);
    assert(peak(g_captured, g_captured_samples) > 8000u);
    printf("ok: overlapping effects stay inside full scale\n");
}

/* Every clip has to arrive at a comparable, non clipping level: the source
 * mp3s differ by up to 26 dB and the ADPCM step index once wrapped on quiet
 * passages, which made them raspy and loud. */
static void test_clip_levels_are_even(void) {
    uint8_t clip;
    for (clip = 0; clip < J3_AUDIO_CLIP_COUNT; ++clip) {
        j3_audio_t audio;
        uint32_t frames =
            (j3_audio_bank[clip].samples +
             (J3_AUDIO_CLIP_RATE_HZ / 50u) - 1u) /
            (J3_AUDIO_CLIP_RATE_HZ / 50u);
        const uint32_t level = peak(g_captured, 0);
        arm(&audio);
        j3_audio_play(&audio, J3_CHANNEL_LAND, clip, J3_GAIN_FULL, 0);
        clear_capture();
        if (frames > 800u) frames = 800u;
        pump(&audio, frames);
        assert(g_submit_calls >= frames);
        {
            printf("   clip %2u: peak %5u over %u frames\n", (unsigned)clip,
                   (unsigned)g_running_peak, (unsigned)frames);
            assert(g_running_peak > 8000u);
            assert(g_running_peak < 24000u);
        }
        (void)level;
    }
    printf("ok: all %u clips are levelled and unclipped\n",
           (unsigned)J3_AUDIO_CLIP_COUNT);
}

static void test_silent_before_ready(void) {
    j3_audio_t audio;
    memset(&audio, 0, sizeof(audio));
    audio.session_handle = 7u;
    j3_audio_play(&audio, J3_CHANNEL_LAND, J3_CLIP_SUCCESS, J3_GAIN_FULL, 0);
    clear_capture();
    pump(&audio, 4);
    /* The request is remembered, but no PCM leaves the Guest until the Host
     * has committed the speaker graph. */
    assert(g_submit_calls == 0u);
    assert(g_captured_samples == 0u);
    assert(audio.voices[J3_CHANNEL_LAND].active == 1u);
    printf("ok: nothing is mixed before the session is ready\n");
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    test_bank_is_intact();
    test_clip_plays_audio();
    test_channels_replace_each_other();
    test_rate_conversion();
    test_loops_wrap();
    test_background_music_ducks();
    test_release_fades();
    test_overlap_never_clips();
    test_clip_levels_are_even();
    test_silent_before_ready();
    printf("jump-jump-3d audio tests passed\n");
    return 0;
}
