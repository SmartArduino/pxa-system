#include "pxa_audio.h"

#include <assert.h>
#include <string.h>

static uint8_t captured[128];
static uint32_t captured_length;
static uint32_t io_handle;
static uint32_t io_operation;
static uint32_t io_length;
static uint8_t io_data[64];

static void write_u16(uint8_t *output, uint16_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *output, uint32_t value) {
    for (uint8_t index = 0; index < 4; ++index)
        output[index] = (uint8_t)(value >> (index * 8u));
}

static void write_u64(uint8_t *output, uint64_t value) {
    for (uint8_t index = 0; index < 8; ++index)
        output[index] = (uint8_t)(value >> (index * 8u));
}

int32_t pxa_control(const uint8_t *data, uint32_t length) {
    assert(length <= sizeof(captured));
    memcpy(captured, data, length);
    captured_length = length;
    return PXA_STATUS_OK;
}

int32_t pxa_io(uint32_t handle, uint32_t operation, uint8_t *data, uint32_t length) {
    io_handle = handle;
    io_operation = operation;
    io_length = length;
    if (length <= sizeof(io_data) && data != NULL)
        memcpy(io_data, data, length);
    return (int32_t)length;
}

int main(void) {
    uint8_t payload[64];
    uint8_t packet[96];
    uint8_t pcm[] = {0, 0, 1, 0};
    const uint8_t opened[] = {10, 0, 1, 0, 3, 0, 0, 0, 31, 0, 0, 0,
                              0, 0, 0, 0, 3, 0, 4, 0, 9, 0, 0, 0,
                              4, 0, 4, 0, 0x80, 0x3e, 0, 0, 5, 0, 1, 0,
                              1, 6, 0, 2, 0, 20, 0};
    pxa_event_t event;
    pxa_audio_open_result_t result;
    pxa_audio_state_result_t state_result;
    uint8_t state_event[56] = {0};
    assert(pxa_audio_open_media(3, 0x10203040, payload, sizeof(payload), packet, sizeof(packet)));
    assert(captured_length == 26 && pxa_read_u16(captured) == PXA_SERVICE_AUDIO);
    assert(pxa_audio_commit_speaker_graph(4, 99, -256, 1500, 256, 256,
                                          payload, sizeof(payload), packet, sizeof(packet)));
    assert(pxa_audio_write_pcm(99, pcm, sizeof(pcm)) == (int32_t)sizeof(pcm));
    assert(io_handle == 99 && io_operation == PXA_IO_WRITE &&
           io_length == sizeof(pcm));
    assert(pxa_audio_play_tone(99, PXA_AUDIO_TONE_TRIANGLE, 440, 80,
                               -6 * 256) == 8);
    assert(io_handle == 99 && io_operation == PXA_AUDIO_IO_PLAY_TONE &&
           io_length == 8 && io_data[0] == 0xb8 && io_data[1] == 0x01 &&
           io_data[2] == 80 && io_data[3] == 0 && io_data[4] == 0 &&
           io_data[5] == 0xfa && io_data[6] == PXA_AUDIO_TONE_TRIANGLE &&
           io_data[7] == 0);
    assert(pxa_audio_play_tone_enveloped(
               99, PXA_AUDIO_TONE_TRIANGLE, 440, 80, -6 * 256,
               5, 30, 40) == 14);
    assert(io_length == 14 && io_data[8] == 5 && io_data[9] == 0 &&
           io_data[10] == 30 && io_data[11] == 0 && io_data[12] == 40 &&
           io_data[13] == 0);
    assert(pxa_audio_play_tone_enveloped(
               99, PXA_AUDIO_TONE_TRIANGLE, 440, 80, -6 * 256,
               81, 30, 40) == PXA_STATUS_INVALID_ARGUMENT);
    {
        static const char path[] = "audio/music.ogg";
        uint8_t command[32];
        assert(pxa_audio_play_asset(99, path, sizeof(path) - 1u, 1,
                                    -12 * 256, command,
                                    sizeof(command)) ==
               (int32_t)(8 + sizeof(path) - 1u));
        assert(io_handle == 99 && io_operation == PXA_AUDIO_IO_PLAY_ASSET &&
               io_length == 8 + sizeof(path) - 1u && io_data[0] == 15 &&
               io_data[2] == 0 && io_data[3] == 0xf4 &&
               io_data[4] == PXA_AUDIO_ASSET_LOOP &&
               memcmp(io_data + 8, path, sizeof(path) - 1u) == 0);
        assert(pxa_audio_play_asset(99, path, sizeof(path) - 1u, 0, 1,
                                    command, sizeof(command)) ==
               PXA_STATUS_INVALID_ARGUMENT);
    }
    assert(pxa_audio_control_asset(99, PXA_AUDIO_ASSET_PAUSE, 0) == 4);
    assert(io_operation == PXA_AUDIO_IO_CONTROL_ASSET &&
           io_data[0] == PXA_AUDIO_ASSET_PAUSE);
    assert(pxa_audio_control_asset(99, PXA_AUDIO_ASSET_SET_GAIN,
                                   -18 * 256) == 4);
    assert(io_data[0] == PXA_AUDIO_ASSET_SET_GAIN && io_data[2] == 0 &&
           io_data[3] == 0xee);
    assert(pxa_parse_event(opened, sizeof(opened), &event));
    assert(pxa_audio_parse_open(&event, &result));
    assert(result.session_handle == 9 && result.sample_rate == 16000 && result.frame_ms == 20);
    assert(pxa_audio_query_state(5, 99, packet, sizeof(packet)));
    assert(pxa_read_u16(captured + 2) == PXA_AUDIO_QUERY_STATE &&
           pxa_read_u32(captured + 16) == 99);
    assert(pxa_audio_flush(6, 99, packet, sizeof(packet)));
    assert(pxa_read_u16(captured + 2) == PXA_AUDIO_FLUSH);

    write_u16(state_event, PXA_SERVICE_AUDIO);
    write_u16(state_event + 2, PXA_AUDIO_QUERY_STATE);
    write_u32(state_event + 4, 5);
    write_u32(state_event + 8, 44);
    write_u16(state_event + 16, PXA_AUDIO_STATE_SUBMITTED_SAMPLES);
    write_u16(state_event + 18, 8);
    write_u64(state_event + 20, 320);
    write_u16(state_event + 28, PXA_AUDIO_STATE_ACCEPTED_SAMPLES);
    write_u16(state_event + 30, 8);
    write_u64(state_event + 32, 256);
    write_u16(state_event + 40, PXA_AUDIO_STATE_QUEUED_SAMPLES);
    write_u16(state_event + 42, 4);
    write_u32(state_event + 44, 64);
    write_u16(state_event + 48, PXA_AUDIO_STATE_FLAGS);
    write_u16(state_event + 50, 4);
    write_u32(state_event + 52,
              PXA_AUDIO_STATE_ACCEPTED_IS_SINK_SUBMITTED);
    assert(pxa_parse_event(state_event, sizeof(state_event), &event));
    assert(pxa_audio_parse_state(&event, &state_result));
    assert(state_result.submitted_samples == 320 &&
           state_result.accepted_samples == 256 &&
           state_result.queued_samples == 64);
    return 0;
}
