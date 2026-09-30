#include "pxa_ui_components.h"

static pxa_ui_builder_t ui;
static uint32_t generation;
static uint32_t button;
static uint8_t scratch[128];
static uint32_t ancestors[8];

int32_t pxa_app_start(const uint8_t* config, uint32_t length) {
    (void)config;
    (void)length;
    if (!pxa_ui_builder_begin(&ui, &generation, PXA_UI_PRIMARY_SURFACE, 0,
                              PXA_UI_TRANSACTION_REPLACE_SURFACE, scratch,
                              sizeof(scratch), ancestors, 8))
        return PXA_STATUS_INTERNAL;

    if (!pxa_ui_builder_auto_enter(&ui, PXA_UI_NODE_ROOT) ||
        !pxa_ui_text(&ui, "Hello, PXA", 2, PXA_UI_THEME_TEXT) ||
        !(button = pxa_ui_button(&ui, "Tap me", PXA_UI_THEME_PRIMARY,
                                 PXA_UI_THEME_ON_PRIMARY)) ||
        !pxa_ui_builder_leave(&ui)) {
        (void)pxa_ui_builder_abort(&ui);
        return PXA_STATUS_INTERNAL;
    }
    return pxa_ui_builder_end(&ui) ? PXA_STATUS_OK : PXA_STATUS_INTERNAL;
}

int32_t pxa_app_on_event(const uint8_t* bytes, uint32_t length) {
    pxa_event_t event;
    pxa_ui_event_data_t input;
    if (!pxa_parse_event(bytes, length, &event))
        return PXA_STATUS_PROTOCOL_ERROR;
    if (!pxa_ui_parse_event(&event, &input) ||
        !pxa_ui_event_is_current(&input, PXA_UI_PRIMARY_SURFACE, generation) ||
        input.kind != PXA_UI_EVENT_CLICK_KIND || input.node != button)
        return PXA_EVENT_UNHANDLED;
    return pxa_log_write(PXA_LOG_LEVEL_INFO, "Button tapped") == PXA_STATUS_OK
               ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }
