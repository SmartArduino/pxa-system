#include "pxa/wire.h"
#include "pxa/wire_v1.h"

#include <limits.h>
#include <string.h>

int pxa_status_is_known(pxa_status_t status) {
    return status <= PXA_STATUS_OK && status >= PXA_STATUS_LIMIT_EXCEEDED;
}
uint16_t pxa_read_u16(const uint8_t *value) {
    return (uint16_t)((uint16_t)value[0] | ((uint16_t)value[1] << 8));
}

uint32_t pxa_read_u32(const uint8_t *value) {
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

uint64_t pxa_read_u64(const uint8_t *value) {
    return (uint64_t)pxa_read_u32(value) |
           ((uint64_t)pxa_read_u32(value + 4) << 32);
}

void pxa_write_u16(uint8_t *output, uint16_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
}

void pxa_write_u32(uint8_t *output, uint32_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

void pxa_write_u64(uint8_t *output, uint64_t value) {
    pxa_write_u32(output, (uint32_t)value);
    pxa_write_u32(output + 4, (uint32_t)(value >> 32));
}

pxa_status_t pxa_message_decode(const uint8_t *data, size_t size,
                                size_t max_size, pxa_message_view_t *output) {
    pxa_wire_v0_view_t decoded;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    if (!pxa_wire_v0_decode(data, size, max_size, &decoded))
        return PXA_STATUS_INVALID_ARGUMENT;
    output->service = decoded.service;
    output->opcode = decoded.opcode;
    output->request_id = decoded.request_id;
    output->payload.data = decoded.payload;
    output->payload.size = decoded.payload_size;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_v1_message_decode(const uint8_t *data, size_t size,
                                   size_t max_size,
                                   pxa_v1_message_view_t *output) {
    pxa_wire_v1_view_t decoded;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    if (!pxa_wire_v1_decode(data, size, max_size, &decoded))
        return PXA_STATUS_INVALID_ARGUMENT;
    output->service = decoded.service;
    output->opcode = decoded.opcode;
    output->request_token = decoded.request_token;
    output->payload.data = decoded.payload;
    output->payload.size = decoded.payload_size;
    return PXA_STATUS_OK;
}

void pxa_record_iterator_init(pxa_record_iterator_t *iterator,
                              pxa_bytes_t bytes) {
    if (iterator == NULL) return;
    iterator->bytes = bytes;
    iterator->offset = 0;
}

pxa_status_t pxa_record_next(pxa_record_iterator_t *iterator,
                             pxa_record_view_t *output) {
    pxa_wire_record_view_t decoded;
    size_t consumed = 0;
    if (iterator == NULL || output == NULL ||
        (iterator->bytes.data == NULL && iterator->bytes.size != 0)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    memset(output, 0, sizeof(*output));
    if (iterator->offset == iterator->bytes.size) return PXA_STATUS_WOULD_BLOCK;
    if (iterator->offset > iterator->bytes.size ||
        !pxa_wire_record_decode(iterator->bytes.data + iterator->offset,
                                iterator->bytes.size - iterator->offset,
                                &decoded, &consumed)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    output->raw_tag = decoded.raw_tag;
    output->tag = decoded.tag;
    output->optional = decoded.optional;
    output->payload.data = decoded.payload;
    output->payload.size = decoded.payload_size;
    iterator->offset += consumed;
    return PXA_STATUS_OK;
}

void pxa_writer_init(pxa_writer_t *writer, uint8_t *data, size_t capacity) {
    if (writer == NULL) return;
    writer->data = data;
    writer->capacity = capacity;
    writer->size = 0;
    writer->status = (data == NULL && capacity != 0)
                         ? PXA_STATUS_INVALID_ARGUMENT
                         : PXA_STATUS_OK;
}

pxa_status_t pxa_writer_bytes(pxa_writer_t *writer, const void *data,
                              size_t size) {
    if (writer == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    if (writer->status != PXA_STATUS_OK) return writer->status;
    if ((data == NULL && size != 0) || writer->size > writer->capacity ||
        size > writer->capacity - writer->size) {
        writer->status = PXA_STATUS_RESOURCE_LIMIT;
        return writer->status;
    }
    if (size != 0) memcpy(writer->data + writer->size, data, size);
    writer->size += size;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_writer_u16(pxa_writer_t *writer, uint16_t value) {
    uint8_t bytes[2];
    pxa_write_u16(bytes, value);
    return pxa_writer_bytes(writer, bytes, sizeof(bytes));
}

pxa_status_t pxa_writer_u32(pxa_writer_t *writer, uint32_t value) {
    uint8_t bytes[4];
    pxa_write_u32(bytes, value);
    return pxa_writer_bytes(writer, bytes, sizeof(bytes));
}

pxa_status_t pxa_writer_u64(pxa_writer_t *writer, uint64_t value) {
    uint8_t bytes[8];
    pxa_write_u64(bytes, value);
    return pxa_writer_bytes(writer, bytes, sizeof(bytes));
}

pxa_status_t pxa_writer_record(pxa_writer_t *writer, uint16_t raw_tag,
                               const void *payload, size_t payload_size) {
    size_t written = 0;
    if (writer == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    if (writer->status != PXA_STATUS_OK) return writer->status;
    if ((raw_tag & PXA_RECORD_TAG_MASK) == 0 || payload_size > UINT16_MAX ||
        (payload == NULL && payload_size != 0)) {
        writer->status = PXA_STATUS_INVALID_ARGUMENT;
        return writer->status;
    }
    if (writer->size > writer->capacity ||
        PXA_RECORD_HEADER_SIZE + payload_size >
            writer->capacity - writer->size) {
        writer->status = PXA_STATUS_RESOURCE_LIMIT;
        return writer->status;
    }
    if (!pxa_wire_record_encode(writer->data + writer->size,
                                 writer->capacity - writer->size, raw_tag,
                                 payload, payload_size, &written)) {
        writer->status = PXA_STATUS_INVALID_ARGUMENT;
        return writer->status;
    }
    writer->size += written;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_writer_message(pxa_writer_t *writer, uint16_t service,
                                uint16_t opcode, uint32_t request_id,
                                const void *payload, size_t payload_size) {
    size_t written = 0;
    if (writer == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    if (writer->status != PXA_STATUS_OK) return writer->status;
    if (payload_size > UINT32_MAX ||
        payload_size > PXA_MAX_CONTROL_MESSAGE - PXA_ENVELOPE_SIZE ||
        (payload == NULL && payload_size != 0)) {
        writer->status = PXA_STATUS_INVALID_ARGUMENT;
        return writer->status;
    }
    if (writer->size > writer->capacity ||
        PXA_ENVELOPE_SIZE + payload_size > writer->capacity - writer->size) {
        writer->status = PXA_STATUS_RESOURCE_LIMIT;
        return writer->status;
    }
    if (!pxa_wire_v0_encode(writer->data + writer->size,
                             writer->capacity - writer->size,
                             service, opcode, request_id, payload,
                             payload_size, &written)) {
        writer->status = PXA_STATUS_INVALID_ARGUMENT;
        return writer->status;
    }
    writer->size += written;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_v1_writer_message(pxa_writer_t *writer, uint16_t service,
                                   uint16_t opcode, uint64_t request_token,
                                   const void *payload, size_t payload_size) {
    size_t written = 0;
    if (writer == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    if (writer->status != PXA_STATUS_OK) return writer->status;
    if (service == 0 || opcode == 0 ||
        payload_size > PXA_MAX_CONTROL_MESSAGE - PXA_V1_ENVELOPE_SIZE ||
        (payload == NULL && payload_size != 0)) {
        writer->status = PXA_STATUS_INVALID_ARGUMENT;
        return writer->status;
    }
    if (writer->size > writer->capacity ||
        PXA_V1_ENVELOPE_SIZE + payload_size >
            writer->capacity - writer->size) {
        writer->status = PXA_STATUS_RESOURCE_LIMIT;
        return writer->status;
    }
    if (!pxa_wire_v1_encode(writer->data + writer->size,
                             writer->capacity - writer->size,
                             service, opcode, request_token, payload,
                             payload_size, &written)) {
        writer->status = PXA_STATUS_INVALID_ARGUMENT;
        return writer->status;
    }
    writer->size += written;
    return PXA_STATUS_OK;
}
