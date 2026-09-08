#ifndef PXA_LEASE_H
#define PXA_LEASE_H

#include "pxa.h"

#define PXA_CORE_ACQUIRE_LEASE 3u
#define PXA_CORE_LEASE_REVOKED 0x8003u

#define PXA_LEASE_KIND 1u
#define PXA_LEASE_DURATION_MS 2u
#define PXA_LEASE_GRANTED_HANDLE 4u

#define PXA_LEASE_FOREGROUND 1u
#define PXA_LEASE_AUDIO_PLAYBACK 2u
#define PXA_LEASE_AUDIO_CAPTURE 3u
#define PXA_LEASE_NETWORK_TRANSFER 4u
#define PXA_LEASE_SENSOR_MONITOR 5u
#define PXA_LEASE_SCHEDULED_JOB 6u

typedef struct {
    int32_t status;
    uint32_t handle;
} pxa_lease_result_t;

typedef struct {
    uint32_t handle;
    int32_t reason;
} pxa_lease_revoked_t;

static inline int pxa_lease_acquire(uint32_t request_id, uint16_t kind,
                                    uint32_t duration_ms, uint8_t* payload,
                                    size_t payload_capacity, uint8_t* packet,
                                    size_t packet_capacity) {
    pxa_writer_t payload_writer;
    pxa_writer_t packet_writer;
    uint8_t encoded_kind[2] = {(uint8_t)kind, (uint8_t)(kind >> 8)};
    uint8_t encoded_duration[4] = {(uint8_t)duration_ms, (uint8_t)(duration_ms >> 8),
                                   (uint8_t)(duration_ms >> 16),
                                   (uint8_t)(duration_ms >> 24)};
    if (request_id == 0 || kind < PXA_LEASE_FOREGROUND ||
        kind > PXA_LEASE_SCHEDULED_JOB || duration_ms == 0 || payload == NULL ||
        packet == NULL || packet_capacity < 12) return 0;
    pxa_writer_init(&payload_writer, payload, payload_capacity);
    if (!pxa_record(&payload_writer, PXA_LEASE_KIND, encoded_kind,
                    sizeof(encoded_kind)) ||
        !pxa_record(&payload_writer, PXA_LEASE_DURATION_MS, encoded_duration,
                    sizeof(encoded_duration)) || payload_writer.failed) return 0;
    pxa_writer_init(&packet_writer, packet, packet_capacity);
    return pxa_message(&packet_writer, PXA_SERVICE_CORE, PXA_CORE_ACQUIRE_LEASE,
                       request_id, payload_writer.data, payload_writer.length) &&
           pxa_control(packet_writer.data, (uint32_t)packet_writer.length) == PXA_STATUS_OK;
}

static inline int pxa_lease_parse_result(const pxa_event_t* event,
                                         pxa_lease_result_t* output) {
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_CORE ||
        event->opcode != PXA_CORE_ACQUIRE_LEASE || event->request_id == 0 ||
        event->payload_length < 4) return 0;
    output->status = (int32_t)pxa_read_u32(event->payload);
    if (output->status != PXA_STATUS_OK) return event->payload_length == 4;
    if (event->payload_length != 12 || pxa_read_u16(event->payload + 4) !=
        PXA_LEASE_GRANTED_HANDLE || pxa_read_u16(event->payload + 6) != 4) return 0;
    output->handle = pxa_read_u32(event->payload + 8);
    return output->handle != 0;
}

static inline int pxa_lease_parse_revoked(const pxa_event_t* event,
                                          pxa_lease_revoked_t* output) {
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_CORE ||
        event->opcode != PXA_CORE_LEASE_REVOKED || event->request_id != 0 ||
        event->payload_length != 8) return 0;
    output->handle = pxa_read_u32(event->payload);
    output->reason = (int32_t)pxa_read_u32(event->payload + 4);
    return output->handle != 0 && output->reason <= PXA_STATUS_OK &&
           output->reason >= PXA_STATUS_INTERNAL;
}

#endif
