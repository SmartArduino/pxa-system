#include "pxsys/pxa_gateway_wire.h"

#include <string.h>

static pxsys_status_t next_record(pxa_record_iterator_t* iterator, pxa_record_view_t* record,
                                  uint16_t* previous) {
    pxa_status_t status = pxa_record_next(iterator, record);
    if (status == PXA_STATUS_WOULD_BLOCK)
        return PXSYS_STATUS_NOT_FOUND;
    if (status != PXA_STATUS_OK || record->raw_tag < *previous)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *previous = record->raw_tag;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_pxa_gateway_decode_service(pxa_bytes_t payload,
                                                pxsys_service_request_t* output) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    uint8_t seen = 0;
    pxsys_status_t status;
    if (output == NULL || (payload.data == NULL && payload.size != 0))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    output->struct_size = sizeof(*output);
    pxa_record_iterator_init(&iterator, payload);
    for (;;) {
        status = next_record(&iterator, &record, &previous);
        if (status == PXSYS_STATUS_NOT_FOUND)
            break;
        if (status != PXSYS_STATUS_OK)
            return status;
        if (record.optional && record.tag > 5)
            continue;
        if (record.tag == 1 && !(seen & 1u) && !record.optional) {
            output->interface_id =
                pxsys_string((const char*)record.payload.data, record.payload.size);
            seen |= 1u;
        } else if (record.tag == 2 && !(seen & 2u) && !record.optional &&
                   record.payload.size == 4) {
            output->version.major = pxa_read_u16(record.payload.data);
            output->version.minor = pxa_read_u16(record.payload.data + 2);
            seen |= 2u;
        } else if (record.tag == 3 && !(seen & 4u) && !record.optional &&
                   record.payload.size == 4) {
            output->operation = pxa_read_u32(record.payload.data);
            seen |= 4u;
        } else if (record.tag == 4 && !(seen & 8u) && record.payload.size == 4) {
            output->flags = pxa_read_u32(record.payload.data);
            seen |= 8u;
        } else if (record.tag == 5 && !(seen & 16u)) {
            output->payload = pxsys_bytes(record.payload.data, record.payload.size);
            seen |= 16u;
        } else {
            return PXSYS_STATUS_INVALID_ARGUMENT;
        }
    }
    return (seen & 7u) == 7u && output->operation != 0 ? PXSYS_STATUS_OK
                                                       : PXSYS_STATUS_INVALID_ARGUMENT;
}

pxsys_status_t pxsys_pxa_gateway_decode_topic(pxa_bytes_t payload, pxsys_topic_event_t* output) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    uint8_t seen = 0;
    pxsys_status_t status;
    if (output == NULL || (payload.data == NULL && payload.size != 0))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    output->struct_size = sizeof(*output);
    pxa_record_iterator_init(&iterator, payload);
    for (;;) {
        status = next_record(&iterator, &record, &previous);
        if (status == PXSYS_STATUS_NOT_FOUND)
            break;
        if (status != PXSYS_STATUS_OK)
            return status;
        if (record.optional && record.tag > 5)
            continue;
        if (record.tag == 1 && !(seen & 1u) && !record.optional) {
            output->topic = pxsys_string((const char*)record.payload.data, record.payload.size);
            seen |= 1u;
        } else if (record.tag == 2 && !(seen & 2u) && !record.optional &&
                   record.payload.size == 4) {
            output->version.major = pxa_read_u16(record.payload.data);
            output->version.minor = pxa_read_u16(record.payload.data + 2);
            seen |= 2u;
        } else if (record.tag == 3 && !(seen & 4u) && !record.optional &&
                   record.payload.size == 4) {
            output->event = pxa_read_u32(record.payload.data);
            seen |= 4u;
        } else if (record.tag == 4 && !(seen & 8u) && record.payload.size == 8) {
            output->sequence = pxa_read_u64(record.payload.data);
            seen |= 8u;
        } else if (record.tag == 5 && !(seen & 16u)) {
            output->payload = pxsys_bytes(record.payload.data, record.payload.size);
            seen |= 16u;
        } else {
            return PXSYS_STATUS_INVALID_ARGUMENT;
        }
    }
    return (seen & 7u) == 7u ? PXSYS_STATUS_OK : PXSYS_STATUS_INVALID_ARGUMENT;
}

pxsys_status_t pxsys_pxa_gateway_encode_topic(const pxsys_topic_event_t* event, uint8_t* output,
                                              size_t capacity, size_t* output_size) {
    pxa_writer_t writer;
    uint8_t value[8];
    if (output_size == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *output_size = 0;
    if (event == NULL || event->struct_size < sizeof(*event) || output == NULL ||
        !pxsys_identifier_validate(event->topic, UINT16_MAX) || event->payload.size > UINT16_MAX ||
        (event->payload.data == NULL && event->payload.size != 0)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    pxa_writer_init(&writer, output, capacity);
    if (pxa_writer_record(&writer, 1, event->topic.data, event->topic.size) != PXA_STATUS_OK)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    pxa_write_u16(value, event->version.major);
    pxa_write_u16(value + 2, event->version.minor);
    if (pxa_writer_record(&writer, 2, value, 4) != PXA_STATUS_OK)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    pxa_write_u32(value, event->event);
    if (pxa_writer_record(&writer, 3, value, 4) != PXA_STATUS_OK)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    pxa_write_u64(value, event->sequence);
    if (pxa_writer_record(&writer, 4 | PXA_RECORD_OPTIONAL_MASK, value, 8) != PXA_STATUS_OK ||
        (event->payload.size != 0 &&
         pxa_writer_record(&writer, 5 | PXA_RECORD_OPTIONAL_MASK, event->payload.data,
                           event->payload.size) != PXA_STATUS_OK)) {
        return PXSYS_STATUS_RESOURCE_LIMIT;
    }
    *output_size = writer.size;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_pxa_gateway_decode_service_descriptor(pxa_bytes_t payload,
                                                           pxsys_pxa_service_descriptor_t* output) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    uint8_t seen = 0;
    pxsys_status_t status;
    if (output == NULL || (payload.data == NULL && payload.size != 0))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    pxa_record_iterator_init(&iterator, payload);
    for (;;) {
        status = next_record(&iterator, &record, &previous);
        if (status == PXSYS_STATUS_NOT_FOUND)
            break;
        if (status != PXSYS_STATUS_OK)
            return status;
        if (record.optional && record.tag > 3)
            continue;
        if (record.tag == 1 && !(seen & 1u) && !record.optional) {
            output->interface_id =
                pxsys_string((const char*)record.payload.data, record.payload.size);
            seen |= 1u;
        } else if (record.tag == 2 && !(seen & 2u) && !record.optional &&
                   record.payload.size == 4) {
            output->version.major = pxa_read_u16(record.payload.data);
            output->version.minor = pxa_read_u16(record.payload.data + 2);
            seen |= 2u;
        } else if (record.tag == 3 && !(seen & 4u) && record.payload.size == 8) {
            output->features = pxa_read_u64(record.payload.data);
            seen |= 4u;
        } else {
            return PXSYS_STATUS_INVALID_ARGUMENT;
        }
    }
    return (seen & 3u) == 3u ? PXSYS_STATUS_OK : PXSYS_STATUS_INVALID_ARGUMENT;
}

pxsys_status_t pxsys_pxa_gateway_encode_service_request(const pxsys_service_request_t* request,
                                                        uint64_t call_id, uint8_t* output,
                                                        size_t capacity, size_t* output_size) {
    pxa_writer_t writer;
    uint8_t value[32];
    if (output_size == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *output_size = 0;
    if (request == NULL || request->struct_size < sizeof(*request) || request->caller == NULL ||
        request->caller->struct_size < sizeof(*request->caller) || call_id == 0 || output == NULL ||
        !pxsys_identifier_validate(request->interface_id, UINT16_MAX) || request->operation == 0 ||
        request->payload.size > UINT16_MAX || request->caller->app.app_id.size > UINT16_MAX ||
        request->caller->component_id.size > UINT16_MAX ||
        (request->payload.data == NULL && request->payload.size != 0)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    pxa_writer_init(&writer, output, capacity);
    if (pxa_writer_record(&writer, 1, request->interface_id.data, request->interface_id.size) !=
        PXA_STATUS_OK)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    pxa_write_u16(value, request->version.major);
    pxa_write_u16(value + 2, request->version.minor);
    if (pxa_writer_record(&writer, 2, value, 4) != PXA_STATUS_OK)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    pxa_write_u32(value, request->operation);
    if (pxa_writer_record(&writer, 3, value, 4) != PXA_STATUS_OK)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    pxa_write_u32(value, request->flags);
    if (pxa_writer_record(&writer, 4 | PXA_RECORD_OPTIONAL_MASK, value, 4) != PXA_STATUS_OK ||
        (request->payload.size != 0 &&
         pxa_writer_record(&writer, 5 | PXA_RECORD_OPTIONAL_MASK, request->payload.data,
                           request->payload.size) != PXA_STATUS_OK)) {
        return PXSYS_STATUS_RESOURCE_LIMIT;
    }
    pxa_write_u64(value, call_id);
    if (pxa_writer_record(&writer, 6 | PXA_RECORD_OPTIONAL_MASK, value, 8) != PXA_STATUS_OK)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    memcpy(value, request->caller->app.publisher_root, sizeof(value));
    if (pxa_writer_record(&writer, 7 | PXA_RECORD_OPTIONAL_MASK, value, sizeof(value)) !=
            PXA_STATUS_OK ||
        pxa_writer_record(&writer, 8 | PXA_RECORD_OPTIONAL_MASK, request->caller->app.app_id.data,
                          request->caller->app.app_id.size) != PXA_STATUS_OK ||
        pxa_writer_record(&writer, 9 | PXA_RECORD_OPTIONAL_MASK, request->caller->component_id.data,
                          request->caller->component_id.size) != PXA_STATUS_OK) {
        return PXSYS_STATUS_RESOURCE_LIMIT;
    }
    *output_size = writer.size;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_pxa_gateway_decode_service_completion(pxa_bytes_t payload,
                                                           pxsys_pxa_service_completion_t* output) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    uint8_t seen = 0;
    pxsys_status_t status;
    if (output == NULL || (payload.data == NULL && payload.size != 0))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    pxa_record_iterator_init(&iterator, payload);
    for (;;) {
        status = next_record(&iterator, &record, &previous);
        if (status == PXSYS_STATUS_NOT_FOUND)
            break;
        if (status != PXSYS_STATUS_OK)
            return status;
        if (record.optional && record.tag > 3)
            continue;
        if (record.tag == 1 && !(seen & 1u) && !record.optional && record.payload.size == 8) {
            output->call_id = pxa_read_u64(record.payload.data);
            seen |= 1u;
        } else if (record.tag == 2 && !(seen & 2u) && !record.optional &&
                   record.payload.size == 4) {
            output->status = (pxa_status_t)(int32_t)pxa_read_u32(record.payload.data);
            seen |= 2u;
        } else if (record.tag == 3 && !(seen & 4u)) {
            output->payload = pxsys_bytes(record.payload.data, record.payload.size);
            seen |= 4u;
        } else {
            return PXSYS_STATUS_INVALID_ARGUMENT;
        }
    }
    return (seen & 3u) == 3u && output->call_id != 0 && pxa_status_is_known(output->status) &&
                   output->status != PXA_STATUS_WOULD_BLOCK
               ? PXSYS_STATUS_OK
               : PXSYS_STATUS_INVALID_ARGUMENT;
}
