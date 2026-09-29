#include <assert.h>
#include <string.h>

#include "pxa_sensor.h"

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
    return -3;
}

int main(void) {
    const uint64_t permission = UINT64_C(0x1234567800000001);
    const uint64_t subscription = UINT64_C(0x2345678900000002);
    uint8_t packet[64];
    uint8_t payload[100] = {0};
    uint8_t nested[100];
    uint8_t value[8];
    uint32_t written = 0;
    size_t size = 0;
    size_t offset = 0;
    size_t nested_size = 0;
    pxa_event_t event = {0};
    pxa_wire_record_view_t record;
    pxa_sensor_subscribe_result_t acquired;
    pxa_sensor_sample_t sample;
    pxa_sensor_list_result_t list;
    pxa_sensor_descriptor_t descriptor;
    assert(pxa_sensor_build_subscribe(packet, sizeof(packet), 77, 3,
                                         100, permission, &written) &&
           written == 46);
    assert(pxa_parse_event(packet, written, &event) &&
           event.service == PXA_SENSOR_SERVICE &&
           event.opcode == PXA_SENSOR_SUBSCRIBE && event.token == 77);
    assert(pxa_wire_record_decode(event.payload, event.payload_size,
                                  &record, &size) && record.tag == 1);
    offset = size;
    assert(pxa_wire_record_decode(event.payload + offset,
                                  event.payload_size - offset,
                                  &record, &size) && record.tag == 2);
    offset += size;
    assert(pxa_wire_record_decode(event.payload + offset,
                                  event.payload_size - offset,
                                  &record, &size) && record.tag == 3 &&
           record.payload_size == 8 &&
           pxa_load_u64(record.payload) == permission);
    assert(!pxa_sensor_build_subscribe(packet, sizeof(packet), 77, 3,
                                          100, 1, &written));

    pxa_store_u32(payload, 0);
    pxa_store_u64(payload + 4, subscription);
    event.service = PXA_SENSOR_SERVICE;
    event.opcode = PXA_SENSOR_SUBSCRIBE;
    event.token = 77;
    event.payload = payload;
    event.payload_size = 12;
    assert(pxa_sensor_parse_subscribe(&event, 77, &acquired) &&
           acquired.handle == subscription);
    assert(!pxa_sensor_parse_subscribe(&event, 78, &acquired));
    event.payload_size = 8;
    assert(!pxa_sensor_parse_subscribe(&event, 77, &acquired));

    offset = 0;
    pxa_store_u64(value, subscription);
    assert(pxa_wire_record_encode(payload + offset, sizeof(payload) - offset,
                                  4, value, 8, &size));
    offset += size;
    pxa_store_u64(value, 123456);
    assert(pxa_wire_record_encode(payload + offset, sizeof(payload) - offset,
                                  2, value, 8, &size));
    offset += size;
    pxa_store_u16(value, 1);
    assert(pxa_wire_record_encode(payload + offset, sizeof(payload) - offset,
                                  3, value, 2, &size));
    offset += size;
    pxa_store_u32(value, UINT32_C(0xfffffffe));
    pxa_store_u32(value + 4, 42);
    assert(pxa_wire_record_encode(payload + offset, sizeof(payload) - offset,
                                  4, value, 8, &size));
    offset += size;
    event.opcode = PXA_SENSOR_SAMPLE;
    event.token = 0;
    event.payload_size = (uint32_t)offset;
    assert(pxa_sensor_parse_sample(&event, &sample) &&
           sample.handle == subscription && sample.timestamp_us == 123456 &&
           sample.count == 1 && sample.dimensions == 2 &&
           sample.values[0] == -2 && sample.values[1] == 42);
    payload[2] = 4;
    assert(!pxa_sensor_parse_sample(&event, &sample));

    nested_size = 0;
    pxa_store_u16(value, 3);
    assert(pxa_wire_record_encode(nested + nested_size,
                                  sizeof(nested) - nested_size,
                                  1, value, 2, &size));
    nested_size += size;
    assert(pxa_wire_record_encode(nested + nested_size,
                                  sizeof(nested) - nested_size,
                                  2, (const uint8_t *)"ambient.light", 13,
                                  &size));
    nested_size += size;
    pxa_store_u16(value, 4);
    assert(pxa_wire_record_encode(nested + nested_size,
                                  sizeof(nested) - nested_size,
                                  3, value, 2, &size));
    nested_size += size;
    value[0] = 1;
    assert(pxa_wire_record_encode(nested + nested_size,
                                  sizeof(nested) - nested_size,
                                  4, value, 1, &size));
    nested_size += size;
    pxa_store_u32(value, 100);
    assert(pxa_wire_record_encode(nested + nested_size,
                                  sizeof(nested) - nested_size,
                                  5, value, 4, &size));
    nested_size += size;
    pxa_store_u32(value, 1000);
    assert(pxa_wire_record_encode(nested + nested_size,
                                  sizeof(nested) - nested_size,
                                  6, value, 4, &size));
    nested_size += size;
    pxa_store_u32(payload, 0);
    assert(pxa_wire_record_encode(payload + 4, sizeof(payload) - 4,
                                  1, nested, nested_size, &size));
    event.opcode = PXA_SENSOR_LIST;
    event.token = 78;
    event.payload_size = (uint32_t)(4 + size);
    assert(pxa_sensor_parse_list(&event, 78, &list));
    offset = 0;
    assert(pxa_sensor_descriptor_next(&list, &offset, &descriptor) &&
           descriptor.id == 3 && descriptor.unit == 4 &&
           descriptor.dimensions == 1 && descriptor.min_period_ms == 100 &&
           descriptor.max_period_ms == 1000 && offset == list.size &&
           memcmp(descriptor.semantic, "ambient.light", 13) == 0);
    return 0;
}
