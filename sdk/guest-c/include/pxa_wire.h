/* Generated from spec/draft/abi-1.0-envelope.json and
 * spec/draft/pxa-core.json for the Guest SDK. Do not edit by hand. */
#ifndef PXA_GUEST_WIRE_H
#define PXA_GUEST_WIRE_H

#include <stddef.h>
#include <stdint.h>

#define PXA_WIRE_MAX_CONTROL_MESSAGE ((size_t)4096)

/* The Host and Guest copies share the byte and record helpers; keep one
 * definition when a translation unit includes both. */
#ifndef PXA_WIRE_GENERATED_H
static inline uint16_t pxa_wire_generated_load_u16(
    const uint8_t *bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static inline uint32_t pxa_wire_generated_load_u32(
    const uint8_t *bytes) {
    return (uint32_t)pxa_wire_generated_load_u16(bytes) |
           ((uint32_t)pxa_wire_generated_load_u16(bytes + 2) << 16);
}

static inline uint64_t pxa_wire_generated_load_u64(
    const uint8_t *bytes) {
    return (uint64_t)pxa_wire_generated_load_u32(bytes) |
           ((uint64_t)pxa_wire_generated_load_u32(bytes + 4) << 32);
}

static inline void pxa_wire_generated_store_u16(
    uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static inline void pxa_wire_generated_store_u32(
    uint8_t *bytes, uint32_t value) {
    pxa_wire_generated_store_u16(bytes, (uint16_t)value);
    pxa_wire_generated_store_u16(bytes + 2, (uint16_t)(value >> 16));
}

static inline void pxa_wire_generated_store_u64(
    uint8_t *bytes, uint64_t value) {
    pxa_wire_generated_store_u32(bytes, (uint32_t)value);
    pxa_wire_generated_store_u32(bytes + 4, (uint32_t)(value >> 32));
}

#define PXA_WIRE_RECORD_SIZE ((size_t)4)
#define PXA_WIRE_RECORD_TAG_OFFSET ((size_t)0)
#define PXA_WIRE_RECORD_PAYLOAD_LEN_OFFSET ((size_t)2)
#define PXA_WIRE_RECORD_OPTIONAL_MASK UINT16_C(32768)
#define PXA_WIRE_RECORD_TAG_MASK UINT16_C(32767)

typedef struct {
    uint16_t raw_tag;
    uint16_t tag;
    uint8_t optional;
    const uint8_t *payload;
    uint16_t payload_size;
} pxa_wire_record_view_t;

static inline int pxa_wire_record_decode(
    const uint8_t *data, size_t size, pxa_wire_record_view_t *output,
    size_t *consumed) {
    uint16_t raw_tag;
    uint16_t payload_size;
    if (consumed != NULL) *consumed = 0;
    if (output == NULL || consumed == NULL) return 0;
    output->raw_tag = 0;
    output->tag = 0;
    output->optional = 0;
    output->payload = NULL;
    output->payload_size = 0;
    if (data == NULL || size < PXA_WIRE_RECORD_SIZE) return 0;
    raw_tag = pxa_wire_generated_load_u16(
        data + PXA_WIRE_RECORD_TAG_OFFSET);
    payload_size = pxa_wire_generated_load_u16(
        data + PXA_WIRE_RECORD_PAYLOAD_LEN_OFFSET);
    if ((raw_tag & PXA_WIRE_RECORD_TAG_MASK) == 0 ||
        payload_size > size - PXA_WIRE_RECORD_SIZE) return 0;
    output->raw_tag = raw_tag;
    output->tag = raw_tag & PXA_WIRE_RECORD_TAG_MASK;
    output->optional =
        (uint8_t)((raw_tag & PXA_WIRE_RECORD_OPTIONAL_MASK) != 0);
    output->payload = data + PXA_WIRE_RECORD_SIZE;
    output->payload_size = payload_size;
    *consumed = PXA_WIRE_RECORD_SIZE + payload_size;
    return 1;
}

static inline int pxa_wire_record_encode(
    uint8_t *out, size_t capacity, uint16_t raw_tag,
    const uint8_t *payload, size_t payload_size, size_t *written) {
    if (written != NULL) *written = 0;
    if (out == NULL || written == NULL ||
        (raw_tag & PXA_WIRE_RECORD_TAG_MASK) == 0 ||
        (payload == NULL && payload_size != 0) ||
        payload_size > UINT16_MAX ||
        capacity < PXA_WIRE_RECORD_SIZE + payload_size) return 0;
    pxa_wire_generated_store_u16(
        out + PXA_WIRE_RECORD_TAG_OFFSET, raw_tag);
    pxa_wire_generated_store_u16(
        out + PXA_WIRE_RECORD_PAYLOAD_LEN_OFFSET,
        (uint16_t)payload_size);
    for (size_t i = 0; i < payload_size; ++i)
        out[PXA_WIRE_RECORD_SIZE + i] = payload[i];
    *written = PXA_WIRE_RECORD_SIZE + payload_size;
    return 1;
}

#endif /* PXA_WIRE_GENERATED_H */

#define PXA_WIRE_SIZE ((size_t)20)
#define PXA_WIRE_SERVICE_OFFSET ((size_t)0)
#define PXA_WIRE_OPCODE_OFFSET ((size_t)2)
#define PXA_WIRE_REQUEST_TOKEN_OFFSET ((size_t)4)
#define PXA_WIRE_PAYLOAD_LEN_OFFSET ((size_t)12)
#define PXA_WIRE_FLAGS_OFFSET ((size_t)16)

typedef struct {
    uint16_t service;
    uint16_t opcode;
    uint64_t request_token;
    const uint8_t *payload;
    uint32_t payload_size;
} pxa_wire_view_t;

static inline int pxa_wire_decode(
    const uint8_t *data, size_t size, size_t max_size,
    pxa_wire_view_t *output) {
    uint32_t payload_size;
    if (output == NULL) return 0;
    output->service = 0;
    output->opcode = 0;
    output->request_token = 0;
    output->payload = NULL;
    output->payload_size = 0;
    if (data == NULL ||
        size < PXA_WIRE_SIZE ||
        size > max_size ||
        size > PXA_WIRE_MAX_CONTROL_MESSAGE) return 0;
    payload_size = pxa_wire_generated_load_u32(
        data + PXA_WIRE_PAYLOAD_LEN_OFFSET);
    if ((size_t)payload_size != size - PXA_WIRE_SIZE) return 0;
    if (pxa_wire_generated_load_u16(data + PXA_WIRE_SERVICE_OFFSET) == 0) return 0;
    if (pxa_wire_generated_load_u16(data + PXA_WIRE_OPCODE_OFFSET) == 0) return 0;
    if (pxa_wire_generated_load_u32(data + PXA_WIRE_FLAGS_OFFSET) != 0) return 0;
    output->service = pxa_wire_generated_load_u16(data + PXA_WIRE_SERVICE_OFFSET);
    output->opcode = pxa_wire_generated_load_u16(data + PXA_WIRE_OPCODE_OFFSET);
    output->request_token = pxa_wire_generated_load_u64(data + PXA_WIRE_REQUEST_TOKEN_OFFSET);
    output->payload = data + PXA_WIRE_SIZE;
    output->payload_size = payload_size;
    return 1;
}

static inline int pxa_wire_encode(
    uint8_t *out, size_t capacity, uint16_t service, uint16_t opcode, uint64_t request_token,
    const uint8_t *payload, size_t payload_size, size_t *written) {
    if (written != NULL) *written = 0;
    if (out == NULL || written == NULL ||
        (payload == NULL && payload_size != 0) ||
        payload_size > PXA_WIRE_MAX_CONTROL_MESSAGE - PXA_WIRE_SIZE ||
        capacity < PXA_WIRE_SIZE + payload_size) return 0;
    if (service == 0) return 0;
    if (opcode == 0) return 0;
    pxa_wire_generated_store_u16(out + PXA_WIRE_SERVICE_OFFSET, service);
    pxa_wire_generated_store_u16(out + PXA_WIRE_OPCODE_OFFSET, opcode);
    pxa_wire_generated_store_u64(out + PXA_WIRE_REQUEST_TOKEN_OFFSET, request_token);
    pxa_wire_generated_store_u32(out + PXA_WIRE_PAYLOAD_LEN_OFFSET, (uint32_t)payload_size);
    pxa_wire_generated_store_u32(out + PXA_WIRE_FLAGS_OFFSET, 0);
    for (size_t i = 0; i < payload_size; ++i)
        out[PXA_WIRE_SIZE + i] = payload[i];
    *written = PXA_WIRE_SIZE + payload_size;
    return 1;
}

#endif
