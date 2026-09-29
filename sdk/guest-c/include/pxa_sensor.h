#ifndef PXA_GUEST_SENSOR_H
#define PXA_GUEST_SENSOR_H

#include "pxa_core.h"

#define PXA_SENSOR_SERVICE 8u
#define PXA_SENSOR_LIST 1u
#define PXA_SENSOR_SUBSCRIBE 2u
#define PXA_SENSOR_SAMPLE 0x8001u
#define PXA_SENSOR_MAX_DESCRIPTORS 32u
#define PXA_SENSOR_MAX_SEMANTIC 64u
#define PXA_SENSOR_MAX_DIMENSIONS 3u

typedef struct {
    int32_t status;
    const uint8_t *records;
    size_t size;
} pxa_sensor_list_result_t;

typedef struct {
    uint16_t id;
    const uint8_t *semantic;
    uint16_t semantic_size;
    uint16_t unit;
    uint8_t dimensions;
    uint32_t min_period_ms;
    uint32_t max_period_ms;
} pxa_sensor_descriptor_t;

typedef struct {
    int32_t status;
    uint64_t handle;
} pxa_sensor_subscribe_result_t;

typedef struct {
    uint64_t handle;
    uint64_t timestamp_us;
    uint16_t count;
    uint8_t dimensions;
    int32_t values[PXA_SENSOR_MAX_DIMENSIONS];
} pxa_sensor_sample_t;

static inline int pxa_sensor_build_list(uint8_t *packet,
                                             size_t capacity, uint64_t token,
                                             uint32_t *written) {
    if (token == 0) return 0;
    return pxa_build_message(packet, capacity, PXA_SENSOR_SERVICE,
                                PXA_SENSOR_LIST, token, NULL, 0,
                                written);
}

static inline int32_t pxa_sensor_request_list(uint64_t token) {
    uint8_t packet[PXA_HEADER_BYTES];
    uint32_t size = 0;
    if (!pxa_sensor_build_list(packet, sizeof(packet), token, &size))
        return -1;
    return pxa_submit(packet, size);
}

static inline int pxa_sensor_build_subscribe(
    uint8_t *packet, size_t capacity, uint64_t token, uint16_t sensor_id,
    uint32_t period_ms, uint64_t permission_handle, uint32_t *written) {
    uint8_t value[8];
    size_t offset = PXA_HEADER_BYTES;
    size_t size = 0;
    if (written != NULL) *written = 0;
    if (packet == NULL || written == NULL || token == 0 || sensor_id == 0 ||
        period_ms == 0 || (permission_handle >> 32) == 0 ||
        capacity < PXA_HEADER_BYTES + 26u) return 0;
    pxa_store_u16(value, sensor_id);
    if (!pxa_wire_record_encode(packet + offset, capacity - offset,
                                1, value, 2, &size)) return 0;
    offset += size;
    pxa_store_u32(value, period_ms);
    if (!pxa_wire_record_encode(packet + offset, capacity - offset,
                                2, value, 4, &size)) return 0;
    offset += size;
    pxa_store_u64(value, permission_handle);
    if (!pxa_wire_record_encode(packet + offset, capacity - offset,
                                3, value, 8, &size)) return 0;
    offset += size;
    return pxa_finish_message_in_place(
        packet, capacity, PXA_SENSOR_SERVICE,
        PXA_SENSOR_SUBSCRIBE, token, offset, written);
}

static inline int32_t pxa_sensor_request_subscribe(
    uint64_t token, uint16_t sensor_id, uint32_t period_ms,
    uint64_t permission_handle) {
    uint8_t packet[PXA_HEADER_BYTES + 26u];
    uint32_t size = 0;
    if (!pxa_sensor_build_subscribe(packet, sizeof(packet), token,
                                        sensor_id, period_ms,
                                        permission_handle, &size)) return -1;
    return pxa_submit(packet, size);
}

static inline int pxa_sensor_parse_list(
    const pxa_event_t *event, uint64_t token,
    pxa_sensor_list_result_t *out) {
    if (out == NULL) return 0;
    pxa_zero(out, sizeof(*out));
    if (event == NULL || event->service != PXA_SENSOR_SERVICE ||
        event->opcode != PXA_SENSOR_LIST || token == 0 ||
        event->token != token || event->payload == NULL ||
        event->payload_size < 4) return 0;
    out->status = (int32_t)pxa_load_u32(event->payload);
    if (out->status != 0) return event->payload_size == 4;
    out->records = event->payload + 4;
    out->size = event->payload_size - 4;
    return 1;
}

static inline int pxa_sensor_nested_next(
    const uint8_t *data, size_t size, size_t *cursor,
    pxa_wire_record_view_t *record) {
    size_t consumed = 0;
    if (cursor == NULL || *cursor > size ||
        !pxa_wire_record_decode(data + *cursor, size - *cursor,
                                record, &consumed)) return 0;
    *cursor += consumed;
    return 1;
}

static inline int pxa_sensor_descriptor_next(
    const pxa_sensor_list_result_t *list, size_t *offset,
    pxa_sensor_descriptor_t *out) {
    pxa_wire_record_view_t outer;
    pxa_wire_record_view_t record;
    size_t consumed = 0;
    size_t cursor = 0;
    if (list == NULL || offset == NULL || out == NULL ||
        *offset >= list->size || list->records == NULL ||
        !pxa_wire_record_decode(list->records + *offset,
                                list->size - *offset, &outer, &consumed) ||
        outer.raw_tag != 1) return 0;
    pxa_zero(out, sizeof(*out));
    if (!pxa_sensor_nested_next(outer.payload, outer.payload_size,
                                    &cursor, &record)) return 0;
    if (record.raw_tag != 1 || record.payload_size != 2) return 0;
    out->id = pxa_load_u16(record.payload);
    if (!pxa_sensor_nested_next(outer.payload, outer.payload_size,
                                    &cursor, &record) ||
        record.raw_tag != 2 || record.payload_size == 0 ||
        record.payload_size > PXA_SENSOR_MAX_SEMANTIC) return 0;
    out->semantic = record.payload;
    out->semantic_size = record.payload_size;
    if (!pxa_sensor_nested_next(outer.payload, outer.payload_size,
                                    &cursor, &record) ||
        record.raw_tag != 3 || record.payload_size != 2) return 0;
    out->unit = pxa_load_u16(record.payload);
    if (!pxa_sensor_nested_next(outer.payload, outer.payload_size,
                                    &cursor, &record) ||
        record.raw_tag != 4 || record.payload_size != 1) return 0;
    out->dimensions = record.payload[0];
    if (!pxa_sensor_nested_next(outer.payload, outer.payload_size,
                                    &cursor, &record) ||
        record.raw_tag != 5 || record.payload_size != 4) return 0;
    out->min_period_ms = pxa_load_u32(record.payload);
    if (!pxa_sensor_nested_next(outer.payload, outer.payload_size,
                                    &cursor, &record) ||
        record.raw_tag != 6 || record.payload_size != 4 ||
        cursor != outer.payload_size) return 0;
    out->max_period_ms = pxa_load_u32(record.payload);
    if (out->id == 0 || out->unit == 0 || out->dimensions == 0 ||
        out->dimensions > PXA_SENSOR_MAX_DIMENSIONS ||
        out->min_period_ms == 0 ||
        out->min_period_ms > out->max_period_ms) return 0;
    *offset += consumed;
    return 1;
}

static inline int pxa_sensor_parse_subscribe(
    const pxa_event_t *event, uint64_t token,
    pxa_sensor_subscribe_result_t *out) {
    if (out == NULL) return 0;
    pxa_zero(out, sizeof(*out));
    if (event == NULL || event->service != PXA_SENSOR_SERVICE ||
        event->opcode != PXA_SENSOR_SUBSCRIBE || token == 0 ||
        event->token != token || event->payload == NULL ||
        event->payload_size < 4) return 0;
    out->status = (int32_t)pxa_load_u32(event->payload);
    if (out->status != 0) return event->payload_size == 4;
    if (event->payload_size != 12) return 0;
    out->handle = pxa_load_u64(event->payload + 4);
    return (out->handle >> 32) != 0;
}

static inline int pxa_sensor_parse_sample(
    const pxa_event_t *event, pxa_sensor_sample_t *out) {
    pxa_wire_record_view_t record;
    size_t offset = 0;
    size_t consumed = 0;
    if (out == NULL) return 0;
    pxa_zero(out, sizeof(*out));
    if (event == NULL || event->service != PXA_SENSOR_SERVICE ||
        event->opcode != PXA_SENSOR_SAMPLE || event->token != 0 ||
        event->payload == NULL || event->payload_size < 38) return 0;
    if (!pxa_wire_record_decode(event->payload + offset,
                                event->payload_size - offset,
                                &record, &consumed) || record.raw_tag != 4 ||
        record.payload_size != 8) return 0;
    out->handle = pxa_load_u64(record.payload);
    offset += consumed;
    if (!pxa_wire_record_decode(event->payload + offset,
                                event->payload_size - offset,
                                &record, &consumed) || record.raw_tag != 2 ||
        record.payload_size != 8) return 0;
    out->timestamp_us = pxa_load_u64(record.payload);
    offset += consumed;
    if (!pxa_wire_record_decode(event->payload + offset,
                                event->payload_size - offset,
                                &record, &consumed) || record.raw_tag != 3 ||
        record.payload_size != 2) return 0;
    out->count = pxa_load_u16(record.payload);
    offset += consumed;
    if (!pxa_wire_record_decode(event->payload + offset,
                                event->payload_size - offset,
                                &record, &consumed) || record.raw_tag != 4 ||
        record.payload_size == 0 || record.payload_size > 12 ||
        record.payload_size % 4 != 0 ||
        offset + consumed != event->payload_size) return 0;
    out->dimensions = (uint8_t)(record.payload_size / 4);
    for (uint8_t i = 0; i < out->dimensions; ++i) {
        const uint32_t bits = pxa_load_u32(record.payload + 4u * i);
        out->values[i] = bits <= INT32_MAX
            ? (int32_t)bits
            : (int32_t)((int64_t)bits - INT64_C(4294967296));
    }
    return (out->handle >> 32) != 0 && out->count == 1;
}

#endif
