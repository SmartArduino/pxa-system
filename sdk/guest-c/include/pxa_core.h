#ifndef PXA_GUEST_CORE_H
#define PXA_GUEST_CORE_H

#include <stddef.h>
#include <stdint.h>

#include "pxa_common.h"
#include "pxa_wire.h"
#include "pxa_log_levels.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Core control transport: cancellation, Log, Device info/MAC, Window
 * configure/snapshot/toast, Permission check/acquire, Storage, private FS,
 * IPC call/reply, Net fetch/HTTP, Audio media, Sensor, Lease, Work, Surface
 * Clock, UI, GameRender, Assets and the privileged Store Installer,
 * I/O and close. Other services remain
 * gated. */
#define PXA_HEADER_BYTES PXA_WIRE_SIZE
#define PXA_MAX_CONTROL_BYTES PXA_WIRE_MAX_CONTROL_MESSAGE
#define PXA_CORE_SERVICE 1u
#ifndef PXA_CORE_CANCEL_REQUEST
#define PXA_CORE_CANCEL_REQUEST 1u
#define PXA_CORE_CLOSE_HANDLE 2u
#endif
#define PXA_LOG_SERVICE 19u
#define PXA_LOG_WRITE 1u
#define PXA_LOG_MAX_MESSAGE_BYTES 256u

typedef struct {
    uint16_t service;
    uint16_t opcode;
    uint64_t token;
    const uint8_t *payload;
    uint32_t payload_size;
} pxa_event_t;

/* Caller-owned token sequence. A zero-initialized cursor first yields 1.
 * Keep one cursor per Component; use a distinct range if another subsystem
 * generates request tokens. A pending table detects a live-token collision. */
static inline uint64_t pxa_next_token(uint64_t *cursor) {
    if (cursor == NULL) return 0;
    *cursor = *cursor == UINT64_MAX ? 1u : *cursor + 1u;
    return *cursor;
}

static inline void pxa_zero(void *pointer, size_t size) {
    uint8_t *bytes = (uint8_t *)pointer;
    for (size_t i = 0; i < size; ++i) bytes[i] = 0;
}

__attribute__((import_module("pxa.core.v1"), import_name("pxa_submit")))
int32_t pxa_submit(const uint8_t *data, uint32_t length);

__attribute__((import_module("pxa.core.v1"), import_name("pxa_io")))
int32_t pxa_io(uint64_t handle, uint32_t operation, uint8_t *data,
                  uint32_t length);

#ifdef __cplusplus
}
#endif

static inline void pxa_store_u16(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static inline void pxa_store_u32(uint8_t *out, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) out[i] = (uint8_t)(value >> (8u * i));
}

static inline void pxa_store_u64(uint8_t *out, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) out[i] = (uint8_t)(value >> (8u * i));
}

static inline uint16_t pxa_load_u16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static inline uint32_t pxa_load_u32(const uint8_t *bytes) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= (uint32_t)bytes[i] << (8u * i);
    return value;
}

static inline uint64_t pxa_load_u64(const uint8_t *bytes) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= (uint64_t)bytes[i] << (8u * i);
    return value;
}

/* The returned payload view is valid only while the callback owns bytes. */
static inline int pxa_parse_event(const uint8_t *bytes, size_t size,
                                      pxa_event_t *event) {
    pxa_wire_view_t decoded;
    if (event == NULL) return 0;
    pxa_zero(event, sizeof(*event));
    if (!pxa_wire_decode(bytes, size, PXA_MAX_CONTROL_BYTES, &decoded))
        return 0;
    event->service = decoded.service;
    event->opcode = decoded.opcode;
    event->token = decoded.request_token;
    event->payload = decoded.payload;
    event->payload_size = decoded.payload_size;
    return 1;
}

static inline int pxa_finish_message_in_place(
    uint8_t *out, size_t capacity, uint16_t service, uint16_t opcode,
    uint64_t token, size_t total_size, uint32_t *encoded_size);

static inline int pxa_build_message(
    uint8_t *out, size_t capacity, uint16_t service, uint16_t opcode,
    uint64_t token, const uint8_t *payload, size_t payload_size,
    uint32_t *encoded_size) {
    size_t written = 0;
    if (encoded_size != NULL) *encoded_size = 0;
    if (out != NULL && capacity >= PXA_HEADER_BYTES &&
        payload == out + PXA_HEADER_BYTES &&
        payload_size <= PXA_MAX_CONTROL_BYTES - PXA_HEADER_BYTES)
        return pxa_finish_message_in_place(
            out, capacity, service, opcode, token,
            PXA_HEADER_BYTES + payload_size, encoded_size);
    if (encoded_size == NULL ||
        !pxa_wire_encode(out, capacity, service, opcode, token,
                            payload, payload_size, &written))
        return 0;
    *encoded_size = (uint32_t)written;
    return 1;
}

/* Finish an envelope whose payload was written directly after the header.
 * This avoids copying a large request payload over itself. */
static inline int pxa_finish_message_in_place(
    uint8_t *out, size_t capacity, uint16_t service, uint16_t opcode,
    uint64_t token, size_t total_size, uint32_t *encoded_size) {
    if (encoded_size != NULL) *encoded_size = 0;
    if (out == NULL || encoded_size == NULL || service == 0 || opcode == 0 ||
        total_size < PXA_HEADER_BYTES || total_size > capacity ||
        total_size > PXA_MAX_CONTROL_BYTES) return 0;
    pxa_store_u16(out + PXA_WIRE_SERVICE_OFFSET, service);
    pxa_store_u16(out + PXA_WIRE_OPCODE_OFFSET, opcode);
    pxa_store_u64(out + PXA_WIRE_REQUEST_TOKEN_OFFSET, token);
    pxa_store_u32(out + PXA_WIRE_PAYLOAD_LEN_OFFSET,
                     (uint32_t)(total_size - PXA_HEADER_BYTES));
    pxa_store_u32(out + PXA_WIRE_FLAGS_OFFSET, 0);
    *encoded_size = (uint32_t)total_size;
    return 1;
}

static inline int pxa_build_cancel(
    uint8_t *out, size_t capacity, uint64_t target_token,
    uint32_t *encoded_size) {
    uint8_t payload[8];
    if (encoded_size != NULL) *encoded_size = 0;
    if (target_token == 0) return 0;
    pxa_store_u64(payload, target_token);
    return pxa_build_message(out, capacity, PXA_CORE_SERVICE,
                                PXA_CORE_CANCEL_REQUEST, 0, payload,
                                sizeof(payload), encoded_size);
}

static inline int32_t pxa_cancel(uint64_t target_token) {
    uint8_t packet[PXA_HEADER_BYTES + 8u];
    uint32_t size = 0;
    if (!pxa_build_cancel(packet, sizeof(packet), target_token, &size))
        return -1;
    return pxa_submit(packet, size);
}

static inline int32_t pxa_close_handle(uint64_t handle) {
    uint8_t packet[PXA_HEADER_BYTES + 8u];
    uint8_t payload[8];
    uint32_t size = 0;
    if (handle == 0) return -1;
    pxa_store_u64(payload, handle);
    if (!pxa_build_message(packet, sizeof(packet), PXA_CORE_SERVICE,
                              PXA_CORE_CLOSE_HANDLE, 0, payload,
                              sizeof(payload), &size))
        return -1;
    return pxa_submit(packet, size);
}

static inline int32_t pxa_log_write(uint8_t level,
                                        const char *message) {
    uint8_t packet[PXA_HEADER_BYTES + 1u + PXA_LOG_MAX_MESSAGE_BYTES];
    size_t length = 0;
    if (message == NULL || level > 4u) return -1;
    while (length <= PXA_LOG_MAX_MESSAGE_BYTES && message[length] != '\0')
        ++length;
    if (length == 0 || length > PXA_LOG_MAX_MESSAGE_BYTES) return -1;
    pxa_store_u16(packet + PXA_WIRE_SERVICE_OFFSET,
                     PXA_LOG_SERVICE);
    pxa_store_u16(packet + PXA_WIRE_OPCODE_OFFSET, PXA_LOG_WRITE);
    pxa_store_u64(packet + PXA_WIRE_REQUEST_TOKEN_OFFSET, 0);
    pxa_store_u32(packet + PXA_WIRE_PAYLOAD_LEN_OFFSET,
                     (uint32_t)length + 1u);
    pxa_store_u32(packet + PXA_WIRE_FLAGS_OFFSET, 0);
    packet[PXA_HEADER_BYTES] = level;
    for (size_t i = 0; i < length; ++i)
        packet[PXA_HEADER_BYTES + 1u + i] = (uint8_t)message[i];
    return pxa_submit(packet,
                         (uint32_t)(PXA_HEADER_BYTES + 1u + length));
}

#endif
