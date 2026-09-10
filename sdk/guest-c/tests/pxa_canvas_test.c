#include "pxa_canvas.h"

#include <assert.h>
#include <string.h>

static int32_t control_status;
static uint16_t opcodes[32];
static size_t opcode_count;
static uint8_t transaction_stream[512];
static size_t transaction_stream_size;
static uint8_t last_present[13 + 4 * 16];
static size_t last_present_size;
static size_t io_calls;
static uint32_t io_handle;
static uint32_t io_length;
static int io_accept;

int32_t pxa_control(const uint8_t* data, uint32_t length) {
    assert(data != NULL);
    assert(length >= 12);
    assert(pxa_read_u16(data) == PXA_SERVICE_UI);
    assert(pxa_read_u32(data + 8) == length - 12u);
    if (opcode_count < sizeof(opcodes) / sizeof(opcodes[0]))
        opcodes[opcode_count++] = pxa_read_u16(data + 2);
    if (pxa_read_u16(data + 2) == PXA_UI_TX_BEGIN) {
        transaction_stream_size = 0;
    } else if (pxa_read_u16(data + 2) == PXA_UI_TX_WRITE) {
        size_t chunk = length - 16u;
        assert(length >= 16u);
        assert(chunk <= sizeof(transaction_stream) - transaction_stream_size);
        memcpy(transaction_stream + transaction_stream_size, data + 16, chunk);
        transaction_stream_size += chunk;
    } else if (pxa_read_u16(data + 2) == PXA_UI_CANVAS_PRESENT) {
        last_present_size = length - 12u;
        assert(last_present_size <= sizeof(last_present));
        memcpy(last_present, data + 12, last_present_size);
    }
    return control_status;
}

static int transaction_has_event_mask(uint32_t node, uint64_t mask) {
    size_t offset = 0;
    while (offset + 4u <= transaction_stream_size) {
        uint8_t opcode = transaction_stream[offset];
        uint16_t size = pxa_read_u16(transaction_stream + offset + 2u);
        const uint8_t *payload = transaction_stream + offset + 4u;
        assert(size <= transaction_stream_size - offset - 4u);
        if (opcode == PXA_UI_COMMAND_SET_PROPERTY && size == 14u &&
            pxa_read_u32(payload) == node &&
            pxa_read_u16(payload + 4u) == PXA_UI_PROPERTY_EVENT_MASK &&
            pxa_read_u64(payload + 6u) == mask)
            return 1;
        offset += 4u + size;
    }
    assert(offset == transaction_stream_size);
    return 0;
}

int32_t pxa_io(uint32_t handle, uint32_t operation, uint8_t* data,
               uint32_t length) {
    assert(operation == PXA_IO_WRITE && data != NULL);
    ++io_calls;
    io_handle = handle;
    io_length = length;
    return io_accept ? (int32_t)length : PXA_STATUS_UNSUPPORTED;
}

static void test_primitives_are_length_prefixed(void) {
    uint8_t exact[32];
    pxa_canvas_frame_t frame;
    pxa_canvas_begin(&frame, exact, sizeof(exact));
    assert(pxa_canvas_rect(&frame, 1, 2, 3, 4, 0x123456, 2));
    assert(frame.length == sizeof(exact));
    assert(frame.records == 1);
    assert(exact[0] == PXA_CANVAS_DRAW_RECT);
    assert(pxa_read_u16(exact + 2) == 28);
    assert(pxa_read_u32(exact + 4) == 1);
    assert(pxa_read_u32(exact + 20) == 0x123456ffu);
    assert(!pxa_canvas_circle(&frame, 1, 2, 3, 0x123456));
    assert(frame.length == sizeof(exact));

    {
        uint8_t short_buffer[31];
        memset(short_buffer, 0xa5, sizeof(short_buffer));
        pxa_canvas_begin(&frame, short_buffer, sizeof(short_buffer));
        assert(!pxa_canvas_rect(&frame, 1, 2, 3, 4, 0x123456, 2));
        assert(frame.length == 0);
        for (size_t i = 0; i < sizeof(short_buffer); ++i)
            assert(short_buffer[i] == 0xa5);
    }
}

static void test_streamed_text_and_clip(void) {
    uint8_t data[512];
    char text[180];
    pxa_canvas_frame_t frame;
    memset(text, 'x', sizeof(text));
    pxa_canvas_begin(&frame, data, sizeof(data));
    assert(pxa_canvas_clip_push(&frame, -2, 3, 200, 100));
    assert(pxa_canvas_text(&frame, 0, 0, 200, 0xffffff,
                           PXA_CANVAS_ALIGN_LEFT, text, sizeof(text)));
    assert(pxa_canvas_clip_pop(&frame));
    assert(frame.records == 3);
    assert(frame.length == 20u + 4u + 18u + sizeof(text) + 4u);
}

static void test_rgb565_bitmap(void) {
    static const uint8_t pixels[] = {
        0x00, 0xf8, 0xe0, 0x07,
        0x1f, 0x00, 0xff, 0xff,
    };
    uint8_t data[32];
    pxa_canvas_frame_t frame;
    pxa_canvas_begin(&frame, data, sizeof(data));
    assert(pxa_canvas_bitmap_rgb565(&frame, -1, 3, 2, 2, 4,
                                    pixels, sizeof(pixels)));
    assert(frame.length == sizeof(data) && frame.records == 1);
    assert(data[0] == PXA_CANVAS_DRAW_BITMAP_RGB565);
    assert(pxa_read_u16(data + 2) == 28);
    assert((int32_t)pxa_read_u32(data + 4) == -1);
    assert(pxa_read_u32(data + 12) == 2);
    assert(pxa_read_u32(data + 20) == 4);
    assert(memcmp(data + 24, pixels, sizeof(pixels)) == 0);

    pxa_canvas_begin(&frame, data, sizeof(data));
    {
        uint8_t *reserved = pxa_canvas_bitmap_rgb565_reserve(
            &frame, -1, 3, 2, 2, 4);
        assert(reserved == data + 24 && frame.length == sizeof(data));
        memcpy(reserved, pixels, sizeof(pixels));
        assert(memcmp(data + 24, pixels, sizeof(pixels)) == 0);
    }

    pxa_canvas_begin(&frame, data, sizeof(data));
    assert(!pxa_canvas_bitmap_rgb565(&frame, 0, 0, 3, 2, 4,
                                     pixels, sizeof(pixels)));
    assert(frame.length == 0);
    assert(!pxa_canvas_bitmap_rgb565(&frame, 0, 0, 2, 2, 4,
                                     pixels, sizeof(pixels) - 1u));

    {
        static uint8_t nes_pixels[256u * 240u * 2u];
        static uint8_t encoded[sizeof(nes_pixels) + 48u];
        const size_t second = 4u + 20u + 127u * 512u;
        pxa_canvas_begin(&frame, encoded, sizeof(encoded));
        assert(pxa_canvas_bitmap_rgb565(&frame, 0, 0, 256, 240, 512,
                                        nes_pixels, sizeof(nes_pixels)));
        assert(frame.length == sizeof(encoded) && frame.records == 2);
        assert(pxa_read_u32(encoded + 16) == 127);
        assert(encoded[second] == PXA_CANVAS_DRAW_BITMAP_RGB565);
        assert(pxa_read_u32(encoded + second + 8) == 127);
        assert(pxa_read_u32(encoded + second + 16) == 113);
    }
}

static void test_icon_font_role(void) {
    static const char icon[] = PXA_CANVAS_ICON_PAUSE;
    uint8_t data[64];
    pxa_canvas_frame_t frame;
    assert(sizeof(icon) == 4);
    pxa_canvas_begin(&frame, data, sizeof(data));
    assert(pxa_canvas_text_role(&frame, 0, 0, 32, pxa_canvas_rgba(0xffffff),
                                PXA_CANVAS_FONT_ICON, PXA_CANVAS_ALIGN_CENTER,
                                icon, sizeof(icon) - 1));
    assert(frame.records == 1);
    assert(data[20] == PXA_CANVAS_FONT_ICON);
    assert(memcmp(data + 22, icon, sizeof(icon) - 1) == 0);
}

static void test_text_box_role(void) {
    static const char text[] = "按钮";
    uint8_t data[64];
    pxa_canvas_frame_t frame;
    pxa_canvas_begin(&frame, data, sizeof(data));
    assert(pxa_canvas_text_box_role(
        &frame, 12, 24, 100, 36, pxa_canvas_rgba(0xffffff),
        PXA_CANVAS_FONT_TITLE, PXA_CANVAS_ALIGN_CENTER,
        PXA_CANVAS_TEXT_ALIGN_MIDDLE, text, sizeof(text) - 1));
    assert(frame.records == 1);
    assert(data[0] == PXA_CANVAS_DRAW_TEXT_BOX);
    assert(pxa_read_u16(data + 2) == 24u + sizeof(text) - 1u);
    assert(pxa_read_u32(data + 12) == 100);
    assert(pxa_read_u32(data + 16) == 36);
    assert(data[24] == PXA_CANVAS_FONT_TITLE);
    assert(data[25] == PXA_CANVAS_ALIGN_CENTER);
    assert(data[26] == PXA_CANVAS_TEXT_ALIGN_MIDDLE);
    assert(memcmp(data + 28, text, sizeof(text) - 1) == 0);
}

static void test_event_parser(void) {
    const uint8_t event[] = {
        PXA_SERVICE_UI, 0, 1, 0x80, 0, 0, 0, 0, 36, 0, 0, 0,
        1, 0, 0, 0, 2, 0, 0, 0, 7, 0, 0, 0,
        PXA_UI_EVENT_POINTER_KIND, 0, 0, 0,
        0x40, 0x42, 0x0f, 0, 0, 0, 0, 0,
        3, PXA_POINTER_MOVE, 1, 0,
        21, 0, 0, 0, 34, 0, 0, 0,
    };
    pxa_canvas_event_t parsed;
    pxa_ui_pointer_data_t pointer;
    pxa_ui_controller_data_t controller;
    const uint8_t controller_event[] = {
        PXA_SERVICE_UI, 0, 1, 0x80, 0, 0, 0, 0, 32, 0, 0, 0,
        1, 0, 0, 0, 1, 0, 0, 0, 7, 0, 0, 0,
        PXA_UI_EVENT_CONTROLLER_STATE_KIND, 0, 0, 0,
        0x40, 0x42, 0x0f, 0, 0, 0, 0, 0,
        0, 1, 0, 0, 0x18, 0, 0, 0,
    };
    assert(pxa_canvas_parse_event(event, sizeof(event), &parsed));
    assert(pxa_canvas_parse_pointer(&parsed, 2, &pointer));
    assert(pointer.pointer_id == 3);
    assert(pointer.phase == PXA_POINTER_MOVE);
    assert(pointer.x == 21 && pointer.y == 34);
    assert(pointer.timestamp_us == 1000000u);
    assert(!pxa_canvas_parse_event(event, sizeof(event) - 1, &parsed));
    assert(pxa_canvas_parse_event(controller_event, sizeof(controller_event),
                                  &parsed));
    assert(pxa_ui_parse_controller(&parsed, &controller));
    assert(controller.controller == 0 && controller.connected == 1 &&
           controller.buttons == (PXA_CONTROLLER_RIGHT | PXA_CONTROLLER_A));
}

static void test_present_is_chunked_and_atomic(void) {
    uint8_t draw[256];
    uint8_t commands[1];
    uint8_t packet[64];
    pxa_canvas_frame_t frame;
    uint32_t generation = 0;
    uint8_t initialized = 0;
    pxa_canvas_begin(&frame, draw, sizeof(draw));
    assert(pxa_canvas_rect(&frame, 0, 0, 10, 10, 0, 0));
    assert(pxa_canvas_rect(&frame, 10, 10, 20, 20, 0xffffff, 1));

    control_status = PXA_STATUS_INTERNAL;
    opcode_count = 0;
    assert(!pxa_canvas_present(2, &frame, &generation, &initialized,
                               commands, sizeof(commands), packet,
                               sizeof(packet)));
    assert(generation == 0 && initialized == 0);

    control_status = PXA_STATUS_OK;
    opcode_count = 0;
    assert(pxa_canvas_present(2, &frame, &generation, &initialized,
                              commands, sizeof(commands), packet,
                              sizeof(packet)));
    assert(generation == 1 && initialized == 1);
    assert(opcodes[0] == PXA_UI_TX_BEGIN);
    assert(opcodes[opcode_count - 1] == PXA_UI_CANVAS_PRESENT);
    assert(transaction_has_event_mask(1, 0));
    assert(transaction_has_event_mask(2, PXA_UI_EVENT_MASK_POINTER));
    {
        size_t write_count = 0;
        for (size_t i = 0; i < opcode_count; ++i)
            if (opcodes[i] == PXA_UI_CANVAS_WRITE) ++write_count;
        assert(write_count == 2);
    }

    control_status = PXA_STATUS_INTERNAL;
    opcode_count = 0;
    assert(!pxa_canvas_present(2, &frame, &generation, &initialized,
                               commands, sizeof(commands), packet,
                               sizeof(packet)));
    assert(generation == 1 && initialized == 1);
}

static void test_dirty_regions(void) {
    uint8_t data[32], packet[128];
    pxa_canvas_frame_t frame;
    uint32_t generation = 0;
    uint8_t initialized = 0;
    pxa_canvas_dirty_rect_t rects[2] = {{-2, 3, 8, 9}, {12, 20, 4, 5}};
    pxa_canvas_begin(&frame, data, sizeof(data));
    assert(pxa_canvas_rect(&frame, 1, 2, 3, 4, 0xffffff, 0));
    control_status = PXA_STATUS_OK;
    assert(pxa_canvas_present_regions(2, &frame, &generation, &initialized,
        PXA_UI_EVENT_MASK_KEY, NULL, 0, packet, sizeof(packet), rects, 2));
    assert(transaction_has_event_mask(1, PXA_UI_EVENT_MASK_KEY));
    assert(last_present_size == 13 && last_present[12] == 0);
    assert(pxa_canvas_present_regions(2, &frame, &generation, &initialized,
        0, NULL, 0, packet, sizeof(packet), rects, 2));
    assert(last_present_size == 45 && last_present[12] == 2);
    assert((int32_t)pxa_read_u32(last_present + 13) == -2);
    assert(pxa_read_u32(last_present + 29) == 12);
    rects[0].width = 0;
    assert(!pxa_canvas_present_regions(2, &frame, &generation, &initialized,
        0, NULL, 0, packet, sizeof(packet), rects, 2));
    assert(generation == 2);
    assert(!pxa_canvas_present_regions(2, &frame, &generation, &initialized,
        0, NULL, 0, packet, sizeof(packet), NULL, 1));
}

static void test_stream_io_fast_path(void) {
    uint8_t data[64], packet[64];
    pxa_canvas_frame_t frame;
    uint32_t generation = 4;
    uint8_t initialized = 1;
    size_t write_count = 0;
    pxa_canvas_begin(&frame, data, sizeof(data));
    assert(pxa_canvas_rect(&frame, 1, 2, 3, 4, 0xffffff, 0));
    control_status = PXA_STATUS_OK;
    io_accept = 1;
    io_calls = 0;
    opcode_count = 0;
    assert(pxa_canvas_present_stream(77, 2, &frame, &generation,
                                     &initialized, NULL, 0, packet,
                                     sizeof(packet)));
    assert(generation == 5 && io_calls == 1 && io_handle == 77 &&
           io_length == frame.length);
    for (size_t i = 0; i < opcode_count; ++i)
        if (opcodes[i] == PXA_UI_CANVAS_WRITE) ++write_count;
    assert(write_count == 0 && opcodes[0] == PXA_UI_CANVAS_BEGIN &&
           opcodes[opcode_count - 1] == PXA_UI_CANVAS_PRESENT);

    opcode_count = 0;
    assert(pxa_canvas_stream_open(9, 2));
    assert(opcode_count == 1 && opcodes[0] == PXA_UI_CANVAS_STREAM_OPEN);
}

int main(void) {
    test_stream_io_fast_path();
    test_dirty_regions();
    test_primitives_are_length_prefixed();
    test_streamed_text_and_clip();
    test_rgb565_bitmap();
    test_icon_font_role();
    test_text_box_role();
    test_event_parser();
    test_present_is_chunked_and_atomic();
    return 0;
}
