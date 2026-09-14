#include "audio.h"
#include "pxa_audio.h"
#include "world.h"

#include <assert.h>

static int host_tones_supported = 1;
static uint32_t tone_writes;
static uint32_t pcm_writes;

int32_t pxa_control(const uint8_t *data, uint32_t length) {
    (void)data;
    (void)length;
    return PXA_STATUS_OK;
}

int32_t pxa_io(uint32_t handle, uint32_t operation, uint8_t *data,
               uint32_t length) {
    assert(handle == 77);
    assert(data != 0);
    if (operation == PXA_AUDIO_IO_PLAY_TONE) {
        ++tone_writes;
        if (!host_tones_supported) return PXA_STATUS_UNSUPPORTED;
        assert(length == 14);
        assert(data[6] <= PXA_AUDIO_TONE_NOISE);
        return (int32_t)length;
    }
    assert(operation == PXA_IO_WRITE);
    assert(length == AUDIO_FRAME_SAMPLES * sizeof(int16_t));
    ++pcm_writes;
    return (int32_t)length;
}

static void clock_tick(pxa_event_t *event, uint8_t *message,
                       uint64_t timestamp_us) {
    uint8_t payload[8];
    pxa_writer_t writer;
    for (uint8_t index = 0; index < 8; ++index)
        payload[index] = (uint8_t)(timestamp_us >> (index * 8u));
    pxa_writer_init(&writer, message, 32);
    assert(pxa_message(&writer, PXA_SERVICE_CLOCK, PXA_CLOCK_TICK, 0, payload,
                       sizeof(payload)));
    assert(pxa_parse_event(message, (uint32_t)writer.length, event));
}

int main(void) {
    game_audio_t audio = {0};
    uint8_t message[32];
    pxa_event_t event;

    audio.state = AUDIO_READY;
    audio.session_handle = 77;
    audio_play(&audio, SND_SHOTGUN, 255);
    assert(audio.tone_mode == AUDIO_TONE_MODE_HOST);
    assert(tone_writes == 3);
    assert(pcm_writes == 0);
    clock_tick(&event, message, 1000);
    audio_tick(&audio, &event);
    assert(pcm_writes == 0);

    audio = (game_audio_t){0};
    audio.state = AUDIO_READY;
    audio.session_handle = 77;
    host_tones_supported = 0;
    tone_writes = 0;
    audio_play(&audio, SND_SHOTGUN, 255);
    assert(audio.tone_mode == AUDIO_TONE_MODE_PCM_FALLBACK);
    assert(tone_writes == 1);
    clock_tick(&event, message, 2000);
    audio_tick(&audio, &event);
    assert(pcm_writes != 0);
    return 0;
}
