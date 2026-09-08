#include "ui/ui_internal.h"
#include "common/bytes_internal.h"

#include <limits.h>

static int utf8_valid(const uint8_t *data, size_t size) {
    return pxa_utf8_validate(data, size,
                             PXA_UTF8_REJECT_C0 |
                                 PXA_UTF8_ALLOW_TEXT_WHITESPACE);
}

static int package_path_valid(const uint8_t *data, size_t size) {
    size_t index;
    size_t segment = 0;
    if (data == NULL || size == 0 || data[0] == '/' || data[size - 1u] == '/')
        return 0;
    for (index = 0; index < size; ++index) {
        uint8_t value = data[index];
        if (value == '/') {
            if (segment == 0 ||
                (segment == 1 && data[index - 1u] == '.') ||
                (segment == 2 && data[index - 1u] == '.' &&
                 data[index - 2u] == '.'))
                return 0;
            segment = 0;
        } else {
            if (!((value >= 'a' && value <= 'z') ||
                  (value >= 'A' && value <= 'Z') ||
                  (value >= '0' && value <= '9') || value == '.' ||
                  value == '_' || value == '-'))
                return 0;
            ++segment;
        }
    }
    return !(segment == 1 && data[size - 1u] == '.') &&
           !(segment == 2 && data[size - 1u] == '.' &&
             data[size - 2u] == '.');
}

int pxa_ui_node_type_valid(uint8_t type, uint8_t subtype,
                              pxa_ui_features_t features) {
    if (type < PXA_UI_NODE_ROOT || type > PXA_UI_NODE_MEDIA_SURFACE)
        return 0;
    if (type == PXA_UI_NODE_CONTROL) {
        if (subtype < PXA_UI_CONTROL_BUTTON ||
            subtype > PXA_UI_CONTROL_SELECTION)
            return 0;
    } else if (subtype != PXA_UI_CONTROL_NONE) {
        return 0;
    }
    if (type == PXA_UI_NODE_CANVAS &&
        (features & PXA_UI_FEATURE_CANVAS) == 0)
        return 0;
    if (type == PXA_UI_NODE_VIRTUAL_LIST &&
        (features & PXA_UI_FEATURE_VIRTUAL_LIST) == 0)
        return 0;
    if (type == PXA_UI_NODE_MEDIA_SURFACE &&
        (features & PXA_UI_FEATURE_MEDIA_SURFACE) == 0)
        return 0;
    return 1;
}

static int length_valid(pxa_bytes_t value, int signed_value) {
    uint8_t unit;
    int32_t amount;
    if (value.size != 8 || value.data[1] != 0 || value.data[2] != 0 ||
        value.data[3] != 0)
        return 0;
    unit = value.data[0];
    amount = (int32_t)pxa_read_u32(value.data + 4);
    if (unit > PXA_UI_LENGTH_VIEWPORT_HEIGHT_Q16) return 0;
    if ((unit == PXA_UI_LENGTH_AUTO || unit == PXA_UI_LENGTH_CONTENT ||
         unit == PXA_UI_LENGTH_FILL) && amount != 0)
        return 0;
    if ((unit == PXA_UI_LENGTH_PERCENT_Q16 ||
         unit == PXA_UI_LENGTH_VIEWPORT_WIDTH_Q16 ||
         unit == PXA_UI_LENGTH_VIEWPORT_HEIGHT_Q16) &&
        (amount < 0 || amount > (INT32_C(100) << 16)))
        return 0;
    if (!signed_value && unit == PXA_UI_LENGTH_LOGICAL_PX && amount < 0)
        return 0;
    return 1;
}

static int color_valid(pxa_bytes_t value) {
    return value.size == 8 && value.data[0] <= 1 && value.data[2] == 0 &&
           value.data[3] == 0 &&
           (value.data[0] != 0 || value.data[1] < 32);
}

static int property_allowed(pxa_ui_property_t property,
                            pxa_ui_node_type_t type,
                            pxa_ui_control_type_t subtype) {
    switch (property) {
        case PXA_UI_PROPERTY_TEXT:
            return type == PXA_UI_NODE_TEXT ||
                   (type == PXA_UI_NODE_CONTROL &&
                    (subtype == PXA_UI_CONTROL_BUTTON ||
                     subtype == PXA_UI_CONTROL_TEXT_INPUT ||
                     subtype == PXA_UI_CONTROL_SELECTION));
        case PXA_UI_PROPERTY_ICON:
            return type == PXA_UI_NODE_TEXT ||
                   (type == PXA_UI_NODE_CONTROL &&
                    subtype == PXA_UI_CONTROL_BUTTON);
        case PXA_UI_PROPERTY_ASSET:
        case PXA_UI_PROPERTY_IMAGE_FIT:
            return type == PXA_UI_NODE_IMAGE;
        case PXA_UI_PROPERTY_VALUE:
            return type == PXA_UI_NODE_PROGRESS ||
                   type == PXA_UI_NODE_CONTROL;
        case PXA_UI_PROPERTY_MIN_VALUE:
        case PXA_UI_PROPERTY_MAX_VALUE:
            return type == PXA_UI_NODE_PROGRESS ||
                   (type == PXA_UI_NODE_CONTROL &&
                    subtype == PXA_UI_CONTROL_SLIDER);
        case PXA_UI_PROPERTY_STEP:
            return type == PXA_UI_NODE_CONTROL &&
                   subtype == PXA_UI_CONTROL_SLIDER;
        case PXA_UI_PROPERTY_SCROLL_AXIS:
        case PXA_UI_PROPERTY_SCROLLBAR:
        case PXA_UI_PROPERTY_SCROLL_POSITION:
            return type == PXA_UI_NODE_SCROLL ||
                   type == PXA_UI_NODE_VIRTUAL_LIST;
        case PXA_UI_PROPERTY_ITEM_COUNT:
        case PXA_UI_PROPERTY_ITEM_EXTENT:
            return type == PXA_UI_NODE_VIRTUAL_LIST;
        default:
            return 1;
    }
}

pxa_status_t pxa_ui_validate_clear_property(
    pxa_ui_features_t features, uint16_t property,
    pxa_ui_node_type_t node_type, pxa_ui_control_type_t subtype) {
    if (!property_allowed(property, node_type, subtype))
        return PXA_STATUS_INVALID_ARGUMENT;
    if ((property == PXA_UI_PROPERTY_GRID_COLUMNS ||
         property == PXA_UI_PROPERTY_GRID_ROWS ||
         property == PXA_UI_PROPERTY_GRID_CELL) &&
        (features & PXA_UI_FEATURE_GRID) == 0)
        return PXA_STATUS_UNSUPPORTED;
    switch (property) {
        case PXA_UI_PROPERTY_VISIBLE:
        case PXA_UI_PROPERTY_ENABLED:
        case PXA_UI_PROPERTY_EVENT_MASK:
        case PXA_UI_PROPERTY_ACCESSIBILITY_ROLE:
        case PXA_UI_PROPERTY_ACCESSIBILITY_LABEL:
        case PXA_UI_PROPERTY_WIDTH:
        case PXA_UI_PROPERTY_HEIGHT:
        case PXA_UI_PROPERTY_MIN_WIDTH:
        case PXA_UI_PROPERTY_MAX_WIDTH:
        case PXA_UI_PROPERTY_MIN_HEIGHT:
        case PXA_UI_PROPERTY_MAX_HEIGHT:
        case PXA_UI_PROPERTY_LAYOUT:
        case PXA_UI_PROPERTY_WRAP:
        case PXA_UI_PROPERTY_JUSTIFY:
        case PXA_UI_PROPERTY_ALIGN:
        case PXA_UI_PROPERTY_ALIGN_SELF:
        case PXA_UI_PROPERTY_GAP:
        case PXA_UI_PROPERTY_PADDING:
        case PXA_UI_PROPERTY_GROW:
        case PXA_UI_PROPERTY_SHRINK:
        case PXA_UI_PROPERTY_POSITION:
        case PXA_UI_PROPERTY_X:
        case PXA_UI_PROPERTY_Y:
        case PXA_UI_PROPERTY_GRID_COLUMNS:
        case PXA_UI_PROPERTY_GRID_ROWS:
        case PXA_UI_PROPERTY_GRID_CELL:
        case PXA_UI_PROPERTY_VARIANT:
        case PXA_UI_PROPERTY_FOREGROUND:
        case PXA_UI_PROPERTY_BACKGROUND:
        case PXA_UI_PROPERTY_BORDER_COLOR:
        case PXA_UI_PROPERTY_OPACITY:
        case PXA_UI_PROPERTY_RADIUS:
        case PXA_UI_PROPERTY_BORDER_WIDTH:
        case PXA_UI_PROPERTY_FONT_ROLE:
        case PXA_UI_PROPERTY_TEXT_ALIGN:
        case PXA_UI_PROPERTY_SHADOW:
        case PXA_UI_PROPERTY_COMPOSITION:
        case PXA_UI_PROPERTY_TEXT:
        case PXA_UI_PROPERTY_ICON:
        case PXA_UI_PROPERTY_ASSET:
        case PXA_UI_PROPERTY_IMAGE_FIT:
        case PXA_UI_PROPERTY_VALUE:
        case PXA_UI_PROPERTY_MIN_VALUE:
        case PXA_UI_PROPERTY_MAX_VALUE:
        case PXA_UI_PROPERTY_STEP:
        case PXA_UI_PROPERTY_SCROLL_AXIS:
        case PXA_UI_PROPERTY_SCROLLBAR:
        case PXA_UI_PROPERTY_SCROLL_POSITION:
        case PXA_UI_PROPERTY_ITEM_COUNT:
        case PXA_UI_PROPERTY_ITEM_EXTENT:
            return PXA_STATUS_OK;
        default:
            return PXA_STATUS_UNSUPPORTED;
    }
}

pxa_status_t pxa_ui_validate_property(
    pxa_ui_features_t features, uint16_t property,
    pxa_ui_node_type_t node_type, pxa_ui_control_type_t subtype,
    pxa_bytes_t value) {
    if ((value.data == NULL && value.size != 0) ||
        !property_allowed(property, node_type, subtype))
        return PXA_STATUS_INVALID_ARGUMENT;
    if ((property == PXA_UI_PROPERTY_GRID_COLUMNS ||
         property == PXA_UI_PROPERTY_GRID_ROWS ||
         property == PXA_UI_PROPERTY_GRID_CELL) &&
        (features & PXA_UI_FEATURE_GRID) == 0)
        return PXA_STATUS_UNSUPPORTED;
    switch (property) {
        case PXA_UI_PROPERTY_VISIBLE:
        case PXA_UI_PROPERTY_ENABLED:
        case PXA_UI_PROPERTY_WRAP:
            return value.size == 1 && value.data[0] <= 1
                       ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
        case PXA_UI_PROPERTY_EVENT_MASK:
            return value.size == 8 ? PXA_STATUS_OK
                                   : PXA_STATUS_INVALID_ARGUMENT;
        case PXA_UI_PROPERTY_ACCESSIBILITY_ROLE:
        case PXA_UI_PROPERTY_GROW:
        case PXA_UI_PROPERTY_SHRINK:
        case PXA_UI_PROPERTY_VARIANT:
        case PXA_UI_PROPERTY_FONT_ROLE:
            return value.size == 2 ? PXA_STATUS_OK
                                   : PXA_STATUS_INVALID_ARGUMENT;
        case PXA_UI_PROPERTY_ACCESSIBILITY_LABEL:
        case PXA_UI_PROPERTY_TEXT:
            return utf8_valid(value.data, value.size)
                       ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
        case PXA_UI_PROPERTY_WIDTH:
        case PXA_UI_PROPERTY_HEIGHT:
        case PXA_UI_PROPERTY_MIN_WIDTH:
        case PXA_UI_PROPERTY_MAX_WIDTH:
        case PXA_UI_PROPERTY_MIN_HEIGHT:
        case PXA_UI_PROPERTY_MAX_HEIGHT:
            return length_valid(value, 0) ? PXA_STATUS_OK
                                          : PXA_STATUS_INVALID_ARGUMENT;
        case PXA_UI_PROPERTY_X:
        case PXA_UI_PROPERTY_Y:
            return length_valid(value, 1) ? PXA_STATUS_OK
                                          : PXA_STATUS_INVALID_ARGUMENT;
        case PXA_UI_PROPERTY_LAYOUT:
            return value.size == 1 && value.data[0] >= PXA_UI_LAYOUT_ROW &&
                           value.data[0] <= PXA_UI_LAYOUT_GRID &&
                           (value.data[0] != PXA_UI_LAYOUT_GRID ||
                            (features & PXA_UI_FEATURE_GRID) != 0)
                       ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
        case PXA_UI_PROPERTY_JUSTIFY:
        case PXA_UI_PROPERTY_ALIGN:
        case PXA_UI_PROPERTY_ALIGN_SELF:
            return value.size == 1 && value.data[0] <= 5
                       ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
        case PXA_UI_PROPERTY_POSITION:
            return value.size == 1 && value.data[0] <= 1
                       ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
        case PXA_UI_PROPERTY_GAP:
        case PXA_UI_PROPERTY_RADIUS:
        case PXA_UI_PROPERTY_BORDER_WIDTH:
        case PXA_UI_PROPERTY_ITEM_EXTENT:
        case PXA_UI_PROPERTY_SCROLL_POSITION:
            return value.size == 4 && (int32_t)pxa_read_u32(value.data) >= 0
                       ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
        case PXA_UI_PROPERTY_PADDING:
            if (value.size != 16) return PXA_STATUS_INVALID_ARGUMENT;
            for (size_t i = 0; i < 4; ++i)
                if ((int32_t)pxa_read_u32(value.data + i * 4u) < 0)
                    return PXA_STATUS_INVALID_ARGUMENT;
            return PXA_STATUS_OK;
        case PXA_UI_PROPERTY_GRID_COLUMNS:
        case PXA_UI_PROPERTY_GRID_ROWS:
            return value.size >= 8 && value.size % 8u == 0
                       ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
        case PXA_UI_PROPERTY_GRID_CELL:
            return value.size == 8 ? PXA_STATUS_OK
                                   : PXA_STATUS_INVALID_ARGUMENT;
        case PXA_UI_PROPERTY_FOREGROUND:
        case PXA_UI_PROPERTY_BACKGROUND:
        case PXA_UI_PROPERTY_BORDER_COLOR:
            return color_valid(value) ? PXA_STATUS_OK
                                      : PXA_STATUS_INVALID_ARGUMENT;
        case PXA_UI_PROPERTY_OPACITY:
            return value.size == 1 ? PXA_STATUS_OK
                                   : PXA_STATUS_INVALID_ARGUMENT;
        case PXA_UI_PROPERTY_COMPOSITION:
            return value.size == 1 &&
                           value.data[0] <= PXA_UI_COMPOSITION_ALPHA_OVERLAY
                       ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
        case PXA_UI_PROPERTY_TEXT_ALIGN:
        case PXA_UI_PROPERTY_IMAGE_FIT:
        case PXA_UI_PROPERTY_SCROLL_AXIS:
        case PXA_UI_PROPERTY_SCROLLBAR:
            return value.size == 1 && value.data[0] <= 3
                       ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
        case PXA_UI_PROPERTY_SHADOW:
            return value.size == 20 ? PXA_STATUS_OK
                                    : PXA_STATUS_INVALID_ARGUMENT;
        case PXA_UI_PROPERTY_ICON:
        case PXA_UI_PROPERTY_VALUE:
        case PXA_UI_PROPERTY_MIN_VALUE:
        case PXA_UI_PROPERTY_MAX_VALUE:
        case PXA_UI_PROPERTY_STEP:
        case PXA_UI_PROPERTY_ITEM_COUNT:
            return value.size == 4 ? PXA_STATUS_OK
                                   : PXA_STATUS_INVALID_ARGUMENT;
        case PXA_UI_PROPERTY_ASSET:
            return package_path_valid(value.data, value.size)
                       ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
        default:
            return PXA_STATUS_UNSUPPORTED;
    }
}

pxa_status_t pxa_ui_validate_canvas(pxa_ui_features_t features,
                                       const uint8_t *data, size_t size) {
    size_t offset = 0;
    size_t clip_depth = 0;
    if ((features & PXA_UI_FEATURE_CANVAS) == 0)
        return PXA_STATUS_UNSUPPORTED;
    if (data == NULL && size != 0) return PXA_STATUS_INVALID_ARGUMENT;
    while (offset < size) {
        uint8_t type;
        uint8_t flags;
        uint16_t length;
        const uint8_t *value;
        if (size - offset < 4) return PXA_STATUS_INVALID_ARGUMENT;
        type = data[offset];
        flags = data[offset + 1u];
        length = pxa_read_u16(data + offset + 2u);
        offset += 4;
        if (flags != 0 || length > size - offset)
            return PXA_STATUS_INVALID_ARGUMENT;
        value = data + offset;
        switch (type) {
            case PXA_UI_CANVAS_RECT:
                if (length != 28 || pxa_read_u32(value + 8) == 0 ||
                    pxa_read_u32(value + 12) == 0)
                    return PXA_STATUS_INVALID_ARGUMENT;
                break;
            case PXA_UI_CANVAS_ELLIPSE:
                if (length != 26 || pxa_read_u32(value + 8) == 0 ||
                    pxa_read_u32(value + 12) == 0)
                    return PXA_STATUS_INVALID_ARGUMENT;
                break;
            case PXA_UI_CANVAS_LINE:
                if (length != 22 || pxa_read_u16(value + 20) == 0)
                    return PXA_STATUS_INVALID_ARGUMENT;
                break;
            case PXA_UI_CANVAS_ARC:
                if (length != 22 || pxa_read_u32(value + 8) == 0 ||
                    pxa_read_u32(value + 8) > UINT16_MAX ||
                    pxa_read_u16(value + 20) == 0)
                    return PXA_STATUS_INVALID_ARGUMENT;
                break;
            case PXA_UI_CANVAS_TEXT:
                if (length < 18 || value[16] > 3 || value[17] > 2 ||
                    !utf8_valid(value + 18, length - 18u))
                    return PXA_STATUS_INVALID_ARGUMENT;
                break;
            case PXA_UI_CANVAS_TEXT_BOX:
                if (length < 24 || pxa_read_u32(value + 8) == 0 ||
                    pxa_read_u32(value + 12) == 0 || value[20] > 3 ||
                    value[21] > 2 ||
                    value[22] > PXA_UI_CANVAS_TEXT_ALIGN_BOTTOM ||
                    value[23] != 0 || !utf8_valid(value + 24, length - 24u))
                    return PXA_STATUS_INVALID_ARGUMENT;
                break;
            case PXA_UI_CANVAS_IMAGE:
                if (length <= 18 || pxa_read_u32(value + 8) == 0 ||
                    pxa_read_u32(value + 12) == 0 || value[17] > 2 ||
                    !package_path_valid(value + 18, length - 18u))
                    return PXA_STATUS_INVALID_ARGUMENT;
                break;
            case PXA_UI_CANVAS_BITMAP_RGB565: {
                uint32_t width;
                uint32_t height;
                uint32_t stride;
                size_t pixel_bytes;
                if ((features & PXA_UI_FEATURE_RGB565_BITMAP) == 0)
                    return PXA_STATUS_UNSUPPORTED;
                if (length < 20) return PXA_STATUS_INVALID_ARGUMENT;
                width = pxa_read_u32(value + 8);
                height = pxa_read_u32(value + 12);
                stride = pxa_read_u32(value + 16);
                if (width == 0 || width > UINT16_MAX || height == 0 ||
                    height > UINT16_MAX || stride > UINT16_MAX ||
                    width > stride / 2u || height > SIZE_MAX / stride)
                    return PXA_STATUS_INVALID_ARGUMENT;
                pixel_bytes = (size_t)stride * height;
                if (pixel_bytes != (size_t)length - 20u)
                    return PXA_STATUS_INVALID_ARGUMENT;
                break;
            }
            case PXA_UI_CANVAS_CLIP_PUSH:
                if (length != 16 || pxa_read_u32(value + 8) == 0 ||
                    pxa_read_u32(value + 12) == 0 || clip_depth == SIZE_MAX)
                    return PXA_STATUS_INVALID_ARGUMENT;
                ++clip_depth;
                break;
            case PXA_UI_CANVAS_CLIP_POP:
                if (length != 0 || clip_depth == 0)
                    return PXA_STATUS_INVALID_ARGUMENT;
                --clip_depth;
                break;
            default:
                return PXA_STATUS_UNSUPPORTED;
        }
        offset += length;
    }
    return clip_depth == 0 ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
}
