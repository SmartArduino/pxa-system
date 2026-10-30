#ifndef PXA_GUEST_HAPTICS_H
#define PXA_GUEST_HAPTICS_H
#include "pxa_core.h"
#define PXA_HAPTICS_SERVICE 23u
#define PXA_HAPTICS_CAPABILITIES 1u
#define PXA_HAPTICS_OPEN 2u
#define PXA_HAPTICS_VIBRATE 3u
#define PXA_HAPTICS_STOP 4u
#define PXA_HAPTICS_CAP_VIBRATE 1u
#define PXA_HAPTICS_CAP_AMPLITUDE 2u
typedef struct { int32_t status; uint32_t flags, max_duration_ms; } pxa_haptics_capabilities_t;
static inline int32_t pxa_haptics_request_capabilities(uint64_t token) {
    uint8_t packet[PXA_HEADER_BYTES]; uint32_t size = 0;
    if (!token || !pxa_build_message(packet, sizeof(packet), 23, 1, token, NULL, 0, &size)) return -1;
    return pxa_control_packet(packet, size);
}
static inline int32_t pxa_haptics_request_open(uint64_t token, uint64_t permission) {
    uint8_t packet[PXA_HEADER_BYTES+8], payload[8]; uint32_t size = 0;
    if (!token || !(permission >> 32)) return -1;
    pxa_store_u64(payload, permission);
    if (!pxa_build_message(packet, sizeof(packet), 23, 2, token, payload, 8, &size)) return -1;
    return pxa_control_packet(packet, size);
}
static inline int32_t pxa_haptics_request_vibrate(uint64_t token, uint64_t handle,
    uint32_t duration_ms, uint16_t amplitude) {
    uint8_t packet[PXA_HEADER_BYTES+16], payload[16]; uint32_t size = 0;
    if (!token || !(handle >> 32) || !duration_ms || !amplitude || amplitude > 1000) return -1;
    pxa_store_u64(payload, handle); pxa_store_u32(payload+8, duration_ms);
    pxa_store_u16(payload+12, amplitude); pxa_store_u16(payload+14, 0);
    if (!pxa_build_message(packet, sizeof(packet), 23, 3, token, payload, 16, &size)) return -1;
    return pxa_control_packet(packet, size);
}
static inline int32_t pxa_haptics_request_stop(uint64_t token, uint64_t handle) {
    uint8_t packet[PXA_HEADER_BYTES+8], payload[8]; uint32_t size = 0;
    if (!token || !(handle >> 32)) return -1;
    pxa_store_u64(payload, handle);
    if (!pxa_build_message(packet, sizeof(packet), 23, 4, token, payload, 8, &size)) return -1;
    return pxa_control_packet(packet, size);
}
static inline int pxa_haptics_parse_capabilities(const pxa_event_t *event, uint64_t token,
    pxa_haptics_capabilities_t *output) {
    if (!output) return 0;
    pxa_zero(output, sizeof(*output));
    if (!event || event->service != 23 || event->opcode != 1 || !token || event->token != token ||
        !event->payload || event->payload_size < 4) return 0;
    output->status = (int32_t)pxa_load_u32(event->payload);
    if (output->status) return event->payload_size == 4;
    if (event->payload_size != 12) return 0;
    output->flags = pxa_load_u32(event->payload+4);
    output->max_duration_ms = pxa_load_u32(event->payload+8);
    return !(output->flags & ~3u) && (!(output->flags & 2u) || (output->flags & 1u)) &&
        !!(output->flags & 1u) == !!output->max_duration_ms;
}
static inline int pxa_haptics_parse_open(const pxa_event_t *event, uint64_t token,
    int32_t *status, uint64_t *handle) {
    if (!status || !handle) return 0;
    *handle = 0;
    if (!event || event->service != 23 || event->opcode != 2 || !token || event->token != token ||
        !event->payload || event->payload_size < 4) return 0;
    *status = (int32_t)pxa_load_u32(event->payload);
    if (*status) return event->payload_size == 4;
    if (event->payload_size != 12) return 0;
    *handle = pxa_load_u64(event->payload+4);
    return (*handle >> 32) != 0;
}
#endif
