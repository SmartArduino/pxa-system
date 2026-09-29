#ifndef PXA_GUEST_STORAGE_H
#define PXA_GUEST_STORAGE_H

#include "pxa_core.h"

#define PXA_STORAGE_SERVICE 6u
#define PXA_STORAGE_GET 1u
#define PXA_STORAGE_SET 2u
#define PXA_STORAGE_REMOVE 3u
#define PXA_STORAGE_LIST 4u
#define PXA_STORAGE_MAX_KEY 64u
#define PXA_STORAGE_MAX_VALUE 2048u
#define PXA_STORAGE_MAX_LIST 14u
#define PXA_STORAGE_MAX_PACKET \
    (PXA_HEADER_BYTES + 8u + PXA_STORAGE_MAX_KEY + \
     PXA_STORAGE_MAX_VALUE)

typedef struct {
    const uint8_t *data;
    uint16_t size;
} pxa_storage_bytes_t;

typedef struct {
    int32_t status;
    pxa_storage_bytes_t value;
} pxa_storage_get_result_t;

typedef struct {
    int32_t status;
    uint16_t count;
    pxa_storage_bytes_t keys[PXA_STORAGE_MAX_LIST];
} pxa_storage_list_result_t;

static inline int pxa_storage_key_valid(const char *key, size_t size) {
    if (key == NULL || size == 0 || size > PXA_STORAGE_MAX_KEY ||
        !((key[0] >= 'A' && key[0] <= 'Z') ||
          (key[0] >= 'a' && key[0] <= 'z'))) return 0;
    for (size_t i = 1; i < size; ++i) {
        char value = key[i];
        if (!((value >= 'A' && value <= 'Z') ||
              (value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') ||
              value == '.' || value == '_' || value == '-')) return 0;
    }
    return 1;
}

/* Writes records into the final envelope: no heap or second payload buffer. */
static inline int pxa_storage_build(
    uint8_t *out, size_t capacity, uint16_t opcode, uint64_t token,
    const char *key, size_t key_size, const uint8_t *value,
    size_t value_size, uint32_t *written) {
    size_t payload_size = 0;
    size_t record_size = 0;
    uint8_t *payload;
    if (written != NULL) *written = 0;
    if (out == NULL || written == NULL || token == 0 ||
        opcode < PXA_STORAGE_GET || opcode > PXA_STORAGE_LIST ||
        (key == NULL && key_size != 0) ||
        (opcode != PXA_STORAGE_LIST &&
         !pxa_storage_key_valid(key, key_size)) ||
        (key != NULL && !pxa_storage_key_valid(key, key_size)) ||
        (value == NULL && value_size != 0) ||
        value_size > PXA_STORAGE_MAX_VALUE ||
        (opcode != PXA_STORAGE_SET && value_size != 0) ||
        capacity < PXA_HEADER_BYTES +
                       (key == NULL ? 0u : 4u + key_size) +
                       (opcode == PXA_STORAGE_SET ? 4u + value_size : 0u))
        return 0;
    payload = out + PXA_HEADER_BYTES;
    if (key != NULL) {
        if (!pxa_wire_record_encode(payload, capacity - PXA_HEADER_BYTES,
                                    1, (const uint8_t *)key, key_size,
                                    &record_size)) return 0;
        payload_size += record_size;
    }
    if (opcode == PXA_STORAGE_SET) {
        if (!pxa_wire_record_encode(
                payload + payload_size,
                capacity - PXA_HEADER_BYTES - payload_size,
                2, value, value_size, &record_size)) return 0;
        payload_size += record_size;
    }
    return pxa_build_message(out, capacity, PXA_STORAGE_SERVICE,
                                opcode, token, payload, payload_size,
                                written);
}

static inline int32_t pxa_storage_request_small(
    uint16_t opcode, uint64_t token, const char *key, size_t key_size) {
    uint8_t packet[PXA_HEADER_BYTES + 4u + PXA_STORAGE_MAX_KEY];
    uint32_t size = 0;
    if (opcode == PXA_STORAGE_SET ||
        !pxa_storage_build(packet, sizeof(packet), opcode, token,
                              key, key_size, NULL, 0, &size)) return -1;
    return pxa_submit(packet, size);
}

static inline int32_t pxa_storage_request_get(
    uint64_t token, const char *key, size_t key_size) {
    return pxa_storage_request_small(PXA_STORAGE_GET, token,
                                         key, key_size);
}

static inline int32_t pxa_storage_request_remove(
    uint64_t token, const char *key, size_t key_size) {
    return pxa_storage_request_small(PXA_STORAGE_REMOVE, token,
                                         key, key_size);
}

static inline int32_t pxa_storage_request_list(
    uint64_t token, const char *after_key, size_t key_size) {
    return pxa_storage_request_small(PXA_STORAGE_LIST, token,
                                         after_key, key_size);
}

/* The caller supplies the packet for large values, avoiding a 2 KiB helper
 * stack frame and allowing the same buffer to be reused across requests. */
static inline int32_t pxa_storage_request_set(
    uint8_t *packet, size_t capacity, uint64_t token,
    const char *key, size_t key_size, const uint8_t *value,
    size_t value_size) {
    uint32_t size = 0;
    if (!pxa_storage_build(packet, capacity, PXA_STORAGE_SET,
                              token, key, key_size, value, value_size,
                              &size)) return -1;
    return pxa_submit(packet, size);
}

static inline int pxa_storage_parse_status(
    const pxa_event_t *event, uint64_t token, uint16_t opcode,
    int32_t *status) {
    if (status == NULL || event == NULL || token == 0 ||
        event->service != PXA_STORAGE_SERVICE ||
        event->opcode != opcode || event->token != token ||
        event->payload == NULL || event->payload_size != 4) return 0;
    *status = (int32_t)pxa_load_u32(event->payload);
    return 1;
}

static inline int pxa_storage_parse_get(
    const pxa_event_t *event, uint64_t token,
    pxa_storage_get_result_t *output) {
    pxa_wire_record_view_t record;
    size_t consumed = 0;
    if (output == NULL) return 0;
    pxa_zero(output, sizeof(*output));
    if (event == NULL || token == 0 ||
        event->service != PXA_STORAGE_SERVICE ||
        event->opcode != PXA_STORAGE_GET || event->token != token ||
        event->payload == NULL || event->payload_size < 4) return 0;
    output->status = (int32_t)pxa_load_u32(event->payload);
    if (output->status != 0) return event->payload_size == 4;
    if (!pxa_wire_record_decode(event->payload + 4,
                                event->payload_size - 4, &record,
                                &consumed) || record.raw_tag != 2 ||
        record.payload_size > PXA_STORAGE_MAX_VALUE ||
        consumed != event->payload_size - 4) return 0;
    output->value.data = record.payload;
    output->value.size = record.payload_size;
    return 1;
}

static inline int pxa_storage_key_before(
    pxa_storage_bytes_t left, pxa_storage_bytes_t right) {
    size_t count = left.size < right.size ? left.size : right.size;
    for (size_t i = 0; i < count; ++i) {
        if (left.data[i] < right.data[i]) return 1;
        if (left.data[i] > right.data[i]) return 0;
    }
    return left.size < right.size;
}

static inline int pxa_storage_parse_list(
    const pxa_event_t *event, uint64_t token,
    pxa_storage_list_result_t *output) {
    size_t offset = 4;
    if (output == NULL) return 0;
    pxa_zero(output, sizeof(*output));
    if (event == NULL || token == 0 ||
        event->service != PXA_STORAGE_SERVICE ||
        event->opcode != PXA_STORAGE_LIST || event->token != token ||
        event->payload == NULL || event->payload_size < 4) return 0;
    output->status = (int32_t)pxa_load_u32(event->payload);
    if (output->status != 0) return event->payload_size == 4;
    while (offset < event->payload_size) {
        pxa_wire_record_view_t record;
        size_t consumed = 0;
        pxa_storage_bytes_t key;
        if (output->count == PXA_STORAGE_MAX_LIST ||
            !pxa_wire_record_decode(event->payload + offset,
                                    event->payload_size - offset,
                                    &record, &consumed) ||
            record.raw_tag != 1 ||
            !pxa_storage_key_valid((const char *)record.payload,
                                      record.payload_size)) return 0;
        key.data = record.payload;
        key.size = record.payload_size;
        if (output->count != 0 &&
            !pxa_storage_key_before(output->keys[output->count - 1],
                                        key)) return 0;
        output->keys[output->count++] = key;
        offset += consumed;
    }
    return 1;
}

#endif
