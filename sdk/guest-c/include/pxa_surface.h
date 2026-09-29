#ifndef PXA_GUEST_SURFACE_H
#define PXA_GUEST_SURFACE_H

#include "pxa_core.h"
#include "pxa_surface_geometry.h"

#define PXA_SURFACE_SERVICE 16u
#define PXA_SURFACE_CREATE 1u
#define PXA_SURFACE_CONFIGURE_LAYER 2u
#define PXA_SURFACE_QUEUE_FRAME 3u
#define PXA_SURFACE_QUERY_STATE 4u
#define PXA_SURFACE_CONFIGURE_OPAQUE_UI_REGIONS 5u
#define PXA_SURFACE_RELEASED 0x8001u
#define PXA_SURFACE_RGB565 1u
#define PXA_SURFACE_ARGB8888_PREMULTIPLIED 2u
#define PXA_SURFACE_PREMULTIPLIED_ALPHA 1u
#define PXA_SURFACE_PREFER_DIRECT_SCANOUT 2u
#define PXA_SURFACE_GUEST_MAPPED 4u
#define PXA_SURFACE_MAX_RECTS 8u
#define PXA_SURFACE_IO_REGISTER_BUFFERS 0x100u
#define PXA_SURFACE_IO_ACQUIRE 0x101u
#define PXA_SURFACE_IO_PRESENT 0x102u
#define PXA_SURFACE_BUFFER_ALIGNMENT 64u

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t format;
    uint8_t buffer_count;
    uint8_t flags;
} pxa_surface_desc_t;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} pxa_surface_rect_t;

typedef struct {
    int32_t status;
    uint64_t handle;
    uint32_t stride_bytes;
    uint32_t frame_bytes;
    uint8_t buffer_count;
} pxa_surface_create_result_t;

typedef struct {
    int32_t status;
    uint64_t submitted_frames;
    uint64_t presented_frames;
    uint64_t dropped_frames;
    uint64_t replaced_frames;
    uint64_t released_frames;
    uint32_t free_buffers;
    uint32_t flags;
} pxa_surface_state_t;

typedef struct {
    uint64_t handle;
    uint8_t buffer_index;
    uint64_t frame_id;
} pxa_surface_released_t;

static inline int pxa_surface_finish(uint8_t *packet, size_t capacity,
                                        uint16_t opcode, uint64_t token,
                                        size_t payload_size,
                                        uint32_t *written) {
    if (packet == NULL || capacity < PXA_HEADER_BYTES ||
        payload_size > capacity - PXA_HEADER_BYTES) return 0;
    return pxa_finish_message_in_place(
        packet, capacity, PXA_SURFACE_SERVICE, opcode, token,
        PXA_HEADER_BYTES + payload_size, written);
}

static inline int pxa_surface_build_create(
    uint8_t *packet, size_t capacity, uint64_t token,
    pxa_surface_desc_t desc, uint32_t *written) {
    uint8_t *out;
    if (written != NULL) *written = 0;
    if (packet == NULL || written == NULL || token == 0 ||
        capacity < PXA_HEADER_BYTES + 8u || desc.width == 0 ||
        desc.height == 0 || desc.buffer_count < 2 ||
        (desc.format == PXA_SURFACE_RGB565 &&
         (desc.flags & ~(PXA_SURFACE_PREFER_DIRECT_SCANOUT |
                         PXA_SURFACE_GUEST_MAPPED)) != 0) ||
        (desc.format == PXA_SURFACE_ARGB8888_PREMULTIPLIED &&
         desc.flags != PXA_SURFACE_PREMULTIPLIED_ALPHA) ||
        (desc.format != PXA_SURFACE_RGB565 &&
         desc.format != PXA_SURFACE_ARGB8888_PREMULTIPLIED)) return 0;
    out = packet + PXA_HEADER_BYTES;
    pxa_store_u16(out, desc.width);
    pxa_store_u16(out + 2, desc.height);
    pxa_store_u16(out + 4, desc.format);
    out[6] = desc.buffer_count;
    out[7] = desc.flags;
    return pxa_surface_finish(packet, capacity, PXA_SURFACE_CREATE,
                                  token, 8, written);
}

static inline int pxa_surface_build_configure_layer(
    uint8_t *packet, size_t capacity, uint64_t token, uint64_t handle,
    int32_t x, int32_t y, uint16_t width, uint16_t height, int16_t z,
    uint8_t visible, uint32_t *written) {
    uint8_t *out;
    if (written != NULL) *written = 0;
    if (packet == NULL || written == NULL || token == 0 || handle == 0 ||
        width == 0 || height == 0 || visible > 1 ||
        capacity < PXA_HEADER_BYTES + 24u) return 0;
    out = packet + PXA_HEADER_BYTES;
    pxa_store_u64(out, handle);
    pxa_store_u32(out + 8, (uint32_t)x);
    pxa_store_u32(out + 12, (uint32_t)y);
    pxa_store_u16(out + 16, width);
    pxa_store_u16(out + 18, height);
    pxa_store_u16(out + 20, (uint16_t)z);
    out[22] = visible;
    out[23] = 0;
    return pxa_surface_finish(packet, capacity,
                                  PXA_SURFACE_CONFIGURE_LAYER,
                                  token, 24, written);
}

static inline int pxa_surface_rects(
    uint8_t *out, uint64_t handle, const pxa_surface_rect_t *rects,
    uint8_t count, size_t rect_offset) {
    if (handle == 0 || count > PXA_SURFACE_MAX_RECTS ||
        (count != 0 && rects == NULL)) return 0;
    pxa_store_u64(out, handle);
    for (uint8_t i = 0; i < count; ++i) {
        uint8_t *record = out + rect_offset + (size_t)i * 8u;
        if (rects[i].width == 0 || rects[i].height == 0) return 0;
        pxa_store_u16(record, rects[i].x);
        pxa_store_u16(record + 2, rects[i].y);
        pxa_store_u16(record + 4, rects[i].width);
        pxa_store_u16(record + 6, rects[i].height);
    }
    return 1;
}

static inline int pxa_surface_build_queue_frame(
    uint8_t *packet, size_t capacity, uint64_t handle, uint64_t frame_id,
    const pxa_surface_rect_t *damage, uint8_t count,
    uint32_t *written) {
    uint8_t *out;
    size_t payload_size = 20u + (size_t)count * 8u;
    if (written != NULL) *written = 0;
    if (packet == NULL || written == NULL || frame_id == 0 ||
        count > PXA_SURFACE_MAX_RECTS ||
        capacity < PXA_HEADER_BYTES + payload_size) return 0;
    out = packet + PXA_HEADER_BYTES;
    if (!pxa_surface_rects(out, handle, damage, count, 20u)) return 0;
    pxa_store_u64(out + 8, frame_id);
    out[16] = count;
    out[17] = out[18] = out[19] = 0;
    /* The queue command is one-way, so its envelope token is zero. */
    return pxa_surface_finish(packet, capacity,
                                  PXA_SURFACE_QUEUE_FRAME, 0,
                                  payload_size, written);
}

static inline int pxa_surface_build_opaque_ui_regions(
    uint8_t *packet, size_t capacity, uint64_t token, uint64_t handle,
    const pxa_surface_rect_t *regions, uint8_t count,
    uint32_t *written) {
    uint8_t *out;
    size_t payload_size = 12u + (size_t)count * 8u;
    if (written != NULL) *written = 0;
    if (packet == NULL || written == NULL || token == 0 ||
        count > PXA_SURFACE_MAX_RECTS ||
        capacity < PXA_HEADER_BYTES + payload_size) return 0;
    out = packet + PXA_HEADER_BYTES;
    if (!pxa_surface_rects(out, handle, regions, count, 12u)) return 0;
    out[8] = count;
    out[9] = out[10] = out[11] = 0;
    return pxa_surface_finish(packet, capacity,
                                  PXA_SURFACE_CONFIGURE_OPAQUE_UI_REGIONS,
                                  token, payload_size, written);
}

static inline int pxa_surface_build_query_state(
    uint8_t *packet, size_t capacity, uint64_t token, uint64_t handle,
    uint32_t *written) {
    if (written != NULL) *written = 0;
    if (packet == NULL || written == NULL || token == 0 || handle == 0 ||
        capacity < PXA_HEADER_BYTES + 8u) return 0;
    pxa_store_u64(packet + PXA_HEADER_BYTES, handle);
    return pxa_surface_finish(packet, capacity,
                                  PXA_SURFACE_QUERY_STATE,
                                  token, 8, written);
}

static inline int32_t pxa_surface_write_frame(
    uint64_t handle, uint8_t *pixels, uint32_t frame_bytes) {
    if (handle == 0 || pixels == NULL || frame_bytes == 0) return -1;
    return pxa_io(handle, 2u, pixels, frame_bytes);
}

static inline int32_t pxa_surface_register_buffers(
    uint64_t handle, void *buffers, uint32_t frame_bytes,
    uint8_t buffer_count) {
    const uint64_t total = (uint64_t)frame_bytes * buffer_count;
    if (handle == 0 || buffers == NULL || frame_bytes == 0 ||
        buffer_count < 2 || total > UINT32_MAX ||
        ((uintptr_t)buffers & (PXA_SURFACE_BUFFER_ALIGNMENT - 1u)) != 0)
        return -1;
    return pxa_io(handle, PXA_SURFACE_IO_REGISTER_BUFFERS,
                     (uint8_t *)buffers, (uint32_t)total);
}

static inline int32_t pxa_surface_acquire_buffer(
    uint64_t handle, uint8_t *buffer_index) {
    uint8_t record[4] = {0};
    int32_t result;
    if (handle == 0 || buffer_index == NULL) return -1;
    result = pxa_io(handle, PXA_SURFACE_IO_ACQUIRE,
                       record, (uint32_t)sizeof(record));
    if (result == (int32_t)sizeof(record)) *buffer_index = record[0];
    return result;
}

static inline int32_t pxa_surface_present_buffer(
    uint64_t handle, uint8_t buffer_index, uint64_t frame_id) {
    uint8_t record[16] = {0};
    if (handle == 0 || frame_id == 0) return -1;
    record[0] = buffer_index;
    pxa_store_u64(record + 8, frame_id);
    return pxa_io(handle, PXA_SURFACE_IO_PRESENT,
                     record, (uint32_t)sizeof(record));
}

static inline int pxa_surface_parse_status(
    const pxa_event_t *event, uint64_t token, uint16_t opcode,
    int32_t *status) {
    if (event == NULL || status == NULL || token == 0 ||
        event->service != PXA_SURFACE_SERVICE || event->opcode != opcode ||
        event->token != token || event->payload == NULL ||
        event->payload_size != 4 ||
        (opcode != PXA_SURFACE_CONFIGURE_LAYER &&
         opcode != PXA_SURFACE_CONFIGURE_OPAQUE_UI_REGIONS)) return 0;
    *status = (int32_t)pxa_load_u32(event->payload);
    return 1;
}

static inline int pxa_surface_parse_create(
    const pxa_event_t *event, uint64_t token,
    pxa_surface_create_result_t *out) {
    const uint8_t *bytes;
    if (out == NULL) return 0;
    pxa_zero(out, sizeof(*out));
    if (event == NULL || event->service != PXA_SURFACE_SERVICE ||
        event->opcode != PXA_SURFACE_CREATE || token == 0 ||
        event->token != token || event->payload == NULL ||
        event->payload_size < 4) return 0;
    bytes = event->payload;
    out->status = (int32_t)pxa_load_u32(bytes);
    if (out->status != 0) return event->payload_size == 4;
    if (event->payload_size != 24 || bytes[21] != 0 ||
        bytes[22] != 0 || bytes[23] != 0) return 0;
    out->handle = pxa_load_u64(bytes + 4);
    out->stride_bytes = pxa_load_u32(bytes + 12);
    out->frame_bytes = pxa_load_u32(bytes + 16);
    out->buffer_count = bytes[20];
    return out->handle != 0 && out->stride_bytes != 0 &&
           out->frame_bytes != 0 && out->buffer_count != 0;
}

static inline int pxa_surface_parse_state(
    const pxa_event_t *event, uint64_t token,
    pxa_surface_state_t *out) {
    const uint8_t *bytes;
    if (out == NULL) return 0;
    pxa_zero(out, sizeof(*out));
    if (event == NULL || event->service != PXA_SURFACE_SERVICE ||
        event->opcode != PXA_SURFACE_QUERY_STATE || token == 0 ||
        event->token != token || event->payload == NULL ||
        event->payload_size < 4) return 0;
    bytes = event->payload;
    out->status = (int32_t)pxa_load_u32(bytes);
    if (out->status != 0) return event->payload_size == 4;
    if (event->payload_size != 52) return 0;
    out->submitted_frames = pxa_load_u64(bytes + 4);
    out->presented_frames = pxa_load_u64(bytes + 12);
    out->dropped_frames = pxa_load_u64(bytes + 20);
    out->replaced_frames = pxa_load_u64(bytes + 28);
    out->released_frames = pxa_load_u64(bytes + 36);
    out->free_buffers = pxa_load_u32(bytes + 44);
    out->flags = pxa_load_u32(bytes + 48);
    return 1;
}

static inline int pxa_surface_parse_released(
    const pxa_event_t *event, pxa_surface_released_t *out) {
    const uint8_t *bytes;
    if (out == NULL) return 0;
    pxa_zero(out, sizeof(*out));
    if (event == NULL || event->service != PXA_SURFACE_SERVICE ||
        event->opcode != PXA_SURFACE_RELEASED || event->token != 0 ||
        event->payload == NULL || event->payload_size != 20) return 0;
    bytes = event->payload;
    if (bytes[9] != 0 || bytes[10] != 0 || bytes[11] != 0) return 0;
    out->handle = pxa_load_u64(bytes);
    out->buffer_index = bytes[8];
    out->frame_id = pxa_load_u64(bytes + 12);
    return out->handle != 0 && out->frame_id != 0;
}

#endif
