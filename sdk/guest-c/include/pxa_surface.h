#ifndef PXA_SURFACE_GUEST_H
#define PXA_SURFACE_GUEST_H

#include "pxa.h"

#define PXA_SURFACE_CREATE 1u
#define PXA_SURFACE_CONFIGURE_LAYER 2u
#define PXA_SURFACE_QUEUE_FRAME 3u
#define PXA_SURFACE_QUERY_STATE 4u
#define PXA_SURFACE_CONFIGURE_OPAQUE_UI_REGIONS 5u
#define PXA_SURFACE_FORMAT_RGB565 1u
#define PXA_SURFACE_FORMAT_ARGB8888_PREMULTIPLIED 2u
#define PXA_SURFACE_FLAG_PREMULTIPLIED_ALPHA 1u
#define PXA_SURFACE_MAX_DAMAGE_RECTS 8u
#define PXA_SURFACE_MAX_OPAQUE_UI_REGIONS 8u
#define PXA_SURFACE_STATE_FLAG_SUPPORTS_OPAQUE_UI_REGIONS UINT32_C(1)
#define PXA_SURFACE_STATE_FLAG_SUPPORTS_ALPHA_COMPOSITING UINT32_C(2)
#define PXA_SURFACE_STATE_FLAG_UI_ALPHA_PLANE_ACTIVE UINT32_C(4)

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} pxa_surface_damage_rect_t;

typedef struct {
    int32_t status;
    uint32_t surface_handle;
    uint32_t stride_bytes;
    uint32_t frame_bytes;
    uint8_t buffer_count;
} pxa_surface_create_result_t;

typedef struct {
    int32_t status;
    uint64_t submitted_frames;
    uint64_t presented_frames;
    uint64_t dropped_frames;
    uint32_t free_buffers;
    uint32_t flags;
} pxa_surface_state_result_t;

static inline void pxa_surface_store_u16(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static inline void pxa_surface_store_u32(uint8_t *out, uint32_t value) {
    for (uint8_t index = 0; index < 4; ++index)
        out[index] = (uint8_t)(value >> (index * 8u));
}

static inline void pxa_surface_store_u64(uint8_t *out, uint64_t value) {
    for (uint8_t index = 0; index < 8; ++index)
        out[index] = (uint8_t)(value >> (index * 8u));
}

static inline int pxa_surface_create(
    uint32_t request_id, uint16_t width, uint16_t height, uint16_t format,
    uint8_t flags, uint8_t buffer_count, uint8_t *packet,
    size_t packet_capacity) {
    uint8_t payload[8];
    pxa_writer_t writer;
    if (request_id == 0 || width == 0 || height == 0 || buffer_count < 2 ||
        packet == NULL ||
        (format != PXA_SURFACE_FORMAT_RGB565 &&
         format != PXA_SURFACE_FORMAT_ARGB8888_PREMULTIPLIED) ||
        (format == PXA_SURFACE_FORMAT_RGB565 && flags != 0) ||
        (format == PXA_SURFACE_FORMAT_ARGB8888_PREMULTIPLIED &&
         flags != PXA_SURFACE_FLAG_PREMULTIPLIED_ALPHA))
        return 0;
    pxa_surface_store_u16(payload, width);
    pxa_surface_store_u16(payload + 2, height);
    pxa_surface_store_u16(payload + 4, format);
    payload[6] = buffer_count;
    payload[7] = flags;
    pxa_writer_init(&writer, packet, packet_capacity);
    return pxa_message(&writer, PXA_SERVICE_SURFACE,
                       PXA_SURFACE_CREATE, request_id, payload,
                       sizeof(payload)) &&
           pxa_control(writer.data, (uint32_t)writer.length) == PXA_STATUS_OK;
}

static inline int pxa_surface_create_rgb565(
    uint32_t request_id, uint16_t width, uint16_t height,
    uint8_t buffer_count, uint8_t *packet, size_t packet_capacity) {
    return pxa_surface_create(
        request_id, width, height, PXA_SURFACE_FORMAT_RGB565, 0,
        buffer_count, packet, packet_capacity);
}

/* Pixels are native-endian 0xAARRGGBB words whose RGB channels have already
 * been multiplied by alpha. This avoids divisions in the Host compositor. */
static inline int pxa_surface_create_argb8888_premultiplied(
    uint32_t request_id, uint16_t width, uint16_t height,
    uint8_t buffer_count, uint8_t *packet, size_t packet_capacity) {
    return pxa_surface_create(
        request_id, width, height, PXA_SURFACE_FORMAT_ARGB8888_PREMULTIPLIED,
        PXA_SURFACE_FLAG_PREMULTIPLIED_ALPHA, buffer_count, packet,
        packet_capacity);
}

static inline int pxa_surface_configure_layer(
    uint32_t request_id, uint32_t surface_handle, int32_t x, int32_t y,
    uint16_t width, uint16_t height, int16_t z, uint8_t visible,
    uint8_t *packet, size_t packet_capacity) {
    uint8_t payload[20];
    pxa_writer_t writer;
    if (request_id == 0 || surface_handle == 0 || width == 0 || height == 0 ||
        visible > 1 || packet == NULL)
        return 0;
    pxa_surface_store_u32(payload, surface_handle);
    pxa_surface_store_u32(payload + 4, (uint32_t)x);
    pxa_surface_store_u32(payload + 8, (uint32_t)y);
    pxa_surface_store_u16(payload + 12, width);
    pxa_surface_store_u16(payload + 14, height);
    pxa_surface_store_u16(payload + 16, (uint16_t)z);
    payload[18] = visible;
    payload[19] = 0;
    pxa_writer_init(&writer, packet, packet_capacity);
    return pxa_message(&writer, PXA_SERVICE_SURFACE,
                       PXA_SURFACE_CONFIGURE_LAYER, request_id, payload,
                       sizeof(payload)) &&
           pxa_control(writer.data, (uint32_t)writer.length) == PXA_STATUS_OK;
}

/* Regions are relative to the Surface and must be fully covered by opaque
 * LVGL content. They are intentionally configured out of the frame loop. */
static inline int pxa_surface_configure_opaque_ui_regions(
    uint32_t request_id, uint32_t surface_handle,
    const pxa_surface_damage_rect_t *regions, uint8_t region_count,
    uint8_t *packet, size_t packet_capacity) {
    uint8_t payload[8 + PXA_SURFACE_MAX_OPAQUE_UI_REGIONS * 8];
    pxa_writer_t writer;
    if (request_id == 0 || surface_handle == 0 ||
        region_count > PXA_SURFACE_MAX_OPAQUE_UI_REGIONS ||
        (region_count != 0 && regions == NULL) || packet == NULL)
        return 0;
    pxa_surface_store_u32(payload, surface_handle);
    payload[4] = region_count;
    payload[5] = payload[6] = payload[7] = 0;
    for (uint8_t index = 0; index < region_count; ++index) {
        uint8_t *record = payload + 8u + (size_t)index * 8u;
        if (regions[index].width == 0 || regions[index].height == 0) return 0;
        pxa_surface_store_u16(record, regions[index].x);
        pxa_surface_store_u16(record + 2, regions[index].y);
        pxa_surface_store_u16(record + 4, regions[index].width);
        pxa_surface_store_u16(record + 6, regions[index].height);
    }
    pxa_writer_init(&writer, packet, packet_capacity);
    return pxa_message(&writer, PXA_SERVICE_SURFACE,
                       PXA_SURFACE_CONFIGURE_OPAQUE_UI_REGIONS, request_id, payload,
                       8u + (size_t)region_count * 8u) &&
           pxa_control(writer.data, (uint32_t)writer.length) == PXA_STATUS_OK;
}

static inline int32_t pxa_surface_write_frame(uint32_t surface_handle,
                                                uint8_t *pixels,
                                                uint32_t frame_bytes) {
    if (surface_handle == 0 || pixels == NULL || frame_bytes == 0)
        return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_io(surface_handle, PXA_IO_WRITE, pixels, frame_bytes);
}

static inline int32_t pxa_surface_queue_frame(
    uint32_t surface_handle, uint64_t frame_id,
    const pxa_surface_damage_rect_t *damage, uint8_t damage_count,
    uint8_t *packet, size_t packet_capacity) {
    uint8_t payload[16 + PXA_SURFACE_MAX_DAMAGE_RECTS * 8];
    pxa_writer_t writer;
    if (surface_handle == 0 || frame_id == 0 ||
        damage_count > PXA_SURFACE_MAX_DAMAGE_RECTS ||
        (damage_count != 0 && damage == NULL) || packet == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    pxa_surface_store_u32(payload, surface_handle);
    pxa_surface_store_u64(payload + 4, frame_id);
    payload[12] = damage_count;
    payload[13] = payload[14] = payload[15] = 0;
    for (uint8_t index = 0; index < damage_count; ++index) {
        uint8_t *rect = payload + 16 + (size_t)index * 8u;
        if (damage[index].width == 0 || damage[index].height == 0)
            return PXA_STATUS_INVALID_ARGUMENT;
        pxa_surface_store_u16(rect, damage[index].x);
        pxa_surface_store_u16(rect + 2, damage[index].y);
        pxa_surface_store_u16(rect + 4, damage[index].width);
        pxa_surface_store_u16(rect + 6, damage[index].height);
    }
    pxa_writer_init(&writer, packet, packet_capacity);
    if (!pxa_message(&writer, PXA_SERVICE_SURFACE,
                     PXA_SURFACE_QUEUE_FRAME, 0, payload,
                     16u + (size_t)damage_count * 8u))
        return PXA_STATUS_LIMIT_EXCEEDED;
    return pxa_control(writer.data, (uint32_t)writer.length);
}

static inline int pxa_surface_query_state(uint32_t request_id,
                                            uint32_t surface_handle,
                                            uint8_t *packet,
                                            size_t packet_capacity) {
    uint8_t payload[4];
    pxa_writer_t writer;
    if (request_id == 0 || surface_handle == 0 || packet == NULL) return 0;
    pxa_surface_store_u32(payload, surface_handle);
    pxa_writer_init(&writer, packet, packet_capacity);
    return pxa_message(&writer, PXA_SERVICE_SURFACE,
                       PXA_SURFACE_QUERY_STATE, request_id, payload,
                       sizeof(payload)) &&
           pxa_control(writer.data, (uint32_t)writer.length) == PXA_STATUS_OK;
}

static inline int pxa_surface_parse_create(
    const pxa_event_t *event, pxa_surface_create_result_t *output) {
    if (event == NULL || output == NULL ||
        event->service != PXA_SERVICE_SURFACE ||
        event->opcode != PXA_SURFACE_CREATE ||
        event->request_id == 0 || event->payload_length < 4)
        return 0;
    output->status = (int32_t)pxa_read_u32(event->payload);
    if (output->status != PXA_STATUS_OK) return event->payload_length == 4;
    if (event->payload_length != 20) return 0;
    output->surface_handle = pxa_read_u32(event->payload + 4);
    output->stride_bytes = pxa_read_u32(event->payload + 8);
    output->frame_bytes = pxa_read_u32(event->payload + 12);
    output->buffer_count = event->payload[16];
    return output->surface_handle != 0 && output->stride_bytes != 0 &&
           output->frame_bytes != 0 && output->buffer_count >= 2;
}

static inline int pxa_surface_parse_state(
    const pxa_event_t *event, pxa_surface_state_result_t *output) {
    if (event == NULL || output == NULL ||
        event->service != PXA_SERVICE_SURFACE ||
        event->opcode != PXA_SURFACE_QUERY_STATE ||
        event->request_id == 0 || event->payload_length < 4)
        return 0;
    output->status = (int32_t)pxa_read_u32(event->payload);
    if (output->status != PXA_STATUS_OK) return event->payload_length == 4;
    if (event->payload_length != 36) return 0;
    output->submitted_frames = pxa_read_u64(event->payload + 4);
    output->presented_frames = pxa_read_u64(event->payload + 12);
    output->dropped_frames = pxa_read_u64(event->payload + 20);
    output->free_buffers = pxa_read_u32(event->payload + 28);
    output->flags = pxa_read_u32(event->payload + 32);
    return 1;
}

#endif
