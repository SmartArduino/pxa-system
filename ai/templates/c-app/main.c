#include "pxa_ui_components.h"

static pxa_ui_builder_t ui;
static uint32_t generation;
static uint32_t button;
static uint32_t count;
static uint32_t held_buttons;
static uint8_t scratch[256];
static uint32_t ancestors[8];

static int32_t render(void) {
    char value[6];
    uint32_t number = count;
    unsigned offset = sizeof(value) - 1;
    value[offset] = '\0';
    do {
        value[--offset] = (char)('0' + number % 10);
        number /= 10;
    } while (number);
    if (!pxa_ui_builder_begin(&ui, &generation, PXA_UI_PRIMARY_SURFACE, 0,
                              PXA_UI_TRANSACTION_REPLACE_SURFACE, scratch,
                              sizeof(scratch), ancestors, 8))
        return PXA_STATUS_INTERNAL;

    uint32_t root = pxa_ui_builder_auto_enter(&ui, PXA_UI_NODE_ROOT);
    pxa_ui_transaction_t* tx = pxa_ui_builder_transaction(&ui);
    if (!root ||
        !pxa_ui_set_event_mask(tx, root, PXA_UI_EVENT_MASK_CONTROLLER_STATE) ||
        !pxa_ui_set_length(tx, root, PXA_UI_PROPERTY_WIDTH, PXA_UI_LENGTH_PERCENT, 100) ||
        !pxa_ui_set_length(tx, root, PXA_UI_PROPERTY_HEIGHT, PXA_UI_LENGTH_PERCENT, 100) ||
        !pxa_ui_set_u8(tx, root, PXA_UI_PROPERTY_LAYOUT, PXA_UI_LAYOUT_COLUMN) ||
        !pxa_ui_set_u8(tx, root, PXA_UI_PROPERTY_JUSTIFY, PXA_UI_ALIGN_CENTER) ||
        !pxa_ui_set_u8(tx, root, PXA_UI_PROPERTY_ALIGN, PXA_UI_ALIGN_CENTER) ||
        !pxa_ui_set_padding(tx, root, 16, 24, 16, 16) ||
        !pxa_ui_set_dp(tx, root, PXA_UI_PROPERTY_GAP, 12) ||
        !pxa_ui_text(&ui, "Hello, PXA", 2, PXA_UI_THEME_TEXT) ||
        !pxa_ui_text(&ui, value + offset, 2, PXA_UI_THEME_TEXT) ||
        !(button = pxa_ui_button(&ui, "+1", PXA_UI_THEME_PRIMARY,
                                 PXA_UI_THEME_ON_PRIMARY)) ||
        !pxa_ui_set_padding(tx, button, 24, 10, 24, 10) ||
        !pxa_ui_builder_leave(&ui)) {
        (void)pxa_ui_builder_abort(&ui);
        return PXA_STATUS_INTERNAL;
    }
    return pxa_ui_builder_end(&ui) ? PXA_STATUS_OK : PXA_STATUS_INTERNAL;
}

int32_t pxa_app_start(const uint8_t* config, uint32_t length) {
    (void)config;
    (void)length;
    count = 0;
    held_buttons = 0;
    return render();
}

int32_t pxa_app_on_event(const uint8_t* bytes, uint32_t length) {
    pxa_event_t event;
    pxa_ui_event_data_t input;
    if (!pxa_parse_event(bytes, length, &event))
        return PXA_STATUS_PROTOCOL_ERROR;
    if (!pxa_ui_parse_event(&event, &input) ||
        !pxa_ui_event_is_current(&input, PXA_UI_PRIMARY_SURFACE, generation))
        return PXA_EVENT_UNHANDLED;
    if (input.kind == PXA_UI_EVENT_CONTROLLER_STATE_KIND) {
        pxa_ui_controller_data_t controller;
        if (!pxa_ui_parse_controller(&event, &controller))
            return PXA_EVENT_UNHANDLED;
        uint32_t pressed = controller.buttons & ~held_buttons;
        held_buttons = controller.buttons;
        if (!(pressed & PXA_CONTROLLER_A))
            return PXA_EVENT_HANDLED;
    } else if (input.kind != PXA_UI_EVENT_CLICK_KIND || input.node != button) {
        return PXA_EVENT_UNHANDLED;
    }
    count = (count + 1) % 100000;
    return render() == PXA_STATUS_OK ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
