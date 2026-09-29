#ifndef PXA_GUEST_DEVICE_H
#define PXA_GUEST_DEVICE_H

#include "pxa_core.h"
#include "pxa_device_format.h"
#include "pxa_device_runtime_info_generated.h"

#define PXA_DEVICE_SERVICE 15u
#define PXA_DEVICE_GET_MAC 1u
#define PXA_DEVICE_GET_RUNTIME_INFO 2u
#define PXA_DEVICE_GET_MAC_PACKET_BYTES (PXA_HEADER_BYTES + 18u)

#define PXA_DEVICE_MAC_WIFI_STATION_HARDWARE 1u
#define PXA_DEVICE_MAC_WIFI_SOFTAP_HARDWARE 2u
#define PXA_DEVICE_MAC_BLUETOOTH_HARDWARE 3u
#define PXA_DEVICE_MAC_ETHERNET_HARDWARE 4u
#define PXA_DEVICE_MAC_WIFI_STATION_CURRENT 5u

typedef struct {
    int32_t status;
    uint16_t kind;
    uint8_t mac[6];
    uint32_t flags;
} pxa_device_mac_result_t;

typedef pxa_device_runtime_info_payload_t pxa_device_runtime_info_t;

static inline int pxa_device_build_get_mac(
    uint8_t *out, size_t capacity, uint64_t token, uint16_t kind,
    uint64_t permission_handle, uint32_t *written) {
    uint8_t *payload;
    if (written != NULL) *written = 0;
    if (out == NULL || written == NULL ||
        capacity < PXA_DEVICE_GET_MAC_PACKET_BYTES || token == 0 ||
        kind < PXA_DEVICE_MAC_WIFI_STATION_HARDWARE ||
        kind > PXA_DEVICE_MAC_WIFI_STATION_CURRENT ||
        (permission_handle >> 32) == 0) return 0;
    payload = out + PXA_HEADER_BYTES;
    pxa_store_u16(payload, 1);
    pxa_store_u16(payload + 2, 2);
    pxa_store_u16(payload + 4, kind);
    pxa_store_u16(payload + 6, 2);
    pxa_store_u16(payload + 8, 8);
    pxa_store_u64(payload + 10, permission_handle);
    return pxa_build_message(out, capacity, PXA_DEVICE_SERVICE,
                                PXA_DEVICE_GET_MAC, token, payload, 18,
                                written);
}

static inline int32_t pxa_device_request_get_mac(
    uint64_t token, uint16_t kind, uint64_t permission_handle) {
    uint8_t packet[PXA_DEVICE_GET_MAC_PACKET_BYTES];
    uint32_t size = 0;
    if (!pxa_device_build_get_mac(packet, sizeof(packet), token, kind,
                                     permission_handle, &size)) return -1;
    return pxa_submit(packet, size);
}

static inline int pxa_device_parse_get_mac(
    const pxa_event_t *event, uint64_t expected_token,
    pxa_device_mac_result_t *output) {
    size_t offset = 4;
    pxa_wire_record_view_t record;
    size_t consumed;
    if (output == NULL) return 0;
    pxa_zero(output, sizeof(*output));
    if (event == NULL || event->service != PXA_DEVICE_SERVICE ||
        event->opcode != PXA_DEVICE_GET_MAC || expected_token == 0 ||
        event->token != expected_token || event->payload == NULL ||
        event->payload_size < 4) return 0;
    output->status = (int32_t)pxa_load_u32(event->payload);
    if (output->status != 0) return event->payload_size == 4;
    if (!pxa_wire_record_decode(event->payload + offset,
                                event->payload_size - offset,
                                &record, &consumed) ||
        record.raw_tag != 1 || record.payload_size != 2) return 0;
    output->kind = pxa_load_u16(record.payload);
    if (output->kind < PXA_DEVICE_MAC_WIFI_STATION_HARDWARE ||
        output->kind > PXA_DEVICE_MAC_WIFI_STATION_CURRENT) return 0;
    offset += consumed;
    if (!pxa_wire_record_decode(event->payload + offset,
                                event->payload_size - offset,
                                &record, &consumed) ||
        record.raw_tag != 2 || record.payload_size != 6) return 0;
    for (size_t i = 0; i < 6; ++i) output->mac[i] = record.payload[i];
    offset += consumed;
    if (!pxa_wire_record_decode(event->payload + offset,
                                event->payload_size - offset,
                                &record, &consumed) ||
        record.raw_tag != 3 || record.payload_size != 4) return 0;
    output->flags = pxa_load_u32(record.payload);
    offset += consumed;
    return offset == event->payload_size;
}

static inline int32_t pxa_device_request_runtime_info(uint64_t token) {
    uint8_t packet[PXA_HEADER_BYTES];
    uint32_t size = 0;
    if (token == 0 ||
        !pxa_build_message(packet, sizeof(packet), PXA_DEVICE_SERVICE,
                              PXA_DEVICE_GET_RUNTIME_INFO, token,
                              NULL, 0, &size))
        return -1;
    return pxa_submit(packet, size);
}

static inline int pxa_device_parse_runtime_info(
    const pxa_event_t *event, uint64_t expected_token,
    pxa_device_runtime_info_t *output) {
    if (output == NULL) return 0;
    pxa_zero(output, sizeof(*output));
    if (event == NULL || event->service != PXA_DEVICE_SERVICE ||
        event->opcode != PXA_DEVICE_GET_RUNTIME_INFO ||
        expected_token == 0 || event->token != expected_token ||
        event->payload == NULL || event->payload_size < 4)
        return 0;
    return pxa_device_runtime_info_decode(event->payload,
                                           event->payload_size, output);
}

#endif
