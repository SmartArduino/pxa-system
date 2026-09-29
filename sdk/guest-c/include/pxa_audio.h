#ifndef PXA_GUEST_AUDIO_H
#define PXA_GUEST_AUDIO_H

#include "pxa_core.h"

#define PXA_AUDIO_SERVICE 10u
#define PXA_AUDIO_OPEN_SESSION 1u
#define PXA_AUDIO_COMMIT_GRAPH 2u
#define PXA_AUDIO_QUERY_STATE 3u
#define PXA_AUDIO_FLUSH 4u
#define PXA_AUDIO_USAGE_MEDIA 1u
#define PXA_AUDIO_ROUTE_SPEAKER 1u
#define PXA_AUDIO_IO_WRITE 2u
#define PXA_AUDIO_IO_PLAY_TONE 0x100u
#define PXA_AUDIO_IO_PLAY_ASSET 0x101u
#define PXA_AUDIO_IO_CONTROL_ASSET 0x102u
#define PXA_AUDIO_IO_PLAY_SOUND 0x103u
#define PXA_AUDIO_IO_PLAY_MUSIC 0x104u
#define PXA_AUDIO_PLAYBACK_EVENT 0x8001u
#define PXA_AUDIO_PLAYBACK_READY 1u
#define PXA_AUDIO_PLAYBACK_ENDED 2u
#define PXA_AUDIO_PLAYBACK_STOPPED 3u
#define PXA_AUDIO_PLAYBACK_REPLACED 4u
#define PXA_AUDIO_PLAYBACK_ERROR 5u
#define PXA_AUDIO_MAX_EQ_BANDS 5u
#define PXA_AUDIO_TONE_SINE 0u
#define PXA_AUDIO_TONE_SQUARE 1u
#define PXA_AUDIO_TONE_TRIANGLE 2u
#define PXA_AUDIO_TONE_NOISE 3u
#define PXA_AUDIO_ASSET_LOOP 1u
#define PXA_AUDIO_ASSET_PAUSE 1u
#define PXA_AUDIO_ASSET_RESUME 2u
#define PXA_AUDIO_ASSET_STOP 3u
#define PXA_AUDIO_ASSET_SET_GAIN 4u

typedef struct {
    uint16_t frequency_hz;
    int16_t gain_db_q8;
    uint16_t q_q8;
} pxa_audio_eq_band_t;

typedef struct {
    int16_t gain_db_q8;
    const pxa_audio_eq_band_t *eq_bands;
    uint8_t eq_band_count;
} pxa_audio_graph_t;

typedef struct {
    int32_t status;
    uint64_t handle;
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

typedef struct {
    uint64_t session, instance;
    int32_t status;
    uint8_t state;
} pxa_audio_playback_event_t;

static inline int pxa_audio_parse_playback(const pxa_event_t *event,
    pxa_audio_playback_event_t *out) {
    if (!event || !out || event->service!=PXA_AUDIO_SERVICE ||
        event->opcode!=PXA_AUDIO_PLAYBACK_EVENT || event->token ||
        event->payload_size!=24 || !event->payload) return 0;
    const uint8_t *p=event->payload;
    out->session=pxa_load_u64(p); out->instance=pxa_load_u64(p+8);
    out->state=p[16]; out->status=(int32_t)pxa_load_u32(p+20);
    return (out->session>>32)!=0 && out->instance && !p[17] && !p[18] && !p[19] &&
        out->state>=PXA_AUDIO_PLAYBACK_READY && out->state<=PXA_AUDIO_PLAYBACK_ERROR &&
        (out->state==PXA_AUDIO_PLAYBACK_ERROR ? out->status<0 && out->status>=-16 : out->status==0);
}

static inline int pxa_audio_append(uint8_t *packet, size_t capacity,
                                       size_t *offset, uint16_t tag,
                                       const uint8_t *value, size_t size) {
    size_t written = 0;
    if (*offset > capacity ||
        !pxa_wire_record_encode(packet + *offset, capacity - *offset,
                                tag, value, size, &written)) return 0;
    *offset += written;
    return 1;
}

static inline int pxa_audio_build_open(uint8_t *packet, size_t capacity,
                                            uint64_t token,
                                            uint64_t permission_handle,
                                            uint32_t *written) {
    uint8_t value[8];
    size_t offset = PXA_HEADER_BYTES;
    if (written != NULL) *written = 0;
    if (packet == NULL || written == NULL || token == 0 ||
        (permission_handle >> 32) == 0 ||
        capacity < PXA_HEADER_BYTES) return 0;
    pxa_store_u64(value, permission_handle);
    if (!pxa_audio_append(packet, capacity, &offset, 1, value, 8)) return 0;
    pxa_store_u16(value, PXA_AUDIO_USAGE_MEDIA);
    if (!pxa_audio_append(packet, capacity, &offset, 2, value, 2)) return 0;
    return pxa_finish_message_in_place(
        packet, capacity, PXA_AUDIO_SERVICE,
        PXA_AUDIO_OPEN_SESSION, token, offset, written);
}

static inline int32_t pxa_audio_open_media(uint64_t token,
                                               uint64_t permission_handle) {
    uint8_t packet[PXA_HEADER_BYTES + 18u];
    uint32_t size = 0;
    if (!pxa_audio_build_open(packet, sizeof(packet), token,
                                 permission_handle, &size)) return -1;
    return pxa_submit(packet, size);
}

static inline int pxa_audio_build_graph(
    uint8_t *packet, size_t capacity, uint64_t token, uint64_t session,
    const pxa_audio_graph_t *graph, uint32_t *written) {
    uint8_t value[8];
    size_t offset = PXA_HEADER_BYTES;
    if (written != NULL) *written = 0;
    if (packet == NULL || written == NULL || token == 0 ||
        (session >> 32) == 0 || graph == NULL ||
        graph->gain_db_q8 < -48 * 256 || graph->gain_db_q8 > 12 * 256 ||
        graph->eq_band_count > PXA_AUDIO_MAX_EQ_BANDS ||
        (graph->eq_bands == NULL && graph->eq_band_count != 0) ||
        capacity < PXA_HEADER_BYTES) return 0;
    pxa_store_u64(value, session);
    if (!pxa_audio_append(packet, capacity, &offset, 1, value, 8)) return 0;
    pxa_store_u16(value, (uint16_t)graph->gain_db_q8);
    if (!pxa_audio_append(packet, capacity, &offset, 2, value, 2)) return 0;
    for (uint8_t i = 0; i < graph->eq_band_count; ++i) {
        const pxa_audio_eq_band_t *band = &graph->eq_bands[i];
        if (band->frequency_hz < 20 || band->frequency_hz > 20000 ||
            band->gain_db_q8 < -12 * 256 || band->gain_db_q8 > 12 * 256 ||
            band->q_q8 < 64 || band->q_q8 > 4096) return 0;
        pxa_store_u16(value, band->frequency_hz);
        pxa_store_u16(value + 2, (uint16_t)band->gain_db_q8);
        pxa_store_u16(value + 4, band->q_q8);
        if (!pxa_audio_append(packet, capacity, &offset, 3, value, 6))
            return 0;
    }
    pxa_store_u16(value, PXA_AUDIO_ROUTE_SPEAKER);
    if (!pxa_audio_append(packet, capacity, &offset, 4, value, 2)) return 0;
    return pxa_finish_message_in_place(
        packet, capacity, PXA_AUDIO_SERVICE,
        PXA_AUDIO_COMMIT_GRAPH, token, offset, written);
}

static inline int32_t pxa_audio_commit_graph(
    uint64_t token, uint64_t session, const pxa_audio_graph_t *graph) {
    uint8_t packet[PXA_HEADER_BYTES + 74u];
    uint32_t size = 0;
    if (!pxa_audio_build_graph(packet, sizeof(packet), token,
                                  session, graph, &size)) return -1;
    return pxa_submit(packet, size);
}

static inline int pxa_audio_build_session_request(
    uint8_t *packet, size_t capacity, uint16_t opcode, uint64_t token,
    uint64_t session, uint32_t *written) {
    uint8_t value[8];
    size_t offset = PXA_HEADER_BYTES;
    if (written != NULL) *written = 0;
    if (packet == NULL || written == NULL || token == 0 ||
        (session >> 32) == 0 || capacity < PXA_HEADER_BYTES ||
        (opcode != PXA_AUDIO_QUERY_STATE &&
         opcode != PXA_AUDIO_FLUSH)) return 0;
    pxa_store_u64(value, session);
    if (!pxa_audio_append(packet, capacity, &offset, 1, value, 8)) return 0;
    return pxa_finish_message_in_place(packet, capacity,
                                          PXA_AUDIO_SERVICE, opcode,
                                          token, offset, written);
}

static inline int32_t pxa_audio_session_request(uint16_t opcode,
                                                     uint64_t token,
                                                     uint64_t session) {
    uint8_t packet[PXA_HEADER_BYTES + 12u];
    uint32_t size = 0;
    if (!pxa_audio_build_session_request(packet, sizeof(packet), opcode,
                                             token, session, &size)) return -1;
    return pxa_submit(packet, size);
}

static inline int pxa_audio_parse_status(const pxa_event_t *event,
                                             uint64_t token, uint16_t opcode,
                                             int32_t *status) {
    if (event == NULL || status == NULL || token == 0 ||
        event->service != PXA_AUDIO_SERVICE || event->opcode != opcode ||
        event->token != token || event->payload == NULL ||
        event->payload_size != 4) return 0;
    *status = (int32_t)pxa_load_u32(event->payload);
    return 1;
}

static inline int pxa_audio_parse_open(const pxa_event_t *event,
                                           uint64_t token,
                                           pxa_audio_open_result_t *out) {
    pxa_wire_record_view_t record;
    size_t consumed = 0;
    size_t offset = 4;
    if (out == NULL) return 0;
    pxa_zero(out, sizeof(*out));
    if (event == NULL || event->service != PXA_AUDIO_SERVICE ||
        event->opcode != PXA_AUDIO_OPEN_SESSION || token == 0 ||
        event->token != token || event->payload == NULL ||
        event->payload_size < 4) return 0;
    out->status = (int32_t)pxa_load_u32(event->payload);
    if (out->status != 0) return event->payload_size == 4;
    if (event->payload_size != 35 ||
        !pxa_wire_record_decode(event->payload + offset,
                                event->payload_size - offset,
                                &record, &consumed) || record.raw_tag != 3 ||
        record.payload_size != 8) return 0;
    out->handle = pxa_load_u64(record.payload);
    offset += consumed;
    if (!pxa_wire_record_decode(event->payload + offset,
                                event->payload_size - offset,
                                &record, &consumed) || record.raw_tag != 4 ||
        record.payload_size != 4) return 0;
    out->sample_rate = pxa_load_u32(record.payload);
    offset += consumed;
    if (!pxa_wire_record_decode(event->payload + offset,
                                event->payload_size - offset,
                                &record, &consumed) || record.raw_tag != 5 ||
        record.payload_size != 1) return 0;
    out->channels = record.payload[0];
    offset += consumed;
    if (!pxa_wire_record_decode(event->payload + offset,
                                event->payload_size - offset,
                                &record, &consumed) || record.raw_tag != 6 ||
        record.payload_size != 2 || offset + consumed != event->payload_size)
        return 0;
    out->frame_ms = pxa_load_u16(record.payload);
    return (out->handle >> 32) != 0 &&
           out->sample_rate >= 8000 && out->sample_rate <= 48000 &&
           (out->channels == 1 || out->channels == 2) &&
           out->frame_ms >= 5 && out->frame_ms <= 120;
}

static inline int pxa_audio_parse_state(
    const pxa_event_t *event, uint64_t token,
    pxa_audio_state_result_t *out) {
    pxa_wire_record_view_t record;
    size_t offset = 4;
    size_t consumed = 0;
    if (out == NULL) return 0;
    pxa_zero(out, sizeof(*out));
    if (event == NULL || event->service != PXA_AUDIO_SERVICE ||
        event->opcode != PXA_AUDIO_QUERY_STATE || token == 0 ||
        event->token != token || event->payload == NULL ||
        event->payload_size < 4) return 0;
    out->status = (int32_t)pxa_load_u32(event->payload);
    if (out->status != 0) return event->payload_size == 4;
    if (event->payload_size != 44 ||
        !pxa_wire_record_decode(event->payload + offset,
                                event->payload_size - offset,
                                &record, &consumed) || record.raw_tag != 2 ||
        record.payload_size != 8) return 0;
    out->submitted_samples = pxa_load_u64(record.payload);
    offset += consumed;
    if (!pxa_wire_record_decode(event->payload + offset,
                                event->payload_size - offset,
                                &record, &consumed) || record.raw_tag != 3 ||
        record.payload_size != 8) return 0;
    out->accepted_samples = pxa_load_u64(record.payload);
    offset += consumed;
    if (!pxa_wire_record_decode(event->payload + offset,
                                event->payload_size - offset,
                                &record, &consumed) || record.raw_tag != 4 ||
        record.payload_size != 4) return 0;
    out->queued_samples = pxa_load_u32(record.payload);
    offset += consumed;
    if (!pxa_wire_record_decode(event->payload + offset,
                                event->payload_size - offset,
                                &record, &consumed) || record.raw_tag != 5 ||
        record.payload_size != 4 || offset + consumed != event->payload_size)
        return 0;
    out->flags = pxa_load_u32(record.payload);
    return out->accepted_samples <= out->submitted_samples &&
           out->queued_samples <= out->submitted_samples &&
           (out->flags & ~UINT32_C(1)) == 0;
}

static inline int32_t pxa_audio_write_pcm(uint64_t session,
                                              uint8_t *pcm, uint32_t size) {
    if ((session >> 32) == 0 || (pcm == NULL && size != 0)) return -1;
    return pxa_io(session, PXA_AUDIO_IO_WRITE, pcm, size);
}

static inline int32_t pxa_audio_play_tone(
    uint64_t session, uint8_t waveform, uint16_t frequency_hz,
    uint16_t duration_ms, int16_t gain_db_q8,
    uint16_t attack_ms, uint16_t release_ms, uint16_t delay_ms) {
    uint8_t command[14];
    if ((session >> 32) == 0 || waveform > PXA_AUDIO_TONE_NOISE ||
        frequency_hz < 40 || frequency_hz > 8000 || duration_ms < 10 ||
        duration_ms > 1000 || gain_db_q8 < -60 * 256 || gain_db_q8 > 0 ||
        attack_ms > duration_ms || release_ms > duration_ms ||
        delay_ms > 1000) return -1;
    pxa_store_u16(command, frequency_hz);
    pxa_store_u16(command + 2, duration_ms);
    pxa_store_u16(command + 4, (uint16_t)gain_db_q8);
    command[6] = waveform;
    command[7] = 0;
    pxa_store_u16(command + 8, attack_ms);
    pxa_store_u16(command + 10, release_ms);
    pxa_store_u16(command + 12, delay_ms);
    return pxa_io(session, PXA_AUDIO_IO_PLAY_TONE,
                     command, sizeof(command));
}

static inline int32_t pxa_audio_play_asset(
    uint64_t session, const char *path, size_t path_size, int loop,
    int16_t gain_db_q8, uint8_t *command, size_t capacity) {
    if ((session >> 32) == 0 || path == NULL || path_size == 0 ||
        path_size > UINT16_MAX - 8u || command == NULL ||
        capacity < path_size + 8u || gain_db_q8 < -60 * 256 ||
        gain_db_q8 > 0) return -1;
    pxa_store_u16(command, (uint16_t)path_size);
    pxa_store_u16(command + 2, (uint16_t)gain_db_q8);
    command[4] = loop ? PXA_AUDIO_ASSET_LOOP : 0;
    command[5] = command[6] = command[7] = 0;
    for (size_t i = 0; i < path_size; ++i)
        command[8 + i] = (uint8_t)path[i];
    return pxa_io(session, PXA_AUDIO_IO_PLAY_ASSET,
                     command, (uint32_t)(path_size + 8u));
}

static inline int32_t pxa_audio_control_asset(uint64_t session,
                                                   uint8_t action,
                                                   int16_t gain_db_q8) {
    uint8_t command[4];
    if ((session >> 32) == 0 || action < PXA_AUDIO_ASSET_PAUSE ||
        action > PXA_AUDIO_ASSET_SET_GAIN ||
        (action == PXA_AUDIO_ASSET_SET_GAIN
            ? (gain_db_q8 < -60 * 256 || gain_db_q8 > 0)
            : gain_db_q8 != 0)) return -1;
    command[0] = action;
    command[1] = 0;
    pxa_store_u16(command + 2, (uint16_t)gain_db_q8);
    return pxa_io(session, PXA_AUDIO_IO_CONTROL_ASSET,
                     command, sizeof(command));
}

/* Convenience helpers preserve the asynchronous request contract. Wait for
 * the matching result token after open/commit/query/flush before depending on it. */
static inline int32_t pxa_audio_commit_gain(uint64_t token,
                                               uint64_t session,
                                               int16_t gain_db_q8) {
    const pxa_audio_graph_t graph = {gain_db_q8, NULL, 0};
    return pxa_audio_commit_graph(token, session, &graph);
}
static inline int32_t pxa_audio_query(uint64_t token, uint64_t session) {
    return pxa_audio_session_request(PXA_AUDIO_QUERY_STATE, token, session);
}
static inline int32_t pxa_audio_flush(uint64_t token, uint64_t session) {
    return pxa_audio_session_request(PXA_AUDIO_FLUSH, token, session);
}
/* A short notification with click-reducing attack/release defaults. */
static inline int32_t pxa_audio_beep(uint64_t session, uint16_t frequency_hz,
                                       uint16_t duration_ms, int16_t gain_db_q8) {
    return pxa_audio_play_tone(session, PXA_AUDIO_TONE_SINE,
        frequency_hz, duration_ms, gain_db_q8, 3, 5, 0);
}
/* Audio 0.6: play an Assets 1.3 prepared PCM handle. The accepted voice keeps
 * an independent reference, so the caller can close its Guest handle. */
static inline int32_t pxa_audio_play_sound(uint64_t session,uint64_t sound,int16_t gain_db_q8) {
    if (!(session >> 32) || !(sound >> 32) || gain_db_q8 > 0 || gain_db_q8 < -60*256) return -1;
    uint8_t command[12] = {0};
    pxa_store_u64(command,sound); pxa_store_u16(command+8,(uint16_t)gain_db_q8);
    return pxa_io(session,PXA_AUDIO_IO_PLAY_SOUND,command,sizeof(command));
}
/* Audio 0.7: success returns accepted bytes and a nonzero playback instance.
 * It does not imply decoder readiness. Match later events by session+instance.
 * On rejection, instance is zero and existing music is unchanged. */
static inline int32_t pxa_audio_play_music(uint64_t session,const char *path,
    int loop,int16_t gain_db_q8,uint64_t *instance) {
    uint8_t command[528]={0};
    if (!instance) return -1;
    *instance=0;
    if (!(session>>32) || !path || gain_db_q8>0 || gain_db_q8 < -60*256) return -1;
    uint32_t length=0;
    while (length<512 && path[length]) ++length;
    if (!length || length==512) return -1;
    pxa_store_u16(command+8,(uint16_t)length);
    pxa_store_u16(command+10,(uint16_t)gain_db_q8);
    command[12]=loop ? PXA_AUDIO_ASSET_LOOP : 0;
    for (uint32_t i=0;i<length;++i) command[16+i]=(uint8_t)path[i];
    int32_t status=pxa_io(session,PXA_AUDIO_IO_PLAY_MUSIC,command,length+16);
    if (status==(int32_t)(length+16)) {
        *instance=pxa_load_u64(command);
        if (!*instance) return -11;
    }
    return status;
}
/* Bounded stack scratch; use play_asset directly to reuse a caller buffer. */
static inline int32_t pxa_audio_play_file(uint64_t session, const char *path,
                                            int loop, int16_t gain_db_q8) {
    uint8_t command[520];
    size_t size = 0;
    if (path == NULL) return -1;
    while (size < 512 && path[size]) ++size;
    if (size == 512) return -1;
    return pxa_audio_play_asset(session, path, size, loop, gain_db_q8,
                                    command, sizeof(command));
}

#endif
