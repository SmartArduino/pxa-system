#include <assert.h>
#include <stdint.h>

#include "pxa_sensor.h"

int32_t pxa_control(const uint8_t *data, uint32_t length) {
    assert(length == 12);
    assert(pxa_read_u16(data) == PXA_SERVICE_SENSOR);
    assert(pxa_read_u16(data + 2) == PXA_SENSOR_LIST);
    return PXA_STATUS_OK;
}

int32_t pxa_io(uint32_t handle, uint32_t operation, uint8_t *data, uint32_t length) {
    (void)handle;
    (void)operation;
    (void)data;
    (void)length;
    return PXA_STATUS_UNSUPPORTED;
}

int main(void) {
    uint8_t packet[64];
    assert(pxa_sensor_list(1, packet, sizeof(packet)));
    const uint8_t subscribe[] = {
        8, 0, 2, 0, 2, 0, 0, 0, 8, 0, 0, 0,
        0, 0, 0, 0, 7, 0, 1, 0,
    };
    pxa_event_t event;
    pxa_sensor_subscribe_result_t result;
    assert(pxa_parse_event(subscribe, sizeof(subscribe), &event));
    assert(pxa_sensor_parse_subscribe(&event, &result));
    assert(result.status == PXA_STATUS_OK && result.handle == 0x00010007u);
    const uint8_t sample[] = {
        8, 0, 1, 0x80, 0, 0, 0, 0, 34, 0, 0, 0,
        4, 0, 4, 0, 7, 0, 1, 0,
        2, 0, 8, 0, 0xe8, 3, 0, 0, 0, 0, 0, 0,
        3, 0, 2, 0, 1, 0,
        4, 0, 4, 0, 0x0c, 0x54, 0, 0,
    };
    pxa_sensor_sample_t decoded;
    assert(pxa_parse_event(sample, sizeof(sample), &event));
    assert(pxa_sensor_parse_sample(&event, &decoded));
    assert(decoded.handle == 0x00010007u && decoded.timestamp_us == 1000 &&
           decoded.count == 1 && pxa_read_u32(decoded.values) == 21516u);
    return 0;
}
