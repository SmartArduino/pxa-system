#ifndef PXA_NET_H
#define PXA_NET_H

#include "pxa.h"

#define PXA_SERVICE_NET 9u
#define PXA_NET_FETCH 1u
#define PXA_NET_HTTP_REQUEST 2u
#define PXA_NET_METHOD_GET 1u
#define PXA_NET_METHOD_HEAD 2u
#define PXA_NET_METHOD_POST 3u
#define PXA_NET_METHOD_PUT 4u
#define PXA_NET_METHOD_PATCH 5u
#define PXA_NET_METHOD_DELETE 6u
#define PXA_NET_URL 1u
#define PXA_NET_METHOD 2u
#define PXA_NET_PERMISSION_HANDLE 3u
#define PXA_NET_MAX_RESPONSE_BYTES 4u
#define PXA_NET_STATUS_CODE 5u
#define PXA_NET_CONTENT_TYPE 6u
#define PXA_NET_BODY_HANDLE 7u
#define PXA_NET_TIMEOUT_MS 8u
#define PXA_NET_HEADER 9u
#define PXA_NET_BODY 10u
#define PXA_NET_WANTED_RESPONSE_HEADER 11u
#define PXA_NET_BODY_LENGTH 12u
#define PXA_NET_RESPONSE_FLAGS 13u
#define PXA_NET_RESPONSE_BODY_PRESENT 1u
#define PXA_NET_RESPONSE_BODY_LENGTH_KNOWN 2u
#define PXA_NET_MAX_HEADERS 8u
#define PXA_NET_MAX_HEADER_NAME_BYTES 64u
#define PXA_NET_MAX_HEADER_VALUE_BYTES 256u
#define PXA_NET_MAX_HEADER_BLOCK_BYTES 2048u
#define PXA_NET_MAX_INLINE_BODY_BYTES 2048u
#define PXA_NET_MAX_RESPONSE_BODY_BYTES 262144u
#define PXA_NET_MIN_TIMEOUT_MS 100u
#define PXA_NET_DEFAULT_TIMEOUT_MS 15000u
#define PXA_NET_MAX_TIMEOUT_MS 60000u

typedef struct {
    const char *name;
    uint16_t name_length;
    const uint8_t *value;
    uint16_t value_length;
} pxa_net_header_t;

typedef struct {
    uint16_t method;
    const char *url;
    uint16_t url_length;
    uint32_t permission_handle;
    uint32_t max_response_bytes;
    uint32_t timeout_ms;
    const pxa_net_header_t *headers;
    uint16_t header_count;
    const uint8_t *body;
    uint16_t body_length;
    const char *const *wanted_response_headers;
    const uint16_t *wanted_response_header_lengths;
    uint16_t wanted_response_header_count;
} pxa_net_http_request_t;

typedef struct {
    const uint8_t *name;
    uint16_t name_length;
    const uint8_t *value;
    uint16_t value_length;
} pxa_net_header_view_t;

typedef struct {
    int32_t status;
    uint16_t status_code;
    const uint8_t *content_type;
    uint16_t content_type_length;
    uint32_t body_handle;
} pxa_net_fetch_result_t;

typedef struct {
    int32_t status;
    uint16_t status_code;
    const uint8_t *content_type;
    uint16_t content_type_length;
    uint32_t body_handle;
    uint64_t body_length;
    uint32_t flags;
    uint16_t header_count;
    pxa_net_header_view_t headers[PXA_NET_MAX_HEADERS];
} pxa_net_http_result_t;

static inline int pxa_net_header_name_valid(const char *name, size_t length) {
    size_t index;
    if (name == NULL || length == 0 || length > PXA_NET_MAX_HEADER_NAME_BYTES)
        return 0;
    for (index = 0; index < length; ++index) {
        const uint8_t value = (uint8_t)name[index];
        if (!((value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '!' || value == '#' ||
              value == '$' || value == '%' || value == '&' || value == '\'' ||
              value == '*' || value == '+' || value == '-' || value == '.' ||
              value == '^' || value == '_' || value == '`' || value == '|' ||
              value == '~')) return 0;
    }
    return 1;
}

static inline int pxa_net_name_equal(const char *name, size_t length,
                                     const char *literal) {
    size_t index = 0;
    if (name == NULL || literal == NULL) return 0;
    while (literal[index] != '\0') {
        if (index >= length || name[index] != literal[index]) return 0;
        ++index;
    }
    return index == length;
}

static inline int pxa_net_request_header_name_valid(const char *name,
                                                    size_t length) {
    return pxa_net_header_name_valid(name, length) &&
           !pxa_net_name_equal(name, length, "connection") &&
           !pxa_net_name_equal(name, length, "content-length") &&
           !pxa_net_name_equal(name, length, "host") &&
           !pxa_net_name_equal(name, length, "proxy-connection") &&
           !pxa_net_name_equal(name, length, "te") &&
           !pxa_net_name_equal(name, length, "trailer") &&
           !pxa_net_name_equal(name, length, "transfer-encoding") &&
           !pxa_net_name_equal(name, length, "upgrade");
}

static inline int pxa_net_header_value_valid(const uint8_t *value,
                                              size_t length) {
    size_t index;
    if ((value == NULL && length != 0) || length > PXA_NET_MAX_HEADER_VALUE_BYTES)
        return 0;
    for (index = 0; index < length; ++index) {
        if (value[index] != '\t' &&
            (value[index] < 0x20u || value[index] > 0x7eu)) return 0;
    }
    return 1;
}

static inline int pxa_net_content_type_valid(const uint8_t *value,
                                             size_t length) {
    size_t index;
    if ((value == NULL && length != 0) || length > 96u) return 0;
    for (index = 0; index < length; ++index) {
        if (value[index] < 0x20u || value[index] > 0x7eu) return 0;
    }
    return 1;
}

static inline int pxa_net_http_request(uint32_t request_id,
                                       const pxa_net_http_request_t *options,
                                       uint8_t *payload,
                                       size_t payload_capacity,
                                       uint8_t *packet,
                                       size_t packet_capacity) {
    pxa_writer_t request;
    pxa_writer_t message;
    uint8_t value[4];
    uint16_t index;
    size_t header_bytes = 0;
    if (request_id == 0 || options == NULL || options->url == NULL ||
        options->url_length == 0 || options->url_length > 512 ||
        options->method < PXA_NET_METHOD_GET ||
        options->method > PXA_NET_METHOD_DELETE ||
        options->permission_handle == 0 || options->max_response_bytes == 0 ||
        options->max_response_bytes > PXA_NET_MAX_RESPONSE_BODY_BYTES ||
        options->timeout_ms < PXA_NET_MIN_TIMEOUT_MS ||
        options->timeout_ms > PXA_NET_MAX_TIMEOUT_MS ||
        options->header_count > PXA_NET_MAX_HEADERS ||
        options->wanted_response_header_count > PXA_NET_MAX_HEADERS ||
        options->body_length > PXA_NET_MAX_INLINE_BODY_BYTES ||
        (options->headers == NULL && options->header_count != 0) ||
        (options->body == NULL && options->body_length != 0) ||
        (options->wanted_response_headers == NULL &&
         options->wanted_response_header_count != 0) ||
        (options->wanted_response_header_lengths == NULL &&
         options->wanted_response_header_count != 0) || payload == NULL ||
        packet == NULL || packet_capacity < 12) return 0;
    if ((options->method == PXA_NET_METHOD_GET ||
         options->method == PXA_NET_METHOD_HEAD) && options->body_length != 0)
        return 0;
    pxa_writer_init(&request, payload, payload_capacity);
    if (!pxa_record(&request, PXA_NET_URL, (const uint8_t *)options->url,
                    options->url_length)) return 0;
    value[0] = (uint8_t)options->method;
    value[1] = (uint8_t)(options->method >> 8);
    if (!pxa_record(&request, PXA_NET_METHOD, value, 2)) return 0;
    value[0] = (uint8_t)options->permission_handle;
    value[1] = (uint8_t)(options->permission_handle >> 8);
    value[2] = (uint8_t)(options->permission_handle >> 16);
    value[3] = (uint8_t)(options->permission_handle >> 24);
    if (!pxa_record(&request, PXA_NET_PERMISSION_HANDLE, value, 4)) return 0;
    value[0] = (uint8_t)options->max_response_bytes;
    value[1] = (uint8_t)(options->max_response_bytes >> 8);
    value[2] = (uint8_t)(options->max_response_bytes >> 16);
    value[3] = (uint8_t)(options->max_response_bytes >> 24);
    if (!pxa_record(&request, PXA_NET_MAX_RESPONSE_BYTES, value, 4)) return 0;
    value[0] = (uint8_t)options->timeout_ms;
    value[1] = (uint8_t)(options->timeout_ms >> 8);
    value[2] = (uint8_t)(options->timeout_ms >> 16);
    value[3] = (uint8_t)(options->timeout_ms >> 24);
    if (!pxa_record(&request, PXA_NET_TIMEOUT_MS, value, 4)) return 0;
    for (index = 0; index < options->header_count; ++index) {
        uint8_t encoded[8 + PXA_NET_MAX_HEADER_NAME_BYTES +
                        PXA_NET_MAX_HEADER_VALUE_BYTES];
        pxa_writer_t header;
        const pxa_net_header_t *item = &options->headers[index];
        uint16_t previous_index;
        if (!pxa_net_request_header_name_valid(item->name, item->name_length) ||
            !pxa_net_header_value_valid(item->value, item->value_length)) return 0;
        for (previous_index = 0; previous_index < index; ++previous_index) {
            const pxa_net_header_t *previous =
                &options->headers[previous_index];
            size_t byte_index;
            if (previous->name_length != item->name_length) continue;
            for (byte_index = 0; byte_index < item->name_length; ++byte_index) {
                if (previous->name[byte_index] != item->name[byte_index]) break;
            }
            if (byte_index == item->name_length) return 0;
        }
        pxa_writer_init(&header, encoded, sizeof(encoded));
        if (!pxa_record(&header, 1, (const uint8_t *)item->name,
                        item->name_length) ||
            !pxa_record(&header, 2, item->value, item->value_length) ||
            header.length + 4u > PXA_NET_MAX_HEADER_BLOCK_BYTES -
                                     header_bytes ||
            !pxa_record(&request, PXA_NET_HEADER, encoded, header.length)) return 0;
        header_bytes += header.length + 4u;
    }
    if (options->body_length != 0 &&
        !pxa_record(&request, PXA_NET_BODY, options->body,
                    options->body_length)) return 0;
    for (index = 0; index < options->wanted_response_header_count; ++index) {
        const char *name = options->wanted_response_headers[index];
        const uint16_t length = options->wanted_response_header_lengths[index];
        uint16_t previous_index;
        if (!pxa_net_header_name_valid(name, length) ||
            !pxa_record(&request, PXA_NET_WANTED_RESPONSE_HEADER,
                        (const uint8_t *)name, length)) return 0;
        for (previous_index = 0; previous_index < index; ++previous_index) {
            const char *previous = options->wanted_response_headers[previous_index];
            const uint16_t previous_length =
                options->wanted_response_header_lengths[previous_index];
            size_t byte_index;
            if (previous_length != length) continue;
            for (byte_index = 0; byte_index < length; ++byte_index) {
                if (previous[byte_index] != name[byte_index]) break;
            }
            if (byte_index == length) return 0;
        }
    }
    pxa_writer_init(&message, packet, packet_capacity);
    return pxa_message(&message, PXA_SERVICE_NET, PXA_NET_HTTP_REQUEST,
                       request_id, request.data, request.length) &&
           pxa_control(message.data, (uint32_t)message.length) == PXA_STATUS_OK;
}

static inline int pxa_net_fetch_get(uint32_t request_id, const char *url,
                                    size_t url_length, uint32_t permission_handle,
                                    uint32_t max_response_bytes, uint8_t *payload,
                                    size_t payload_capacity, uint8_t *packet,
                                    size_t packet_capacity) {
    pxa_writer_t request;
    pxa_writer_t message;
    uint8_t method[2] = {PXA_NET_METHOD_GET, 0};
    uint8_t permission[4];
    uint8_t limit[4];
    if (request_id == 0 || url == NULL || url_length == 0 || url_length > 512 ||
        permission_handle == 0 || max_response_bytes == 0 || payload == NULL ||
        packet == NULL || packet_capacity < 12) return 0;
    permission[0] = (uint8_t)permission_handle;
    permission[1] = (uint8_t)(permission_handle >> 8);
    permission[2] = (uint8_t)(permission_handle >> 16);
    permission[3] = (uint8_t)(permission_handle >> 24);
    limit[0] = (uint8_t)max_response_bytes;
    limit[1] = (uint8_t)(max_response_bytes >> 8);
    limit[2] = (uint8_t)(max_response_bytes >> 16);
    limit[3] = (uint8_t)(max_response_bytes >> 24);
    pxa_writer_init(&request, payload, payload_capacity);
    if (!pxa_record(&request, PXA_NET_URL, (const uint8_t *)url, url_length) ||
        !pxa_record(&request, PXA_NET_METHOD, method, sizeof(method)) ||
        !pxa_record(&request, PXA_NET_PERMISSION_HANDLE, permission, sizeof(permission)) ||
        !pxa_record(&request, PXA_NET_MAX_RESPONSE_BYTES, limit, sizeof(limit))) return 0;
    pxa_writer_init(&message, packet, packet_capacity);
    return pxa_message(&message, PXA_SERVICE_NET, PXA_NET_FETCH, request_id,
                       request.data, request.length) &&
           pxa_control(message.data, (uint32_t)message.length) == PXA_STATUS_OK;
}

static inline int pxa_net_parse_fetch(const pxa_event_t *event,
                                      pxa_net_fetch_result_t *output) {
    const uint8_t *value;
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_NET ||
        event->opcode != PXA_NET_FETCH || event->request_id == 0 ||
        event->payload_length < 4) return 0;
    value = event->payload;
    output->status = (int32_t)pxa_read_u32(value);
    if (output->status != PXA_STATUS_OK) return event->payload_length == 4;
    if (event->payload_length < 22 || pxa_read_u16(value + 4) != PXA_NET_STATUS_CODE ||
        pxa_read_u16(value + 6) != 2 || pxa_read_u16(value + 10) != PXA_NET_CONTENT_TYPE)
        return 0;
    output->status_code = pxa_read_u16(value + 8);
    output->content_type_length = pxa_read_u16(value + 12);
    if (output->content_type_length > event->payload_length - 14 ||
        14u + output->content_type_length + 8u != event->payload_length) return 0;
    output->content_type = value + 14;
    value += 14 + output->content_type_length;
    if (pxa_read_u16(value) != PXA_NET_BODY_HANDLE || pxa_read_u16(value + 2) != 4) return 0;
    output->body_handle = pxa_read_u32(value + 4);
    return output->status_code >= 100 && output->status_code <= 599 && output->body_handle != 0;
}

static inline int pxa_net_parse_header_view(const uint8_t *data, size_t length,
                                            pxa_net_header_view_t *output) {
    size_t offset = 0;
    uint8_t seen = 0;
    uint16_t previous = 0;
    output->name = NULL;
    output->name_length = 0;
    output->value = NULL;
    output->value_length = 0;
    while (offset < length) {
        uint16_t tag;
        uint16_t size;
        if (length - offset < 4) return 0;
        tag = pxa_read_u16(data + offset);
        size = pxa_read_u16(data + offset + 2);
        offset += 4;
        if (tag < previous || size > length - offset) return 0;
        previous = tag;
        if (tag == 1 && (seen & 1u) == 0 && size != 0 &&
            size <= PXA_NET_MAX_HEADER_NAME_BYTES) {
            output->name = data + offset;
            output->name_length = size;
            seen |= 1u;
        } else if (tag == 2 && (seen & 2u) == 0 &&
                   size <= PXA_NET_MAX_HEADER_VALUE_BYTES) {
            output->value = data + offset;
            output->value_length = size;
            seen |= 2u;
        } else if ((tag & 0x8000u) == 0) {
            return 0;
        }
        offset += size;
    }
    return seen == 3u &&
           pxa_net_header_name_valid((const char *)output->name,
                                     output->name_length) &&
           pxa_net_header_value_valid(output->value, output->value_length);
}

static inline int pxa_net_parse_http_result(const pxa_event_t *event,
                                            pxa_net_http_result_t *output) {
    size_t offset = 4;
    uint16_t previous = 0;
    uint8_t seen_status = 0;
    uint8_t seen_content_type = 0;
    uint8_t seen_handle = 0;
    uint8_t seen_length = 0;
    uint8_t seen_flags = 0;
    size_t header_bytes = 0;
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_NET ||
        event->opcode != PXA_NET_HTTP_REQUEST || event->request_id == 0 ||
        event->payload_length < 4) return 0;
    output->status = (int32_t)pxa_read_u32(event->payload);
    output->status_code = 0;
    output->content_type = NULL;
    output->content_type_length = 0;
    output->body_handle = 0;
    output->body_length = 0;
    output->flags = 0;
    output->header_count = 0;
    if (output->status != PXA_STATUS_OK) return event->payload_length == 4;
    while (offset < event->payload_length) {
        uint16_t tag;
        uint16_t size;
        const uint8_t *value;
        if (event->payload_length - offset < 4) return 0;
        tag = pxa_read_u16(event->payload + offset);
        size = pxa_read_u16(event->payload + offset + 2);
        offset += 4;
        if (tag < previous || size > event->payload_length - offset) return 0;
        previous = tag;
        value = event->payload + offset;
        if (tag == PXA_NET_STATUS_CODE && !seen_status && size == 2) {
            output->status_code = pxa_read_u16(value);
            seen_status = 1;
        } else if (tag == PXA_NET_CONTENT_TYPE && !seen_content_type &&
                   pxa_net_content_type_valid(value, size)) {
            output->content_type = value;
            output->content_type_length = size;
            seen_content_type = 1;
        } else if (tag == PXA_NET_BODY_HANDLE && !seen_handle && size == 4) {
            output->body_handle = pxa_read_u32(value);
            seen_handle = 1;
        } else if (tag == PXA_NET_HEADER &&
                   output->header_count < PXA_NET_MAX_HEADERS &&
                   size + 4u <= PXA_NET_MAX_HEADER_BLOCK_BYTES - header_bytes &&
                   pxa_net_parse_header_view(
                       value, size, &output->headers[output->header_count])) {
            uint16_t previous_index;
            for (previous_index = 0;
                 previous_index < output->header_count; ++previous_index) {
                const pxa_net_header_view_t *previous =
                    &output->headers[previous_index];
                size_t byte_index;
                if (previous->name_length !=
                    output->headers[output->header_count].name_length) continue;
                for (byte_index = 0; byte_index < previous->name_length;
                     ++byte_index) {
                    if (previous->name[byte_index] !=
                        output->headers[output->header_count].name[byte_index]) {
                        break;
                    }
                }
                if (byte_index == previous->name_length) return 0;
            }
            header_bytes += size + 4u;
            output->header_count++;
        } else if (tag == PXA_NET_BODY_LENGTH && !seen_length && size == 8) {
            output->body_length = pxa_read_u64(value);
            seen_length = 1;
        } else if (tag == PXA_NET_RESPONSE_FLAGS && !seen_flags && size == 4) {
            output->flags = pxa_read_u32(value);
            seen_flags = 1;
        } else if ((tag & 0x8000u) == 0) {
            return 0;
        }
        offset += size;
    }
    if (!seen_status || !seen_content_type || !seen_flags ||
        output->status_code < 100 || output->status_code > 599 ||
        (seen_handle && output->body_handle == 0) ||
        (output->flags & ~(PXA_NET_RESPONSE_BODY_PRESENT |
                           PXA_NET_RESPONSE_BODY_LENGTH_KNOWN)) != 0 ||
        (((output->flags & PXA_NET_RESPONSE_BODY_PRESENT) != 0) !=
         (seen_handle && output->body_handle != 0)) ||
        (((output->flags & PXA_NET_RESPONSE_BODY_LENGTH_KNOWN) != 0) !=
         (seen_length != 0)) ||
        ((output->flags & PXA_NET_RESPONSE_BODY_PRESENT) == 0 &&
         output->body_length != 0)) return 0;
    return 1;
}

#endif
