#ifndef PXA_DEVICE_H
#define PXA_DEVICE_H

#include "pxa.h"

#define PXA_SERVICE_DEVICE 15u
#define PXA_DEVICE_GET_MAC 1u
#define PXA_DEVICE_GET_RUNTIME_INFO 2u
#define PXA_DEVICE_FORMAT_WASM 1u
#define PXA_DEVICE_FORMAT_AOT 2u

typedef struct {
    int32_t status;
    char target[32];
    char architecture[24];
    char engine[24];
    char engine_abi[80];
    uint32_t formats;
} pxa_device_runtime_info_t;

static inline int pxa_device_get_runtime_info(uint32_t request_id,
                                              uint8_t *packet,
                                              size_t packet_capacity) {
    pxa_writer_t message;
    if (request_id == 0 || packet == NULL) return 0;
    pxa_writer_init(&message, packet, packet_capacity);
    return pxa_message(&message, PXA_SERVICE_DEVICE,
                       PXA_DEVICE_GET_RUNTIME_INFO, request_id, NULL, 0) &&
           pxa_control(message.data, (uint32_t)message.length) == PXA_STATUS_OK;
}

static inline int pxa_device_parse_runtime_info(
    const pxa_event_t *event, pxa_device_runtime_info_t *output) {
    size_t offset = 4;
    uint8_t seen = 0;
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_DEVICE ||
        event->opcode != PXA_DEVICE_GET_RUNTIME_INFO || event->request_id == 0 ||
        event->payload_length < 4) return 0;
    output->status = (int32_t)pxa_read_u32(event->payload);
    if (output->status != PXA_STATUS_OK) return event->payload_length == 4;
    while (offset < event->payload_length) {
        uint16_t tag;
        uint16_t length;
        char *destination = NULL;
        size_t capacity = 0;
        if (event->payload_length - offset < 4) return 0;
        tag = pxa_read_u16(event->payload + offset);
        length = pxa_read_u16(event->payload + offset + 2);
        offset += 4;
        if (tag < 1 || tag > 5 || tag != (uint16_t)(seen + 1u) ||
            length > event->payload_length - offset) return 0;
        if (tag == 1) { destination = output->target; capacity = sizeof(output->target); }
        if (tag == 2) { destination = output->architecture; capacity = sizeof(output->architecture); }
        if (tag == 3) { destination = output->engine; capacity = sizeof(output->engine); }
        if (tag == 4) { destination = output->engine_abi; capacity = sizeof(output->engine_abi); }
        if (tag == 5) {
            if (length != 4) return 0;
            output->formats = pxa_read_u32(event->payload + offset);
        } else {
            if (length == 0 || length >= capacity) return 0;
            for (size_t index = 0; index < length; ++index) {
                if (event->payload[offset + index] == 0) return 0;
                destination[index] = (char)event->payload[offset + index];
            }
            destination[length] = '\0';
        }
        seen = (uint8_t)tag;
        offset += length;
    }
    return seen == 5;
}

#define PXA_DEVICE_MAC_KIND_WIFI_STATION_HARDWARE 1u
#define PXA_DEVICE_MAC_KIND_WIFI_SOFTAP_HARDWARE 2u
#define PXA_DEVICE_MAC_KIND_BLUETOOTH_HARDWARE 3u
#define PXA_DEVICE_MAC_KIND_ETHERNET_HARDWARE 4u
#define PXA_DEVICE_MAC_KIND_WIFI_STATION_CURRENT 5u

#define PXA_DEVICE_MAC_FLAG_HARDWARE 1u
#define PXA_DEVICE_MAC_FLAG_CURRENT 2u
#define PXA_DEVICE_MAC_FLAG_LOCALLY_ADMINISTERED 4u

#define PXA_DEVICE_MAC_KIND 1u
#define PXA_DEVICE_PERMISSION_HANDLE 2u
#define PXA_DEVICE_MAC 2u
#define PXA_DEVICE_FLAGS 3u

typedef struct {
    int32_t status;
    uint16_t kind;
    uint8_t mac[6];
    uint32_t flags;
} pxa_device_mac_result_t;

static inline int pxa_device_get_mac(uint32_t request_id, uint16_t kind,
                                     uint32_t permission_handle,
                                     uint8_t *payload, size_t payload_capacity,
                                     uint8_t *packet, size_t packet_capacity) {
    pxa_writer_t request;
    pxa_writer_t message;
    uint8_t value[4];
    if (request_id == 0 || kind < PXA_DEVICE_MAC_KIND_WIFI_STATION_HARDWARE ||
        kind > PXA_DEVICE_MAC_KIND_WIFI_STATION_CURRENT ||
        permission_handle == 0 || payload == NULL || packet == NULL ||
        packet_capacity < 12) return 0;
    pxa_writer_init(&request, payload, payload_capacity);
    value[0] = (uint8_t)kind;
    value[1] = (uint8_t)(kind >> 8);
    if (!pxa_record(&request, PXA_DEVICE_MAC_KIND, value, 2)) return 0;
    value[0] = (uint8_t)permission_handle;
    value[1] = (uint8_t)(permission_handle >> 8);
    value[2] = (uint8_t)(permission_handle >> 16);
    value[3] = (uint8_t)(permission_handle >> 24);
    if (!pxa_record(&request, PXA_DEVICE_PERMISSION_HANDLE, value, 4)) return 0;
    pxa_writer_init(&message, packet, packet_capacity);
    return pxa_message(&message, PXA_SERVICE_DEVICE, PXA_DEVICE_GET_MAC,
                       request_id, request.data, request.length) &&
           pxa_control(message.data, (uint32_t)message.length) == PXA_STATUS_OK;
}

static inline int pxa_device_parse_mac(const pxa_event_t *event,
                                       pxa_device_mac_result_t *output) {
    size_t offset = 4;
    uint16_t previous = 0;
    uint8_t seen = 0;
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_DEVICE ||
        event->opcode != PXA_DEVICE_GET_MAC || event->request_id == 0 ||
        event->payload_length < 4) return 0;
    output->status = (int32_t)pxa_read_u32(event->payload);
    if (output->status != PXA_STATUS_OK) return event->payload_length == 4;
    while (offset < event->payload_length) {
        uint16_t tag;
        uint16_t length;
        if (event->payload_length - offset < 4) return 0;
        tag = pxa_read_u16(event->payload + offset);
        length = pxa_read_u16(event->payload + offset + 2);
        offset += 4;
        if (tag < previous || length > event->payload_length - offset) return 0;
        previous = tag;
        if (tag == PXA_DEVICE_MAC_KIND && (seen & 1u) == 0 && length == 2) {
            output->kind = pxa_read_u16(event->payload + offset);
            seen |= 1u;
        } else if (tag == PXA_DEVICE_MAC && (seen & 2u) == 0 && length == 6) {
            for (size_t index = 0; index < 6; ++index) output->mac[index] = event->payload[offset + index];
            seen |= 2u;
        } else if (tag == PXA_DEVICE_FLAGS && (seen & 4u) == 0 && length == 4) {
            output->flags = pxa_read_u32(event->payload + offset);
            seen |= 4u;
        } else {
            return 0;
        }
        offset += length;
    }
    return seen == 7u && output->kind >= PXA_DEVICE_MAC_KIND_WIFI_STATION_HARDWARE &&
           output->kind <= PXA_DEVICE_MAC_KIND_WIFI_STATION_CURRENT;
}

static inline int pxa_device_format_mac_colon(const uint8_t mac[6], char *output,
                                               size_t output_capacity) {
    static const char hex[] = "0123456789ABCDEF";
    size_t index;
    if (mac == NULL || output == NULL || output_capacity < 18) return 0;
    for (index = 0; index < 6; ++index) {
        output[index * 3] = hex[mac[index] >> 4];
        output[index * 3 + 1u] = hex[mac[index] & 0x0fu];
        if (index != 5) output[index * 3 + 2u] = ':';
    }
    output[17] = '\0';
    return 1;
}

#endif
