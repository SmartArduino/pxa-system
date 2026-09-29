#ifndef PXA_GUEST_WINDOW_H
#define PXA_GUEST_WINDOW_H

#include "pxa_core.h"
#include "pxa_window_snapshot_generated.h"

#define PXA_WINDOW_SERVICE 2u
#define PXA_WINDOW_CONFIGURE 1u
#define PXA_WINDOW_GET_SNAPSHOT 2u
#define PXA_WINDOW_SHOW_TOAST 3u
#define PXA_WINDOW_METRICS_CHANGED 0x8001u
#define PXA_WINDOW_BACK_REQUESTED 0x8002u
#define PXA_WINDOW_MAX_CONFIG_PACKET (PXA_HEADER_BYTES + 41u)
#define PXA_WINDOW_MAX_TOAST_BYTES 240u
#define PXA_WINDOW_MAX_TOAST_PACKET \
    (PXA_HEADER_BYTES + 2u + PXA_WINDOW_MAX_TOAST_BYTES)

typedef struct {
    uint8_t edge_to_edge;
    uint8_t status_bar_mode;
    uint8_t navigation_bar_mode;
    uint8_t status_bar_icons;
    uint8_t navigation_bar_icons;
    uint32_t status_bar_color;
    uint32_t navigation_bar_color;
} pxa_window_config_t;

typedef pxa_window_wire_insets_t pxa_window_insets_t;
typedef pxa_window_snapshot_wire_t pxa_window_snapshot_t;

static inline int pxa_window_build_configure(
    uint8_t *out, size_t capacity, const pxa_window_config_t *config,
    uint32_t *written) {
    uint8_t *payload;
    size_t offset = 0;
    uint8_t values[5];
    if (written != NULL) *written = 0;
    if (out == NULL || written == NULL || config == NULL ||
        capacity < PXA_WINDOW_MAX_CONFIG_PACKET ||
        config->edge_to_edge > 1 || config->status_bar_mode > 2 ||
        config->navigation_bar_mode > 2 ||
        config->status_bar_icons > 2 ||
        config->navigation_bar_icons > 2) return 0;
    payload = out + PXA_HEADER_BYTES;
    values[0] = config->edge_to_edge;
    values[1] = config->status_bar_mode;
    values[2] = config->navigation_bar_mode;
    values[3] = config->status_bar_icons;
    values[4] = config->navigation_bar_icons;
    for (uint16_t tag = 1; tag <= 7; ++tag) {
        pxa_store_u16(payload + offset, tag);
        if (tag <= 5) {
            pxa_store_u16(payload + offset + 2, 1);
            payload[offset + 4] = values[tag - 1];
            offset += 5;
        } else {
            pxa_store_u16(payload + offset + 2, 4);
            pxa_store_u32(payload + offset + 4,
                             tag == 6 ? config->status_bar_color
                                      : config->navigation_bar_color);
            offset += 8;
        }
    }
    return pxa_build_message(out, capacity, PXA_WINDOW_SERVICE,
                                PXA_WINDOW_CONFIGURE, 0, payload, offset,
                                written);
}

static inline int32_t pxa_window_configure(
    const pxa_window_config_t *config) {
    uint8_t packet[PXA_WINDOW_MAX_CONFIG_PACKET];
    uint32_t size = 0;
    if (!pxa_window_build_configure(packet, sizeof(packet), config,
                                       &size)) return -1;
    return pxa_submit(packet, size);
}

static inline int32_t pxa_window_fullscreen(void) {
    uint8_t packet[PXA_HEADER_BYTES + 15u];
    uint8_t *payload = packet + PXA_HEADER_BYTES;
    uint32_t size = 0;
    for (uint16_t tag = 1; tag <= 3; ++tag) {
        const size_t offset = (size_t)(tag - 1u) * 5u;
        pxa_store_u16(payload + offset, tag);
        pxa_store_u16(payload + offset + 2u, 1);
        payload[offset + 4u] = tag == 1 ? 1u : 2u;
    }
    if (!pxa_finish_message_in_place(
            packet, sizeof(packet), PXA_WINDOW_SERVICE,
            PXA_WINDOW_CONFIGURE, 0, sizeof(packet), &size)) return -1;
    return pxa_submit(packet, size);
}

static inline int pxa_window_build_toast(
    uint8_t *out, size_t capacity, uint16_t duration_ms,
    const char *text, size_t length, uint32_t *written) {
    uint8_t *payload;
    if (written != NULL) *written = 0;
    if (out == NULL || written == NULL || text == NULL || length == 0 ||
        length > PXA_WINDOW_MAX_TOAST_BYTES ||
        duration_ms < 500 || duration_ms > 5000 ||
        capacity < PXA_HEADER_BYTES + 2u + length) return 0;
    for (size_t i = 0; i < length; ++i)
        if (text[i] == '\0') return 0;
    payload = out + PXA_HEADER_BYTES;
    pxa_store_u16(payload, duration_ms);
    for (size_t i = 0; i < length; ++i)
        payload[2 + i] = (uint8_t)text[i];
    return pxa_build_message(out, capacity, PXA_WINDOW_SERVICE,
                                PXA_WINDOW_SHOW_TOAST, 0, payload,
                                2u + length, written);
}

static inline int32_t pxa_window_show_toast(
    uint16_t duration_ms, const char *text, size_t length) {
    uint8_t packet[PXA_WINDOW_MAX_TOAST_PACKET];
    uint32_t size = 0;
    if (!pxa_window_build_toast(packet, sizeof(packet), duration_ms,
                                   text, length, &size)) return -1;
    return pxa_submit(packet, size);
}

static inline int pxa_window_build_snapshot(
    uint8_t *out, size_t capacity, uint64_t token, uint32_t *written) {
    return token != 0 &&
           pxa_build_message(out, capacity, PXA_WINDOW_SERVICE,
                                PXA_WINDOW_GET_SNAPSHOT, token,
                                NULL, 0, written);
}

static inline int32_t pxa_window_request_snapshot(uint64_t token) {
    uint8_t packet[PXA_HEADER_BYTES];
    uint32_t size = 0;
    if (!pxa_window_build_snapshot(packet, sizeof(packet), token, &size))
        return -1;
    return pxa_submit(packet, size);
}

static inline int pxa_window_parse_snapshot(
    const pxa_event_t *event, uint64_t expected_token,
    pxa_window_snapshot_t *output) {
    if (output == NULL) return 0;
    pxa_zero(output, sizeof(*output));
    if (event == NULL || event->service != PXA_WINDOW_SERVICE ||
        event->opcode != PXA_WINDOW_GET_SNAPSHOT ||
        expected_token == 0 || event->token != expected_token ||
        event->payload == NULL || event->payload_size < 4)
        return 0;
    return pxa_window_snapshot_result_decode(event->payload,
                                              event->payload_size, output);
}

static inline int pxa_window_parse_metrics_changed(
    const pxa_event_t *event, pxa_window_snapshot_t *output) {
    if (output == NULL) return 0;
    pxa_zero(output, sizeof(*output));
    return event != NULL && event->service == PXA_WINDOW_SERVICE &&
           event->opcode == PXA_WINDOW_METRICS_CHANGED &&
           event->token == 0 && event->payload != NULL &&
           pxa_window_snapshot_records_decode(event->payload,
                                               event->payload_size, output);
}

static inline int pxa_window_is_back_requested(
    const pxa_event_t *event) {
    return event != NULL && event->service == PXA_WINDOW_SERVICE &&
           event->opcode == PXA_WINDOW_BACK_REQUESTED &&
           event->token == 0 && event->payload_size == 0;
}

#endif
