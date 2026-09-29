#include <assert.h>

#include "pxa_ui_wire.h"

int32_t pxa_submit(const uint8_t *bytes, uint32_t size) {
    (void)bytes;
    (void)size;
    return 0;
}

int32_t pxa_io(uint64_t handle, uint32_t operation, uint8_t *data,
                  uint32_t size) {
    (void)handle;
    (void)operation;
    (void)data;
    (void)size;
    return -3;
}

int main(void) {
    uint8_t packet[128] = {0};
    uint8_t payload[80] = {0};
    uint8_t prefix[4] = {1, 0, 0, 0};
    uint8_t value[16];
    uint8_t collected[24];
    size_t collected_size = 0;
    pxa_ui_wire_record_stream_t stream_builder;
    uint32_t size = 0;
    pxa_event_t event;
    pxa_ui_wire_theme_t theme;
    pxa_ui_wire_canvas_stream_t stream;
    pxa_ui_wire_surface_ready_t surface;
    pxa_ui_wire_input_event_t input;
    const uint64_t handle = UINT64_C(0x1234567800000001);
    assert(pxa_ui_wire_build_tx_begin(packet, sizeof(packet), 1, 7, 2,
                                     0, 3, &size) && size == 40);
    assert(pxa_parse_event(packet, size, &event) &&
           event.opcode == PXA_UI_TX_BEGIN && event.token == 0 &&
           event.payload[17] == 1);
    assert(pxa_ui_wire_build_tx_record(packet, sizeof(packet), 7, 1, 0,
                                      prefix, sizeof(prefix), NULL, 0,
                                      &size) && size == 32);
    assert(pxa_parse_event(packet, size, &event) &&
           event.opcode == PXA_UI_TX_WRITE &&
           pxa_load_u32(event.payload) == 7);
    for (uint8_t i = 0; i < sizeof(value); ++i) value[i] = i;
    assert(pxa_ui_wire_record_stream_init(&stream_builder, 7, 2, 0,
                                         prefix, sizeof(prefix),
                                         value, sizeof(value)));
    for (;;) {
        int next = pxa_ui_wire_record_stream_next(&stream_builder, packet,
                                                32, &size);
        if (next == 0) break;
        assert(next == 1 && size <= 32 &&
               pxa_parse_event(packet, size, &event) &&
               event.payload_size > 4 &&
               pxa_load_u32(event.payload) == 7);
        for (size_t i = 4; i < event.payload_size; ++i)
            collected[collected_size++] = event.payload[i];
    }
    assert(collected_size == sizeof(collected) && collected[0] == 2 &&
           collected[1] == 0 && pxa_load_u16(collected + 2) == 20);
    for (size_t i = 0; i < sizeof(prefix); ++i)
        assert(collected[4 + i] == prefix[i]);
    for (size_t i = 0; i < sizeof(value); ++i)
        assert(collected[8 + i] == value[i]);
    assert(pxa_ui_wire_build_tx_end(packet, sizeof(packet), 7, 1, &size) &&
           size == 24);
    assert(pxa_ui_wire_build_theme_get(packet, sizeof(packet), 88, &size) &&
           size == 20);

    pxa_store_u32(payload, 0);
    pxa_store_u32(payload + 4, 1);
    pxa_store_u16(payload + 52, 14);
    event.service = PXA_UI_SERVICE;
    event.opcode = PXA_UI_THEME_GET;
    event.token = 88;
    event.payload = payload;
    event.payload_size = 64;
    assert(pxa_ui_wire_parse_theme(&event, 88, &theme) &&
           theme.generation == 1 && theme.typography_px[0] == 14);
    event.payload_size = 63;
    assert(!pxa_ui_wire_parse_theme(&event, 88, &theme));

    pxa_store_u32(payload, 9);
    pxa_store_u64(payload + 4, handle);
    pxa_store_u32(payload + 12, 0);
    event.opcode = PXA_UI_CANVAS_STREAM_READY;
    event.token = 0;
    event.payload_size = 16;
    assert(pxa_ui_wire_parse_canvas_stream_ready(&event, &stream) &&
           stream.request == 9 && stream.handle == handle);
    assert(pxa_ui_wire_canvas_stream_write(0, payload, 1) == -1);

    pxa_store_u32(payload + 4, 2);
    pxa_store_u32(payload + 8, 0);
    event.opcode = PXA_UI_SURFACE_READY;
    event.payload_size = 12;
    assert(pxa_ui_wire_parse_surface_ready(&event, &surface) &&
           surface.surface == 2);
    pxa_store_u32(payload, 1);
    pxa_store_u32(payload + 4, 2);
    pxa_store_u32(payload + 8, 3);
    pxa_store_u16(payload + 12, 1);
    event.opcode = PXA_UI_EVENT;
    event.payload_size = 24;
    assert(pxa_ui_wire_parse_input_event(&event, &input) &&
           input.surface == 1 && input.node == 2 &&
           input.generation == 3);
    return 0;
}
