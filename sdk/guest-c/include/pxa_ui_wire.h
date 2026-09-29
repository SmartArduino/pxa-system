#ifndef PXA_GUEST_UI_WIRE_H
#define PXA_GUEST_UI_WIRE_H

#include "pxa_core.h"

#define PXA_UI_SERVICE 3u
#define PXA_UI_TX_BEGIN 1u
#define PXA_UI_TX_WRITE 2u
#define PXA_UI_TX_COMMIT 3u
#define PXA_UI_TX_CANCEL 4u
#define PXA_UI_SURFACE_OPEN 5u
#define PXA_UI_SURFACE_CLOSE 6u
#define PXA_UI_CANVAS_BEGIN 7u
#define PXA_UI_CANVAS_WRITE 8u
#define PXA_UI_CANVAS_PRESENT 9u
#define PXA_UI_CANVAS_STREAM_OPEN 10u
#define PXA_UI_THEME_GET 11u
#define PXA_UI_EVENT 0x8001u
#define PXA_UI_ENVIRONMENT_CHANGED 0x8002u
#define PXA_UI_RESOURCE_PRESSURE 0x8003u
#define PXA_UI_SURFACE_READY 0x8004u
#define PXA_UI_CANVAS_STREAM_READY 0x8005u
#define PXA_UI_THEME_CHANGED 0x8006u
#define PXA_UI_PRIMARY_SURFACE 1u
#define PXA_UI_THEME_COLORS 10u
#define PXA_UI_THEME_FONTS 6u

typedef struct {
    int32_t status;
    uint32_t generation;
    uint8_t color_scheme;
    uint32_t rgba[PXA_UI_THEME_COLORS];
    uint16_t typography_px[PXA_UI_THEME_FONTS];
} pxa_ui_wire_theme_t;

typedef struct {
    uint32_t request;
    uint64_t handle;
    int32_t status;
} pxa_ui_wire_canvas_stream_t;

typedef struct {
    uint32_t request;
    uint32_t surface;
    int32_t status;
    const uint8_t *environment_records;
    uint32_t environment_size;
} pxa_ui_wire_surface_ready_t;

typedef struct {
    uint32_t surface;
    uint32_t node;
    uint32_t generation;
    uint16_t kind;
    uint16_t flags;
    uint64_t timestamp_us;
    const uint8_t *value;
    uint32_t value_size;
} pxa_ui_wire_input_event_t;

typedef struct {
    const uint8_t *prefix;
    const uint8_t *value;
    size_t prefix_size;
    size_t value_size;
    size_t offset;
    uint32_t transaction;
    uint8_t command;
    uint8_t flags;
} pxa_ui_wire_record_stream_t;

static inline int pxa_ui_wire_build_command(
    uint8_t *packet, size_t capacity, uint16_t opcode,
    const uint8_t *payload, size_t payload_size, uint32_t *written) {
    if ((payload == NULL && payload_size != 0) ||
        (opcode != PXA_UI_TX_BEGIN && opcode != PXA_UI_TX_WRITE &&
         opcode != PXA_UI_TX_COMMIT && opcode != PXA_UI_TX_CANCEL &&
         opcode != PXA_UI_SURFACE_OPEN &&
         opcode != PXA_UI_SURFACE_CLOSE &&
         opcode != PXA_UI_CANVAS_BEGIN &&
         opcode != PXA_UI_CANVAS_WRITE &&
         opcode != PXA_UI_CANVAS_PRESENT &&
         opcode != PXA_UI_CANVAS_STREAM_OPEN)) return 0;
    return pxa_build_message(packet, capacity, PXA_UI_SERVICE,
                                 opcode, 0, payload, payload_size, written);
}

static inline int pxa_ui_wire_build_theme_get(
    uint8_t *packet, size_t capacity, uint64_t token,
    uint32_t *written) {
    if (token == 0) return 0;
    return pxa_build_message(packet, capacity, PXA_UI_SERVICE,
                                 PXA_UI_THEME_GET, token, NULL, 0,
                                 written);
}

static inline int pxa_ui_wire_build_tx_begin(
    uint8_t *packet, size_t capacity, uint32_t surface,
    uint32_t transaction, uint32_t generation, uint32_t target,
    uint8_t kind, uint32_t *written) {
    uint8_t payload[20] = {0};
    if (surface == 0 || transaction == 0 || generation == 0 ||
        kind < 1 || kind > 3 ||
        (kind == 2 && target == 0) ||
        (kind != 2 && target != 0)) return 0;
    pxa_store_u32(payload, surface);
    pxa_store_u32(payload + 4, transaction);
    pxa_store_u32(payload + 8, generation);
    pxa_store_u32(payload + 12, target);
    payload[16] = kind;
    payload[17] = 1; /* preserve the old tree if commit fails */
    return pxa_ui_wire_build_command(packet, capacity, PXA_UI_TX_BEGIN,
                                    payload, sizeof(payload), written);
}

/* A command record has command:u8, flags:u8, length:u16, bytes[length].
 * Copy its fixed prefix and value into one TX_WRITE packet. */
static inline int pxa_ui_wire_build_tx_record(
    uint8_t *packet, size_t capacity, uint32_t transaction,
    uint8_t command, uint8_t flags, const uint8_t *prefix,
    size_t prefix_size, const uint8_t *value, size_t value_size,
    uint32_t *written) {
    uint8_t *out;
    size_t payload_size;
    if (written != NULL) *written = 0;
    if (packet == NULL || written == NULL || transaction == 0 ||
        command == 0 || prefix_size > UINT16_MAX ||
        value_size > UINT16_MAX - prefix_size ||
        (prefix == NULL && prefix_size != 0) ||
        (value == NULL && value_size != 0)) return 0;
    payload_size = 8u + prefix_size + value_size;
    if (capacity < PXA_HEADER_BYTES ||
        payload_size > capacity - PXA_HEADER_BYTES) return 0;
    out = packet + PXA_HEADER_BYTES;
    pxa_store_u32(out, transaction);
    out[4] = command;
    out[5] = flags;
    pxa_store_u16(out + 6, (uint16_t)(prefix_size + value_size));
    for (size_t i = 0; i < prefix_size; ++i) out[8 + i] = prefix[i];
    for (size_t i = 0; i < value_size; ++i)
        out[8 + prefix_size + i] = value[i];
    return pxa_finish_message_in_place(packet, capacity,
                                           PXA_UI_SERVICE,
                                           PXA_UI_TX_WRITE, 0,
                                           PXA_HEADER_BYTES + payload_size,
                                           written);
}

/* A large property can span TX_WRITE packets without allocating or changing
 * the transaction wire format. The caller submits each produced packet, then
 * commits only after `next` returns zero. */
static inline int pxa_ui_wire_record_stream_init(
    pxa_ui_wire_record_stream_t *stream, uint32_t transaction,
    uint8_t command, uint8_t flags, const uint8_t *prefix,
    size_t prefix_size, const uint8_t *value, size_t value_size) {
    if (stream == NULL || transaction == 0 || command == 0 ||
        prefix_size > UINT16_MAX ||
        value_size > UINT16_MAX - prefix_size ||
        (prefix == NULL && prefix_size != 0) ||
        (value == NULL && value_size != 0)) return 0;
    stream->prefix = prefix;
    stream->value = value;
    stream->prefix_size = prefix_size;
    stream->value_size = value_size;
    stream->offset = 0;
    stream->transaction = transaction;
    stream->command = command;
    stream->flags = flags;
    return 1;
}

/* Returns 1 for a packet, 0 when done, -1 for insufficient capacity. */
static inline int pxa_ui_wire_record_stream_next(
    pxa_ui_wire_record_stream_t *stream, uint8_t *packet,
    size_t capacity, uint32_t *written) {
    size_t total;
    size_t chunk;
    uint8_t *out;
    if (written != NULL) *written = 0;
    if (stream == NULL || packet == NULL || written == NULL) return -1;
    total = 4u + stream->prefix_size + stream->value_size;
    if (stream->offset >= total) return 0;
    if (capacity <= PXA_HEADER_BYTES + 4u) return -1;
    chunk = capacity - PXA_HEADER_BYTES - 4u;
    if (chunk > PXA_MAX_CONTROL_BYTES - PXA_HEADER_BYTES - 4u)
        chunk = PXA_MAX_CONTROL_BYTES - PXA_HEADER_BYTES - 4u;
    if (chunk > total - stream->offset) chunk = total - stream->offset;
    out = packet + PXA_HEADER_BYTES;
    pxa_store_u32(out, stream->transaction);
    for (size_t i = 0; i < chunk; ++i) {
        size_t position = stream->offset + i;
        if (position == 0) out[4 + i] = stream->command;
        else if (position == 1) out[4 + i] = stream->flags;
        else if (position == 2)
            out[4 + i] = (uint8_t)(stream->prefix_size + stream->value_size);
        else if (position == 3)
            out[4 + i] = (uint8_t)((stream->prefix_size +
                                     stream->value_size) >> 8);
        else if (position - 4u < stream->prefix_size)
            out[4 + i] = stream->prefix[position - 4u];
        else
            out[4 + i] = stream->value[position - 4u -
                                       stream->prefix_size];
    }
    if (!pxa_finish_message_in_place(
            packet, capacity, PXA_UI_SERVICE, PXA_UI_TX_WRITE,
            0, PXA_HEADER_BYTES + 4u + chunk, written)) return -1;
    stream->offset += chunk;
    return 1;
}

static inline int pxa_ui_wire_build_tx_end(
    uint8_t *packet, size_t capacity, uint32_t transaction,
    int commit, uint32_t *written) {
    uint8_t payload[4];
    if (transaction == 0) return 0;
    pxa_store_u32(payload, transaction);
    return pxa_ui_wire_build_command(packet, capacity,
                                    commit ? PXA_UI_TX_COMMIT
                                           : PXA_UI_TX_CANCEL,
                                    payload, sizeof(payload), written);
}

static inline int pxa_ui_wire_parse_theme(
    const pxa_event_t *event, uint64_t token, pxa_ui_wire_theme_t *out) {
    const uint8_t *bytes;
    if (out == NULL) return 0;
    pxa_zero(out, sizeof(*out));
    if (event == NULL || event->service != PXA_UI_SERVICE ||
        event->opcode != PXA_UI_THEME_GET || event->token != token ||
        token == 0 || event->payload == NULL || event->payload_size < 4)
        return 0;
    bytes = event->payload;
    out->status = (int32_t)pxa_load_u32(bytes);
    if (out->status != 0) return event->payload_size == 4;
    if (event->payload_size != 64 || bytes[8] > 1 ||
        bytes[9] != 0 || bytes[10] != 0 || bytes[11] != 0) return 0;
    out->generation = pxa_load_u32(bytes + 4);
    out->color_scheme = bytes[8];
    for (size_t i = 0; i < PXA_UI_THEME_COLORS; ++i)
        out->rgba[i] = pxa_load_u32(bytes + 12 + 4u * i);
    for (size_t i = 0; i < PXA_UI_THEME_FONTS; ++i)
        out->typography_px[i] = pxa_load_u16(bytes + 52 + 2u * i);
    return out->generation != 0;
}

static inline int pxa_ui_wire_parse_canvas_stream_ready(
    const pxa_event_t *event, pxa_ui_wire_canvas_stream_t *out) {
    if (out == NULL) return 0;
    pxa_zero(out, sizeof(*out));
    if (event == NULL || event->service != PXA_UI_SERVICE ||
        event->opcode != PXA_UI_CANVAS_STREAM_READY || event->token != 0 ||
        event->payload == NULL || event->payload_size != 16) return 0;
    out->request = pxa_load_u32(event->payload);
    out->handle = pxa_load_u64(event->payload + 4);
    out->status = (int32_t)pxa_load_u32(event->payload + 12);
    return out->request != 0 &&
           ((out->status == 0 && out->handle != 0) ||
            (out->status != 0 && out->handle == 0));
}

static inline int32_t pxa_ui_wire_canvas_stream_write(
    uint64_t handle, uint8_t *bytes, uint32_t size) {
    if (handle == 0 || bytes == NULL || size == 0) return -1;
    return pxa_io(handle, 2u, bytes, size);
}

static inline int pxa_ui_wire_parse_surface_ready(
    const pxa_event_t *event, pxa_ui_wire_surface_ready_t *out) {
    if (out == NULL) return 0;
    pxa_zero(out, sizeof(*out));
    if (event == NULL || event->service != PXA_UI_SERVICE ||
        event->opcode != PXA_UI_SURFACE_READY || event->token != 0 ||
        event->payload == NULL || event->payload_size < 12) return 0;
    out->request = pxa_load_u32(event->payload);
    out->surface = pxa_load_u32(event->payload + 4);
    out->status = (int32_t)pxa_load_u32(event->payload + 8);
    out->environment_records = event->payload + 12;
    out->environment_size = event->payload_size - 12;
    return out->request != 0 &&
           ((out->status == 0 && out->surface != 0) ||
            (out->status != 0 && out->surface == 0));
}

static inline int pxa_ui_wire_parse_input_event(
    const pxa_event_t *event, pxa_ui_wire_input_event_t *out) {
    if (out == NULL) return 0;
    pxa_zero(out, sizeof(*out));
    if (event == NULL || event->service != PXA_UI_SERVICE ||
        event->opcode != PXA_UI_EVENT || event->token != 0 ||
        event->payload == NULL || event->payload_size < 24) return 0;
    out->surface = pxa_load_u32(event->payload);
    out->node = pxa_load_u32(event->payload + 4);
    out->generation = pxa_load_u32(event->payload + 8);
    out->kind = pxa_load_u16(event->payload + 12);
    out->flags = pxa_load_u16(event->payload + 14);
    out->timestamp_us = pxa_load_u64(event->payload + 16);
    out->value = event->payload + 24;
    out->value_size = event->payload_size - 24;
    return out->surface != 0 && out->node != 0 && out->generation != 0 &&
           out->kind != 0;
}

#endif
