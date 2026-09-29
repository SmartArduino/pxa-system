#include <assert.h>
#include <string.h>

#include "pxa_net.h"

int32_t pxa_submit(const uint8_t *data, uint32_t size) {
    (void)data;
    (void)size;
    return 0;
}

int32_t pxa_io(uint64_t handle, uint32_t operation, uint8_t *data,
                  uint32_t size) {
    (void)handle;
    (void)operation;
    (void)data;
    (void)size;
    return 0;
}

int main(void) {
    static const char url[] = "https://example.test/api";
    static const uint8_t body[] = "{}";
    static const uint8_t content_type[] = "application/json";
    static const pxa_net_header_t headers[] = {
        {"content-type", 12, content_type, sizeof(content_type) - 1}};
    static const char *const wanted[] = {"etag"};
    static const uint16_t wanted_sizes[] = {4};
    const uint64_t permission = UINT64_C(0x1234567800000001);
    const uint64_t stream = UINT64_C(0x2345678900000002);
    uint8_t packet[PXA_MAX_CONTROL_BYTES];
    uint8_t result[128];
    uint8_t value[8];
    uint32_t written = 0;
    size_t size = 4;
    size_t n = 0;
    pxa_event_t event;
    pxa_net_result_t parsed;
    pxa_net_request_t request = {0};
    pxa_wire_record_view_t record;
    request.url = url;
    request.url_size = sizeof(url) - 1;
    request.method = PXA_NET_POST;
    request.permission_handle = permission;
    request.max_response_bytes = 1024;
    request.timeout_ms = 2500;
    request.headers = headers;
    request.header_count = 1;
    request.body = body;
    request.body_size = sizeof(body) - 1;
    request.wanted_headers = wanted;
    request.wanted_header_sizes = wanted_sizes;
    request.wanted_header_count = 1;
    assert(pxa_net_build(packet, sizeof(packet), PXA_NET_HTTP_REQUEST,
                            77, &request, &written));
    assert(pxa_parse_event(packet, written, &event));
    assert(event.service == PXA_NET_SERVICE &&
           event.opcode == PXA_NET_HTTP_REQUEST && event.token == 77);
    assert(pxa_wire_record_decode(event.payload, event.payload_size,
                                  &record, &n) && record.tag == 1);
    size_t offset = n;
    assert(pxa_wire_record_decode(event.payload + offset,
                                  event.payload_size - offset,
                                  &record, &n) && record.tag == 2);
    offset += n;
    assert(pxa_wire_record_decode(event.payload + offset,
                                  event.payload_size - offset,
                                  &record, &n) && record.tag == 3 &&
           record.payload_size == 8 &&
           pxa_load_u64(record.payload) == permission);
    request.permission_handle = 1;
    assert(!pxa_net_build(packet, sizeof(packet),
                             PXA_NET_HTTP_REQUEST, 77, &request, &written));
    request.permission_handle = permission;
    request.method = PXA_NET_GET;
    request.body_size = 0;
    request.header_count = 0;
    request.wanted_header_count = 0;
    assert(pxa_net_build(packet, sizeof(packet), PXA_NET_FETCH,
                            78, &request, &written));

    pxa_store_u32(result, 0);
    pxa_store_u16(value, 200);
    assert(pxa_wire_record_encode(result + size, sizeof(result) - size,
                                  5, value, 2, &n));
    size += n;
    assert(pxa_wire_record_encode(result + size, sizeof(result) - size,
                                  6, content_type, sizeof(content_type) - 1,
                                  &n));
    size += n;
    pxa_store_u64(value, stream);
    assert(pxa_wire_record_encode(result + size, sizeof(result) - size,
                                  7, value, 8, &n));
    size += n;
    event.service = PXA_NET_SERVICE;
    event.opcode = PXA_NET_FETCH;
    event.token = 78;
    event.payload = result;
    event.payload_size = (uint32_t)size;
    assert(pxa_net_parse_result(&event, 78, PXA_NET_FETCH, &parsed));
    assert(parsed.status == 0 && parsed.status_code == 200 &&
           parsed.body_handle == stream &&
           parsed.content_type_size == sizeof(content_type) - 1 &&
           memcmp(parsed.content_type, content_type,
                  sizeof(content_type) - 1) == 0);
    assert(!pxa_net_parse_result(&event, 79, PXA_NET_FETCH, &parsed));
    result[size - 10] = 4;
    assert(!pxa_net_parse_result(&event, 78, PXA_NET_FETCH, &parsed));

    {
        uint8_t nested[64];
        size_t nested_size = 0;
        size_t nested_record = 0;
        result[size - 10] = 8;
        assert(pxa_wire_record_encode(nested, sizeof(nested), 1,
                                      (const uint8_t *)"etag", 4,
                                      &nested_record));
        nested_size += nested_record;
        assert(pxa_wire_record_encode(nested + nested_size,
                                      sizeof(nested) - nested_size, 2,
                                      (const uint8_t *)"abc", 3,
                                      &nested_record));
        nested_size += nested_record;
        assert(pxa_wire_record_encode(result + size, sizeof(result) - size,
                                      9, nested, nested_size, &n));
        size += n;
        pxa_store_u64(value, 3);
        assert(pxa_wire_record_encode(result + size, sizeof(result) - size,
                                      12, value, 8, &n));
        size += n;
        pxa_store_u32(value, PXA_NET_BODY_PRESENT |
                                PXA_NET_BODY_LENGTH_KNOWN);
        assert(pxa_wire_record_encode(result + size, sizeof(result) - size,
                                      13, value, 4, &n));
        size += n;
        assert(pxa_wire_record_encode(result + size, sizeof(result) - size,
                                      0x8001u, value, 1, &n));
        size += n;
        event.opcode = PXA_NET_HTTP_REQUEST;
        event.payload_size = (uint32_t)size;
        assert(pxa_net_parse_result(&event, 78,
                                       PXA_NET_HTTP_REQUEST, &parsed));
        assert(parsed.body_handle == stream && parsed.body_length == 3 &&
               parsed.header_count == 1 && parsed.headers[0].name_size == 4 &&
               memcmp(parsed.headers[0].name, "etag", 4) == 0);
        result[size - n - 4] = 0;
        assert(!pxa_net_parse_result(&event, 78,
                                        PXA_NET_HTTP_REQUEST, &parsed));
    }
    return 0;
}
