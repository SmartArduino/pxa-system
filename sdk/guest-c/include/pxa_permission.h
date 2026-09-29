#ifndef PXA_GUEST_PERMISSION_H
#define PXA_GUEST_PERMISSION_H

#include "pxa_core.h"

#define PXA_PERMISSION_SERVICE 11u
#define PXA_PERMISSION_CHECK 1u
#define PXA_PERMISSION_ACQUIRE 2u
#define PXA_PERMISSION_REVOKED 0x8001u
#define PXA_PERMISSION_MAX_NAME 96u
#define PXA_PERMISSION_MAX_SCOPE 1024u
#define PXA_PERMISSION_MAX_PACKET \
    (PXA_HEADER_BYTES + 8u + PXA_PERMISSION_MAX_NAME + \
     PXA_PERMISSION_MAX_SCOPE)

typedef struct {
    int32_t status;
    uint8_t decision;
} pxa_permission_check_result_t;

typedef struct {
    int32_t status;
    uint64_t handle;
} pxa_permission_acquire_result_t;

/* Views point into the callback event and expire when that callback returns. */
typedef struct {
    const uint8_t *name;
    uint16_t name_size;
    const uint8_t *scope;
    uint16_t scope_size;
} pxa_permission_revoked_t;

static inline int pxa_permission_build(
    uint8_t *out, size_t capacity, uint16_t opcode, uint64_t token,
    const char *name, size_t name_size, const uint8_t *scope,
    size_t scope_size, uint32_t *written) {
    size_t payload_size = 0;
    size_t record_size = 0;
    if (written != NULL) *written = 0;
    if (out == NULL || written == NULL ||
        (opcode != PXA_PERMISSION_CHECK &&
         opcode != PXA_PERMISSION_ACQUIRE) || token == 0 ||
        name == NULL || name_size == 0 ||
        name_size > PXA_PERMISSION_MAX_NAME ||
        (scope == NULL && scope_size != 0) ||
        scope_size > PXA_PERMISSION_MAX_SCOPE ||
        capacity < PXA_HEADER_BYTES + 4u + name_size +
                   (scope_size != 0 ? 4u + scope_size : 0u))
        return 0;
    for (size_t i = 0; i < name_size; ++i)
        if (name[i] == '\0') return 0;
    if (!pxa_wire_record_encode(out + PXA_HEADER_BYTES,
                                capacity - PXA_HEADER_BYTES, 1,
                                (const uint8_t *)name, name_size,
                                &record_size)) return 0;
    payload_size += record_size;
    if (scope_size != 0) {
        if (!pxa_wire_record_encode(
                out + PXA_HEADER_BYTES + payload_size,
                capacity - PXA_HEADER_BYTES - payload_size, 2,
                scope, scope_size, &record_size)) return 0;
        payload_size += record_size;
    }
    return pxa_build_message(out, capacity, PXA_PERMISSION_SERVICE,
                                opcode, token, out + PXA_HEADER_BYTES,
                                payload_size, written);
}

static inline int pxa_permission_parse_check(
    const pxa_event_t *event, uint64_t expected_token,
    pxa_permission_check_result_t *output) {
    if (output == NULL) return 0;
    pxa_zero(output, sizeof(*output));
    if (event == NULL || event->service != PXA_PERMISSION_SERVICE ||
        event->opcode != PXA_PERMISSION_CHECK || expected_token == 0 ||
        event->token != expected_token || event->payload == NULL ||
        event->payload_size < 4) return 0;
    output->status = (int32_t)pxa_load_u32(event->payload);
    if (output->status != 0) return event->payload_size == 4;
    if (event->payload_size != 5 || event->payload[4] > 1) return 0;
    output->decision = event->payload[4];
    return 1;
}

static inline int pxa_permission_parse_acquire(
    const pxa_event_t *event, uint64_t expected_token,
    pxa_permission_acquire_result_t *output) {
    if (output == NULL) return 0;
    pxa_zero(output, sizeof(*output));
    if (event == NULL || event->service != PXA_PERMISSION_SERVICE ||
        event->opcode != PXA_PERMISSION_ACQUIRE || expected_token == 0 ||
        event->token != expected_token || event->payload == NULL ||
        event->payload_size < 4) return 0;
    output->status = (int32_t)pxa_load_u32(event->payload);
    if (output->status != 0) return event->payload_size == 4;
    if (event->payload_size != 12) return 0;
    output->handle = pxa_load_u64(event->payload + 4);
    return (output->handle >> 32) != 0;
}

static inline int pxa_permission_parse_revoked(
    const pxa_event_t *event, pxa_permission_revoked_t *output) {
    pxa_wire_record_view_t record;
    size_t consumed = 0;
    size_t offset = 0;
    if (output == NULL) return 0;
    pxa_zero(output, sizeof(*output));
    if (event == NULL || event->service != PXA_PERMISSION_SERVICE ||
        event->opcode != PXA_PERMISSION_REVOKED || event->token != 0 ||
        event->payload == NULL) return 0;
    if (!pxa_wire_record_decode(event->payload, event->payload_size,
                                &record, &consumed) || record.raw_tag != 1 ||
        record.payload_size == 0 ||
        record.payload_size > PXA_PERMISSION_MAX_NAME) return 0;
    output->name = record.payload;
    output->name_size = record.payload_size;
    offset += consumed;
    if (!pxa_wire_record_decode(event->payload + offset,
                                event->payload_size - offset,
                                &record, &consumed) || record.raw_tag != 2 ||
        record.payload_size > PXA_PERMISSION_MAX_SCOPE) return 0;
    output->scope = record.payload;
    output->scope_size = record.payload_size;
    offset += consumed;
    return offset == event->payload_size;
}

#endif
