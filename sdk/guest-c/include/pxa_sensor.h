#ifndef PXA_SENSOR_H
#define PXA_SENSOR_H

#include "pxa.h"

#define PXA_SENSOR_LIST 1u
#define PXA_SENSOR_SUBSCRIBE 2u
#define PXA_SENSOR_SAMPLE 0x8001u

#define PXA_SENSOR_DESCRIPTOR 1u
#define PXA_SENSOR_ID 1u
#define PXA_SENSOR_SEMANTIC 2u
#define PXA_SENSOR_UNIT 3u
#define PXA_SENSOR_DIMENSIONS 4u
#define PXA_SENSOR_MIN_PERIOD_MS 5u
#define PXA_SENSOR_MAX_PERIOD_MS 6u
#define PXA_SENSOR_PERIOD_MS 2u
#define PXA_SENSOR_PERMISSION_HANDLE 3u
#define PXA_SENSOR_SUBSCRIPTION_HANDLE 4u
#define PXA_SENSOR_TIMESTAMP_US 2u
#define PXA_SENSOR_SAMPLE_COUNT 3u
#define PXA_SENSOR_VALUES 4u

typedef struct {
    int32_t status;
    const uint8_t *descriptors;
    uint32_t descriptors_length;
} pxa_sensor_list_result_t;

typedef struct {
    uint16_t id;
    const uint8_t *semantic;
    uint16_t semantic_length;
    uint16_t unit;
    uint8_t dimensions;
    uint32_t min_period_ms;
    uint32_t max_period_ms;
} pxa_sensor_descriptor_t;

typedef struct {
    int32_t status;
    uint32_t handle;
} pxa_sensor_subscribe_result_t;

typedef struct {
    uint32_t handle;
    uint64_t timestamp_us;
    uint16_t count;
    const uint8_t *values;
    uint16_t values_length;
} pxa_sensor_sample_t;

static inline int pxa_sensor_list(uint32_t request_id, uint8_t *packet,
                                  size_t packet_capacity) {
    pxa_writer_t writer;
    if (request_id == 0 || packet == NULL || packet_capacity < 12) return 0;
    pxa_writer_init(&writer, packet, packet_capacity);
    return pxa_message(&writer, PXA_SERVICE_SENSOR, PXA_SENSOR_LIST, request_id,
                       NULL, 0) &&
           pxa_control(writer.data, (uint32_t)writer.length) == PXA_STATUS_OK;
}

static inline int pxa_sensor_subscribe(uint32_t request_id, uint16_t sensor_id,
                                       uint32_t period_ms, uint32_t permission_handle,
                                       uint8_t *payload, size_t payload_capacity,
                                       uint8_t *packet, size_t packet_capacity) {
    pxa_writer_t request;
    pxa_writer_t message;
    if (request_id == 0 || sensor_id == 0 || period_ms == 0 || permission_handle == 0 ||
        payload == NULL || packet == NULL || packet_capacity < 12) return 0;
    pxa_writer_init(&request, payload, payload_capacity);
    if (!pxa_put_u16(&request, PXA_SENSOR_ID) || !pxa_put_u16(&request, 2) ||
        !pxa_put_u16(&request, sensor_id) || !pxa_put_u16(&request, PXA_SENSOR_PERIOD_MS) ||
        !pxa_put_u16(&request, 4) || !pxa_put_u32(&request, period_ms) ||
        !pxa_put_u16(&request, PXA_SENSOR_PERMISSION_HANDLE) ||
        !pxa_put_u16(&request, 4) || !pxa_put_u32(&request, permission_handle)) return 0;
    pxa_writer_init(&message, packet, packet_capacity);
    return pxa_message(&message, PXA_SERVICE_SENSOR, PXA_SENSOR_SUBSCRIBE, request_id,
                       request.data, request.length) &&
           pxa_control(message.data, (uint32_t)message.length) == PXA_STATUS_OK;
}

static inline int pxa_sensor_parse_list(const pxa_event_t *event,
                                        pxa_sensor_list_result_t *output) {
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_SENSOR ||
        event->opcode != PXA_SENSOR_LIST || event->request_id == 0 || event->payload_length < 4)
        return 0;
    output->status = (int32_t)pxa_read_u32(event->payload);
    output->descriptors = event->payload + 4;
    output->descriptors_length = event->payload_length - 4;
    return output->status == PXA_STATUS_OK || output->descriptors_length == 0;
}

static inline int pxa_sensor_descriptor_next(const pxa_sensor_list_result_t *result,
                                             size_t *offset,
                                             pxa_sensor_descriptor_t *output) {
    const uint8_t *item;
    uint16_t item_length;
    size_t cursor = 0;
    if (result == NULL || offset == NULL || output == NULL ||
        *offset >= result->descriptors_length ||
        result->descriptors_length - *offset < 4) return 0;
    item = result->descriptors + *offset;
    item_length = pxa_read_u16(item + 2);
    if (pxa_read_u16(item) != PXA_SENSOR_DESCRIPTOR ||
        item_length > result->descriptors_length - *offset - 4) return 0;
    item += 4;
    if (item_length < 38 || pxa_read_u16(item) != PXA_SENSOR_ID ||
        pxa_read_u16(item + 2) != 2) return 0;
    output->id = pxa_read_u16(item + 4);
    cursor = 6;
    if (cursor + 4 > item_length || pxa_read_u16(item + cursor) != PXA_SENSOR_SEMANTIC)
        return 0;
    output->semantic_length = pxa_read_u16(item + cursor + 2);
    cursor += 4;
    if (output->semantic_length == 0 || cursor + output->semantic_length + 27 > item_length)
        return 0;
    output->semantic = item + cursor;
    cursor += output->semantic_length;
    if (pxa_read_u16(item + cursor) != PXA_SENSOR_UNIT ||
        pxa_read_u16(item + cursor + 2) != 2) return 0;
    output->unit = pxa_read_u16(item + cursor + 4);
    cursor += 6;
    if (pxa_read_u16(item + cursor) != PXA_SENSOR_DIMENSIONS ||
        pxa_read_u16(item + cursor + 2) != 1) return 0;
    output->dimensions = item[cursor + 4];
    cursor += 5;
    if (pxa_read_u16(item + cursor) != PXA_SENSOR_MIN_PERIOD_MS ||
        pxa_read_u16(item + cursor + 2) != 4) return 0;
    output->min_period_ms = pxa_read_u32(item + cursor + 4);
    cursor += 8;
    if (pxa_read_u16(item + cursor) != PXA_SENSOR_MAX_PERIOD_MS ||
        pxa_read_u16(item + cursor + 2) != 4 || cursor + 8 != item_length) return 0;
    output->max_period_ms = pxa_read_u32(item + cursor + 4);
    *offset += 4 + item_length;
    return output->id != 0 && output->dimensions >= 1 && output->dimensions <= 3 &&
           output->min_period_ms != 0 && output->min_period_ms <= output->max_period_ms;
}

static inline int pxa_sensor_parse_subscribe(const pxa_event_t *event,
                                             pxa_sensor_subscribe_result_t *output) {
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_SENSOR ||
        event->opcode != PXA_SENSOR_SUBSCRIBE || event->request_id == 0 ||
        event->payload_length < 4) return 0;
    output->status = (int32_t)pxa_read_u32(event->payload);
    if (output->status != PXA_STATUS_OK) return event->payload_length == 4;
    if (event->payload_length != 8) return 0;
    output->handle = pxa_read_u32(event->payload + 4);
    return output->handle != 0;
}

static inline int pxa_sensor_parse_sample(const pxa_event_t *event,
                                          pxa_sensor_sample_t *output) {
    const uint8_t *value;
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_SENSOR ||
        event->opcode != PXA_SENSOR_SAMPLE || event->request_id != 0 ||
        event->payload_length < 30) return 0;
    value = event->payload;
    if (pxa_read_u16(value) != PXA_SENSOR_SUBSCRIPTION_HANDLE ||
        pxa_read_u16(value + 2) != 4 || pxa_read_u16(value + 8) != PXA_SENSOR_TIMESTAMP_US ||
        pxa_read_u16(value + 10) != 8 || pxa_read_u16(value + 20) != PXA_SENSOR_SAMPLE_COUNT ||
        pxa_read_u16(value + 22) != 2 || pxa_read_u16(value + 26) != PXA_SENSOR_VALUES) return 0;
    output->handle = pxa_read_u32(value + 4);
    output->timestamp_us = pxa_read_u64(value + 12);
    output->count = pxa_read_u16(value + 24);
    output->values_length = pxa_read_u16(value + 28);
    output->values = value + 30;
    return output->handle != 0 && output->count != 0 && output->values_length != 0 &&
           output->values_length == event->payload_length - 30;
}

#endif
