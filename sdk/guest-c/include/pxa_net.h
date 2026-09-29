#ifndef PXA_GUEST_NET_H
#define PXA_GUEST_NET_H

#include "pxa_core.h"

#define PXA_NET_SERVICE 9u
#define PXA_NET_FETCH 1u
#define PXA_NET_HTTP_REQUEST 2u
#define PXA_NET_GET 1u
#define PXA_NET_HEAD 2u
#define PXA_NET_POST 3u
#define PXA_NET_PUT 4u
#define PXA_NET_PATCH 5u
#define PXA_NET_DELETE 6u
#define PXA_NET_IO_READ 1u
#define PXA_NET_BODY_PRESENT 1u
#define PXA_NET_BODY_LENGTH_KNOWN 2u
#define PXA_NET_MAX_HEADERS 8u
#define PXA_NET_MAX_URL 512u
#define PXA_NET_MAX_BODY 2048u
#define PXA_NET_MAX_RESPONSE_BODY_BYTES 262144u
#define PXA_NET_MAX_HEADER_BLOCK 2048u

typedef struct {
    const char *name;
    uint16_t name_size;
    const uint8_t *value;
    uint16_t value_size;
} pxa_net_header_t;

typedef struct {
    const char *url;
    uint16_t url_size;
    uint16_t method;
    uint64_t permission_handle;
    uint32_t max_response_bytes;
    uint32_t timeout_ms;
    const pxa_net_header_t *headers;
    uint16_t header_count;
    const uint8_t *body;
    uint16_t body_size;
    const char *const *wanted_headers;
    const uint16_t *wanted_header_sizes;
    uint16_t wanted_header_count;
} pxa_net_request_t;

/* All views borrow the callback event and expire when the callback returns. */
typedef struct {
    int32_t status;
    uint16_t status_code;
    const uint8_t *content_type;
    uint16_t content_type_size;
    uint64_t body_handle;
    uint64_t body_length;
    uint32_t flags;
    uint16_t header_count;
    pxa_net_header_t headers[PXA_NET_MAX_HEADERS];
} pxa_net_result_t;

static inline int pxa_net_equal(const char *a, const char *b, size_t size) {
    if (a == NULL || b == NULL) return 0;
    for (size_t i = 0; i < size; ++i) if (a[i] != b[i]) return 0;
    return 1;
}

static inline int pxa_net_header_name_valid(const char *name,
                                                size_t size) {
    if (name == NULL || size == 0 || size > 64) return 0;
    for (size_t i = 0; i < size; ++i) {
        const uint8_t c = (uint8_t)name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
              c == '\'' || c == '*' || c == '+' || c == '-' || c == '.' ||
              c == '^' || c == '_' || c == '`' || c == '|' || c == '~'))
            return 0;
    }
    return 1;
}

static inline int pxa_net_header_value_valid(const uint8_t *value,
                                                 size_t size) {
    if ((value == NULL && size != 0) || size > 256) return 0;
    for (size_t i = 0; i < size; ++i)
        if (value[i] != '\t' && (value[i] < 0x20u || value[i] > 0x7eu))
            return 0;
    return 1;
}

static inline int pxa_net_append(uint8_t *packet, size_t capacity,
                                     size_t *offset, uint16_t tag,
                                     const uint8_t *data, size_t size) {
    size_t written = 0;
    if (*offset > capacity ||
        !pxa_wire_record_encode(packet + *offset, capacity - *offset,
                                tag, data, size, &written)) return 0;
    *offset += written;
    return 1;
}

/* One caller-owned packet; no second payload buffer or heap allocation. */
static inline int pxa_net_build(
    uint8_t *packet, size_t capacity, uint16_t opcode, uint64_t token,
    const pxa_net_request_t *request, uint32_t *written) {
    uint8_t scalar[8];
    size_t offset = PXA_HEADER_BYTES;
    size_t header_bytes = 0;
    if (written != NULL) *written = 0;
    if (packet == NULL || written == NULL || request == NULL || token == 0 ||
        (opcode != PXA_NET_FETCH && opcode != PXA_NET_HTTP_REQUEST) ||
        capacity < PXA_HEADER_BYTES ||
        request->url == NULL || request->url_size == 0 ||
        request->url_size > PXA_NET_MAX_URL ||
        (request->permission_handle >> 32) == 0 ||
        request->max_response_bytes == 0 ||
        request->max_response_bytes > 262144u ||
        request->method < PXA_NET_GET ||
        request->method > PXA_NET_DELETE ||
        (opcode == PXA_NET_FETCH && request->method != PXA_NET_GET) ||
        request->header_count > PXA_NET_MAX_HEADERS ||
        request->wanted_header_count > PXA_NET_MAX_HEADERS ||
        request->body_size > PXA_NET_MAX_BODY ||
        (request->headers == NULL && request->header_count != 0) ||
        (request->body == NULL && request->body_size != 0) ||
        (request->wanted_headers == NULL && request->wanted_header_count != 0) ||
        (request->wanted_header_sizes == NULL &&
         request->wanted_header_count != 0) ||
        ((request->method == PXA_NET_GET ||
          request->method == PXA_NET_HEAD) && request->body_size != 0) ||
        (opcode == PXA_NET_HTTP_REQUEST &&
         (request->timeout_ms < 100u || request->timeout_ms > 60000u)))
        return 0;
    if (capacity > PXA_MAX_CONTROL_BYTES)
        capacity = PXA_MAX_CONTROL_BYTES;
    if (!pxa_net_append(packet, capacity, &offset, 1,
                           (const uint8_t *)request->url,
                           request->url_size)) return 0;
    pxa_store_u16(scalar, request->method);
    if (!pxa_net_append(packet, capacity, &offset, 2, scalar, 2)) return 0;
    pxa_store_u64(scalar, request->permission_handle);
    if (!pxa_net_append(packet, capacity, &offset, 3, scalar, 8)) return 0;
    pxa_store_u32(scalar, request->max_response_bytes);
    if (!pxa_net_append(packet, capacity, &offset, 4, scalar, 4)) return 0;
    if (opcode == PXA_NET_HTTP_REQUEST) {
        pxa_store_u32(scalar, request->timeout_ms);
        if (!pxa_net_append(packet, capacity, &offset, 8, scalar, 4)) return 0;
        for (uint16_t i = 0; i < request->header_count; ++i) {
            const pxa_net_header_t *h = &request->headers[i];
            size_t nested_size = 8u + h->name_size + h->value_size;
            if (!pxa_net_header_name_valid(h->name, h->name_size) ||
                !pxa_net_header_value_valid(h->value, h->value_size) ||
                nested_size + 4u > PXA_NET_MAX_HEADER_BLOCK - header_bytes ||
                offset > capacity || capacity - offset < nested_size + 4u)
                return 0;
            static const char *const forbidden[] = {
                "connection", "content-length", "host", "proxy-connection",
                "te", "trailer", "transfer-encoding", "upgrade"};
            for (size_t j = 0; j < sizeof(forbidden) / sizeof(forbidden[0]); ++j) {
                size_t name_size = 0;
                while (forbidden[j][name_size] != '\0') ++name_size;
                if (h->name_size == name_size &&
                    pxa_net_equal(h->name, forbidden[j], name_size)) return 0;
            }
            for (uint16_t j = 0; j < i; ++j)
                if (request->headers[j].name_size == h->name_size &&
                    pxa_net_equal(request->headers[j].name, h->name,
                                     h->name_size)) return 0;
            pxa_store_u16(packet + offset, 9);
            pxa_store_u16(packet + offset + 2, (uint16_t)nested_size);
            offset += 4;
            if (!pxa_net_append(packet, capacity, &offset, 1,
                                   (const uint8_t *)h->name, h->name_size) ||
                !pxa_net_append(packet, capacity, &offset, 2,
                                   h->value, h->value_size)) return 0;
            header_bytes += nested_size + 4u;
        }
        if (request->body_size != 0 &&
            !pxa_net_append(packet, capacity, &offset, 10,
                               request->body, request->body_size)) return 0;
        for (uint16_t i = 0; i < request->wanted_header_count; ++i) {
            const char *name = request->wanted_headers[i];
            const uint16_t size = request->wanted_header_sizes[i];
            if (!pxa_net_header_name_valid(name, size)) return 0;
            for (uint16_t j = 0; j < i; ++j)
                if (request->wanted_header_sizes[j] == size &&
                    pxa_net_equal(request->wanted_headers[j], name, size))
                    return 0;
            if (!pxa_net_append(packet, capacity, &offset, 11,
                                   (const uint8_t *)name, size)) return 0;
        }
    }
    return pxa_finish_message_in_place(packet, capacity,
                                          PXA_NET_SERVICE, opcode,
                                          token, offset, written);
}

static inline int32_t pxa_net_submit(
    uint8_t *packet, size_t capacity, uint16_t opcode, uint64_t token,
    const pxa_net_request_t *request) {
    uint32_t written = 0;
    if (!pxa_net_build(packet, capacity, opcode, token, request,
                          &written)) return -1;
    return pxa_submit(packet, written);
}

static inline int pxa_net_parse_header(const uint8_t *data, size_t size,
                                           pxa_net_header_t *out) {
    pxa_wire_record_view_t record;
    size_t consumed = 0;
    if (!pxa_wire_record_decode(data, size, &record, &consumed) ||
        record.raw_tag != 1 ||
        !pxa_net_header_name_valid((const char *)record.payload,
                                       record.payload_size)) return 0;
    out->name = (const char *)record.payload;
    out->name_size = record.payload_size;
    data += consumed;
    size -= consumed;
    if (!pxa_wire_record_decode(data, size, &record, &consumed) ||
        record.raw_tag != 2 || consumed != size ||
        !pxa_net_header_value_valid(record.payload,
                                        record.payload_size)) return 0;
    out->value = record.payload;
    out->value_size = record.payload_size;
    return 1;
}

static inline int pxa_net_parse_result(
    const pxa_event_t *event, uint64_t token, uint16_t opcode,
    pxa_net_result_t *out) {
    size_t offset = 4;
    size_t header_bytes = 0;
    uint16_t previous = 0;
    unsigned seen = 0;
    pxa_wire_record_view_t record;
    size_t consumed = 0;
    if (out == NULL) return 0;
    pxa_zero(out, sizeof(*out));
    if (event == NULL || token == 0 || event->token != token ||
        event->service != PXA_NET_SERVICE || event->opcode != opcode ||
        (opcode != PXA_NET_FETCH && opcode != PXA_NET_HTTP_REQUEST) ||
        event->payload == NULL || event->payload_size < 4) return 0;
    out->status = (int32_t)pxa_load_u32(event->payload);
    if (out->status != 0) return event->payload_size == 4;
    while (offset < event->payload_size) {
        if (!pxa_wire_record_decode(event->payload + offset,
                                    event->payload_size - offset,
                                    &record, &consumed) ||
            record.raw_tag < previous) return 0;
        previous = record.raw_tag;
        if (record.optional) {
            offset += consumed;
            continue;
        }
        if (record.tag == 5 && !(seen & 1u) && record.payload_size == 2) {
            out->status_code = pxa_load_u16(record.payload);
            seen |= 1u;
        } else if (record.tag == 6 && !(seen & 2u) &&
                   record.payload_size <= 96u) {
            for (size_t i = 0; i < record.payload_size; ++i)
                if (record.payload[i] < 0x20u ||
                    record.payload[i] > 0x7eu) return 0;
            out->content_type = record.payload;
            out->content_type_size = record.payload_size;
            seen |= 2u;
        } else if (record.tag == 7 && !(seen & 4u) &&
                   record.payload_size == 8) {
            out->body_handle = pxa_load_u64(record.payload);
            if ((out->body_handle >> 32) == 0) return 0;
            seen |= 4u;
        } else if (opcode == PXA_NET_HTTP_REQUEST && record.tag == 9 &&
                   out->header_count < PXA_NET_MAX_HEADERS &&
                   record.payload_size + 4u <=
                       PXA_NET_MAX_HEADER_BLOCK - header_bytes &&
                   pxa_net_parse_header(record.payload,
                                            record.payload_size,
                                            &out->headers[out->header_count])) {
            const pxa_net_header_t *h = &out->headers[out->header_count];
            for (uint16_t i = 0; i < out->header_count; ++i)
                if (out->headers[i].name_size == h->name_size &&
                    pxa_net_equal(out->headers[i].name, h->name,
                                     h->name_size)) return 0;
            header_bytes += record.payload_size + 4u;
            ++out->header_count;
        } else if (opcode == PXA_NET_HTTP_REQUEST && record.tag == 12 &&
                   !(seen & 8u) && record.payload_size == 8) {
            out->body_length = pxa_load_u64(record.payload);
            seen |= 8u;
        } else if (opcode == PXA_NET_HTTP_REQUEST && record.tag == 13 &&
                   !(seen & 16u) && record.payload_size == 4) {
            out->flags = pxa_load_u32(record.payload);
            seen |= 16u;
        } else return 0;
        offset += consumed;
    }
    if ((seen & 3u) != 3u || out->status_code < 100 ||
        out->status_code > 599) return 0;
    if (opcode == PXA_NET_FETCH)
        return seen == 7u;
    return (seen & 16u) != 0 &&
           (out->flags & ~(PXA_NET_BODY_PRESENT |
                           PXA_NET_BODY_LENGTH_KNOWN)) == 0 &&
           (((out->flags & PXA_NET_BODY_PRESENT) != 0) ==
            ((seen & 4u) != 0)) &&
           (((out->flags & PXA_NET_BODY_LENGTH_KNOWN) != 0) ==
            ((seen & 8u) != 0)) &&
           (((out->flags & PXA_NET_BODY_PRESENT) != 0) ||
            out->body_length == 0);
}

#endif
