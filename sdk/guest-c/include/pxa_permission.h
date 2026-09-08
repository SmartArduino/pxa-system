#ifndef PXA_PERMISSION_H
#define PXA_PERMISSION_H

#include "pxa.h"

#define PXA_SERVICE_PERMISSION 11u

#define PXA_PERMISSION_CHECK 1u
#define PXA_PERMISSION_ACQUIRE 2u
#define PXA_PERMISSION_REVOKED 0x8001u

#define PXA_PERMISSION_NAME 1u
#define PXA_PERMISSION_SCOPE 2u

#define PXA_PERMISSION_DENY 0u
#define PXA_PERMISSION_ALLOW 1u

typedef struct {
    int32_t status;
    uint8_t decision;
} pxa_permission_check_result_t;

typedef struct {
    int32_t status;
    uint32_t handle;
} pxa_permission_acquire_result_t;

static inline int pxa_permission_request(uint16_t opcode, uint32_t request_id,
                                         const char *name, size_t name_length,
                                         const uint8_t *scope, size_t scope_length,
                                         uint8_t *payload, size_t payload_capacity,
                                         uint8_t *packet, size_t packet_capacity) {
    pxa_writer_t payload_writer;
    pxa_writer_t packet_writer;
    if ((opcode != PXA_PERMISSION_CHECK && opcode != PXA_PERMISSION_ACQUIRE) ||
        request_id == 0 || name == NULL || name_length == 0 || name_length > 96 ||
        (scope == NULL && scope_length != 0) || scope_length > 1024 ||
        payload == NULL || packet == NULL || packet_capacity < 12) return 0;
    pxa_writer_init(&payload_writer, payload, payload_capacity);
    if (!pxa_record(&payload_writer, PXA_PERMISSION_NAME, (const uint8_t *)name,
                    name_length) ||
        (scope_length != 0 && !pxa_record(&payload_writer, PXA_PERMISSION_SCOPE, scope,
                                           scope_length)) ||
        payload_writer.failed) return 0;
    pxa_writer_init(&packet_writer, packet, packet_capacity);
    return pxa_message(&packet_writer, PXA_SERVICE_PERMISSION, opcode, request_id,
                       payload_writer.data, payload_writer.length) &&
           pxa_control(packet_writer.data, (uint32_t)packet_writer.length) == PXA_STATUS_OK;
}

static inline int pxa_permission_check(uint32_t request_id, const char *name,
                                       size_t name_length, const uint8_t *scope,
                                       size_t scope_length, uint8_t *payload,
                                       size_t payload_capacity, uint8_t *packet,
                                       size_t packet_capacity) {
    return pxa_permission_request(PXA_PERMISSION_CHECK, request_id, name, name_length,
                                  scope, scope_length, payload, payload_capacity,
                                  packet, packet_capacity);
}

static inline int pxa_permission_acquire(uint32_t request_id, const char *name,
                                         size_t name_length, const uint8_t *scope,
                                         size_t scope_length, uint8_t *payload,
                                         size_t payload_capacity, uint8_t *packet,
                                         size_t packet_capacity) {
    return pxa_permission_request(PXA_PERMISSION_ACQUIRE, request_id, name,
                                  name_length, scope, scope_length, payload,
                                  payload_capacity, packet, packet_capacity);
}

static inline int pxa_permission_parse_check(const pxa_event_t *event,
                                             pxa_permission_check_result_t *output) {
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_PERMISSION ||
        event->opcode != PXA_PERMISSION_CHECK || event->request_id == 0 ||
        event->payload_length < 4) return 0;
    output->status = (int32_t)pxa_read_u32(event->payload);
    if (output->status != PXA_STATUS_OK) return event->payload_length == 4;
    if (event->payload_length != 5 || event->payload[4] > PXA_PERMISSION_ALLOW) return 0;
    output->decision = event->payload[4];
    return 1;
}

static inline int pxa_permission_parse_acquire(
    const pxa_event_t *event, pxa_permission_acquire_result_t *output) {
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_PERMISSION ||
        event->opcode != PXA_PERMISSION_ACQUIRE || event->request_id == 0 ||
        event->payload_length < 4) return 0;
    output->status = (int32_t)pxa_read_u32(event->payload);
    if (output->status != PXA_STATUS_OK) return event->payload_length == 4;
    if (event->payload_length != 8) return 0;
    output->handle = pxa_read_u32(event->payload + 4);
    return output->handle != 0;
}

#endif
