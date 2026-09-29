#include <assert.h>
#include <string.h>

#include "pxa_audio.h"

static uint64_t last_io_handle;
static uint32_t last_io_operation;
static uint8_t last_io_data[12];

int32_t pxa_submit(const uint8_t *data, uint32_t size) {
    (void)data;
    (void)size;
    return 0;
}

int32_t pxa_io(uint64_t handle, uint32_t operation, uint8_t *data,
                  uint32_t size) {
    if (operation==PXA_AUDIO_IO_PLAY_MUSIC) {
        assert(size>16 && !pxa_load_u64(data));
        assert(pxa_load_u16(data+8)==size-16 && !data[13] && !data[14] && !data[15]);
        pxa_store_u64(data,UINT64_C(0x1234567800000001));
    }
    if (size == sizeof(last_io_data)) memcpy(last_io_data,data,size);
    last_io_handle = handle;
    last_io_operation = operation;
    return (int32_t)size;
}

int main(void) {
    const uint64_t permission = UINT64_C(0x1234567800000001);
    const uint64_t session = UINT64_C(0x2345678900000002);
    uint8_t packet[128];
    uint8_t payload[80] = {0};
    uint8_t value[8];
    uint8_t pcm[4] = {0};
    uint32_t written = 0;
    size_t offset = 4;
    size_t size = 0;
    pxa_event_t event = {0};
    pxa_audio_open_result_t opened;
    pxa_audio_state_result_t state;
    pxa_wire_record_view_t record;
    pxa_audio_eq_band_t band = {1500, 256, 256};
    pxa_audio_graph_t graph = {-256, &band, 1};
    assert(pxa_audio_build_open(packet, sizeof(packet), 77,
                                   permission, &written) && written == 38);
    assert(pxa_parse_event(packet, written, &event) &&
           event.service == PXA_AUDIO_SERVICE &&
           event.opcode == PXA_AUDIO_OPEN_SESSION && event.token == 77);
    assert(pxa_wire_record_decode(event.payload, event.payload_size,
                                  &record, &size) && record.tag == 1 &&
           record.payload_size == 8 &&
           pxa_load_u64(record.payload) == permission);
    assert(!pxa_audio_build_open(packet, sizeof(packet), 77, 1, &written));
    assert(pxa_audio_build_graph(packet, sizeof(packet), 78, session,
                                    &graph, &written) && written == 54);
    assert(pxa_parse_event(packet, written, &event) &&
           event.opcode == PXA_AUDIO_COMMIT_GRAPH &&
           event.payload_size == 34);
    assert(pxa_audio_build_session_request(
               packet, sizeof(packet), PXA_AUDIO_QUERY_STATE, 79,
               session, &written) && written == 32);
    assert(!pxa_audio_build_session_request(
               packet, sizeof(packet), PXA_AUDIO_FLUSH, 79,
               2, &written));

    pxa_store_u32(payload, 0);
    pxa_store_u64(value, session);
    assert(pxa_wire_record_encode(payload + offset, sizeof(payload) - offset,
                                  3, value, 8, &size));
    offset += size;
    pxa_store_u32(value, 16000);
    assert(pxa_wire_record_encode(payload + offset, sizeof(payload) - offset,
                                  4, value, 4, &size));
    offset += size;
    value[0] = 1;
    assert(pxa_wire_record_encode(payload + offset, sizeof(payload) - offset,
                                  5, value, 1, &size));
    offset += size;
    pxa_store_u16(value, 20);
    assert(pxa_wire_record_encode(payload + offset, sizeof(payload) - offset,
                                  6, value, 2, &size));
    offset += size;
    event.service = PXA_AUDIO_SERVICE;
    event.opcode = PXA_AUDIO_OPEN_SESSION;
    event.token = 77;
    event.payload = payload;
    event.payload_size = (uint32_t)offset;
    assert(pxa_audio_parse_open(&event, 77, &opened) &&
           opened.status == 0 && opened.handle == session &&
           opened.sample_rate == 16000 && opened.channels == 1 &&
           opened.frame_ms == 20);
    assert(!pxa_audio_parse_open(&event, 78, &opened));
    payload[4 + 2] = 4;
    assert(!pxa_audio_parse_open(&event, 77, &opened));

    offset = 4;
    pxa_store_u64(value, 320);
    assert(pxa_wire_record_encode(payload + offset, sizeof(payload) - offset,
                                  2, value, 8, &size));
    offset += size;
    pxa_store_u64(value, 256);
    assert(pxa_wire_record_encode(payload + offset, sizeof(payload) - offset,
                                  3, value, 8, &size));
    offset += size;
    pxa_store_u32(value, 64);
    assert(pxa_wire_record_encode(payload + offset, sizeof(payload) - offset,
                                  4, value, 4, &size));
    offset += size;
    pxa_store_u32(value, 1);
    assert(pxa_wire_record_encode(payload + offset, sizeof(payload) - offset,
                                  5, value, 4, &size));
    offset += size;
    event.opcode = PXA_AUDIO_QUERY_STATE;
    event.token = 79;
    event.payload_size = (uint32_t)offset;
    assert(pxa_audio_parse_state(&event, 79, &state) &&
           state.submitted_samples == 320 && state.accepted_samples == 256 &&
           state.queued_samples == 64 && state.flags == 1);
    assert(pxa_audio_write_pcm(session, pcm, sizeof(pcm)) == 4 &&
           last_io_handle == session &&
           last_io_operation == PXA_AUDIO_IO_WRITE);
    assert(pxa_audio_play_tone(session, PXA_AUDIO_TONE_TRIANGLE,
                                  440, 80, -6 * 256, 5, 30, 40) == 14 &&
           last_io_operation == PXA_AUDIO_IO_PLAY_TONE);
    assert(pxa_audio_control_asset(session,
                                      PXA_AUDIO_ASSET_PAUSE, 0) == 4 &&
           last_io_operation == PXA_AUDIO_IO_CONTROL_ASSET);
    assert(pxa_audio_play_asset(1, "x", 1, 0, 0,
                                    packet, sizeof(packet)) == -1);
    assert(pxa_audio_commit_gain(80, session, -6*256) == 0);
    assert(pxa_audio_query(81, session) == 0);
    assert(pxa_audio_flush(82, session) == 0);
    assert(pxa_audio_beep(session, 440, 80, -6*256) == 14);
    assert(pxa_audio_play_file(session, "assets/tone.ogg", 1, -12*256) == 23);
    assert(pxa_audio_play_file(session, NULL, 0, 0) == -1);
    uint64_t sound = UINT64_C(0x8765432100000010);
    assert(pxa_audio_play_sound(session,sound,-60*256)==12);
    assert(last_io_operation==PXA_AUDIO_IO_PLAY_SOUND && last_io_handle==session);
    assert(pxa_load_u64(last_io_data)==sound);
    assert((int16_t)pxa_load_u16(last_io_data+8)==-60*256);
    assert(!last_io_data[10] && !last_io_data[11]);
    assert(pxa_audio_play_sound(session,sound,1)==-1);
    assert(pxa_audio_play_sound(session,sound,-60*256-1)==-1);
    assert(pxa_audio_play_sound(session,0,0)==-1);
    assert(pxa_audio_play_sound(session,(uint32_t)sound,0)==-1);
    assert(pxa_audio_play_sound((uint32_t)session,sound,0)==-1);
    uint64_t instance;
    assert(pxa_audio_play_music(session,"assets/tone.ogg",0,-6*256,&instance)==31);
    assert(instance==UINT64_C(0x1234567800000001));
    pxa_zero(payload,24);
    pxa_store_u64(payload,session); pxa_store_u64(payload+8,instance);
    payload[16]=PXA_AUDIO_PLAYBACK_READY;
    event=(pxa_event_t){PXA_AUDIO_SERVICE,PXA_AUDIO_PLAYBACK_EVENT,0,payload,24};
    pxa_audio_playback_event_t playback;
    assert(pxa_audio_parse_playback(&event,&playback) && playback.instance==instance && playback.session==session);
    payload[17]=1; assert(!pxa_audio_parse_playback(&event,&playback)); payload[17]=0;
    payload[16]=PXA_AUDIO_PLAYBACK_ERROR;
    assert(!pxa_audio_parse_playback(&event,&playback));
    pxa_store_u32(payload+20,(uint32_t)-14);
    assert(pxa_audio_parse_playback(&event,&playback) && playback.status==-14);
    assert(pxa_audio_play_music(session,NULL,0,0,&instance)==-1 && !instance);
    return 0;
}
