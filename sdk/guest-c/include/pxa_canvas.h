#ifndef PXA_CANVAS_H
#define PXA_CANVAS_H

#include "pxa_ui.h"

#define PXA_CANVAS_DRAW_RECT 1u
#define PXA_CANVAS_DRAW_ELLIPSE 2u
#define PXA_CANVAS_DRAW_LINE 3u
#define PXA_CANVAS_DRAW_ARC 4u
#define PXA_CANVAS_DRAW_TEXT 5u
#define PXA_CANVAS_DRAW_IMAGE 6u
#define PXA_CANVAS_CLIP_PUSH 7u
#define PXA_CANVAS_CLIP_POP 8u
#define PXA_CANVAS_DRAW_TEXT_BOX 9u
#define PXA_CANVAS_DRAW_BITMAP_RGB565 10u

/* Use the full control-message budget when streaming large display lists.
 * Smaller buffers remain valid but require more WASM-to-Host calls. */
#define PXA_CANVAS_STREAM_PACKET_BYTES 4096u

#define PXA_CANVAS_ALIGN_LEFT 0u
#define PXA_CANVAS_ALIGN_CENTER 1u
#define PXA_CANVAS_ALIGN_RIGHT 2u

#define PXA_CANVAS_TEXT_ALIGN_TOP 0u
#define PXA_CANVAS_TEXT_ALIGN_MIDDLE 1u
#define PXA_CANVAS_TEXT_ALIGN_BOTTOM 2u

#define PXA_CANVAS_FONT_CAPTION 0u
#define PXA_CANVAS_FONT_BODY 1u
#define PXA_CANVAS_FONT_TITLE 2u
#define PXA_CANVAS_FONT_ICON 3u

#define PXA_CANVAS_ICON_PLAY "\xef\x81\x8b"
#define PXA_CANVAS_ICON_PAUSE "\xef\x81\x8c"

typedef pxa_writer_t pxa_canvas_frame_t;
typedef pxa_event_t pxa_canvas_event_t;

/* The returned handle is node-instance scoped and must be closed when the
 * Canvas is no longer used. A removed and recreated node needs a new stream. */
static inline int pxa_canvas_stream_open(uint32_t request, uint32_t node) {
    uint8_t payload[12];
    uint8_t packet[24];
    if (request == 0 || node == 0) return 0;
    pxa_ui_write_u32(payload, request);
    pxa_ui_write_u32(payload + 4, PXA_UI_PRIMARY_SURFACE);
    pxa_ui_write_u32(payload + 8, node);
    return pxa_ui_send_packet(packet, sizeof(packet),
                              PXA_UI_CANVAS_STREAM_OPEN, payload,
                              sizeof(payload));
}

static inline int pxa_canvas_parse_stream_ready(
    const pxa_event_t* event, uint32_t* request, uint32_t* handle,
    int32_t* status) {
    if (event == NULL || request == NULL || handle == NULL || status == NULL ||
        event->service != PXA_SERVICE_UI ||
        event->opcode != PXA_UI_CANVAS_STREAM_READY ||
        event->payload == NULL || event->payload_length != 12)
        return 0;
    *request = pxa_read_u32(event->payload);
    *handle = pxa_read_u32(event->payload + 4);
    *status = (int32_t)pxa_read_u32(event->payload + 8);
    return (*status == PXA_STATUS_OK && *handle != 0) ||
           (*status != PXA_STATUS_OK && *handle == 0);
}

static inline uint32_t pxa_canvas_rgba(uint32_t color) {
    return color <= UINT32_C(0xffffff) ? (color << 8) | UINT32_C(0xff)
                                      : color;
}

static inline void pxa_canvas_begin(pxa_canvas_frame_t* frame,
                                    uint8_t* buffer, size_t capacity) {
    pxa_writer_init(frame, buffer, capacity);
}

static inline int pxa_canvas_can_append(pxa_canvas_frame_t* frame,
                                        size_t size) {
    if (frame == NULL || frame->failed || frame->data == NULL ||
        frame->length > frame->capacity || size > frame->capacity - frame->length) {
        if (frame != NULL) frame->failed = 1;
        return 0;
    }
    return 1;
}

static inline int pxa_canvas_primitive(pxa_canvas_frame_t* frame,
                                       uint8_t type, uint16_t payload_size) {
    return pxa_canvas_can_append(frame, (size_t)payload_size + 4u) &&
           pxa_put_u8(frame, type) && pxa_put_u8(frame, 0) &&
           pxa_put_u16(frame, payload_size);
}

static inline int pxa_canvas_rect_rgba(
    pxa_canvas_frame_t* frame, int32_t x, int32_t y, uint32_t width,
    uint32_t height, uint32_t fill_rgba, uint16_t radius,
    uint16_t border_width, uint32_t border_rgba) {
    int ok;
    if (width == 0 || height == 0 ||
        !pxa_canvas_primitive(frame, PXA_CANVAS_DRAW_RECT, 28)) return 0;
    ok = pxa_put_u32(frame, (uint32_t)x) &&
         pxa_put_u32(frame, (uint32_t)y) && pxa_put_u32(frame, width) &&
         pxa_put_u32(frame, height) && pxa_put_u32(frame, fill_rgba) &&
         pxa_put_u16(frame, radius) && pxa_put_u16(frame, border_width) &&
         pxa_put_u32(frame, border_rgba);
    if (ok) ++frame->records;
    return ok;
}

static inline int pxa_canvas_rect(pxa_canvas_frame_t* frame, int16_t x,
                                  int16_t y, uint16_t width, uint16_t height,
                                  uint32_t color, uint8_t radius) {
    return pxa_canvas_rect_rgba(frame, x, y, width, height,
                                pxa_canvas_rgba(color), radius, 0, 0);
}

static inline int pxa_canvas_ellipse_rgba(
    pxa_canvas_frame_t* frame, int32_t x, int32_t y, uint32_t width,
    uint32_t height, uint32_t fill_rgba, uint16_t border_width,
    uint32_t border_rgba) {
    int ok;
    if (width == 0 || height == 0 ||
        !pxa_canvas_primitive(frame, PXA_CANVAS_DRAW_ELLIPSE, 26)) return 0;
    ok = pxa_put_u32(frame, (uint32_t)x) &&
         pxa_put_u32(frame, (uint32_t)y) && pxa_put_u32(frame, width) &&
         pxa_put_u32(frame, height) && pxa_put_u32(frame, fill_rgba) &&
         pxa_put_u16(frame, border_width) && pxa_put_u32(frame, border_rgba);
    if (ok) ++frame->records;
    return ok;
}

static inline int pxa_canvas_circle(pxa_canvas_frame_t* frame, int16_t x,
                                    int16_t y, uint16_t radius,
                                    uint32_t color) {
    if (radius == 0 || radius > UINT16_MAX / 2u) return 0;
    return pxa_canvas_ellipse_rgba(
        frame, (int32_t)x - radius, (int32_t)y - radius,
        (uint32_t)radius * 2u, (uint32_t)radius * 2u,
        pxa_canvas_rgba(color), 0, 0);
}

static inline int pxa_canvas_line_rgba(
    pxa_canvas_frame_t* frame, int32_t x1, int32_t y1, int32_t x2,
    int32_t y2, uint32_t rgba, uint16_t width) {
    int ok;
    if (width == 0 ||
        !pxa_canvas_primitive(frame, PXA_CANVAS_DRAW_LINE, 22)) return 0;
    ok = pxa_put_u32(frame, (uint32_t)x1) &&
         pxa_put_u32(frame, (uint32_t)y1) &&
         pxa_put_u32(frame, (uint32_t)x2) &&
         pxa_put_u32(frame, (uint32_t)y2) && pxa_put_u32(frame, rgba) &&
         pxa_put_u16(frame, width);
    if (ok) ++frame->records;
    return ok;
}

static inline int pxa_canvas_line(pxa_canvas_frame_t* frame, int16_t x1,
                                  int16_t y1, int16_t x2, int16_t y2,
                                  uint32_t color, uint8_t width) {
    return pxa_canvas_line_rgba(frame, x1, y1, x2, y2,
                                pxa_canvas_rgba(color), width);
}

static inline int pxa_canvas_arc(
    pxa_canvas_frame_t* frame, int32_t center_x, int32_t center_y,
    uint32_t radius, uint16_t start_angle, uint16_t end_angle,
    uint32_t rgba, uint16_t width) {
    int ok;
    if (radius == 0 || radius > UINT16_MAX || width == 0 ||
        !pxa_canvas_primitive(frame, PXA_CANVAS_DRAW_ARC, 22)) return 0;
    ok = pxa_put_u32(frame, (uint32_t)center_x) &&
         pxa_put_u32(frame, (uint32_t)center_y) &&
         pxa_put_u32(frame, radius) && pxa_put_u16(frame, start_angle) &&
         pxa_put_u16(frame, end_angle) && pxa_put_u32(frame, rgba) &&
         pxa_put_u16(frame, width);
    if (ok) ++frame->records;
    return ok;
}

static inline int pxa_canvas_text_role(
    pxa_canvas_frame_t* frame, int32_t x, int32_t y, uint32_t width,
    uint32_t rgba, uint8_t font_role, uint8_t align, const char* text,
    size_t text_length) {
    int ok;
    if (width == 0 || font_role > PXA_CANVAS_FONT_ICON ||
        align > PXA_CANVAS_ALIGN_RIGHT || text == NULL ||
        text_length > UINT16_MAX - 18u ||
        !pxa_canvas_primitive(frame, PXA_CANVAS_DRAW_TEXT,
                              (uint16_t)(18u + text_length))) return 0;
    ok = pxa_put_u32(frame, (uint32_t)x) &&
         pxa_put_u32(frame, (uint32_t)y) && pxa_put_u32(frame, width) &&
         pxa_put_u32(frame, rgba) && pxa_put_u8(frame, font_role) &&
         pxa_put_u8(frame, align) &&
         pxa_put_bytes(frame, (const uint8_t*)text, text_length);
    if (ok) ++frame->records;
    return ok;
}

static inline int pxa_canvas_text(pxa_canvas_frame_t* frame, int16_t x,
                                  int16_t y, uint16_t width, uint32_t color,
                                  uint8_t align, const char* text,
                                  size_t text_length) {
    return pxa_canvas_text_role(frame, x, y, width, pxa_canvas_rgba(color),
                                PXA_CANVAS_FONT_BODY, align, text,
                                text_length);
}

static inline int pxa_canvas_text_box_role(
    pxa_canvas_frame_t* frame, int32_t x, int32_t y, uint32_t width,
    uint32_t height, uint32_t rgba, uint8_t font_role, uint8_t align,
    uint8_t vertical_align, const char* text, size_t text_length) {
    int ok;
    if (width == 0 || height == 0 || font_role > PXA_CANVAS_FONT_ICON ||
        align > PXA_CANVAS_ALIGN_RIGHT ||
        vertical_align > PXA_CANVAS_TEXT_ALIGN_BOTTOM || text == NULL ||
        text_length > UINT16_MAX - 24u ||
        !pxa_canvas_primitive(frame, PXA_CANVAS_DRAW_TEXT_BOX,
                              (uint16_t)(24u + text_length))) return 0;
    ok = pxa_put_u32(frame, (uint32_t)x) &&
         pxa_put_u32(frame, (uint32_t)y) && pxa_put_u32(frame, width) &&
         pxa_put_u32(frame, height) && pxa_put_u32(frame, rgba) &&
         pxa_put_u8(frame, font_role) && pxa_put_u8(frame, align) &&
         pxa_put_u8(frame, vertical_align) && pxa_put_u8(frame, 0) &&
         pxa_put_bytes(frame, (const uint8_t*)text, text_length);
    if (ok) ++frame->records;
    return ok;
}

static inline int pxa_canvas_text_box(
    pxa_canvas_frame_t* frame, int16_t x, int16_t y, uint16_t width,
    uint16_t height, uint32_t color, uint8_t align, uint8_t vertical_align,
    const char* text, size_t text_length) {
    return pxa_canvas_text_box_role(
        frame, x, y, width, height, pxa_canvas_rgba(color),
        PXA_CANVAS_FONT_BODY, align, vertical_align, text, text_length);
}

static inline int pxa_canvas_image(
    pxa_canvas_frame_t* frame, int32_t x, int32_t y, uint32_t width,
    uint32_t height, uint8_t opacity, uint8_t fit, const char* path,
    size_t path_length) {
    int ok;
    if (width == 0 || height == 0 || fit > 2 || path == NULL ||
        path_length == 0 || path_length > UINT16_MAX - 18u ||
        !pxa_canvas_primitive(frame, PXA_CANVAS_DRAW_IMAGE,
                              (uint16_t)(18u + path_length))) return 0;
    ok = pxa_put_u32(frame, (uint32_t)x) &&
         pxa_put_u32(frame, (uint32_t)y) && pxa_put_u32(frame, width) &&
         pxa_put_u32(frame, height) && pxa_put_u8(frame, opacity) &&
         pxa_put_u8(frame, fit) &&
         pxa_put_bytes(frame, (const uint8_t*)path, path_length);
    if (ok) ++frame->records;
    return ok;
}

/* Reserves one RGB565 strip directly in the display list. This lets scanline
 * renderers avoid a second full-frame buffer and copy. */
static inline uint8_t* pxa_canvas_bitmap_rgb565_reserve(
    pxa_canvas_frame_t* frame, int32_t x, int32_t y, uint32_t width,
    uint32_t height, uint32_t stride) {
    size_t pixel_bytes;
    uint8_t* pixels;
    if (width == 0 || width > UINT16_MAX || height == 0 ||
        height > UINT16_MAX || stride > UINT16_MAX ||
        width > stride / 2u || height > SIZE_MAX / stride ||
        stride > UINT16_MAX - 20u ||
        (int64_t)y + (int64_t)height - 1 > INT32_MAX)
        return NULL;
    pixel_bytes = (size_t)stride * height;
    if (pixel_bytes > UINT16_MAX - 20u ||
        !pxa_canvas_primitive(frame, PXA_CANVAS_DRAW_BITMAP_RGB565,
                              (uint16_t)(20u + pixel_bytes)) ||
        !pxa_put_u32(frame, (uint32_t)x) ||
        !pxa_put_u32(frame, (uint32_t)y) ||
        !pxa_put_u32(frame, width) || !pxa_put_u32(frame, height) ||
        !pxa_put_u32(frame, stride))
        return NULL;
    pixels = frame->data + frame->length;
    frame->length += pixel_bytes;
    ++frame->records;
    return pixels;
}

/* Appends an RGB565 bitmap, splitting large images into horizontal strips.
 * Pixels are little-endian and stride includes any row padding. */
static inline int pxa_canvas_bitmap_rgb565(
    pxa_canvas_frame_t* frame, int32_t x, int32_t y, uint32_t width,
    uint32_t height, uint32_t stride, const uint8_t* pixels,
    size_t pixel_bytes) {
    uint32_t row = 0;
    uint32_t max_rows;
    if (width == 0 || width > UINT16_MAX || height == 0 ||
        height > UINT16_MAX || stride > UINT16_MAX ||
        width > stride / 2u || height > SIZE_MAX / stride ||
        pixel_bytes != (size_t)stride * height || pixels == NULL ||
        stride > UINT16_MAX - 20u ||
        (int64_t)y + (int64_t)height - 1 > INT32_MAX)
        return 0;
    max_rows = (UINT16_MAX - 20u) / stride;
    while (row < height) {
        uint32_t rows = height - row;
        size_t strip_bytes;
        size_t byte;
        uint8_t* destination;
        if (rows > max_rows) rows = max_rows;
        strip_bytes = (size_t)stride * rows;
        destination = pxa_canvas_bitmap_rgb565_reserve(
            frame, x, (int32_t)(y + (int64_t)row), width, rows, stride);
        if (destination == NULL) return 0;
        for (byte = 0; byte < strip_bytes; ++byte)
            destination[byte] = pixels[(size_t)row * stride + byte];
        row += rows;
    }
    return 1;
}

static inline int pxa_canvas_clip_push(pxa_canvas_frame_t* frame, int32_t x,
                                       int32_t y, uint32_t width,
                                       uint32_t height) {
    int ok;
    if (width == 0 || height == 0 ||
        !pxa_canvas_primitive(frame, PXA_CANVAS_CLIP_PUSH, 16)) return 0;
    ok = pxa_put_u32(frame, (uint32_t)x) &&
         pxa_put_u32(frame, (uint32_t)y) && pxa_put_u32(frame, width) &&
         pxa_put_u32(frame, height);
    if (ok) ++frame->records;
    return ok;
}

static inline int pxa_canvas_clip_pop(pxa_canvas_frame_t* frame) {
    int ok = pxa_canvas_primitive(frame, PXA_CANVAS_CLIP_POP, 0);
    if (ok) ++frame->records;
    return ok;
}

static inline int pxa_canvas_parse_event(const uint8_t* event,
                                         uint32_t length,
                                         pxa_canvas_event_t* output) {
    return pxa_parse_event(event, length, output);
}

static inline int pxa_canvas_parse_pointer(const pxa_event_t* event,
                                           uint32_t node,
                                           pxa_ui_pointer_data_t* output) {
    return pxa_ui_parse_pointer(event, output) && output->node == node;
}

static inline int pxa_canvas_send(uint8_t* scratch, size_t capacity,
                                  uint16_t opcode, const uint8_t* payload,
                                  size_t payload_size) {
    return pxa_ui_send_packet(scratch, capacity, opcode, payload,
                              payload_size);
}

/* The retained Canvas node is committed once. Later display lists stream into
 * independently generated frames without duplicating the frame buffer. */
typedef struct {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} pxa_canvas_dirty_rect_t;

/* Regions cover all changed pixels, including the previous position of moving
 * objects. The first frame always invalidates the full Canvas. */
static inline int pxa_canvas_present_regions_via(
    uint32_t node_id, const pxa_canvas_frame_t* canvas, uint32_t* generation,
    uint8_t* initialized, uint64_t root_event_mask, uint8_t* commands,
    size_t commands_capacity, uint8_t* packet, size_t packet_capacity,
    const pxa_canvas_dirty_rect_t* dirty_rects, uint8_t dirty_count,
    uint32_t stream_handle) {
    uint32_t next_generation;
    size_t offset = 0;
    uint8_t begin_payload[16] = {0};
    uint8_t present_payload[13 + 4 * 16] = {0};
    uint8_t index;
    pxa_writer_t value;
    (void)commands;
    (void)commands_capacity;
    if (node_id == 0 || canvas == NULL || canvas->failed ||
        canvas->data == NULL || generation == NULL || initialized == NULL ||
        packet == NULL || packet_capacity < 32 || dirty_count > 4 ||
        (dirty_count != 0 && dirty_rects == NULL) ||
        packet_capacity < 25u + (size_t)dirty_count * 16u)
        return 0;
    for (index = 0; index < dirty_count; ++index)
        if (dirty_rects[index].width <= 0 || dirty_rects[index].height <= 0)
            return 0;
    next_generation = *generation + 1u;
    if (next_generation == 0) return 0;
    if (!*initialized) {
        dirty_count = 0;
        pxa_ui_transaction_t transaction = {0};
        if (!pxa_ui_transaction_begin(&transaction, next_generation,
                                      PXA_UI_TRANSACTION_REPLACE_SURFACE, packet,
                                      packet_capacity) ||
            !pxa_ui_create(&transaction, 1, 0, 0, PXA_UI_NODE_ROOT) ||
            !pxa_ui_create(&transaction, node_id, 1, 0,
                           PXA_UI_NODE_CANVAS) ||
            !pxa_ui_set_event_mask(&transaction, 1,
                                   root_event_mask) ||
            !pxa_ui_set_length(&transaction, node_id, PXA_UI_PROPERTY_WIDTH,
                               PXA_UI_LENGTH_FILL, 0) ||
            !pxa_ui_set_length(&transaction, node_id, PXA_UI_PROPERTY_HEIGHT,
                               PXA_UI_LENGTH_FILL, 0) ||
            !pxa_ui_set_event_mask(&transaction, node_id,
                                   PXA_UI_EVENT_MASK_POINTER) ||
            !pxa_ui_transaction_commit(&transaction)) {
            if (transaction.active)
                (void)pxa_ui_transaction_cancel(&transaction);
            return 0;
        }
        *initialized = 1;
    }
    pxa_writer_init(&value, begin_payload, sizeof(begin_payload));
    if (!pxa_put_u32(&value, PXA_UI_PRIMARY_SURFACE) ||
        !pxa_put_u32(&value, node_id) ||
        !pxa_put_u32(&value, next_generation) || !pxa_put_u32(&value, 0) ||
        !pxa_canvas_send(packet, packet_capacity, PXA_UI_CANVAS_BEGIN,
                         begin_payload, sizeof(begin_payload))) return 0;
    if (stream_handle != 0) {
        if (canvas->length > UINT32_MAX ||
            pxa_io(stream_handle, PXA_IO_WRITE, canvas->data,
                   (uint32_t)canvas->length) != (int32_t)canvas->length)
            return 0;
        offset = canvas->length;
    }
    while (offset < canvas->length) {
        pxa_writer_t append;
        size_t chunk = canvas->length - offset;
        if (packet_capacity <= 24u) return 0;
        if (chunk > packet_capacity - 24u) chunk = packet_capacity - 24u;
        pxa_writer_init(&append, packet, packet_capacity);
        if (!pxa_put_u16(&append, PXA_SERVICE_UI) ||
            !pxa_put_u16(&append, PXA_UI_CANVAS_WRITE) ||
            !pxa_put_u32(&append, 0) ||
            !pxa_put_u32(&append, (uint32_t)(12u + chunk)) ||
            !pxa_put_u32(&append, PXA_UI_PRIMARY_SURFACE) ||
            !pxa_put_u32(&append, node_id) ||
            !pxa_put_u32(&append, next_generation) ||
            !pxa_put_bytes(&append, canvas->data + offset, chunk) ||
            pxa_control(append.data, (uint32_t)append.length) != PXA_STATUS_OK)
            return 0;
        offset += chunk;
    }
    pxa_writer_init(&value, present_payload, sizeof(present_payload));
    if (!pxa_put_u32(&value, PXA_UI_PRIMARY_SURFACE) ||
        !pxa_put_u32(&value, node_id) ||
        !pxa_put_u32(&value, next_generation) || !pxa_put_u8(&value, dirty_count))
        return 0;
    for (index = 0; index < dirty_count; ++index) {
        if (!pxa_put_u32(&value, (uint32_t)dirty_rects[index].x) ||
            !pxa_put_u32(&value, (uint32_t)dirty_rects[index].y) ||
            !pxa_put_u32(&value, (uint32_t)dirty_rects[index].width) ||
            !pxa_put_u32(&value, (uint32_t)dirty_rects[index].height)) return 0;
    }
    if (!pxa_canvas_send(packet, packet_capacity, PXA_UI_CANVAS_PRESENT,
                         present_payload, value.length)) return 0;
    *generation = next_generation;
    return 1;
}

static inline int pxa_canvas_present_regions(
    uint32_t node_id, const pxa_canvas_frame_t* canvas, uint32_t* generation,
    uint8_t* initialized, uint64_t root_event_mask, uint8_t* commands,
    size_t commands_capacity, uint8_t* packet, size_t packet_capacity,
    const pxa_canvas_dirty_rect_t* dirty_rects, uint8_t dirty_count) {
    return pxa_canvas_present_regions_via(
        node_id, canvas, generation, initialized, root_event_mask, commands,
        commands_capacity, packet, packet_capacity, dirty_rects, dirty_count,
        0);
}

static inline int pxa_canvas_present_stream(
    uint32_t stream_handle, uint32_t node_id,
    const pxa_canvas_frame_t* canvas, uint32_t* generation,
    uint8_t* initialized, uint8_t* commands, size_t commands_capacity,
    uint8_t* packet, size_t packet_capacity) {
    if (stream_handle == 0) return 0;
    return pxa_canvas_present_regions_via(
        node_id, canvas, generation, initialized, 0, commands,
        commands_capacity, packet, packet_capacity, NULL, 0, stream_handle);
}

static inline int pxa_canvas_present_stream_regions(
    uint32_t stream_handle, uint32_t node_id,
    const pxa_canvas_frame_t* canvas, uint32_t* generation,
    uint8_t* initialized, uint8_t* commands, size_t commands_capacity,
    uint8_t* packet, size_t packet_capacity,
    const pxa_canvas_dirty_rect_t* dirty_rects, uint8_t dirty_count) {
    if (stream_handle == 0) return 0;
    return pxa_canvas_present_regions_via(
        node_id, canvas, generation, initialized, 0, commands,
        commands_capacity, packet, packet_capacity, dirty_rects, dirty_count,
        stream_handle);
}

static inline int pxa_canvas_present_with_root_event_mask(
    uint32_t node_id, const pxa_canvas_frame_t* canvas, uint32_t* generation,
    uint8_t* initialized, uint64_t root_event_mask, uint8_t* commands,
    size_t commands_capacity, uint8_t* packet, size_t packet_capacity) {
    return pxa_canvas_present_regions(node_id, canvas, generation, initialized,
        root_event_mask, commands, commands_capacity, packet, packet_capacity,
        NULL, 0);
}

static inline int pxa_canvas_present(
    uint32_t node_id, const pxa_canvas_frame_t* canvas, uint32_t* generation,
    uint8_t* initialized, uint8_t* commands, size_t commands_capacity,
    uint8_t* packet, size_t packet_capacity) {
    return pxa_canvas_present_with_root_event_mask(
        node_id, canvas, generation, initialized, 0, commands,
        commands_capacity, packet, packet_capacity);
}

static inline size_t pxa_canvas_u32_text(char* output, uint32_t value) {
    char reverse[10];
    size_t count = 0;
    size_t index;
    do {
        reverse[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0);
    for (index = 0; index < count; ++index)
        output[index] = reverse[count - index - 1u];
    return count;
}

#endif
