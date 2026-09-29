#include <assert.h>

#include "pxa_ui.h"

static unsigned submitted;
static unsigned writes;
static unsigned themes;
static const uint64_t theme_token = UINT64_C(0x10000004d);

int32_t pxa_submit(const uint8_t *packet, uint32_t size) {
    pxa_event_t message;
    assert(pxa_parse_event(packet, size, &message));
    assert(message.service == PXA_UI_SERVICE);
    if (message.opcode == PXA_UI_TX_WRITE) {
        assert(message.token == 0 && message.payload_size > 4 &&
               pxa_load_u32(message.payload) == 7);
        ++writes;
    }
    if (message.opcode == PXA_UI_THEME_GET) {
        assert(message.token == theme_token && message.payload_size == 0);
        ++themes;
    }
    ++submitted;
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
    uint8_t packet[48];
    uint8_t controller_bytes[32] = {0};
    char text[80];
    pxa_ui_transaction_t transaction = {0};
    pxa_ui_controller_data_t controller;
    pxa_ui_event_data_t input;
    pxa_event_t event = {PXA_UI_SERVICE, PXA_UI_EVENT, 0,
                            controller_bytes, sizeof(controller_bytes)};
    for (unsigned i = 0; i < sizeof(text); ++i) text[i] = 'x';
    assert(pxa_ui_transaction_begin(&transaction, 7,
                                     PXA_UI_TRANSACTION_REPLACE_SURFACE,
                                     packet, sizeof(packet)));
    assert(pxa_ui_create(&transaction, 1, 0, 0, PXA_UI_NODE_ROOT));
    assert(pxa_ui_create(&transaction, 2, 1, 0, PXA_UI_NODE_TEXT));
    assert(pxa_ui_set_text(&transaction, 2, text, sizeof(text)));
    assert(pxa_ui_transaction_commit(&transaction));
    assert(writes > 4 && pxa_ui_theme_get(theme_token));
    assert(pxa_ui_surface_open(9, PXA_UI_SURFACE_DIALOG));
    assert(pxa_ui_surface_close(2));
    assert(themes == 1 && submitted > writes);
    pxa_store_u32(controller_bytes, 1);
    pxa_store_u32(controller_bytes + 4, 2);
    pxa_store_u32(controller_bytes + 8, 3);
    pxa_store_u16(controller_bytes + 12,
                     PXA_UI_EVENT_CONTROLLER_STATE_KIND);
    pxa_store_u64(controller_bytes + 16, 1234);
    controller_bytes[24] = 1;
    controller_bytes[25] = 1;
    pxa_store_u32(controller_bytes + 28, 7);
    assert(pxa_ui_parse_controller(&event, &controller));
    assert(pxa_ui_parse_controller(&event, &controller));
    assert(controller.node == 2 && controller.buttons == 7 &&
           controller.timestamp_us == 1234);
    controller_bytes[25] = 0;
    assert(!pxa_ui_parse_controller(&event, &controller));
    pxa_store_u16(controller_bytes + 12, PXA_UI_EVENT_KEY_KIND);
    pxa_store_u32(controller_bytes + 24, PXA_UI_KEY_VOLUME_UP);
    event.payload_size = 28;
    assert(pxa_ui_parse_event(&event, &input));
    assert(pxa_ui_parse_event(&event, &input));
    assert(input.node == 2 && input.kind == PXA_UI_EVENT_KEY_KIND &&
           input.value == PXA_UI_KEY_VOLUME_UP && input.data_size == 4);
    event.token = 1;
    assert(!pxa_ui_parse_event(&event, &input));
    {
        uint8_t theme_bytes[64] = {0};
        pxa_ui_theme_t theme;
        pxa_store_u32(theme_bytes + 4, 9);
        for (unsigned i = 0; i < PXA_UI_THEME_FONT_COUNT; ++i)
            pxa_store_u16(theme_bytes + 52 + i * 2, 12);
        event.opcode = PXA_UI_THEME_GET;
        event.token = theme_token;
        event.payload = theme_bytes;
        event.payload_size = sizeof(theme_bytes);
        assert(pxa_ui_parse_theme_event(&event, &theme));
        assert(theme.generation == 9 && theme.typography_px[5] == 12);
        event.token = 0;
        assert(!pxa_ui_parse_theme_event(&event, &theme));
    }
    {
        uint8_t pressure_bytes[4] = {PXA_UI_PRESSURE_CONSTRAINED, 0, 0, 0};
        uint8_t pressure = 0;
        event.opcode = PXA_UI_RESOURCE_PRESSURE;
        event.token = 0;
        event.payload = pressure_bytes;
        event.payload_size = sizeof(pressure_bytes);
        assert(pxa_ui_parse_resource_pressure(&event, &pressure));
        assert(pressure == PXA_UI_PRESSURE_CONSTRAINED);
        event.token = 1;
        assert(!pxa_ui_parse_resource_pressure(&event, &pressure));
    }
    {
        uint8_t ready_bytes[12] = {0};
        pxa_ui_environment_t environment;
        uint32_t request = 0;
        int32_t status = 0;
        pxa_store_u32(ready_bytes, 17);
        pxa_store_u32(ready_bytes + 8, (uint32_t)PXA_STATUS_RESOURCE_LIMIT);
        event.opcode = PXA_UI_SURFACE_READY;
        event.token = 0;
        event.payload = ready_bytes;
        event.payload_size = sizeof(ready_bytes);
        assert(pxa_ui_parse_surface_ready(&event, &request, &status,
                                          &environment));
        assert(request == 17 && status == PXA_STATUS_RESOURCE_LIMIT);
    }
    return 0;
}
