#include "pxa_net.h"

#include <assert.h>
#include <string.h>

static uint8_t captured[512];
static uint32_t captured_length;

int32_t pxa_control(const uint8_t *data, uint32_t length) {
    assert(length <= sizeof(captured));
    memcpy(captured, data, length);
    captured_length = length;
    return PXA_STATUS_OK;
}

int32_t pxa_io(uint32_t handle, uint32_t operation, uint8_t *data,
               uint32_t length) {
    (void)handle;
    (void)operation;
    (void)data;
    (void)length;
    return PXA_STATUS_UNSUPPORTED;
}

int main(void) {
    static const char url[] = "https://example.test?mode=post";
    static const char content_type_name[] = "content-type";
    static const uint8_t content_type_value[] = "application/json";
    static const uint8_t request_body[] = "{}";
    static const char etag_name[] = "etag";
    static const char *const wanted[] = {etag_name};
    static const uint16_t wanted_lengths[] = {sizeof(etag_name) - 1};
    static const pxa_net_header_t headers[] = {
        {content_type_name, sizeof(content_type_name) - 1,
         content_type_value, sizeof(content_type_value) - 1},
    };
    uint8_t payload[384];
    uint8_t packet[512];
    uint8_t result_payload[192];
    uint8_t result_packet[224];
    uint8_t nested[64];
    uint8_t value[8];
    pxa_writer_t result;
    pxa_writer_t message;
    pxa_writer_t header;
    pxa_event_t event;
    pxa_net_http_request_t request = {0};
    pxa_net_http_result_t decoded;

    request.method = PXA_NET_METHOD_POST;
    request.url = url;
    request.url_length = sizeof(url) - 1;
    request.permission_handle = 0x00010003u;
    request.max_response_bytes = 1024;
    request.timeout_ms = 2500;
    request.headers = headers;
    request.header_count = 1;
    request.body = request_body;
    request.body_length = sizeof(request_body) - 1;
    request.wanted_response_headers = wanted;
    request.wanted_response_header_lengths = wanted_lengths;
    request.wanted_response_header_count = 1;
    assert(pxa_net_http_request(77, &request, payload, sizeof(payload), packet,
                                sizeof(packet)));
    assert(captured_length > 12 && pxa_read_u16(captured) == PXA_SERVICE_NET &&
           pxa_read_u16(captured + 2) == PXA_NET_HTTP_REQUEST &&
           pxa_read_u32(captured + 4) == 77);

    request.timeout_ms = PXA_NET_MIN_TIMEOUT_MS - 1u;
    assert(!pxa_net_http_request(78, &request, payload, sizeof(payload), packet,
                                 sizeof(packet)));
    request.timeout_ms = 2500;
    request.body_length = PXA_NET_MAX_INLINE_BODY_BYTES + 1u;
    assert(!pxa_net_http_request(78, &request, payload, sizeof(payload), packet,
                                 sizeof(packet)));
    request.body_length = sizeof(request_body) - 1;
    {
        static const char forbidden_name[] = "host";
        static const uint8_t forbidden_value[] = "example.test";
        static const pxa_net_header_t forbidden[] = {{
            forbidden_name, sizeof(forbidden_name) - 1, forbidden_value,
            sizeof(forbidden_value) - 1}};
        request.headers = forbidden;
        assert(!pxa_net_http_request(78, &request, payload, sizeof(payload),
                                     packet, sizeof(packet)));
        request.headers = headers;
    }
    request.method = PXA_NET_METHOD_GET;
    assert(!pxa_net_http_request(78, &request, payload, sizeof(payload), packet,
                                 sizeof(packet)));
    request.method = PXA_NET_METHOD_POST;

    pxa_writer_init(&result, result_payload, sizeof(result_payload));
    assert(pxa_put_u32(&result, PXA_STATUS_OK));
    value[0] = 200;
    value[1] = 0;
    assert(pxa_record(&result, PXA_NET_STATUS_CODE, value, 2));
    assert(pxa_record(&result, PXA_NET_CONTENT_TYPE, content_type_value,
                      sizeof(content_type_value) - 1));
    value[0] = 4;
    value[1] = 0;
    value[2] = 1;
    value[3] = 0;
    assert(pxa_record(&result, PXA_NET_BODY_HANDLE, value, 4));
    pxa_writer_init(&header, nested, sizeof(nested));
    assert(pxa_record(&header, 1, (const uint8_t *)etag_name,
                      sizeof(etag_name) - 1));
    assert(pxa_record(&header, 2, (const uint8_t *)"\"test\"", 6));
    assert(pxa_record(&result, PXA_NET_HEADER, nested, header.length));
    value[0] = 11;
    value[1] = value[2] = value[3] = value[4] = value[5] = value[6] = value[7] = 0;
    assert(pxa_record(&result, PXA_NET_BODY_LENGTH, value, 8));
    value[0] = PXA_NET_RESPONSE_BODY_PRESENT |
               PXA_NET_RESPONSE_BODY_LENGTH_KNOWN;
    value[1] = value[2] = value[3] = 0;
    assert(pxa_record(&result, PXA_NET_RESPONSE_FLAGS, value, 4));
    pxa_writer_init(&message, result_packet, sizeof(result_packet));
    assert(pxa_message(&message, PXA_SERVICE_NET, PXA_NET_HTTP_REQUEST, 77,
                       result.data, result.length));
    assert(pxa_parse_event(result_packet, (uint32_t)message.length, &event));
    assert(pxa_net_parse_http_result(&event, &decoded));
    assert(decoded.status == PXA_STATUS_OK && decoded.status_code == 200 &&
           decoded.body_handle == 0x00010004u && decoded.body_length == 11 &&
           decoded.header_count == 1 && decoded.headers[0].name_length == 4 &&
           memcmp(decoded.headers[0].name, "etag", 4) == 0);

    pxa_writer_init(&result, result_payload, sizeof(result_payload));
    assert(pxa_put_u32(&result, (uint32_t)PXA_STATUS_TIMED_OUT));
    pxa_writer_init(&message, result_packet, sizeof(result_packet));
    assert(pxa_message(&message, PXA_SERVICE_NET, PXA_NET_HTTP_REQUEST, 78,
                       result.data, result.length));
    assert(pxa_parse_event(result_packet, (uint32_t)message.length, &event));
    assert(pxa_net_parse_http_result(&event, &decoded) &&
           decoded.status == PXA_STATUS_TIMED_OUT);
    return 0;
}
