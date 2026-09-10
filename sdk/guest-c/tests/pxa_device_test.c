#include "pxa_device.h"

#include <assert.h>
#include <string.h>

static uint8_t captured[64];
static uint32_t captured_length;

int32_t pxa_control(const uint8_t *data, uint32_t length) {
    assert(length <= sizeof(captured));
    memcpy(captured, data, length);
    captured_length = length;
    return PXA_STATUS_OK;
}

int32_t pxa_io(uint32_t handle, uint32_t operation, uint8_t *data,
               uint32_t length) {
    (void)handle;
    (void)operation;
    (void)data;
    (void)length;
    return PXA_STATUS_UNSUPPORTED;
}

int main(void) {
    uint8_t payload[16];
    uint8_t packet[64];
    uint8_t result_payload[32];
    uint8_t result_packet[64];
    uint8_t value[4];
    char formatted[18];
    pxa_writer_t result;
    pxa_writer_t message;
    pxa_event_t event;
    pxa_device_mac_result_t decoded;

    assert(pxa_device_get_mac(
        17, PXA_DEVICE_MAC_KIND_WIFI_STATION_HARDWARE, 0x00010002u,
        payload, sizeof(payload), packet, sizeof(packet)));
    assert(captured_length == 26 && pxa_read_u16(captured) == PXA_SERVICE_DEVICE &&
           pxa_read_u16(captured + 2) == PXA_DEVICE_GET_MAC &&
           pxa_read_u32(captured + 4) == 17);

    pxa_writer_init(&result, result_payload, sizeof(result_payload));
    assert(pxa_put_u32(&result, PXA_STATUS_OK));
    value[0] = PXA_DEVICE_MAC_KIND_WIFI_STATION_HARDWARE;
    value[1] = 0;
    assert(pxa_record(&result, PXA_DEVICE_MAC_KIND, value, 2));
    assert(pxa_record(&result, PXA_DEVICE_MAC,
                      (const uint8_t[]){0x24, 0x6f, 0x28, 0x70, 0x14, 0x01},
                      6));
    value[0] = PXA_DEVICE_MAC_FLAG_HARDWARE;
    value[1] = value[2] = value[3] = 0;
    assert(pxa_record(&result, PXA_DEVICE_FLAGS, value, 4));
    pxa_writer_init(&message, result_packet, sizeof(result_packet));
    assert(pxa_message(&message, PXA_SERVICE_DEVICE, PXA_DEVICE_GET_MAC, 17,
                       result.data, result.length));
    assert(pxa_parse_event(result_packet, (uint32_t)message.length, &event));
    assert(pxa_device_parse_mac(&event, &decoded));
    assert(decoded.status == PXA_STATUS_OK &&
           decoded.kind == PXA_DEVICE_MAC_KIND_WIFI_STATION_HARDWARE &&
           decoded.mac[0] == 0x24 && decoded.mac[5] == 0x01 &&
           decoded.flags == PXA_DEVICE_MAC_FLAG_HARDWARE);
    assert(pxa_device_format_mac_colon(decoded.mac, formatted,
                                       sizeof(formatted)) &&
           strcmp(formatted, "24:6F:28:70:14:01") == 0);
    return 0;
}
