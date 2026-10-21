#ifndef PXA_GUEST_HTTP_H
#define PXA_GUEST_HTTP_H

#include "pxa_net.h"
#include "pxa_resource.h"

#define PXA_HTTP_OPEN 3u
#define PXA_HTTP_CAPABILITIES 4u
#define PXA_HTTP_IO_RESPONSE UINT32_C(4)
#define PXA_HTTP_IO_RESPONSE_SIZE UINT32_C(5)
#define PXA_HTTP_IO_FINISH_UPLOAD UINT32_C(6)

typedef struct {
    const char *url;
    uint16_t url_size, method;
    uint64_t permission_handle;
    const pxa_net_header_t *headers;
    uint16_t header_count;
    const char *const *wanted_headers;
    const uint16_t *wanted_header_sizes;
    uint16_t wanted_header_count;
    const uint8_t *body;
    uint16_t body_size;
    uint64_t upload_bytes, max_body_bytes;
    uint32_t buffer_bytes, header_bytes, timeout_ms;
} pxa_http_request_t;

typedef struct { int32_t status; uint64_t handle; } pxa_http_open_result_t;
typedef struct {
    int32_t status;
    uint32_t features, max_buffer_bytes, max_header_bytes;
    uint32_t max_request_header_bytes, max_url_bytes;
    uint64_t max_upload_bytes;
    uint32_t max_concurrent_requests;
} pxa_http_capabilities_t;
typedef struct {
    uint16_t status_code;
    uint8_t body_length_known;
    uint64_t body_length;
    const uint8_t *headers; /* Borrowed from the caller's response buffer. */
    size_t headers_size;
} pxa_http_response_t;

static inline pxa_http_request_t pxa_http_request_defaults(void) {
    pxa_http_request_t request;
    pxa_zero(&request, sizeof(request));
    request.method = PXA_NET_GET;
    request.max_body_bytes = 1024u * 1024u;
    request.buffer_bytes = 4096; request.header_bytes = 1024; request.timeout_ms = 15000;
    return request;
}

static inline int pxa_http_header_name_valid(const char *name, size_t size) {
    if (!name || !size || size > 64) return 0;
    for (size_t i = 0; i < size; ++i) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));
        if (!pxa_net_header_name_valid(&c, 1)) return 0;
    }
    return 1;
}
static inline int pxa_http_header_value_valid(const uint8_t *value, size_t size) {
    if (!value && size) return 0;
    for (size_t i = 0; i < size; ++i)
        if (value[i] != '\t' && (value[i] < 0x20 || value[i] == 0x7f)) return 0;
    return 1;
}
static inline int pxa_http_header_equal(const char *a, size_t a_size,
    const char *b, size_t b_size) {
    if (a_size != b_size || (!a && a_size) || (!b && b_size)) return 0;
    for (size_t i = 0; i < a_size; ++i) {
        unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + 32);
        if (x != y) return 0;
    }
    return 1;
}

/* Encodes into one caller-owned packet. Inputs must not overlap packet.
 * HTTP streaming uses separate body and buffering budgets, with no Guest heap. */
static inline int pxa_http_build_open(uint8_t *packet, size_t capacity,
    uint64_t token, const pxa_http_request_t *r, uint32_t *written) {
    static const char *const forbidden[] = {"connection", "content-length", "host",
        "proxy-connection", "te", "trailer", "transfer-encoding", "upgrade", "expect"};
    static const uint8_t forbidden_sizes[] = {10, 14, 4, 16, 2, 7, 17, 7, 6};
    uint8_t scalar[8];
    size_t size, header_bytes = 0, offset = PXA_HEADER_BYTES;
    uint64_t upload;
    if (written) *written = 0;
    if (!packet || !written || !r || !token || !r->url || !r->url_size ||
        r->url_size > 512 || !(r->permission_handle >> 32) || r->method < 1 || r->method > 6 ||
        r->buffer_bytes < 512 || r->buffer_bytes > 65536 ||
        (r->buffer_bytes & (r->buffer_bytes - 1)) || r->header_bytes < 16 ||
        r->header_bytes > 4096 || !r->max_body_bytes || r->timeout_ms < 100 || r->timeout_ms > 60000 ||
        r->header_count > 8 || r->wanted_header_count > 8 || r->body_size > 2048 ||
        r->body_size > r->buffer_bytes / 2 || (r->body_size && !r->body) ||
        (r->header_count && !r->headers) ||
        (r->wanted_header_count && (!r->wanted_headers || !r->wanted_header_sizes)) ||
        !((r->url_size >= 7 && pxa_net_equal(r->url, "http://", 7)) ||
          (r->url_size >= 8 && pxa_net_equal(r->url, "https://", 8)))) return 0;
    upload = r->body_size ? r->body_size : r->upload_bytes;
    if (upload > INT32_MAX || (r->body_size && r->upload_bytes && r->upload_bytes != upload) ||
        ((r->method == PXA_NET_GET || r->method == PXA_NET_HEAD) && upload)) return 0;
    for (size_t i = 0; i < r->url_size; ++i)
        if ((uint8_t)r->url[i] < 0x21 || (uint8_t)r->url[i] > 0x7e) return 0;
    size = 70u + r->url_size + (r->body_size ? 4u + r->body_size : 0u);
    for (size_t i = 0; i < r->header_count; ++i) {
        const pxa_net_header_t *h = &r->headers[i];
        if (!pxa_http_header_name_valid(h->name, h->name_size) ||
            !pxa_http_header_value_valid(h->value, h->value_size) ||
            12u + h->name_size + h->value_size > 2048u - header_bytes) return 0;
        for (size_t j = 0; j < sizeof(forbidden_sizes); ++j)
            if (pxa_http_header_equal(h->name, h->name_size, forbidden[j], forbidden_sizes[j])) return 0;
        for (size_t j = 0; j < i; ++j)
            if (pxa_http_header_equal(h->name, h->name_size,
                r->headers[j].name, r->headers[j].name_size)) return 0;
        header_bytes += 12u + h->name_size + h->value_size;
    }
    size += header_bytes;
    for (size_t i = 0; i < r->wanted_header_count; ++i) {
        if (!pxa_http_header_name_valid(r->wanted_headers[i], r->wanted_header_sizes[i])) return 0;
        for (size_t j = 0; j < i; ++j)
            if (pxa_http_header_equal(r->wanted_headers[i], r->wanted_header_sizes[i],
                r->wanted_headers[j], r->wanted_header_sizes[j])) return 0;
        size += 4u + r->wanted_header_sizes[i];
    }
    if (size > PXA_MAX_CONTROL_BYTES - PXA_HEADER_BYTES || capacity < PXA_HEADER_BYTES + size) return 0;
    if (!pxa_net_append(packet, capacity, &offset, 1, (const uint8_t *)r->url, r->url_size)) return 0;
    pxa_store_u16(scalar, r->method);
    if (!pxa_net_append(packet, capacity, &offset, 2, scalar, 2)) return 0;
    pxa_store_u64(scalar, r->permission_handle);
    if (!pxa_net_append(packet, capacity, &offset, 3, scalar, 8)) return 0;
    pxa_store_u64(scalar, r->max_body_bytes);
    if (!pxa_net_append(packet, capacity, &offset, 4, scalar, 8)) return 0;
    pxa_store_u32(scalar, r->timeout_ms);
    if (!pxa_net_append(packet, capacity, &offset, 8, scalar, 4)) return 0;
    for (size_t i = 0; i < r->header_count; ++i) {
        const pxa_net_header_t *h = &r->headers[i];
        pxa_store_u16(packet + offset, 9);
        pxa_store_u16(packet + offset + 2, (uint16_t)(8u + h->name_size + h->value_size));
        offset += 4;
        if (!pxa_net_append(packet, capacity, &offset, 1, (const uint8_t *)h->name, h->name_size) ||
            !pxa_net_append(packet, capacity, &offset, 2, h->value, h->value_size)) return 0;
    }
    if (r->body_size && !pxa_net_append(packet, capacity, &offset, 10, r->body, r->body_size)) return 0;
    for (size_t i = 0; i < r->wanted_header_count; ++i)
        if (!pxa_net_append(packet, capacity, &offset, 11,
            (const uint8_t *)r->wanted_headers[i], r->wanted_header_sizes[i])) return 0;
    pxa_store_u32(scalar, r->buffer_bytes);
    if (!pxa_net_append(packet, capacity, &offset, 14, scalar, 4)) return 0;
    pxa_store_u64(scalar, upload);
    if (!pxa_net_append(packet, capacity, &offset, 15, scalar, 8)) return 0;
    pxa_store_u32(scalar, r->header_bytes);
    if (!pxa_net_append(packet, capacity, &offset, 16, scalar, 4)) return 0;
    return pxa_finish_message_in_place(packet, capacity, PXA_NET_SERVICE,
        PXA_HTTP_OPEN, token, offset, written);
}

static inline int32_t pxa_http_open(uint64_t token, const pxa_http_request_t *request,
    uint8_t *packet, size_t capacity) {
    uint32_t size;
    if (!pxa_http_build_open(packet, capacity, token, request, &size)) return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_control_packet(packet, size);
}
static inline int32_t pxa_http_capabilities(uint64_t token) {
    if (!token) return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_control_call(PXA_NET_SERVICE, PXA_HTTP_CAPABILITIES, token, NULL, 0);
}
static inline int pxa_http_parse_open(const pxa_event_t *event, uint64_t token,
    pxa_http_open_result_t *out) {
    if (!out) return 0;
    pxa_zero(out, sizeof(*out));
    if (!event || !token || event->token != token || event->service != PXA_NET_SERVICE ||
        event->opcode != PXA_HTTP_OPEN || !event->payload || event->payload_size < 4) return 0;
    out->status = (int32_t)pxa_load_u32(event->payload);
    if (out->status) return event->payload_size == 4;
    if (event->payload_size >= 12) out->handle = pxa_load_u64(event->payload + 4);
    return event->payload_size == 12 && (out->handle >> 32) != 0;
}
static inline int pxa_http_parse_capabilities(const pxa_event_t *event, uint64_t token,
    pxa_http_capabilities_t *out) {
    const uint8_t *p;
    if (!out) return 0;
    pxa_zero(out, sizeof(*out));
    if (!event || !token || event->token != token || event->service != PXA_NET_SERVICE ||
        event->opcode != PXA_HTTP_CAPABILITIES || !event->payload || event->payload_size < 4) return 0;
    out->status = (int32_t)pxa_load_u32(event->payload);
    if (out->status) return event->payload_size == 4;
    if (event->payload_size != 36) return 0;
    p = event->payload + 4;
    out->features = pxa_load_u32(p); out->max_buffer_bytes = pxa_load_u32(p + 4);
    out->max_header_bytes = pxa_load_u32(p + 8); out->max_request_header_bytes = pxa_load_u32(p + 12);
    out->max_url_bytes = pxa_load_u32(p + 16); out->max_upload_bytes = pxa_load_u64(p + 20);
    out->max_concurrent_requests = pxa_load_u32(p + 28);
    return 1;
}

/* Iterate validated response fields without a fixed per-header array.
 * Duplicate response headers are preserved in wire order. */
static inline int pxa_http_header_next(const pxa_http_response_t *response,
    size_t *offset, pxa_net_header_t *out) {
    const uint8_t *p;
    size_t size;
    if (!response || !offset || !out || !response->headers || *offset > response->headers_size ||
        response->headers_size - *offset < 4) return 0;
    p = response->headers + *offset;
    size = 4u + pxa_load_u16(p) + pxa_load_u16(p + 2);
    if (size > response->headers_size - *offset) return 0;
    out->name = (const char *)p + 4; out->name_size = pxa_load_u16(p);
    out->value = p + 4 + out->name_size; out->value_size = pxa_load_u16(p + 2);
    *offset += size;
    return 1;
}
static inline int pxa_http_parse_response(const uint8_t *bytes, size_t size,
    pxa_http_response_t *out) {
    pxa_net_header_t header;
    size_t offset = 0;
    uint16_t flags;
    if (!out) return 0;
    pxa_zero(out, sizeof(*out));
    if (!bytes || size < 16 || size > 4096) return 0;
    out->status_code = pxa_load_u16(bytes); flags = pxa_load_u16(bytes + 2);
    if (out->status_code < 200 || out->status_code > 599 || (flags & ~1u) ||
        pxa_load_u32(bytes + 4) != size - 16) return 0;
    out->body_length_known = (uint8_t)(flags & 1u); out->body_length = pxa_load_u64(bytes + 8);
    if (!out->body_length_known && out->body_length != UINT64_MAX) return 0;
    out->headers = bytes + 16; out->headers_size = size - 16;
    while (pxa_http_header_next(out, &offset, &header))
        if (!pxa_http_header_name_valid(header.name, header.name_size) ||
            !pxa_http_header_value_valid(header.value, header.value_size)) return 0;
    return offset == out->headers_size;
}

/* WOULD_BLOCK: wait for HEADERS/READ/WRITE through pxa_resource_wait, then
 * retry. EOF is a successful zero-byte read. Close with pxa_close_handle. */
static inline int32_t pxa_http_response_size(uint64_t handle) {
    if (!handle) return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_io(handle, PXA_HTTP_IO_RESPONSE_SIZE, NULL, 0);
}
static inline int32_t pxa_http_read_response(uint64_t handle, uint8_t *bytes, uint32_t capacity) {
    if (!handle || !bytes || capacity < 16) return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_io(handle, PXA_HTTP_IO_RESPONSE, bytes, capacity);
}
static inline int32_t pxa_http_read(uint64_t handle, uint8_t *bytes, uint32_t size) {
    if (!handle || (!bytes && size)) return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_io(handle, 1, bytes, size);
}
static inline int32_t pxa_http_write(uint64_t handle, const uint8_t *bytes, uint32_t size) {
    if (!handle || (!bytes && size)) return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_io(handle, 2, (uint8_t *)bytes, size);
}
static inline int32_t pxa_http_finish_upload(uint64_t handle) {
    int32_t status;
    if (!handle) return PXA_STATUS_INVALID_ARGUMENT;
    status = pxa_io(handle, PXA_HTTP_IO_FINISH_UPLOAD, NULL, 0);
    return status > 0 ? PXA_STATUS_PROTOCOL_ERROR : status;
}

#endif
