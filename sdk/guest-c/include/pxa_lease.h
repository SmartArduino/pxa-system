#ifndef PXA_GUEST_LEASE_H
#define PXA_GUEST_LEASE_H

#include "pxa_core.h"

#define PXA_LEASE_ACQUIRE 3u
#define PXA_LEASE_REVOKED 0x8003u

typedef struct {
    int32_t status;
    uint64_t handle;
} pxa_lease_result_t;

typedef struct {
    uint64_t handle;
    int32_t reason;
} pxa_lease_revoked_t;

static inline int pxa_lease_build_acquire(
    uint8_t *packet, size_t capacity, uint64_t token, uint16_t kind,
    uint32_t duration_ms, uint32_t *written) {
    uint8_t value[4];
    size_t size = 0;
    size_t offset = PXA_HEADER_BYTES;
    if (written != NULL) *written = 0;
    if (packet == NULL || written == NULL || token == 0 ||
        kind < 1 || kind > 6 || capacity < PXA_HEADER_BYTES + 6u)
        return 0;
    pxa_store_u16(value, kind);
    if (!pxa_wire_record_encode(packet + offset, capacity - offset,
                                1, value, 2, &size)) return 0;
    offset += size;
    if (duration_ms != 0) {
        pxa_store_u32(value, duration_ms);
        if (!pxa_wire_record_encode(packet + offset, capacity - offset,
                                    2, value, 4, &size)) return 0;
        offset += size;
    }
    return pxa_finish_message_in_place(packet, capacity,
                                          PXA_CORE_SERVICE,
                                          PXA_LEASE_ACQUIRE,
                                          token, offset, written);
}

static inline int32_t pxa_lease_acquire(uint64_t token, uint16_t kind,
                                            uint32_t duration_ms) {
    uint8_t packet[PXA_HEADER_BYTES + 14u];
    uint32_t size = 0;
    if (!pxa_lease_build_acquire(packet, sizeof(packet), token,
                                    kind, duration_ms, &size)) return -1;
    return pxa_submit(packet, size);
}

static inline int pxa_lease_parse_result(
    const pxa_event_t *event, uint64_t token,
    pxa_lease_result_t *out) {
    pxa_wire_record_view_t record;
    size_t consumed = 0;
    if (out == NULL) return 0;
    pxa_zero(out, sizeof(*out));
    if (event == NULL || event->service != PXA_CORE_SERVICE ||
        event->opcode != PXA_LEASE_ACQUIRE || token == 0 ||
        event->token != token || event->payload == NULL ||
        event->payload_size < 4) return 0;
    out->status = (int32_t)pxa_load_u32(event->payload);
    if (out->status != 0) return event->payload_size == 4;
    if (!pxa_wire_record_decode(event->payload + 4,
                                event->payload_size - 4,
                                &record, &consumed) ||
        record.raw_tag != 4 || record.payload_size != 8 ||
        consumed != event->payload_size - 4) return 0;
    out->handle = pxa_load_u64(record.payload);
    return (out->handle >> 32) != 0;
}

static inline int pxa_lease_parse_revoked(
    const pxa_event_t *event, pxa_lease_revoked_t *out) {
    if (out == NULL) return 0;
    pxa_zero(out, sizeof(*out));
    if (event == NULL || event->service != PXA_CORE_SERVICE ||
        event->opcode != PXA_LEASE_REVOKED || event->token != 0 ||
        event->payload == NULL || event->payload_size != 12) return 0;
    out->handle = pxa_load_u64(event->payload);
    out->reason = (int32_t)pxa_load_u32(event->payload + 8);
    return (out->handle >> 32) != 0;
}

#endif
