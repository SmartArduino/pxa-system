/* Network and audio integration fixture using the current Guest SDK. */
#include "pxa.h"
#include "pxa_audio.h"
#include "pxa_net.h"
#include "pxa_permission.h"
#include "pxa_storage.h"

#define NET_PERMISSION_TOKEN UINT64_C(1)
#define AUDIO_PERMISSION_TOKEN UINT64_C(2)
#define NET_TOKEN UINT64_C(3)
#define AUDIO_OPEN_TOKEN UINT64_C(4)
#define AUDIO_GRAPH_TOKEN UINT64_C(5)
#define STORAGE_NET_TOKEN UINT64_C(6)
#define STORAGE_AUDIO_TOKEN UINT64_C(7)

static uint64_t s_audio_session;
static uint8_t s_audio_pcm[640];
static _Alignas(4) int32_t s_psg_output[640];
static uint8_t s_psg_voices[3 * PXA_AUDIO_PSG_VOICE_BYTES];
static int8_t s_psg_wave[32];


/* Exercise real wasm offsets, not Host addresses, through WAMR's serialized
 * pxa_io memory borrow. The output then uses the ordinary PCM playback path. */
static int render_psg(void) {
    pxa_audio_psg_voice_t v = {0};
    v.waveform = PXA_AUDIO_PSG_PULSE; v.period = 1192;
    v.left_gain_q8 = 32; v.right_gain_q8 = 32;
    v.level = 15; v.pulse_mask = 15;
    v.envelope_period = 65536; v.envelope_direction = -1;
    if (!pxa_audio_encode_psg_voice(s_psg_voices, 40, &v)) return 0;
    for (unsigned i = 0; i < 32; ++i) s_psg_wave[i] = (int8_t)((int)i * 8 - 128);
    v.waveform = PXA_AUDIO_PSG_WAVE; v.period = 149;
    v.pulse_mask = 0; v.table_offset = (uint32_t)(uintptr_t)s_psg_wave; v.table_length = 32;
    if (!pxa_audio_encode_psg_voice(s_psg_voices + 40, 40, &v)) return 0;
    v.waveform = PXA_AUDIO_PSG_NOISE; v.period = 64;
    v.table_offset = 0; v.table_length = 0;
    v.noise_width = 15; v.noise_tap = 1; v.cursor = 1;
    if (!pxa_audio_encode_psg_voice(s_psg_voices + 80, 40, &v)) return 0;
    if (pxa_audio_psg_render(s_audio_session, (uint32_t)(uintptr_t)s_psg_output,
        320, (uint32_t)(uintptr_t)s_psg_voices, 3, 128)) return 0;
    if (!pxa_audio_decode_psg_voice(s_psg_voices, 40, &v) ||
        v.phase != 320u * 128 % 1192 || v.cursor != (320u * 128 / 1192) % 8 ||
        v.envelope_phase != 320u * 128 || v.level != 15) return 0;
    unsigned audible = 0;
    for (unsigned i = 0; i < 320; ++i) {
        int32_t sample = (s_psg_output[i * 2] + s_psg_output[i * 2 + 1]) / 2;
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        pxa_store_u16(s_audio_pcm + i * 2, (uint16_t)sample);
        audible |= sample != 0;
    }
    return audible != 0;
}

static int32_t storage_set(uint64_t token, const char *key, size_t key_size) {
    uint8_t packet[96];
    return pxa_storage_request_set(packet, sizeof(packet), token,
                                   key, key_size, (const uint8_t *)"1", 1);
}

static int32_t acquire(uint64_t token, const char *name, size_t name_size,
                       const char *scope, size_t scope_size) {
    uint8_t packet[128];
    uint32_t size = 0;
    if (!pxa_permission_build(packet, sizeof(packet), PXA_PERMISSION_ACQUIRE,
                              token, name, name_size,
                              (const uint8_t *)scope, scope_size, &size))
        return PXA_STATUS_INTERNAL;
    return pxa_control_packet(packet, size);
}

int32_t pxa_app_start(const uint8_t *config, uint32_t config_length) {
    (void)config;
    (void)config_length;
    if (acquire(NET_PERMISSION_TOKEN, "net.client", 10,
                "https://example.test", 20) != PXA_STATUS_OK)
        return PXA_STATUS_INTERNAL;
    return acquire(AUDIO_PERMISSION_TOKEN, "audio.playback", 14,
                   "media", 5);
}

int32_t pxa_app_on_event(const uint8_t *event, uint32_t length) {
    pxa_event_t parsed;
    if (!pxa_parse_event(event, length, &parsed)) return PXA_EVENT_UNHANDLED;
    if (parsed.service == PXA_PERMISSION_SERVICE &&
        parsed.opcode == PXA_PERMISSION_ACQUIRE &&
        parsed.token == NET_PERMISSION_TOKEN) {
        static const char content_type[] = "content-type";
        static const uint8_t content_type_value[] = "application/json";
        static const uint8_t request_body[] = "{\"hello\":true}";
        static const char etag[] = "etag";
        static const char *const wanted_headers[] = {etag};
        static const uint16_t wanted_sizes[] = {sizeof(etag) - 1};
        static const pxa_net_header_t headers[] = {{
            content_type, sizeof(content_type) - 1,
            content_type_value, sizeof(content_type_value) - 1}};
        pxa_permission_acquire_result_t acquired;
        pxa_net_request_t request = {0};
        uint8_t packet[320];
        uint32_t size = 0;
        if (!pxa_permission_parse_acquire(&parsed, NET_PERMISSION_TOKEN,
                                          &acquired) ||
            acquired.status != PXA_STATUS_OK) return PXA_EVENT_UNHANDLED;
        request.method = PXA_NET_POST;
        request.url = "https://example.test/hello";
        request.url_size = sizeof("https://example.test/hello") - 1;
        request.permission_handle = acquired.handle;
        request.max_response_bytes = 1024;
        request.timeout_ms = 2500;
        request.headers = headers;
        request.header_count = 1;
        request.body = request_body;
        request.body_size = sizeof(request_body) - 1;
        request.wanted_headers = wanted_headers;
        request.wanted_header_sizes = wanted_sizes;
        request.wanted_header_count = 1;
        if (!pxa_net_build(packet, sizeof(packet), PXA_NET_HTTP_REQUEST,
                           NET_TOKEN, &request, &size) ||
            pxa_control_packet(packet, size) != PXA_STATUS_OK)
            return PXA_EVENT_UNHANDLED;
        return PXA_EVENT_HANDLED;
    }
    if (parsed.service == PXA_PERMISSION_SERVICE &&
        parsed.opcode == PXA_PERMISSION_ACQUIRE &&
        parsed.token == AUDIO_PERMISSION_TOKEN) {
        pxa_permission_acquire_result_t acquired;
        if (!pxa_permission_parse_acquire(&parsed, AUDIO_PERMISSION_TOKEN,
                                          &acquired) ||
            acquired.status != PXA_STATUS_OK) return PXA_EVENT_UNHANDLED;
        return pxa_audio_open_media(AUDIO_OPEN_TOKEN, acquired.handle) ==
                       PXA_STATUS_OK
                   ? PXA_EVENT_HANDLED : PXA_EVENT_UNHANDLED;
    }
    if (parsed.service == PXA_NET_SERVICE &&
        parsed.opcode == PXA_NET_HTTP_REQUEST) {
        static const uint8_t expected_etag[] = "\"test-v1\"";
        pxa_net_result_t result;
        uint8_t body[8];
        if (!pxa_net_parse_result(&parsed, NET_TOKEN, PXA_NET_HTTP_REQUEST,
                                   &result) ||
            result.status != PXA_STATUS_OK || result.status_code != 200 ||
            result.body_handle == 0 || result.body_length != 2 ||
            result.flags != (PXA_NET_BODY_PRESENT |
                             PXA_NET_BODY_LENGTH_KNOWN) ||
            result.header_count != 1 || result.headers[0].name_size != 4 ||
            result.headers[0].value_size != sizeof(expected_etag) - 1)
            return PXA_EVENT_UNHANDLED;
        for (uint32_t i = 0; i < sizeof(expected_etag) - 1; ++i)
            if (result.headers[0].value[i] != expected_etag[i])
                return PXA_EVENT_UNHANDLED;
        if (pxa_io(result.body_handle, PXA_NET_IO_READ, body,
                   sizeof(body)) != 2 || body[0] != 'h' || body[1] != 'i')
            return PXA_EVENT_UNHANDLED;
        return storage_set(STORAGE_NET_TOKEN, "net_ok", 6) == PXA_STATUS_OK
                   ? PXA_EVENT_HANDLED : PXA_EVENT_UNHANDLED;
    }
    if (parsed.service == PXA_AUDIO_SERVICE &&
        parsed.opcode == PXA_AUDIO_OPEN_SESSION) {
        pxa_audio_open_result_t opened;
        pxa_audio_graph_t graph = {0};
        if (!pxa_audio_parse_open(&parsed, AUDIO_OPEN_TOKEN, &opened) ||
            opened.status != PXA_STATUS_OK || opened.handle == 0)
            return PXA_EVENT_UNHANDLED;
        s_audio_session = opened.handle;
        return pxa_audio_commit_graph(AUDIO_GRAPH_TOKEN, s_audio_session,
                                       &graph) == PXA_STATUS_OK
                   ? PXA_EVENT_HANDLED : PXA_EVENT_UNHANDLED;
    }
    if (parsed.service == PXA_AUDIO_SERVICE &&
        parsed.opcode == PXA_AUDIO_COMMIT_GRAPH) {
        int32_t status = 0;
        if (!pxa_audio_parse_status(&parsed, AUDIO_GRAPH_TOKEN,
                                     PXA_AUDIO_COMMIT_GRAPH, &status) ||
            status != PXA_STATUS_OK || !render_psg() ||
            pxa_audio_write_pcm(s_audio_session, s_audio_pcm,
                                sizeof(s_audio_pcm)) !=
                (int32_t)sizeof(s_audio_pcm))
            return PXA_EVENT_UNHANDLED;
        return storage_set(STORAGE_AUDIO_TOKEN, "audio_ok", 8) ==
                       PXA_STATUS_OK
                   ? PXA_EVENT_HANDLED : PXA_EVENT_UNHANDLED;
    }
    return PXA_EVENT_UNHANDLED;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
