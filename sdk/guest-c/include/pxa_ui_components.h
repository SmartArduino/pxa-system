#ifndef PXA_UI_COMPONENTS_H
#define PXA_UI_COMPONENTS_H

#include "pxa_ui_builder.h"

static inline size_t pxa_ui_text_size(const char* text) {
    size_t size = 0;
    if (text == NULL) return 0;
    while (text[size] != '\0') ++size;
    return size;
}

static inline int pxa_ui_component_text(pxa_ui_builder_t* builder,
                                        uint32_t node, const char* text,
                                        uint16_t font_role,
                                        uint8_t color_token) {
    pxa_ui_transaction_t* transaction = pxa_ui_builder_transaction(builder);
    return text != NULL &&
           pxa_ui_builder_node(builder, node, 0, PXA_UI_NODE_TEXT) &&
           pxa_ui_set_text(transaction, node, text, pxa_ui_text_size(text)) &&
           pxa_ui_set_font_role(transaction, node, font_role) &&
           pxa_ui_set_theme_color(transaction, node,
                                  PXA_UI_PROPERTY_FOREGROUND, color_token);
}

static inline int pxa_ui_component_button(
    pxa_ui_builder_t* builder, uint32_t node, uint32_t label_node,
    const char* label, uint8_t background_token, uint8_t foreground_token) {
    pxa_ui_transaction_t* transaction = pxa_ui_builder_transaction(builder);
    if (transaction == NULL || label == NULL ||
        !pxa_ui_builder_enter_typed(builder, node, 0, PXA_UI_NODE_CONTROL,
                                    PXA_UI_CONTROL_BUTTON) ||
        !pxa_ui_set_u8(transaction, node, PXA_UI_PROPERTY_LAYOUT,
                       PXA_UI_LAYOUT_ROW) ||
        !pxa_ui_set_u8(transaction, node, PXA_UI_PROPERTY_JUSTIFY,
                       PXA_UI_ALIGN_CENTER) ||
        !pxa_ui_set_u8(transaction, node, PXA_UI_PROPERTY_ALIGN,
                       PXA_UI_ALIGN_CENTER) ||
        !pxa_ui_set_event_mask(transaction, node, PXA_UI_EVENT_MASK_CLICK) ||
        !pxa_ui_set_theme_color(transaction, node, PXA_UI_PROPERTY_BACKGROUND,
                                background_token) ||
        !pxa_ui_component_text(builder, label_node, label, 1,
                               foreground_token) ||
        !pxa_ui_builder_leave(builder)) {
        if (builder != NULL) builder->failed = 1;
        return 0;
    }
    return 1;
}

static inline int pxa_ui_component_virtual_list(
    pxa_ui_builder_t* builder, uint32_t node, uint32_t item_count,
    int32_t item_extent_dp) {
    pxa_ui_transaction_t* transaction = pxa_ui_builder_transaction(builder);
    return transaction != NULL &&
           pxa_ui_builder_node(builder, node, 0, PXA_UI_NODE_VIRTUAL_LIST) &&
           pxa_ui_set_u32(transaction, node, PXA_UI_PROPERTY_ITEM_COUNT,
                          item_count) &&
           pxa_ui_set_dp(transaction, node, PXA_UI_PROPERTY_ITEM_EXTENT,
                         item_extent_dp) &&
           pxa_ui_set_event_mask(transaction, node,
                                 PXA_UI_EVENT_MASK_VISIBLE_RANGE);
}

static inline int pxa_ui_environment_has(const pxa_ui_environment_t* value,
                                         uint64_t feature) {
    return value != NULL && (value->features & feature) == feature;
}

static inline int pxa_ui_environment_min_width(
    const pxa_ui_environment_t* value, uint32_t logical_width) {
    return value != NULL && value->width >= logical_width;
}

#endif
