#include "services/net/net_internal.h"

#include <string.h>
#include <limits.h>
#include "pxa/http_stream.h"

static int method_valid(uint16_t method) {
    return method >= PXA_NET_METHOD_GET && method <= PXA_NET_METHOD_DELETE;
}

static pxa_status_t parse_header(pxa_bytes_t payload,
                                 pxa_net_header_t *output, int streaming) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint8_t seen = 0;
    uint16_t previous = 0;
    pxa_status_t status;
    memset(output, 0, sizeof(*output));
    pxa_record_iterator_init(&iterator, payload);
    for (;;) {
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK || record.raw_tag < previous) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        previous = record.raw_tag;
        if (record.optional) continue;
        if (record.tag == 1 && (seen & 1u) == 0 &&
            pxa_net_header_name_valid(record.payload, 1)) {
            output->name = record.payload;
            seen |= 1u;
        } else if (record.tag == 2 && (seen & 2u) == 0 &&
                   (streaming || pxa_net_header_value_valid(record.payload))) {
            if (streaming) {
                size_t i;
                for (i=0;i<record.payload.size;++i)
                    if (record.payload.data[i]!='\t' &&
                        (record.payload.data[i]<0x20 || record.payload.data[i]==0x7f))
                        return PXA_STATUS_INVALID_ARGUMENT;
            }
            output->value = record.payload;
            seen |= 2u;
        } else {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
    }
    return seen == 3u ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
}

static int name_equal(pxa_bytes_t a, pxa_bytes_t b) {
    size_t i;
    if (a.size != b.size) return 0;
    for (i = 0; i < a.size; ++i) {
        unsigned x = a.data[i], y = b.data[i];
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return 0;
    }
    return 1;
}

static int bytes_duplicate(const pxa_bytes_t *values, uint16_t count,
                           pxa_bytes_t value) {
    uint16_t index;
    for (index = 0; index < count; ++index)
        if (name_equal(values[index], value)) return 1;
    return 0;
}

static int header_duplicate(const pxa_net_header_t *headers, uint16_t count,
                            pxa_bytes_t name) {
    uint16_t index;
    for (index = 0; index < count; ++index)
        if (name_equal(headers[index].name, name)) return 1;
    return 0;
}

pxa_status_t pxa_net_request_parse(
    const pxa_net_request_limits_t *limits, uint16_t wire_profile,
    uint16_t opcode,
    pxa_bytes_t payload, pxa_net_parsed_request_t *output) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    uint16_t required_seen;
    uint8_t stream_fields = 0;
    int streaming = opcode == PXA_NET_HTTP_OPEN;
    pxa_status_t status;
    if (limits == NULL || output == NULL || wire_profile > 1)
        return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    output->request.struct_size = sizeof(output->request);
    pxa_record_iterator_init(&iterator, payload);
    for (;;) {
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) return status;
        if (record.raw_tag < previous) return PXA_STATUS_INVALID_ARGUMENT;
        previous = record.raw_tag;
        if (record.optional) continue;
        if (record.tag == 1 && (output->seen & 1u) == 0 &&
            pxa_net_parse_web_url(record.payload,
                                  &output->request.origin)) {
            output->request.url = record.payload;
            output->seen |= 1u;
        } else if (record.tag == 2 && (output->seen & 2u) == 0 &&
                   record.payload.size == 2) {
            output->request.method = pxa_read_u16(record.payload.data);
            output->seen |= 2u;
            if ((opcode == PXA_NET_FETCH &&
                 output->request.method != PXA_NET_METHOD_GET) ||
                ((opcode == PXA_NET_HTTP_REQUEST || streaming) &&
                 !method_valid(output->request.method))) {
                return PXA_STATUS_UNSUPPORTED;
            }
        } else if (record.tag == 3 && (output->seen & 4u) == 0 &&
                   record.payload.size == (wire_profile == 1 ? 8u : 4u)) {
            output->permission_handle = wire_profile == 1
                ? pxa_read_u64(record.payload.data)
                : pxa_read_u32(record.payload.data);
            if (wire_profile == 1 &&
                (output->permission_handle >> 32) == 0)
                return PXA_STATUS_INVALID_ARGUMENT;
            output->seen |= 4u;
        } else if (record.tag == 4 && (output->seen & 8u) == 0 &&
                   record.payload.size == (streaming ? 8u : 4u)) {
            if (streaming) {
                output->request.max_body_bytes = pxa_read_u64(record.payload.data);
                output->request.max_response_bytes = UINT32_MAX;
            } else output->request.max_response_bytes =
                pxa_read_u32(record.payload.data);
            output->seen |= 8u;
        } else if ((opcode == PXA_NET_HTTP_REQUEST || streaming) && record.tag == 8 &&
                   (output->seen & 16u) == 0 && record.payload.size == 4) {
            output->request.timeout_ms = pxa_read_u32(record.payload.data);
            output->seen |= 16u;
        } else if ((opcode == PXA_NET_HTTP_REQUEST || streaming) && record.tag == 9 &&
                   output->request.header_count < limits->max_headers) {
            pxa_net_header_t *header =
                &output->headers[output->request.header_count];
            status = parse_header(record.payload, header, streaming);
            if (status != PXA_STATUS_OK ||
                header_duplicate(output->headers,
                                 output->request.header_count, header->name) ||
                record.payload.size + 4u >
                    limits->max_request_header_bytes ||
                output->request_header_bytes >
                    limits->max_request_header_bytes -
                        (record.payload.size + 4u)) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            output->request_header_bytes += record.payload.size + 4u;
            output->request.header_count++;
        } else if ((opcode == PXA_NET_HTTP_REQUEST || streaming) && record.tag == 10 &&
                   (output->seen & 32u) == 0 &&
                   record.payload.size <= limits->max_inline_body_bytes) {
            output->request.body = record.payload;
            output->seen |= 32u;
        } else if ((opcode == PXA_NET_HTTP_REQUEST || streaming) && record.tag == 11 &&
                   output->request.wanted_response_header_count <
                       limits->max_headers &&
                   pxa_net_header_name_valid(record.payload, 0) &&
                   !bytes_duplicate(
                       output->wanted_response_headers,
                       output->request.wanted_response_header_count,
                       record.payload)) {
            output->wanted_response_headers
                [output->request.wanted_response_header_count++] =
                    record.payload;
        } else if (streaming && record.tag == 14 && !(stream_fields & 1u) &&
                   record.payload.size == 4) {
            output->request.buffer_bytes = pxa_read_u32(record.payload.data);
            stream_fields |= 1u;
        } else if (streaming && record.tag == 15 && !(stream_fields & 2u) &&
                   record.payload.size == 8) {
            output->request.upload_bytes = pxa_read_u64(record.payload.data);
            stream_fields |= 2u;
        } else if (streaming && record.tag == 16 && !(stream_fields & 4u) &&
                   record.payload.size == 4) {
            output->request.response_header_bytes = pxa_read_u32(record.payload.data);
            stream_fields |= 4u;
        } else {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
    }
    if (streaming && (stream_fields != 7u || !output->request.max_body_bytes ||
        output->request.upload_bytes > INT32_MAX ||
        output->request.body.size > output->request.buffer_bytes / 2u ||
        (output->request.body.size && output->request.body.size != output->request.upload_bytes) ||
        !pxa_http_stream_storage_size(output->request.buffer_bytes,
                                      output->request.response_header_bytes) ||
        ((output->request.method == PXA_NET_METHOD_GET ||
          output->request.method == PXA_NET_METHOD_HEAD) && output->request.upload_bytes)))
        return PXA_STATUS_INVALID_ARGUMENT;
    required_seen = opcode == PXA_NET_FETCH ? 15u : 31u;
    if ((output->seen & required_seen) != required_seen ||
        output->permission_handle == PXA_HANDLE_INVALID ||
        output->request.max_response_bytes == 0 ||
        ((opcode == PXA_NET_HTTP_REQUEST || streaming) &&
         (output->request.timeout_ms < limits->min_timeout_ms ||
          output->request.timeout_ms > limits->max_timeout_ms)) ||
        ((output->request.method == PXA_NET_METHOD_GET ||
          output->request.method == PXA_NET_METHOD_HEAD) &&
         output->request.body.size != 0)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    output->request.headers = output->headers;
    output->request.wanted_response_headers =
        output->wanted_response_headers;
    if (opcode == PXA_NET_FETCH) {
        output->request.timeout_ms = limits->default_timeout_ms;
        output->request.abi_minor = 0;
    } else {
        output->request.abi_minor = streaming ? 2 : 1;
    }
    return PXA_STATUS_OK;
}
