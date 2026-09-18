#include "pxsys/reference_layout.h"

#include <string.h>

static uint32_t minimum(uint32_t left, uint32_t right) {
    return left < right ? left : right;
}

static pxsys_rect_t inset_rect(pxsys_rect_t rect, uint32_t horizontal_inset,
                               uint32_t vertical_inset) {
    if (horizontal_inset * 2u >= rect.width)
        horizontal_inset = rect.width > 2u ? (rect.width - 1u) / 2u : 0;
    if (vertical_inset * 2u >= rect.height)
        vertical_inset = rect.height > 2u ? (rect.height - 1u) / 2u : 0;
    rect.x += (int32_t)horizontal_inset;
    rect.y += (int32_t)vertical_inset;
    rect.width -= horizontal_inset * 2u;
    rect.height -= vertical_inset * 2u;
    return rect;
}

pxsys_status_t pxsys_reference_layout_compute(
    const pxsys_display_profile_t* display,
    pxsys_reference_layout_t* output) {
    pxsys_rect_t safe;
    uint32_t shortest;
    uint32_t status_height;
    uint32_t navigation_height;
    uint32_t usable_width;
    uint32_t columns;
    if (output == NULL ||
        pxsys_display_content_rect(display, &safe) != PXSYS_STATUS_OK)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    output->struct_size = sizeof(*output);
    output->viewport.width = display->width;
    output->viewport.height = display->height;

    if (display->shape == PXSYS_DISPLAY_SHAPE_CIRCLE &&
        display->safe_insets.top == 0 && display->safe_insets.right == 0 &&
        display->safe_insets.bottom == 0 && display->safe_insets.left == 0) {
        uint32_t diameter = minimum(display->width, display->height);
        uint32_t square = (diameter * 707u) / 1000u;
        safe.x = (int32_t)((display->width - square) / 2u);
        safe.y = (int32_t)((display->height - square) / 2u);
        safe.width = square;
        safe.height = square;
    }
    output->safe_area = safe;
    shortest = minimum(safe.width, safe.height);
    if (shortest <= 240u) {
        output->size_class = PXSYS_UI_SIZE_COMPACT;
        output->outer_padding = 8;
        output->item_gap = 8;
        output->tile_min_width = 118;
        status_height = 20;
        navigation_height = 30;
    } else if (shortest <= 480u) {
        output->size_class = PXSYS_UI_SIZE_REGULAR;
        output->outer_padding = 12;
        output->item_gap = 10;
        output->tile_min_width = 150;
        status_height = 24;
        navigation_height = 38;
    } else {
        output->size_class = PXSYS_UI_SIZE_EXPANDED;
        output->outer_padding = 16;
        output->item_gap = 12;
        output->tile_min_width = 180;
        status_height = 28;
        navigation_height = 44;
    }
    if (status_height + navigation_height + 1u >= safe.height) {
        status_height = minimum(status_height, safe.height / 5u);
        navigation_height = minimum(navigation_height, safe.height / 4u);
    }
    /* Chrome backgrounds extend to the physical display edges. Only their
     * content and the application interaction area are constrained by safe. */
    output->status_bar = output->viewport;
    output->status_bar.height = (uint32_t)safe.y + status_height;
    output->navigation_bar = output->viewport;
    output->navigation_bar.y = safe.y + (int32_t)safe.height -
                               (int32_t)navigation_height;
    output->navigation_bar.height = display->height -
                                    (uint32_t)output->navigation_bar.y;
    output->content = safe;
    output->content.y += (int32_t)status_height;
    output->content.height -= status_height + navigation_height;
    /* Horizontal padding protects rounded edges. Vertical padding can be
     * tighter because status/navigation geometry already protects those
     * edges, which matters on short embedded displays. */
    output->content = inset_rect(output->content, output->outer_padding,
                                 output->outer_padding / 2u);

    usable_width = output->content.width + output->item_gap;
    columns = usable_width / (output->tile_min_width + output->item_gap);
    if (columns == 0) columns = 1;
    if (columns > 6) columns = 6;
    if (display->shape == PXSYS_DISPLAY_SHAPE_CIRCLE && columns > 2) columns = 2;
    output->grid_columns = (uint8_t)columns;
    return PXSYS_STATUS_OK;
}

uint32_t pxsys_reference_layout_gesture_strip_height(
    pxsys_ui_size_class_t size_class) {
    return size_class == PXSYS_UI_SIZE_COMPACT
               ? 20u
               : size_class == PXSYS_UI_SIZE_REGULAR ? 24u : 28u;
}

uint32_t pxsys_reference_layout_back_gesture_width(void) { return 16u; }
