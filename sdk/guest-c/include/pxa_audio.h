#ifndef PXA_AUDIO_H
#define PXA_AUDIO_H

#include "pxa.h"

#define PXA_SERVICE_AUDIO 10u
#define PXA_AUDIO_OPEN_SESSION 1u
#define PXA_AUDIO_COMMIT_GRAPH 2u
#define PXA_AUDIO_QUERY_STATE 3u
#define PXA_AUDIO_FLUSH 4u
#define PXA_AUDIO_IO_PLAY_TONE 0x100u
#define PXA_AUDIO_TONE_SINE 0u
#define PXA_AUDIO_TONE_SQUARE 1u
#define PXA_AUDIO_TONE_TRIANGLE 2u
#define PXA_AUDIO_TONE_NOISE 3u

#define PXA_AUDIO_PERMISSION_HANDLE 1u
#define PXA_AUDIO_USAGE 2u
#define PXA_AUDIO_SESSION_HANDLE 3u
#define PXA_AUDIO_SAMPLE_RATE 4u
#define PXA_AUDIO_CHANNELS 5u
#define PXA_AUDIO_FRAME_MS 6u

#define PXA_AUDIO_STATE_SUBMITTED_SAMPLES 2u
#define PXA_AUDIO_STATE_ACCEPTED_SAMPLES 3u
#define PXA_AUDIO_STATE_QUEUED_SAMPLES 4u
#define PXA_AUDIO_STATE_FLAGS 5u
#define PXA_AUDIO_STATE_ACCEPTED_IS_SINK_SUBMITTED UINT32_C(1)

#define PXA_AUDIO_GRAPH_SESSION_HANDLE 1u
#define PXA_AUDIO_GAIN_DB_Q8 2u
#define PXA_AUDIO_EQ_BAND 3u
#define PXA_AUDIO_ROUTE 4u

#define PXA_AUDIO_USAGE_MEDIA 1u
#define PXA_AUDIO_ROUTE_SPEAKER 1u

typedef struct {
    int32_t status;
    uint32_t session_handle;
    uint32_t sample_rate;
    uint8_t channels;
    uint16_t frame_ms;
} pxa_audio_open_result_t;

typedef struct {
    int32_t status;
    uint64_t submitted_samples;
    uint64_t accepted_samples;
    uint32_t queued_samples;
    uint32_t flags;
} pxa_audio_state_result_t;

static inline int pxa_audio_session_request(uint16_t opcode,
                                            uint32_t request_id,
                                            uint32_t session_handle,
                                            uint8_t *packet,
                                            size_t packet_capacity) {
    uint8_t payload[8];
    uint8_t handle[4];
    pxa_writer_t records;
    pxa_writer_t message;
    if (request_id == 0 || session_handle == 0 || packet == NULL) return 0;
    handle[0] = (uint8_t)session_handle;
    handle[1] = (uint8_t)(session_handle >> 8);
    handle[2] = (uint8_t)(session_handle >> 16);
    handle[3] = (uint8_t)(session_handle >> 24);
    pxa_writer_init(&records, payload, sizeof(payload));
    if (!pxa_record(&records, 1, handle, sizeof(handle))) return 0;
    pxa_writer_init(&message, packet, packet_capacity);
    return pxa_message(&message, PXA_SERVICE_AUDIO, opcode, request_id,
                       records.data, records.length) &&
           pxa_control(message.data, (uint32_t)message.length) == PXA_STATUS_OK;
}

static inline int pxa_audio_query_state(uint32_t request_id,
                                        uint32_t session_handle,
                                        uint8_t *packet,
                                        size_t packet_capacity) {
    return pxa_audio_session_request(PXA_AUDIO_QUERY_STATE, request_id,
                                     session_handle, packet, packet_capacity);
}

static inline int pxa_audio_flush(uint32_t request_id, uint32_t session_handle,
                                  uint8_t *packet,
                                  size_t packet_capacity) {
    return pxa_audio_session_request(PXA_AUDIO_FLUSH, request_id,
                                     session_handle, packet, packet_capacity);
}

static inline int pxa_audio_open_media(uint32_t request_id, uint32_t permission_handle,
                                       uint8_t *payload, size_t payload_capacity,
                                       uint8_t *packet, size_t packet_capacity) {
    pxa_writer_t request;
    pxa_writer_t message;
    uint8_t permission[4];
    const uint8_t usage[2] = {PXA_AUDIO_USAGE_MEDIA, 0};
    if (request_id == 0 || permission_handle == 0 || payload == NULL || packet == NULL)
        return 0;
    permission[0] = (uint8_t)permission_handle;
    permission[1] = (uint8_t)(permission_handle >> 8);
    permission[2] = (uint8_t)(permission_handle >> 16);
    permission[3] = (uint8_t)(permission_handle >> 24);
    pxa_writer_init(&request, payload, payload_capacity);
    if (!pxa_record(&request, PXA_AUDIO_PERMISSION_HANDLE, permission, sizeof(permission)) ||
        !pxa_record(&request, PXA_AUDIO_USAGE, usage, sizeof(usage))) return 0;
    pxa_writer_init(&message, packet, packet_capacity);
    return pxa_message(&message, PXA_SERVICE_AUDIO, PXA_AUDIO_OPEN_SESSION, request_id,
                       request.data, request.length) &&
           pxa_control(message.data, (uint32_t)message.length) == PXA_STATUS_OK;
}

static inline int pxa_audio_commit_speaker_graph(uint32_t request_id, uint32_t session_handle,
                                                 int16_t gain_db_q8, uint16_t frequency_hz,
                                                 int16_t eq_gain_db_q8, uint16_t q_q8,
                                                 uint8_t *payload, size_t payload_capacity,
                                                 uint8_t *packet, size_t packet_capacity) {
    pxa_writer_t request;
    pxa_writer_t message;
    uint8_t session[4];
    uint8_t gain[2];
    uint8_t band[6];
    const uint8_t route[2] = {PXA_AUDIO_ROUTE_SPEAKER, 0};
    if (request_id == 0 || session_handle == 0 || frequency_hz < 20 || frequency_hz > 20000 ||
        q_q8 < 64 || q_q8 > 4096 || payload == NULL || packet == NULL) return 0;
    session[0] = (uint8_t)session_handle;
    session[1] = (uint8_t)(session_handle >> 8);
    session[2] = (uint8_t)(session_handle >> 16);
    session[3] = (uint8_t)(session_handle >> 24);
    gain[0] = (uint8_t)gain_db_q8;
    gain[1] = (uint8_t)((uint16_t)gain_db_q8 >> 8);
    band[0] = (uint8_t)frequency_hz;
    band[1] = (uint8_t)(frequency_hz >> 8);
    band[2] = (uint8_t)eq_gain_db_q8;
    band[3] = (uint8_t)((uint16_t)eq_gain_db_q8 >> 8);
    band[4] = (uint8_t)q_q8;
    band[5] = (uint8_t)(q_q8 >> 8);
    pxa_writer_init(&request, payload, payload_capacity);
    if (!pxa_record(&request, PXA_AUDIO_GRAPH_SESSION_HANDLE, session, sizeof(session)) ||
        !pxa_record(&request, PXA_AUDIO_GAIN_DB_Q8, gain, sizeof(gain)) ||
        !pxa_record(&request, PXA_AUDIO_EQ_BAND, band, sizeof(band)) ||
        !pxa_record(&request, PXA_AUDIO_ROUTE, route, sizeof(route))) return 0;
    pxa_writer_init(&message, packet, packet_capacity);
    return pxa_message(&message, PXA_SERVICE_AUDIO, PXA_AUDIO_COMMIT_GRAPH, request_id,
                       request.data, request.length) &&
           pxa_control(message.data, (uint32_t)message.length) == PXA_STATUS_OK;
}

/* Writes one or fewer negotiated PCM frames to an audio session. The PCM
 * format is signed 16-bit little-endian, interleaved by channel. A short
 * queue is reported as PXA_STATUS_WOULD_BLOCK; callers should retry on a
 * later clock event instead of spinning in the current guest callback. */
static inline int32_t pxa_audio_write_pcm(uint32_t session_handle,
                                          uint8_t *pcm, uint32_t length) {
    if (session_handle == 0 || (pcm == NULL && length != 0)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    return pxa_io(session_handle, PXA_IO_WRITE, pcm, length);
}

static inline int32_t pxa_audio_play_tone(uint32_t session_handle,
                                          uint8_t waveform,
                                          uint16_t frequency_hz,
                                          uint16_t duration_ms,
                                          int16_t gain_db_q8) {
    uint8_t command[8];
    if (session_handle == 0 || waveform > PXA_AUDIO_TONE_NOISE ||
        frequency_hz < 40 || frequency_hz > 8000 || duration_ms < 10 ||
        duration_ms > 1000 || gain_db_q8 > 0 || gain_db_q8 < -60 * 256) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    command[0] = (uint8_t)frequency_hz;
    command[1] = (uint8_t)(frequency_hz >> 8);
    command[2] = (uint8_t)duration_ms;
    command[3] = (uint8_t)(duration_ms >> 8);
    command[4] = (uint8_t)gain_db_q8;
    command[5] = (uint8_t)((uint16_t)gain_db_q8 >> 8);
    command[6] = waveform;
    command[7] = 0;
    return pxa_io(session_handle, PXA_AUDIO_IO_PLAY_TONE, command,
                  sizeof(command));
}

static inline int pxa_audio_parse_open(const pxa_event_t *event,
                                       pxa_audio_open_result_t *output) {
    const uint8_t *value;
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_AUDIO ||
        event->opcode != PXA_AUDIO_OPEN_SESSION || event->request_id == 0 ||
        event->payload_length < 4) return 0;
    value = event->payload;
    output->status = (int32_t)pxa_read_u32(value);
    if (output->status != PXA_STATUS_OK) return event->payload_length == 4;
    if (event->payload_length != 31 || pxa_read_u16(value + 4) != PXA_AUDIO_SESSION_HANDLE ||
        pxa_read_u16(value + 6) != 4 || pxa_read_u16(value + 12) != PXA_AUDIO_SAMPLE_RATE ||
        pxa_read_u16(value + 14) != 4 || pxa_read_u16(value + 20) != PXA_AUDIO_CHANNELS ||
        pxa_read_u16(value + 22) != 1 || pxa_read_u16(value + 25) != PXA_AUDIO_FRAME_MS ||
        pxa_read_u16(value + 27) != 2) return 0;
    output->session_handle = pxa_read_u32(value + 8);
    output->sample_rate = pxa_read_u32(value + 16);
    output->channels = value[24];
    output->frame_ms = pxa_read_u16(value + 29);
    return output->session_handle != 0 && output->sample_rate >= 8000 &&
           (output->channels == 1 || output->channels == 2) && output->frame_ms >= 5;
}

static inline int pxa_audio_parse_status(const pxa_event_t *event, uint16_t opcode,
                                         int32_t *status) {
    if (event == NULL || status == NULL || event->service != PXA_SERVICE_AUDIO ||
        event->opcode != opcode || event->request_id == 0 || event->payload_length != 4) return 0;
    *status = (int32_t)pxa_read_u32(event->payload);
    return 1;
}

static inline int pxa_audio_parse_state(const pxa_event_t *event,
                                        pxa_audio_state_result_t *output) {
    const uint8_t *value;
    if (event == NULL || output == NULL ||
        event->service != PXA_SERVICE_AUDIO ||
        event->opcode != PXA_AUDIO_QUERY_STATE || event->request_id == 0 ||
        event->payload_length < 4)
        return 0;
    value = event->payload;
    output->status = (int32_t)pxa_read_u32(value);
    if (output->status != PXA_STATUS_OK) return event->payload_length == 4;
    if (event->payload_length != 44 ||
        pxa_read_u16(value + 4) != PXA_AUDIO_STATE_SUBMITTED_SAMPLES ||
        pxa_read_u16(value + 6) != 8 ||
        pxa_read_u16(value + 16) != PXA_AUDIO_STATE_ACCEPTED_SAMPLES ||
        pxa_read_u16(value + 18) != 8 ||
        pxa_read_u16(value + 28) != PXA_AUDIO_STATE_QUEUED_SAMPLES ||
        pxa_read_u16(value + 30) != 4 ||
        pxa_read_u16(value + 36) != PXA_AUDIO_STATE_FLAGS ||
        pxa_read_u16(value + 38) != 4)
        return 0;
    output->submitted_samples = pxa_read_u64(value + 8);
    output->accepted_samples = pxa_read_u64(value + 20);
    output->queued_samples = pxa_read_u32(value + 32);
    output->flags = pxa_read_u32(value + 40);
    return output->accepted_samples <= output->submitted_samples &&
           output->queued_samples <= output->submitted_samples;
}

#endif
