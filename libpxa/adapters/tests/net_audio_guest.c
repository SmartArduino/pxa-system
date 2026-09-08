/* Freestanding PXA guest exercising net + audio: acquires net.client and
 * audio.playback permissions, sends a v1.1 HTTPS request and reads the body
 * through pxa_io, opens an audio session and commits a speaker graph.
 * Outcomes are persisted through Storage. No libc dependency. */

#include "pxa.h"
#include "pxa_audio.h"
#include "pxa_net.h"
#include "pxa_permission.h"

#define PXA_STORAGE_SET 2u
#define PXA_IO_READ 1u

static uint32_t s_request_id = 1;
static uint32_t s_net_permission_handle;
static uint32_t s_audio_permission_handle;
static uint32_t s_audio_session_handle;
static uint8_t s_audio_pcm[640];

static uint32_t pxa_length(const uint8_t *value) {
    uint32_t length = 0;
    while (value[length] != 0) ++length;
    return length;
}

static int32_t storage_set(const char *key, const char *value) {
    uint8_t payload[80];
    uint8_t packet[96];
    pxa_writer_t writer;
    pxa_writer_t message;
    pxa_writer_init(&writer, payload, sizeof(payload));
    if (!pxa_record(&writer, 1, (const uint8_t *)key,
                    (uint32_t)pxa_length((const uint8_t *)key)) ||
        !pxa_record(&writer, 2, (const uint8_t *)value,
                    (uint32_t)pxa_length((const uint8_t *)value))) {
        return PXA_STATUS_INTERNAL;
    }
    pxa_writer_init(&message, packet, sizeof(packet));
    if (!pxa_message(&message, PXA_SERVICE_STORAGE, PXA_STORAGE_SET,
                     s_request_id++, writer.data,
                     (uint32_t)writer.length)) {
        return PXA_STATUS_INTERNAL;
    }
    return pxa_control(message.data, (uint32_t)message.length);
}

uint32_t pxa_app_api_version(void) { return PXA_CORE_VERSION; }

int32_t pxa_app_start(const uint8_t *config, uint32_t config_length) {
    uint8_t payload[64];
    uint8_t packet[128];
    (void)config;
    (void)config_length;
    if (!pxa_permission_acquire(
            s_request_id++, "net.client", 10,
            (const uint8_t *)"https://example.test",
            sizeof("https://example.test") - 1, payload,
            sizeof(payload), packet, sizeof(packet))) {
        return PXA_STATUS_INTERNAL;
    }
    if (!pxa_permission_acquire(s_request_id++, "audio.playback", 14,
                                (const uint8_t *)"media", 5, payload,
                                sizeof(payload), packet, sizeof(packet))) {
        return PXA_STATUS_INTERNAL;
    }
    return PXA_STATUS_OK;
}

int32_t pxa_app_on_event(const uint8_t *event, uint32_t length) {
    pxa_event_t parsed;
    if (!pxa_parse_event(event, length, &parsed)) return PXA_EVENT_UNHANDLED;
    if (parsed.service == PXA_SERVICE_PERMISSION &&
        parsed.opcode == PXA_PERMISSION_ACQUIRE && parsed.request_id == 1) {
        pxa_permission_acquire_result_t acquired;
        static const char content_type[] = "content-type";
        static const uint8_t content_type_value[] = "application/json";
        static const uint8_t request_body[] = "{\"hello\":true}";
        static const char etag[] = "etag";
        static const char *const wanted_headers[] = {etag};
        static const uint16_t wanted_header_lengths[] = {sizeof(etag) - 1};
        static const pxa_net_header_t headers[] = {{
            content_type, sizeof(content_type) - 1, content_type_value,
            sizeof(content_type_value) - 1}};
        pxa_net_http_request_t request = {0};
        uint8_t payload[256];
        uint8_t packet[320];
        if (!pxa_permission_parse_acquire(&parsed, &acquired) ||
            acquired.status != PXA_STATUS_OK) {
            return PXA_EVENT_UNHANDLED;
        }
        s_net_permission_handle = acquired.handle;
        request.method = PXA_NET_METHOD_POST;
        request.url = "https://example.test/hello";
        request.url_length = sizeof("https://example.test/hello") - 1;
        request.permission_handle = s_net_permission_handle;
        request.max_response_bytes = 1024;
        request.timeout_ms = 2500;
        request.headers = headers;
        request.header_count = 1;
        request.body = request_body;
        request.body_length = sizeof(request_body) - 1;
        request.wanted_response_headers = wanted_headers;
        request.wanted_response_header_lengths = wanted_header_lengths;
        request.wanted_response_header_count = 1;
        if (!pxa_net_http_request(s_request_id++, &request, payload,
                                  sizeof(payload), packet, sizeof(packet))) {
            return PXA_EVENT_UNHANDLED;
        }
        return PXA_EVENT_HANDLED;
    }
    if (parsed.service == PXA_SERVICE_PERMISSION &&
        parsed.opcode == PXA_PERMISSION_ACQUIRE && parsed.request_id == 2) {
        pxa_permission_acquire_result_t acquired;
        uint8_t payload[128];
        uint8_t packet[256];
        if (!pxa_permission_parse_acquire(&parsed, &acquired) ||
            acquired.status != PXA_STATUS_OK) {
            return PXA_EVENT_UNHANDLED;
        }
        s_audio_permission_handle = acquired.handle;
        if (!pxa_audio_open_media(s_request_id++, s_audio_permission_handle,
                                  payload, sizeof(payload), packet,
                                  sizeof(packet))) {
            return PXA_EVENT_UNHANDLED;
        }
        return PXA_EVENT_HANDLED;
    }
    if (parsed.service == PXA_SERVICE_NET &&
        parsed.opcode == PXA_NET_HTTP_REQUEST) {
        static const uint8_t expected_etag[] = "\"test-v1\"";
        pxa_net_http_result_t result;
        if (!pxa_net_parse_http_result(&parsed, &result) ||
            result.status != PXA_STATUS_OK || result.status_code != 200 ||
            result.body_handle == 0 || result.body_length != 2 ||
            result.flags != (PXA_NET_RESPONSE_BODY_PRESENT |
                             PXA_NET_RESPONSE_BODY_LENGTH_KNOWN) ||
            result.header_count != 1 || result.headers[0].name_length != 4 ||
            result.headers[0].value_length != sizeof(expected_etag) - 1 ||
            result.headers[0].name[0] != 'e' ||
            result.headers[0].name[1] != 't' ||
            result.headers[0].name[2] != 'a' ||
            result.headers[0].name[3] != 'g') {
            return PXA_EVENT_UNHANDLED;
        }
        {
            uint32_t index;
            for (index = 0; index < sizeof(expected_etag) - 1; ++index) {
                if (result.headers[0].value[index] != expected_etag[index]) {
                    return PXA_EVENT_UNHANDLED;
                }
            }
        }
        {
            uint8_t body[8];
            int32_t count = pxa_io(result.body_handle, PXA_IO_READ, body,
                                   sizeof(body));
            if (count != 2 || body[0] != 'h' || body[1] != 'i') {
                return PXA_EVENT_UNHANDLED;
            }
        }
        return storage_set("net_ok", "1") == PXA_STATUS_OK
                   ? PXA_EVENT_HANDLED
                   : PXA_EVENT_UNHANDLED;
    }
    if (parsed.service == PXA_SERVICE_AUDIO &&
        parsed.opcode == PXA_AUDIO_OPEN_SESSION) {
        uint32_t session_handle = 0;
        uint8_t payload[128];
        uint8_t packet[256];
        /* Records: [status u32] tag3 session_handle, tag4 sample_rate,
         * tag5 channels, tag6 frame_ms. */
        if (parsed.payload_length < 8 ||
            (int32_t)pxa_read_u32(parsed.payload) != PXA_STATUS_OK) {
            return PXA_EVENT_UNHANDLED;
        }
        {
            const uint8_t *cursor = parsed.payload + 4;
            const uint8_t *end = parsed.payload + parsed.payload_length;
            while (cursor + 4 <= end) {
                uint16_t tag = pxa_read_u16(cursor);
                uint16_t length = pxa_read_u16(cursor + 2);
                if (length > (uint32_t)(end - cursor - 4)) {
                    return PXA_EVENT_UNHANDLED;
                }
                if (tag == PXA_AUDIO_SESSION_HANDLE && length == 4) {
                    session_handle = pxa_read_u32(cursor + 4);
                }
                cursor += 4 + length;
            }
        }
        if (session_handle == 0) return PXA_EVENT_UNHANDLED;
        s_audio_session_handle = session_handle;
        if (!pxa_audio_commit_speaker_graph(
                s_request_id++, session_handle, 0, 1000, 0, 1024, payload,
                sizeof(payload), packet, sizeof(packet))) {
            return PXA_EVENT_UNHANDLED;
        }
        return PXA_EVENT_HANDLED;
    }
    if (parsed.service == PXA_SERVICE_AUDIO &&
        parsed.opcode == PXA_AUDIO_COMMIT_GRAPH) {
        int32_t commit_status = 0;
        if (!pxa_audio_parse_status(&parsed, PXA_AUDIO_COMMIT_GRAPH,
                                    &commit_status) ||
            commit_status != PXA_STATUS_OK) {
            return PXA_EVENT_UNHANDLED;
        }
        if (pxa_audio_write_pcm(s_audio_session_handle, s_audio_pcm,
                                sizeof(s_audio_pcm)) !=
            (int32_t)sizeof(s_audio_pcm)) {
            return PXA_EVENT_UNHANDLED;
        }
        return storage_set("audio_ok", "1") == PXA_STATUS_OK
                   ? PXA_EVENT_HANDLED
                   : PXA_EVENT_UNHANDLED;
    }
    return PXA_EVENT_UNHANDLED;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
