#include "services/net/net_internal.h"

#include <string.h>

static int header_duplicate(const pxa_net_header_t *headers, uint16_t count,
                            pxa_bytes_t name) {
    uint16_t index;
    for (index = 0; index < count; ++index) {
        if (headers[index].name.size == name.size &&
            memcmp(headers[index].name.data, name.data, name.size) == 0) return 1;
    }
    return 0;
}

static int content_type_valid(pxa_bytes_t content_type) {
    size_t index;
    if (content_type.size > PXA_NET_MAX_CONTENT_TYPE_BYTES ||
        (content_type.data == NULL && content_type.size != 0)) {
        return 0;
    }
    for (index = 0; index < content_type.size; ++index) {
        if (content_type.data[index] < UINT8_C(0x20) ||
            content_type.data[index] > UINT8_C(0x7e)) {
            return 0;
        }
    }
    return 1;
}

static pxa_status_t encode_header_record(pxa_writer_t *writer,
                                         const pxa_net_header_t *header) {
    size_t nested_size = 8u + header->name.size + header->value.size;
    pxa_status_t status;
    if (nested_size > UINT16_MAX) return PXA_STATUS_INTERNAL;
    status = pxa_writer_u16(writer, 9);
    if (status == PXA_STATUS_OK) {
        status = pxa_writer_u16(writer, (uint16_t)nested_size);
    }
    if (status == PXA_STATUS_OK) {
        status = pxa_writer_record(writer, 1, header->name.data,
                                   header->name.size);
    }
    if (status == PXA_STATUS_OK) {
        status = pxa_writer_record(writer, 2, header->value.data,
                                   header->value.size);
    }
    return status == PXA_STATUS_OK ? PXA_STATUS_OK : PXA_STATUS_INTERNAL;
}

static pxa_status_t encode_legacy_response(
    uint8_t *output, size_t capacity, const pxa_net_response_t *response,
    pxa_handle_t handle, size_t *result_size) {
    pxa_writer_t writer;
    uint8_t value[4];
    pxa_status_t status;
    pxa_writer_init(&writer, output, capacity);
    pxa_write_u16(value, response->status_code);
    status = pxa_writer_record(&writer, 5, value, 2);
    if (status == PXA_STATUS_OK) {
        status = pxa_writer_record(&writer, 6, response->content_type.data,
                                   response->content_type.size);
    }
    if (status == PXA_STATUS_OK) {
        pxa_write_u32(value, handle);
        status = pxa_writer_record(&writer, 7, value, 4);
    }
    if (status != PXA_STATUS_OK) return PXA_STATUS_INTERNAL;
    *result_size = writer.size;
    return PXA_STATUS_OK;
}

static pxa_status_t encode_http_response(
    uint8_t *output, size_t capacity, const pxa_net_response_t *response,
    pxa_handle_t handle, size_t *result_size) {
    pxa_writer_t writer;
    uint8_t value[8];
    uint16_t index;
    pxa_status_t status;
    pxa_writer_init(&writer, output, capacity);
    pxa_write_u16(value, response->status_code);
    status = pxa_writer_record(&writer, 5, value, 2);
    if (status == PXA_STATUS_OK) {
        status = pxa_writer_record(&writer, 6, response->content_type.data,
                                   response->content_type.size);
    }
    if (status == PXA_STATUS_OK &&
        (response->flags & PXA_NET_RESPONSE_BODY_PRESENT) != 0) {
        pxa_write_u32(value, handle);
        status = pxa_writer_record(&writer, 7, value, 4);
    }
    for (index = 0; status == PXA_STATUS_OK && index < response->header_count;
         ++index) {
        status = encode_header_record(&writer, &response->headers[index]);
    }
    if (status == PXA_STATUS_OK &&
        (response->flags & PXA_NET_RESPONSE_BODY_LENGTH_KNOWN) != 0) {
        pxa_write_u64(value, response->body_length);
        status = pxa_writer_record(&writer, 12, value, 8);
    }
    if (status == PXA_STATUS_OK) {
        pxa_write_u32(value, response->flags);
        status = pxa_writer_record(&writer, 13, value, 4);
    }
    if (status != PXA_STATUS_OK) return PXA_STATUS_INTERNAL;
    *result_size = writer.size;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_net_response_validate(
    uint16_t max_headers, uint32_t max_response_header_bytes,
    uint16_t opcode, uint32_t max_response_bytes,
    const pxa_net_response_t *response) {
    size_t header_bytes = 0;
    uint16_t index;
    uint32_t flags;
    if (response == NULL) return PXA_STATUS_PROTOCOL_ERROR;
    flags = response->flags;
    if (response->status_code < 100 || response->status_code > 599 ||
        !content_type_valid(response->content_type)) {
        return PXA_STATUS_PROTOCOL_ERROR;
    }
    if (opcode == PXA_NET_FETCH) {
        return response->body_stream != NULL ? PXA_STATUS_OK
                                             : PXA_STATUS_PROTOCOL_ERROR;
    }
    if ((flags & ~(PXA_NET_RESPONSE_BODY_PRESENT |
                   PXA_NET_RESPONSE_BODY_LENGTH_KNOWN)) != 0 ||
        response->header_count > max_headers ||
        (response->header_count != 0 && response->headers == NULL) ||
        ((flags & PXA_NET_RESPONSE_BODY_PRESENT) != 0 &&
         response->body_stream == NULL) ||
        ((flags & PXA_NET_RESPONSE_BODY_PRESENT) == 0 &&
         response->body_length != 0) ||
        ((flags & PXA_NET_RESPONSE_BODY_LENGTH_KNOWN) != 0 &&
         response->body_length > max_response_bytes)) {
        return response->body_length > max_response_bytes
                   ? PXA_STATUS_LIMIT_EXCEEDED
                   : PXA_STATUS_PROTOCOL_ERROR;
    }
    for (index = 0; index < response->header_count; ++index) {
        const pxa_net_header_t *header = &response->headers[index];
        size_t encoded_size;
        if (!pxa_net_header_name_valid(header->name, 0) ||
            !pxa_net_header_value_valid(header->value) ||
            header_duplicate(response->headers, index, header->name)) {
            return PXA_STATUS_PROTOCOL_ERROR;
        }
        encoded_size = header->name.size + header->value.size + 12u;
        if (header_bytes > max_response_header_bytes ||
            encoded_size > max_response_header_bytes - header_bytes) {
            return PXA_STATUS_LIMIT_EXCEEDED;
        }
        header_bytes += encoded_size;
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_net_response_encode(
    uint8_t *output, size_t capacity, uint16_t opcode,
    const pxa_net_response_t *response, pxa_handle_t handle,
    size_t *result_size) {
    if (output == NULL || response == NULL || result_size == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    return opcode == PXA_NET_FETCH
               ? encode_legacy_response(output, capacity, response, handle,
                                        result_size)
               : encode_http_response(output, capacity, response, handle,
                                      result_size);
}
