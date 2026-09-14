#include "pxa/lvgl/pxa_lvgl_ui.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "lvgl.h"
#include "src/indev/lv_indev_private.h"

#define PXA_LVGL_UI_MAGIC UINT32_C(0x504c5632)
#define PXA_LVGL_UI_ALIGNMENT ((size_t)16)

typedef struct pxa_lvgl_ui_node pxa_lvgl_ui_node_t;
typedef struct pxa_lvgl_ui_command pxa_lvgl_ui_command_t;
typedef struct pxa_lvgl_ui_transaction pxa_lvgl_ui_transaction_t;
typedef struct pxa_lvgl_ui_canvas_asset pxa_lvgl_ui_canvas_asset_t;

struct pxa_lvgl_ui_canvas_asset {
    pxa_lvgl_ui_canvas_asset_t *next;
    const void *source;
    size_t path_size;
    uint8_t seen;
    uint8_t path[];
};

typedef struct {
    const uint8_t *bytes;
    size_t size;
    lv_area_t *clip_stack;
    size_t clip_capacity;
    lv_image_dsc_t *bitmap_images;
    size_t bitmap_count;
    size_t bitmap_capacity;
    pxa_lvgl_ui_canvas_asset_t *assets;
    pxa_ui_release_fn release;
    void *release_context;
} pxa_lvgl_ui_canvas_t;

struct pxa_lvgl_ui_node {
    pxa_lvgl_ui_node_t *next;
    pxa_lvgl_ui_node_t *previous;
    struct pxa_lvgl_ui *ui;
    lv_obj_t *object;
    lv_obj_t *content;
    pxa_lvgl_ui_canvas_t *canvas;
    const void *asset_source;
    pxa_lvgl_ui_command_t *owner_command;
    uint32_t surface;
    uint32_t id;
    uint64_t event_mask;
    uint32_t foreground;
    uint32_t background;
    uint32_t border;
    uint32_t item_count;
    uint32_t visible_first;
    uint32_t visible_count;
    pxa_ui_node_type_t type;
    pxa_ui_control_type_t subtype;
    uint8_t foreground_kind;
    uint8_t background_kind;
    uint8_t border_kind;
    uint8_t foreground_token;
    uint8_t background_token;
    uint8_t border_token;
    uint8_t font_role;
    uint8_t opacity;
    uint8_t composition;
    uint8_t visible;
    uint8_t justify;
    uint8_t align;
    uint8_t has_scroll_position;
    int32_t minimum;
    int32_t maximum;
    int32_t item_extent;
    int32_t scroll_position;
};

struct pxa_lvgl_ui_command {
    pxa_lvgl_ui_command_t *next;
    pxa_ui_command_view_t view;
    pxa_lvgl_ui_node_t *created;
    uint8_t value[];
};

struct pxa_lvgl_ui_transaction {
    struct pxa_lvgl_ui *ui;
    pxa_ui_transaction_info_t info;
    pxa_lvgl_ui_command_t *commands;
    pxa_lvgl_ui_command_t *tail;
    pxa_status_t status;
};

struct pxa_lvgl_ui {
    uint32_t magic;
    pxa_lvgl_ui_config_t config;
    pxa_lvgl_ui_theme_t theme;
    pxa_ui_environment_t primary_environment;
    pxa_lvgl_ui_node_t *nodes;
    pxa_lvgl_ui_node_t *primary_root;
    uint16_t *alpha_pixels;
    uint8_t *alpha_values;
    int32_t alpha_x;
    int32_t alpha_y;
    uint16_t alpha_width;
    uint16_t alpha_height;
    uint64_t event_timestamp_us;
};

static void *ui_allocate(pxa_lvgl_ui_t *ui, size_t size) {
    if (ui == NULL || size == 0 || ui->config.allocate == NULL) return NULL;
    return ui->config.allocate(ui->config.allocator_context, size);
}

static void ui_release(pxa_lvgl_ui_t *ui, void *memory) {
    if (ui != NULL && memory != NULL && ui->config.release != NULL)
        ui->config.release(ui->config.allocator_context, memory);
}

static void release_asset_source(pxa_lvgl_ui_t *ui, const void *source) {
    if (ui != NULL && source != NULL && ui->config.release_asset != NULL)
        ui->config.release_asset(source, ui->config.asset_user_data);
}

static void release_canvas_assets(pxa_lvgl_ui_t *ui,
                                  pxa_lvgl_ui_canvas_asset_t *assets) {
    while (assets != NULL) {
        pxa_lvgl_ui_canvas_asset_t *next = assets->next;
        release_asset_source(ui, assets->source);
        ui_release(ui, assets);
        assets = next;
    }
}

static pxa_lvgl_ui_canvas_asset_t *find_canvas_asset(
    pxa_lvgl_ui_canvas_asset_t *assets, const uint8_t *path,
    size_t path_size) {
    for (; assets != NULL; assets = assets->next) {
        if (assets->path_size == path_size &&
            memcmp(assets->path, path, path_size) == 0)
            return assets;
    }
    return NULL;
}

static lv_color_t rgba_color(uint32_t rgba) {
    return lv_color_hex(rgba >> 8);
}

static lv_opa_t rgba_opa(uint32_t rgba) {
    return (lv_opa_t)(rgba & UINT32_C(0xff));
}

static uint32_t resolved_color(const pxa_lvgl_ui_t *ui, uint8_t kind,
                               uint8_t token, uint32_t rgba) {
    return kind == 0 && token < 32 ? ui->theme.rgba[token] : rgba;
}

static int32_t logical_pixels(const pxa_lvgl_ui_t *ui, int32_t value) {
    int64_t scaled = (int64_t)value * ui->primary_environment.density_q16;
    scaled /= (INT64_C(64) << 16);
    if (scaled > LV_COORD_MAX) return LV_COORD_MAX;
    if (scaled < -LV_COORD_MAX) return -LV_COORD_MAX;
    return (int32_t)scaled;
}

static int32_t canvas_pixels(const pxa_lvgl_ui_t *ui, int64_t value) {
    int64_t density = ui->primary_environment.density_q16;
    int64_t scaled;
    if (value > 0 && value > INT64_MAX / density) return LV_COORD_MAX;
    if (value < 0 && value < INT64_MIN / density) return -LV_COORD_MAX;
    scaled = value * density;
    scaled /= INT64_C(1) << 16;
    if (scaled > LV_COORD_MAX) return LV_COORD_MAX;
    if (scaled < -LV_COORD_MAX) return -LV_COORD_MAX;
    return (int32_t)scaled;
}

static int32_t canvas_logical_pixels(const pxa_lvgl_ui_t *ui,
                                     int32_t value) {
    int64_t scaled = (int64_t)value * (INT64_C(1) << 16);
    scaled /= ui->primary_environment.density_q16;
    if (scaled > INT32_MAX) return INT32_MAX;
    if (scaled < INT32_MIN) return INT32_MIN;
    return (int32_t)scaled;
}

static int32_t canvas_end_pixels(const pxa_lvgl_ui_t *ui, int64_t value) {
    int32_t result = canvas_pixels(ui, value);
    int64_t density = ui->primary_environment.density_q16;
    if (value > 0 && value <= INT64_MAX / density && result < LV_COORD_MAX &&
        (value * density) % (INT64_C(1) << 16) != 0)
        ++result;
    return result;
}

static int32_t viewport_length(const pxa_lvgl_ui_t *ui, int32_t fraction_q16,
                               uint32_t logical_extent) {
    int64_t extent = canvas_pixels(ui, logical_extent);
    int64_t scaled = extent * fraction_q16 / (INT64_C(1) << 16);
    if (scaled > LV_COORD_MAX) return LV_COORD_MAX;
    if (scaled < -LV_COORD_MAX) return -LV_COORD_MAX;
    return (int32_t)scaled;
}

static int32_t length_value(const pxa_lvgl_ui_t *ui,
                            const uint8_t *value) {
    uint8_t unit = value[0];
    int32_t amount = (int32_t)pxa_read_u32(value + 4);
    switch (unit) {
        case PXA_UI_LENGTH_LOGICAL_PX:
            return logical_pixels(ui, amount);
        case PXA_UI_LENGTH_PERCENT_Q16:
            return LV_PCT(amount >> 16);
        case PXA_UI_LENGTH_VIEWPORT_WIDTH_Q16:
            return viewport_length(ui, amount,
                                   ui->primary_environment.width);
        case PXA_UI_LENGTH_VIEWPORT_HEIGHT_Q16:
            return viewport_length(ui, amount,
                                   ui->primary_environment.height);
        case PXA_UI_LENGTH_FILL:
            return LV_PCT(100);
        case PXA_UI_LENGTH_AUTO:
        case PXA_UI_LENGTH_CONTENT:
        default:
            return LV_SIZE_CONTENT;
    }
}

static const lv_font_t *font_for_role(const pxa_lvgl_ui_t *ui,
                                      uint16_t role) {
    const void *font =
        role == PXA_UI_FONT_ROLE_CAPTION || role == PXA_UI_FONT_ROLE_LABEL
            ? ui->theme.caption_font
        : role == PXA_UI_FONT_ROLE_TITLE ||
                  role == PXA_UI_FONT_ROLE_HEADLINE ||
                  role == PXA_UI_FONT_ROLE_DISPLAY
            ? ui->theme.title_font
        : role == PXA_UI_FONT_ROLE_ICON ? ui->theme.icon_font
                                        : ui->theme.body_font;
    return font == NULL ? LV_FONT_DEFAULT : (const lv_font_t *)font;
}

static const char *icon_text(uint32_t icon) {
    static const char *const icons[] = {
        "", "\xef\x81\x93", "\xef\x81\x94", "\xef\x80\x95",
        "\xef\x81\x98", "\xef\x81\x99", "\xef\x80\x8c",
        "\xef\x80\x93", "\xef\x80\x8d", "\xef\x81\x9e",
        "\xef\x80\x82", "\xef\x80\x81", "\xef\x80\x84"};
    return icon < sizeof(icons) / sizeof(icons[0]) ? icons[icon] : "?";
}

static uint64_t input_timestamp_us(const pxa_lvgl_ui_t *ui,
                                   const lv_indev_t *input) {
    uint64_t now_us;
    uint64_t age_us;
    uint32_t age_ms;
    if (ui == NULL || ui->config.now_us == NULL) return 0;
    now_us = ui->config.now_us(ui->config.callback_user_data);
    if (input == NULL) return now_us;
    age_ms = lv_tick_diff((uint32_t)(now_us / 1000u), input->timestamp);
    /* Reject timestamps from a different clock domain or stale synthetic
     * events. A real pointer sample reaches the Canvas in the same LVGL pass. */
    if (age_ms > 1000u) return now_us;
    age_us = (uint64_t)age_ms * 1000u;
    return age_us <= now_us ? now_us - age_us : now_us;
}

static void emit_event(pxa_lvgl_ui_node_t *node, lv_indev_t *input,
                       pxa_ui_event_kind_t kind, uint16_t flags,
                       const void *value, size_t value_size) {
    uint64_t previous_timestamp_us;
    if (node->ui->config.event_callback == NULL ||
        (node->event_mask & (UINT64_C(1) << (kind - 1u))) == 0)
        return;
    previous_timestamp_us = node->ui->event_timestamp_us;
    node->ui->event_timestamp_us = input_timestamp_us(node->ui, input);
    node->ui->config.event_callback(node->surface, node->id, kind, flags,
                                    value, value_size,
                                    node->ui->config.callback_user_data);
    node->ui->event_timestamp_us = previous_timestamp_us;
}

static void on_widget_event(lv_event_t *event) {
    pxa_lvgl_ui_node_t *node =
        (pxa_lvgl_ui_node_t *)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    int32_t value;
    if (node == NULL) return;
    if (node->owner_command != NULL)
        node->owner_command->created = NULL;
    if (code == LV_EVENT_CLICKED) {
        emit_event(node, lv_event_get_indev(event), PXA_UI_EVENT_ACTION,
                   PXA_UI_EVENT_FLAG_RELIABLE, NULL, 0);
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        if (node->type != PXA_UI_NODE_CONTROL) return;
        if (node->subtype == PXA_UI_CONTROL_TOGGLE)
            value = lv_obj_has_state(node->object, LV_STATE_CHECKED) ? 1 : 0;
        else if (node->subtype == PXA_UI_CONTROL_SLIDER)
            value = lv_slider_get_value(node->object);
        else
            return;
        emit_event(node, lv_event_get_indev(event),
                   PXA_UI_EVENT_VALUE_CHANGED, 0,
                   &value, sizeof(value));
    } else if (code == LV_EVENT_SCROLL) {
        int32_t scroll_y = lv_obj_get_scroll_y(node->object);
        value = canvas_logical_pixels(node->ui, scroll_y);
        emit_event(node, lv_event_get_indev(event), PXA_UI_EVENT_SCROLL, 0,
                   &value, sizeof(value));
        if (node->type == PXA_UI_NODE_VIRTUAL_LIST &&
            node->item_extent > 0 && node->item_count != 0) {
            uint8_t range[8];
            uint32_t first;
            uint32_t count;
            uint32_t visible;
            uint32_t overscan;
            int32_t viewport = lv_obj_get_content_height(node->object);
            uint64_t end;
            if (scroll_y < 0) scroll_y = 0;
            first = (uint32_t)scroll_y / (uint32_t)node->item_extent;
            visible = viewport <= 0
                          ? 1u
                          : (uint32_t)(viewport + node->item_extent - 1) /
                                (uint32_t)node->item_extent;
            overscan = visible / 2u + 1u;
            first = first > overscan ? first - overscan : 0;
            end = (uint64_t)first + visible + (uint64_t)overscan * 2u;
            if (end > node->item_count) end = node->item_count;
            count = end > first ? (uint32_t)(end - first) : 0;
            if (first != node->visible_first || count != node->visible_count) {
                node->visible_first = first;
                node->visible_count = count;
                pxa_write_u32(range, first);
                pxa_write_u32(range + 4, count);
                emit_event(node, lv_event_get_indev(event),
                           PXA_UI_EVENT_VISIBLE_RANGE, 0, range,
                           sizeof(range));
            }
        }
    }
}

static void on_canvas_draw(lv_event_t *event);
static void on_canvas_pointer(lv_event_t *event);
static void apply_node_colors(pxa_lvgl_ui_node_t *node);
static int node_is_in_alpha_overlay(const pxa_lvgl_ui_t *ui,
                                    const pxa_lvgl_ui_node_t *node);
static int refresh_alpha_plane(pxa_lvgl_ui_t *ui);

static void on_alpha_overlay_state_changed(lv_event_t *event) {
    pxa_lvgl_ui_node_t *node =
        (pxa_lvgl_ui_node_t *)lv_event_get_user_data(event);
    if (node == NULL || node->object == NULL ||
        !node_is_in_alpha_overlay(node->ui, node))
        return;
    (void)refresh_alpha_plane(node->ui);
}

static lv_obj_t *button_content(pxa_lvgl_ui_node_t *node) {
    uint32_t foreground;
    if (node->content != NULL) return node->content;
    node->content = lv_label_create(node->object);
    if (node->content == NULL) return NULL;
    lv_label_set_text(node->content, "");
    lv_obj_set_style_text_font(node->content,
                               font_for_role(node->ui, node->font_role), 0);
    foreground = resolved_color(node->ui, node->foreground_kind,
                                node->foreground_token, node->foreground);
    lv_obj_set_style_text_color(node->content, rgba_color(foreground), 0);
    lv_obj_set_style_text_opa(node->content, rgba_opa(foreground), 0);
    lv_obj_center(node->content);
    return node->content;
}

static void unlink_node(pxa_lvgl_ui_node_t *node) {
    pxa_lvgl_ui_t *ui = node->ui;
    if (node->previous != NULL)
        node->previous->next = node->next;
    else
        ui->nodes = node->next;
    if (node->next != NULL) node->next->previous = node->previous;
    if (ui->primary_root == node) ui->primary_root = NULL;
}

static void on_node_delete(lv_event_t *event) {
    pxa_lvgl_ui_node_t *node =
        (pxa_lvgl_ui_node_t *)lv_event_get_user_data(event);
    if (node == NULL) return;
    node->object = NULL;
    release_asset_source(node->ui, node->asset_source);
    node->asset_source = NULL;
    if (node->canvas != NULL) {
        if (node->canvas->release != NULL && node->canvas->bytes != NULL)
            node->canvas->release(node->canvas->release_context,
                                  (void *)node->canvas->bytes);
        ui_release(node->ui, node->canvas->clip_stack);
        ui_release(node->ui, node->canvas->bitmap_images);
        release_canvas_assets(node->ui, node->canvas->assets);
        ui_release(node->ui, node->canvas);
        node->canvas = NULL;
    }
    unlink_node(node);
    ui_release(node->ui, node);
}

static lv_obj_t *create_object(pxa_lvgl_ui_node_t *node,
                               lv_obj_t *parent) {
    lv_obj_t *object;
    switch (node->type) {
        case PXA_UI_NODE_TEXT:
            object = lv_label_create(parent);
            break;
        case PXA_UI_NODE_IMAGE:
            object = lv_image_create(parent);
            break;
        case PXA_UI_NODE_CONTROL:
            if (node->subtype == PXA_UI_CONTROL_BUTTON)
                object = lv_button_create(parent);
            else if (node->subtype == PXA_UI_CONTROL_TOGGLE)
                object = lv_switch_create(parent);
            else if (node->subtype == PXA_UI_CONTROL_SLIDER)
                object = lv_slider_create(parent);
            else if (node->subtype == PXA_UI_CONTROL_TEXT_INPUT)
                object = lv_textarea_create(parent);
            else
                object = lv_dropdown_create(parent);
            break;
        case PXA_UI_NODE_PROGRESS:
            object = lv_bar_create(parent);
            break;
        default:
            object = lv_obj_create(parent);
            break;
    }
    if (object == NULL) return NULL;
    node->object = object;
    node->visible = 1;
    node->opacity = LV_OPA_COVER;
    node->composition = PXA_UI_COMPOSITION_BASE;
    node->foreground_kind = 0;
    node->foreground_token = 4;
    node->background_kind = 1;
    node->background = UINT32_C(0x00000000);
    node->border_kind = 1;
    node->border = UINT32_C(0x00000000);
    lv_obj_add_event_cb(object, on_node_delete, LV_EVENT_DELETE, node);
    lv_obj_add_event_cb(object, on_widget_event, LV_EVENT_CLICKED, node);
    lv_obj_add_event_cb(object, on_widget_event, LV_EVENT_VALUE_CHANGED, node);
    lv_obj_add_event_cb(object, on_widget_event, LV_EVENT_SCROLL, node);
    lv_obj_add_event_cb(object, on_alpha_overlay_state_changed,
                        LV_EVENT_STATE_CHANGED, node);
    if (node->type == PXA_UI_NODE_CANVAS) {
        lv_obj_add_event_cb(object, on_canvas_draw, LV_EVENT_DRAW_MAIN, node);
        lv_obj_add_event_cb(object, on_canvas_pointer, LV_EVENT_PRESSED, node);
        lv_obj_add_event_cb(object, on_canvas_pointer, LV_EVENT_PRESSING, node);
        lv_obj_add_event_cb(object, on_canvas_pointer, LV_EVENT_RELEASED, node);
        lv_obj_add_event_cb(object, on_canvas_pointer, LV_EVENT_PRESS_LOST, node);
    }
    lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(object, font_for_role(node->ui, 1), 0);
    if (node->type == PXA_UI_NODE_ROOT) {
        node->background_kind = 0;
        node->background_token = 0;
        lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(object, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
        lv_obj_set_flex_flow(object, LV_FLEX_FLOW_COLUMN);
    } else {
        lv_obj_set_size(object, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    }
    if (node->type == PXA_UI_NODE_VIRTUAL_LIST) {
        lv_obj_set_scroll_dir(object, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_AUTO);
        node->content = lv_obj_create(object);
        if (node->content == NULL) return NULL;
        lv_obj_remove_flag(node->content, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_width(node->content, 0, 0);
        lv_obj_set_style_pad_all(node->content, 0, 0);
        lv_obj_set_style_bg_opa(node->content, LV_OPA_TRANSP, 0);
        lv_obj_set_size(node->content, 1, 1);
        lv_obj_set_pos(node->content, 0, 0);
    }
    if (node->type == PXA_UI_NODE_CONTROL &&
        node->subtype == PXA_UI_CONTROL_BUTTON) {
        node->foreground_token = 3;
        node->background_kind = 0;
        node->background_token = 2;
        lv_obj_set_style_radius(object, 6, 0);
        lv_obj_set_style_pad_hor(object, 12, 0);
        lv_obj_set_style_pad_ver(object, 8, 0);
        lv_obj_set_style_shadow_width(object, 0, 0);
        lv_obj_set_style_transform_width(object, 0, LV_STATE_PRESSED);
        lv_obj_set_style_transform_height(object, 0, LV_STATE_PRESSED);
        lv_obj_set_style_recolor_opa(object, LV_OPA_TRANSP,
                                     LV_STATE_PRESSED);
    }
    node->minimum = 0;
    node->maximum = 100;
    apply_node_colors(node);
    return object;
}

static void apply_node_colors(pxa_lvgl_ui_node_t *node) {
    uint32_t fg = resolved_color(node->ui, node->foreground_kind,
                                 node->foreground_token, node->foreground);
    uint32_t bg = resolved_color(node->ui, node->background_kind,
                                 node->background_token, node->background);
    uint32_t border = resolved_color(node->ui, node->border_kind,
                                     node->border_token, node->border);
    lv_obj_set_style_text_color(node->object, rgba_color(fg), 0);
    lv_obj_set_style_text_opa(node->object, rgba_opa(fg), 0);
    lv_obj_set_style_bg_color(node->object, rgba_color(bg), 0);
    lv_obj_set_style_bg_opa(node->object, rgba_opa(bg), 0);
    lv_obj_set_style_border_color(node->object, rgba_color(border), 0);
    lv_obj_set_style_border_opa(node->object, rgba_opa(border), 0);
    if (node->type == PXA_UI_NODE_CONTROL &&
        node->subtype == PXA_UI_CONTROL_BUTTON) {
        lv_obj_set_style_bg_color(node->object,
                                  lv_color_darken(rgba_color(bg), 32),
                                  LV_STATE_PRESSED);
    }
    if (node->content != NULL) {
        lv_obj_set_style_text_color(node->content, rgba_color(fg), 0);
        lv_obj_set_style_text_opa(node->content, rgba_opa(fg), 0);
    }
}

static lv_flex_align_t flex_align(uint8_t value) {
    switch (value) {
        case 1: return LV_FLEX_ALIGN_CENTER;
        case 2: return LV_FLEX_ALIGN_END;
        case 4: return LV_FLEX_ALIGN_SPACE_BETWEEN;
        case 5: return LV_FLEX_ALIGN_SPACE_AROUND;
        default: return LV_FLEX_ALIGN_START;
    }
}

static void apply_color_value(pxa_lvgl_ui_node_t *node,
                              pxa_ui_property_t property,
                              const uint8_t *value) {
    uint8_t kind = value[0];
    uint8_t token = value[1];
    uint32_t rgba = pxa_read_u32(value + 4);
    if (property == PXA_UI_PROPERTY_FOREGROUND) {
        node->foreground_kind = kind;
        node->foreground_token = token;
        node->foreground = rgba;
    } else if (property == PXA_UI_PROPERTY_BACKGROUND) {
        node->background_kind = kind;
        node->background_token = token;
        node->background = rgba;
    } else {
        node->border_kind = kind;
        node->border_token = token;
        node->border = rgba;
    }
    apply_node_colors(node);
}

static void apply_property(pxa_lvgl_ui_node_t *node,
                           pxa_ui_property_t property,
                           pxa_bytes_t value) {
    lv_obj_t *object = node->object;
    const uint8_t *data = value.data;
    int32_t scalar;
    if (object == NULL) return;
    switch (property) {
        case PXA_UI_PROPERTY_VISIBLE:
            node->visible = data[0];
            if (!node->visible) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
            break;
        case PXA_UI_PROPERTY_ENABLED:
            if (data[0]) lv_obj_remove_state(object, LV_STATE_DISABLED);
            else lv_obj_add_state(object, LV_STATE_DISABLED);
            break;
        case PXA_UI_PROPERTY_EVENT_MASK:
            node->event_mask = pxa_read_u64(data);
            if (node->event_mask != 0) lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
            else lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
            break;
        case PXA_UI_PROPERTY_WIDTH:
            lv_obj_set_width(object, length_value(node->ui, data));
            break;
        case PXA_UI_PROPERTY_HEIGHT:
            lv_obj_set_height(object, length_value(node->ui, data));
            break;
        case PXA_UI_PROPERTY_MIN_WIDTH:
            lv_obj_set_style_min_width(object, length_value(node->ui, data), 0);
            break;
        case PXA_UI_PROPERTY_MAX_WIDTH:
            lv_obj_set_style_max_width(object, length_value(node->ui, data), 0);
            break;
        case PXA_UI_PROPERTY_MIN_HEIGHT:
            lv_obj_set_style_min_height(object, length_value(node->ui, data), 0);
            break;
        case PXA_UI_PROPERTY_MAX_HEIGHT:
            lv_obj_set_style_max_height(object, length_value(node->ui, data), 0);
            break;
        case PXA_UI_PROPERTY_LAYOUT:
            if (data[0] == PXA_UI_LAYOUT_ROW)
                lv_obj_set_flex_flow(object, LV_FLEX_FLOW_ROW);
            else if (data[0] == PXA_UI_LAYOUT_COLUMN)
                lv_obj_set_flex_flow(object, LV_FLEX_FLOW_COLUMN);
            else
                lv_obj_set_layout(object, LV_LAYOUT_NONE);
            break;
        case PXA_UI_PROPERTY_WRAP:
            break;
        case PXA_UI_PROPERTY_JUSTIFY:
            node->justify = data[0];
            lv_obj_set_flex_align(object, flex_align(node->justify),
                                  flex_align(node->align),
                                  flex_align(node->align));
            break;
        case PXA_UI_PROPERTY_ALIGN:
            node->align = data[0];
            lv_obj_set_flex_align(object, flex_align(node->justify),
                                  flex_align(node->align),
                                  flex_align(node->align));
            break;
        case PXA_UI_PROPERTY_ALIGN_SELF:
            if (data[0] == 3) lv_obj_set_width(object, LV_PCT(100));
            break;
        case PXA_UI_PROPERTY_GAP:
            scalar = logical_pixels(node->ui, (int32_t)pxa_read_u32(data));
            lv_obj_set_style_pad_row(object, scalar, 0);
            lv_obj_set_style_pad_column(object, scalar, 0);
            break;
        case PXA_UI_PROPERTY_PADDING:
            lv_obj_set_style_pad_left(object, logical_pixels(node->ui,
                (int32_t)pxa_read_u32(data)), 0);
            lv_obj_set_style_pad_top(object, logical_pixels(node->ui,
                (int32_t)pxa_read_u32(data + 4)), 0);
            lv_obj_set_style_pad_right(object, logical_pixels(node->ui,
                (int32_t)pxa_read_u32(data + 8)), 0);
            lv_obj_set_style_pad_bottom(object, logical_pixels(node->ui,
                (int32_t)pxa_read_u32(data + 12)), 0);
            break;
        case PXA_UI_PROPERTY_GROW:
            lv_obj_set_flex_grow(object,
                pxa_read_u16(data) > UINT8_MAX ? UINT8_MAX : pxa_read_u16(data));
            break;
        case PXA_UI_PROPERTY_SHRINK:
            break;
        case PXA_UI_PROPERTY_POSITION:
            if (data[0]) lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
            else lv_obj_remove_flag(object, LV_OBJ_FLAG_FLOATING);
            break;
        case PXA_UI_PROPERTY_X:
            lv_obj_set_x(object, length_value(node->ui, data));
            break;
        case PXA_UI_PROPERTY_Y:
            lv_obj_set_y(object, length_value(node->ui, data));
            break;
        case PXA_UI_PROPERTY_FOREGROUND:
        case PXA_UI_PROPERTY_BACKGROUND:
        case PXA_UI_PROPERTY_BORDER_COLOR:
            apply_color_value(node, property, data);
            break;
        case PXA_UI_PROPERTY_OPACITY:
            node->opacity = data[0];
            lv_obj_set_style_opa(object,
                                 node->composition ==
                                         PXA_UI_COMPOSITION_ALPHA_OVERLAY
                                     ? LV_OPA_TRANSP : node->opacity,
                                 0);
            break;
        case PXA_UI_PROPERTY_COMPOSITION:
            node->composition = data[0];
            lv_obj_set_style_opa(object,
                                 node->composition ==
                                         PXA_UI_COMPOSITION_ALPHA_OVERLAY
                                     ? LV_OPA_TRANSP : node->opacity,
                                 0);
            break;
        case PXA_UI_PROPERTY_RADIUS:
            lv_obj_set_style_radius(object, logical_pixels(node->ui,
                (int32_t)pxa_read_u32(data)), 0);
            break;
        case PXA_UI_PROPERTY_BORDER_WIDTH:
            lv_obj_set_style_border_width(object, logical_pixels(node->ui,
                (int32_t)pxa_read_u32(data)), 0);
            break;
        case PXA_UI_PROPERTY_FONT_ROLE:
            node->font_role = (uint8_t)pxa_read_u16(data);
            lv_obj_set_style_text_font(object,
                                       font_for_role(node->ui, node->font_role), 0);
            if (node->content != NULL)
                lv_obj_set_style_text_font(
                    node->content, font_for_role(node->ui, node->font_role), 0);
            break;
        case PXA_UI_PROPERTY_TEXT_ALIGN:
            lv_obj_set_style_text_align(object,
                data[0] == 1 ? LV_TEXT_ALIGN_CENTER
                : data[0] == 2 ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);
            break;
        case PXA_UI_PROPERTY_TEXT:
            if (node->type == PXA_UI_NODE_TEXT)
                lv_label_set_text(object, value.size == 0 ? "" : (const char *)data);
            else if (node->subtype == PXA_UI_CONTROL_BUTTON) {
                lv_obj_t *content = button_content(node);
                if (content != NULL)
                    lv_label_set_text(content,
                                      value.size == 0 ? "" : (const char *)data);
            }
            else if (node->subtype == PXA_UI_CONTROL_TEXT_INPUT)
                lv_textarea_set_text(object,
                                     value.size == 0 ? "" : (const char *)data);
            else if (node->subtype == PXA_UI_CONTROL_SELECTION)
                lv_dropdown_set_options(object,
                                        value.size == 0 ? "" : (const char *)data);
            break;
        case PXA_UI_PROPERTY_ICON:
            node->font_role = 3;
            if (node->type == PXA_UI_NODE_TEXT) {
                lv_obj_set_style_text_font(
                    object, font_for_role(node->ui, node->font_role), 0);
                lv_label_set_text(object, icon_text(pxa_read_u32(data)));
            } else if (node->subtype == PXA_UI_CONTROL_BUTTON) {
                lv_obj_t *content = button_content(node);
                if (content != NULL) {
                    lv_obj_set_style_text_font(
                        content, font_for_role(node->ui, node->font_role), 0);
                    lv_label_set_text(content, icon_text(pxa_read_u32(data)));
                }
            }
            break;
        case PXA_UI_PROPERTY_ASSET:
            if (node->ui->config.resolve_asset != NULL) {
                const void *source = node->ui->config.resolve_asset(
                    data, value.size, node->ui->config.asset_user_data);
                if (source != NULL) {
                    const void *previous = node->asset_source;
                    lv_image_set_src(object, source);
                    node->asset_source = source;
                    release_asset_source(node->ui, previous);
                }
            }
            break;
        case PXA_UI_PROPERTY_IMAGE_FIT:
            lv_image_set_inner_align(object,
                data[0] == PXA_UI_IMAGE_FIT_STRETCH ? LV_IMAGE_ALIGN_STRETCH
                : data[0] == PXA_UI_IMAGE_FIT_COVER ? LV_IMAGE_ALIGN_COVER
                                                    : LV_IMAGE_ALIGN_CONTAIN);
            break;
        case PXA_UI_PROPERTY_VALUE:
            scalar = (int32_t)pxa_read_u32(data);
            if (node->type == PXA_UI_NODE_PROGRESS)
                lv_bar_set_value(object, scalar, LV_ANIM_OFF);
            else if (node->subtype == PXA_UI_CONTROL_TOGGLE) {
                if (scalar) lv_obj_add_state(object, LV_STATE_CHECKED);
                else lv_obj_remove_state(object, LV_STATE_CHECKED);
            } else if (node->subtype == PXA_UI_CONTROL_SLIDER)
                lv_slider_set_value(object, scalar, LV_ANIM_OFF);
            break;
        case PXA_UI_PROPERTY_MIN_VALUE:
            node->minimum = (int32_t)pxa_read_u32(data);
            if (node->type == PXA_UI_NODE_PROGRESS)
                lv_bar_set_range(object, node->minimum, node->maximum);
            else if (node->subtype == PXA_UI_CONTROL_SLIDER)
                lv_slider_set_range(object, node->minimum, node->maximum);
            break;
        case PXA_UI_PROPERTY_MAX_VALUE:
            node->maximum = (int32_t)pxa_read_u32(data);
            if (node->type == PXA_UI_NODE_PROGRESS)
                lv_bar_set_range(object, node->minimum, node->maximum);
            else if (node->subtype == PXA_UI_CONTROL_SLIDER)
                lv_slider_set_range(object, node->minimum, node->maximum);
            break;
        case PXA_UI_PROPERTY_STEP:
            break;
        case PXA_UI_PROPERTY_SCROLL_AXIS:
            lv_obj_set_scroll_dir(object,
                data[0] == 1 ? LV_DIR_HOR : data[0] == 2 ? LV_DIR_VER
                : data[0] == 3 ? LV_DIR_ALL : LV_DIR_NONE);
            break;
        case PXA_UI_PROPERTY_SCROLLBAR:
            lv_obj_set_scrollbar_mode(object,
                data[0] == 0 ? LV_SCROLLBAR_MODE_OFF
                : data[0] == 1 ? LV_SCROLLBAR_MODE_AUTO
                               : LV_SCROLLBAR_MODE_ACTIVE);
            break;
        case PXA_UI_PROPERTY_SCROLL_POSITION:
            node->scroll_position = (int32_t)pxa_read_u32(data);
            node->has_scroll_position = 1;
            break;
        case PXA_UI_PROPERTY_ITEM_COUNT:
            node->item_count = pxa_read_u32(data);
            if (node->type == PXA_UI_NODE_VIRTUAL_LIST &&
                node->content != NULL) {
                int64_t total = (int64_t)node->item_count * node->item_extent;
                if (total < 1) total = 1;
                if (total > LV_COORD_MAX) total = LV_COORD_MAX;
                lv_obj_set_height(node->content, (int32_t)total);
            }
            break;
        case PXA_UI_PROPERTY_ITEM_EXTENT:
            node->item_extent = logical_pixels(
                node->ui, (int32_t)pxa_read_u32(data));
            if (node->type == PXA_UI_NODE_VIRTUAL_LIST &&
                node->content != NULL) {
                int64_t total = (int64_t)node->item_count * node->item_extent;
                if (total < 1) total = 1;
                if (total > LV_COORD_MAX) total = LV_COORD_MAX;
                lv_obj_set_height(node->content, (int32_t)total);
            }
            break;
        default:
            break;
    }
}

static void clear_property(pxa_lvgl_ui_node_t *node,
                           pxa_ui_property_t property) {
    uint8_t value[16] = {0};
    switch (property) {
        case PXA_UI_PROPERTY_ASSET:
            if (node->object != NULL) lv_image_set_src(node->object, NULL);
            release_asset_source(node->ui, node->asset_source);
            node->asset_source = NULL;
            break;
        case PXA_UI_PROPERTY_VISIBLE:
        case PXA_UI_PROPERTY_ENABLED:
            value[0] = 1;
            apply_property(node, property, (pxa_bytes_t){value, 1});
            break;
        case PXA_UI_PROPERTY_EVENT_MASK:
            apply_property(node, property, (pxa_bytes_t){value, 8});
            break;
        case PXA_UI_PROPERTY_WIDTH:
        case PXA_UI_PROPERTY_HEIGHT:
        case PXA_UI_PROPERTY_MIN_WIDTH:
        case PXA_UI_PROPERTY_MAX_WIDTH:
        case PXA_UI_PROPERTY_MIN_HEIGHT:
        case PXA_UI_PROPERTY_MAX_HEIGHT:
        case PXA_UI_PROPERTY_X:
        case PXA_UI_PROPERTY_Y:
            value[0] = node->type == PXA_UI_NODE_ROOT
                           ? PXA_UI_LENGTH_FILL
                           : PXA_UI_LENGTH_CONTENT;
            apply_property(node, property, (pxa_bytes_t){value, 8});
            break;
        case PXA_UI_PROPERTY_FOREGROUND:
            value[1] = 4;
            apply_property(node, property, (pxa_bytes_t){value, 8});
            break;
        case PXA_UI_PROPERTY_BACKGROUND:
        case PXA_UI_PROPERTY_BORDER_COLOR:
            value[0] = 1;
            apply_property(node, property, (pxa_bytes_t){value, 8});
            break;
        case PXA_UI_PROPERTY_OPACITY:
            value[0] = 255;
            apply_property(node, property, (pxa_bytes_t){value, 1});
            break;
        case PXA_UI_PROPERTY_COMPOSITION:
            apply_property(node, property, (pxa_bytes_t){value, 1});
            break;
        case PXA_UI_PROPERTY_PADDING:
            apply_property(node, property, (pxa_bytes_t){value, 16});
            break;
        case PXA_UI_PROPERTY_TEXT:
            apply_property(node, property, (pxa_bytes_t){value, 0});
            break;
        default:
            apply_property(node, property, (pxa_bytes_t){value, 4});
            break;
    }
}

static void link_node(pxa_lvgl_ui_t *ui, pxa_lvgl_ui_node_t *node) {
    node->next = ui->nodes;
    if (ui->nodes != NULL) ui->nodes->previous = node;
    ui->nodes = node;
}

static uint8_t expand5(uint16_t value) {
    return (uint8_t)((value << 3) | (value >> 2));
}

static uint8_t expand6(uint16_t value) {
    return (uint8_t)((value << 2) | (value >> 4));
}

static uint16_t pack565(uint8_t red, uint8_t green, uint8_t blue) {
    return (uint16_t)(((uint16_t)(red & 0xf8u) << 8) |
                      ((uint16_t)(green & 0xfcu) << 3) | (blue >> 3));
}

static int overlay_has_overlay_parent(const pxa_lvgl_ui_t *ui,
                                      const pxa_lvgl_ui_node_t *node) {
    const lv_obj_t *parent = lv_obj_get_parent(node->object);
    for (; parent != NULL; parent = lv_obj_get_parent(parent)) {
        const pxa_lvgl_ui_node_t *candidate;
        for (candidate = ui->nodes; candidate != NULL;
             candidate = candidate->next) {
            if (candidate->object == parent &&
                candidate->composition == PXA_UI_COMPOSITION_ALPHA_OVERLAY)
                return 1;
        }
    }
    return 0;
}

static int node_is_in_alpha_overlay(const pxa_lvgl_ui_t *ui,
                                    const pxa_lvgl_ui_node_t *node) {
    return node->composition == PXA_UI_COMPOSITION_ALPHA_OVERLAY ||
           overlay_has_overlay_parent(ui, node);
}

/* Alpha-overlay objects remain present for LVGL hit testing, but their visual
 * output is supplied only by the compositor's alpha plane. */
static void set_alpha_overlay_base_visibility(pxa_lvgl_ui_t *ui,
                                              int snapshot_visible) {
    pxa_lvgl_ui_node_t *node;
    for (node = ui->nodes; node != NULL; node = node->next) {
        if (node->object == NULL ||
            !node_is_in_alpha_overlay(ui, node))
            continue;
        if (snapshot_visible) {
            lv_obj_set_style_opa(node->object, node->opacity, 0);
            lv_obj_remove_local_style_prop(node->object,
                                           LV_STYLE_OUTLINE_OPA, 0);
            lv_obj_remove_local_style_prop(node->object,
                                           LV_STYLE_SHADOW_OPA, 0);
            lv_obj_remove_local_style_prop(node->object,
                                           LV_STYLE_IMAGE_OPA, 0);
            apply_node_colors(node);
            continue;
        }
        lv_obj_set_style_opa(node->object, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(node->object, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(node->object, LV_OPA_TRANSP, 0);
        lv_obj_set_style_outline_opa(node->object, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_opa(node->object, LV_OPA_TRANSP, 0);
        lv_obj_set_style_image_opa(node->object, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_opa(node->object, LV_OPA_TRANSP, 0);
        if (node->content != NULL)
            lv_obj_set_style_text_opa(node->content, LV_OPA_TRANSP, 0);
    }
}

static void clear_alpha_plane(pxa_lvgl_ui_t *ui) {
    ui_release(ui, ui->alpha_pixels);
    ui_release(ui, ui->alpha_values);
    ui->alpha_pixels = NULL;
    ui->alpha_values = NULL;
    ui->alpha_x = 0;
    ui->alpha_y = 0;
    ui->alpha_width = 0;
    ui->alpha_height = 0;
}

static int prepare_alpha_plane(pxa_lvgl_ui_t *ui, const lv_area_t *area) {
    const int32_t width = lv_area_get_width(area);
    const int32_t height = lv_area_get_height(area);
    const size_t pixels = (size_t)width * height;
    uint16_t *colors;
    uint8_t *alpha;
    if (width <= 0 || height <= 0 || width > UINT16_MAX ||
        height > UINT16_MAX || pixels > SIZE_MAX / sizeof(*colors))
        return 0;
    if (ui->alpha_pixels != NULL && ui->alpha_values != NULL &&
        ui->alpha_x == area->x1 && ui->alpha_y == area->y1 &&
        ui->alpha_width == (uint16_t)width &&
        ui->alpha_height == (uint16_t)height) {
        memset(ui->alpha_pixels, 0, pixels * sizeof(*colors));
        memset(ui->alpha_values, 0, pixels);
        return 1;
    }
    colors = ui_allocate(ui, pixels * sizeof(*colors));
    alpha = ui_allocate(ui, pixels);
    if (colors == NULL || alpha == NULL) {
        ui_release(ui, colors);
        ui_release(ui, alpha);
        return 0;
    }
    clear_alpha_plane(ui);
    ui->alpha_pixels = colors;
    ui->alpha_values = alpha;
    ui->alpha_x = area->x1;
    ui->alpha_y = area->y1;
    ui->alpha_width = (uint16_t)width;
    ui->alpha_height = (uint16_t)height;
    memset(colors, 0, pixels * sizeof(*colors));
    memset(alpha, 0, pixels);
    return 1;
}

static void alpha_plane_blend_pixel(pxa_lvgl_ui_t *ui, size_t index,
                                    lv_color32_t source) {
    const uint8_t source_alpha = source.alpha;
    const uint8_t destination_alpha = ui->alpha_values[index];
    const uint16_t destination = ui->alpha_pixels[index];
    const uint16_t inverse = (uint16_t)(255u - source_alpha);
    const uint16_t output_alpha = (uint16_t)(source_alpha +
        (destination_alpha * inverse + 127u) / 255u);
    uint32_t red;
    uint32_t green;
    uint32_t blue;
    if (source_alpha == 0) return;
    if (output_alpha == 0) return;
    red = (uint32_t)source.red * source_alpha +
          ((uint32_t)expand5(destination >> 11) * destination_alpha *
           inverse + 127u) / 255u;
    green = (uint32_t)source.green * source_alpha +
            ((uint32_t)expand6((destination >> 5) & 0x3fu) *
             destination_alpha * inverse + 127u) / 255u;
    blue = (uint32_t)source.blue * source_alpha +
           ((uint32_t)expand5(destination & 0x1fu) * destination_alpha *
            inverse + 127u) / 255u;
    ui->alpha_pixels[index] = pack565((uint8_t)(red / output_alpha),
                                      (uint8_t)(green / output_alpha),
                                      (uint8_t)(blue / output_alpha));
    ui->alpha_values[index] = (uint8_t)output_alpha;
}

static int refresh_alpha_plane(pxa_lvgl_ui_t *ui) {
    pxa_lvgl_ui_node_t *node;
    lv_area_t union_area = {0};
    int found = 0;
    int result = 1;
    for (node = ui->nodes; node != NULL; node = node->next) {
        lv_area_t area;
        if (node->object == NULL || !node->visible ||
            node->composition != PXA_UI_COMPOSITION_ALPHA_OVERLAY ||
            overlay_has_overlay_parent(ui, node))
            continue;
        lv_obj_get_coords(node->object, &area);
        if (!found) union_area = area;
        else {
            if (area.x1 < union_area.x1) union_area.x1 = area.x1;
            if (area.y1 < union_area.y1) union_area.y1 = area.y1;
            if (area.x2 > union_area.x2) union_area.x2 = area.x2;
            if (area.y2 > union_area.y2) union_area.y2 = area.y2;
        }
        found = 1;
    }
    if (!found) {
        clear_alpha_plane(ui);
        return 1;
    }
    if (!prepare_alpha_plane(ui, &union_area)) return 0;
    set_alpha_overlay_base_visibility(ui, 1);
    for (node = ui->nodes; node != NULL; node = node->next) {
        lv_draw_buf_t *snapshot;
        lv_area_t area;
        if (node->object == NULL || !node->visible ||
            node->composition != PXA_UI_COMPOSITION_ALPHA_OVERLAY ||
            overlay_has_overlay_parent(ui, node))
            continue;
        snapshot = lv_snapshot_take(node->object, LV_COLOR_FORMAT_ARGB8888);
        if (snapshot == NULL) {
            result = 0;
            break;
        }
        lv_obj_get_coords(node->object, &area);
        for (uint32_t y = 0; y < snapshot->header.h; ++y) {
            const lv_color32_t *source = (const lv_color32_t *)(
                (const uint8_t *)snapshot->data +
                (size_t)y * snapshot->header.stride);
            for (uint32_t x = 0; x < snapshot->header.w; ++x) {
                const int32_t plane_x = area.x1 + (int32_t)x - ui->alpha_x;
                const int32_t plane_y = area.y1 + (int32_t)y - ui->alpha_y;
                if (plane_x >= 0 && plane_y >= 0 &&
                    plane_x < ui->alpha_width && plane_y < ui->alpha_height) {
                    alpha_plane_blend_pixel(
                        ui, (size_t)plane_y * ui->alpha_width + plane_x,
                        source[x]);
                }
            }
        }
        lv_draw_buf_destroy(snapshot);
    }
    set_alpha_overlay_base_visibility(ui, 0);
    return result;
}

static void discard_created(pxa_lvgl_ui_transaction_t *transaction) {
    pxa_lvgl_ui_command_t *command;
    for (command = transaction->commands; command != NULL;
         command = command->next) {
        pxa_lvgl_ui_node_t *node = command->created;
        if (node == NULL) continue;
        command->created = NULL;
        if (node->object != NULL)
            lv_obj_delete(node->object);
        else
            ui_release(transaction->ui, node);
    }
}

static void execute_transaction(void *data) {
    pxa_lvgl_ui_transaction_t *transaction =
        (pxa_lvgl_ui_transaction_t *)data;
    pxa_lvgl_ui_command_t *command;
    pxa_lvgl_ui_node_t *new_root = NULL;
    pxa_lvgl_ui_node_t *old_root =
        (pxa_lvgl_ui_node_t *)transaction->info.target_handle;
    int32_t old_index = -1;
    if (old_root != NULL && old_root->object != NULL)
        old_index = lv_obj_get_index(old_root->object);
    for (command = transaction->commands; command != NULL;
         command = command->next) {
        pxa_lvgl_ui_node_t *node;
        pxa_lvgl_ui_node_t *parent;
        lv_obj_t *parent_object;
        if (command->view.command != PXA_UI_COMMAND_CREATE) continue;
        node = command->created;
        parent = (pxa_lvgl_ui_node_t *)command->view.parent_handle;
        parent_object = parent == NULL ? lv_screen_active() : parent->object;
        if (parent_object == NULL || create_object(node, parent_object) == NULL) {
            transaction->status = PXA_STATUS_RESOURCE_LIMIT;
            discard_created(transaction);
            return;
        }
        link_node(transaction->ui, node);
        if ((transaction->info.kind == PXA_UI_REPLACE_SURFACE &&
             node->type == PXA_UI_NODE_ROOT) ||
            (transaction->info.kind == PXA_UI_REPLACE_SUBTREE &&
             node->id == transaction->info.target))
            new_root = node;
    }
    for (command = transaction->commands; command != NULL;
         command = command->next) {
        pxa_lvgl_ui_node_t *node =
            command->view.command == PXA_UI_COMMAND_CREATE
                ? command->created
                : (pxa_lvgl_ui_node_t *)command->view.node_handle;
        if (node == NULL) continue;
        if (command->view.command == PXA_UI_COMMAND_SET_PROPERTY)
            apply_property(node, command->view.property, command->view.value);
        else if (command->view.command == PXA_UI_COMMAND_CLEAR_PROPERTY)
            clear_property(node, command->view.property);
    }
    for (command = transaction->commands; command != NULL;
         command = command->next) {
        if (command->view.command == PXA_UI_COMMAND_MOVE) {
            pxa_lvgl_ui_node_t *node =
                (pxa_lvgl_ui_node_t *)command->view.node_handle;
            pxa_lvgl_ui_node_t *parent =
                (pxa_lvgl_ui_node_t *)command->view.parent_handle;
            pxa_lvgl_ui_node_t *before =
                (pxa_lvgl_ui_node_t *)command->view.before_handle;
            if (node != NULL && node->object != NULL && parent != NULL &&
                parent->object != NULL) {
                lv_obj_set_parent(node->object, parent->object);
                if (before != NULL && before->object != NULL)
                    lv_obj_move_to_index(node->object,
                                         lv_obj_get_index(before->object));
                else
                    lv_obj_move_to_index(node->object, -1);
            }
        }
    }
    if (transaction->info.kind != PXA_UI_PATCH) {
        if (old_root != NULL && old_root->object != NULL)
            lv_obj_delete(old_root->object);
        if (new_root != NULL && new_root->object != NULL && old_index >= 0)
            lv_obj_move_to_index(new_root->object, old_index);
    } else {
        for (command = transaction->commands; command != NULL;
             command = command->next) {
            if (command->view.command == PXA_UI_COMMAND_REMOVE) {
                pxa_lvgl_ui_node_t *node =
                    (pxa_lvgl_ui_node_t *)command->view.node_handle;
                if (node != NULL && node->object != NULL)
                    lv_obj_delete(node->object);
            }
        }
    }
    for (command = transaction->commands; command != NULL;
         command = command->next) {
        pxa_lvgl_ui_node_t *node = command->created;
        if (node != NULL && node->object != NULL && node->visible)
            lv_obj_remove_flag(node->object, LV_OBJ_FLAG_HIDDEN);
    }
    if (transaction->info.kind == PXA_UI_REPLACE_SURFACE &&
        transaction->info.surface == PXA_UI_PRIMARY_SURFACE)
        transaction->ui->primary_root = new_root;
    lv_obj_update_layout(lv_screen_active());
    for (command = transaction->commands; command != NULL;
         command = command->next) {
        pxa_lvgl_ui_node_t *node = command->view.command == PXA_UI_COMMAND_CREATE
                                       ? command->created
                                       : (pxa_lvgl_ui_node_t *)command->view.node_handle;
        if (node == NULL || node->object == NULL || !node->has_scroll_position)
            continue;
        lv_obj_scroll_to_y(node->object,
                           logical_pixels(node->ui, node->scroll_position),
                           LV_ANIM_OFF);
        node->has_scroll_position = 0;
    }
    transaction->status = refresh_alpha_plane(transaction->ui)
                              ? PXA_STATUS_OK : PXA_STATUS_RESOURCE_LIMIT;
}

static void free_transaction(pxa_lvgl_ui_transaction_t *transaction,
                             int keep_created) {
    pxa_lvgl_ui_command_t *command;
    pxa_lvgl_ui_command_t *next;
    if (!keep_created) discard_created(transaction);
    for (command = transaction->commands; command != NULL; command = next) {
        next = command->next;
        if (keep_created && command->created != NULL) {
            command->created->owner_command = NULL;
            command->created = NULL;
        }
        ui_release(transaction->ui, command);
    }
    ui_release(transaction->ui, transaction);
}

static pxa_status_t backend_begin(
    void *context, const pxa_ui_transaction_info_t *info,
    void **backend_transaction) {
    pxa_lvgl_ui_t *ui = (pxa_lvgl_ui_t *)context;
    pxa_lvgl_ui_transaction_t *transaction;
    if (ui == NULL || ui->magic != PXA_LVGL_UI_MAGIC || info == NULL ||
        backend_transaction == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    transaction = (pxa_lvgl_ui_transaction_t *)ui_allocate(
        ui, sizeof(*transaction));
    if (transaction == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    memset(transaction, 0, sizeof(*transaction));
    transaction->ui = ui;
    transaction->info = *info;
    transaction->status = PXA_STATUS_INTERNAL;
    *backend_transaction = transaction;
    return PXA_STATUS_OK;
}

static pxa_status_t backend_apply(
    void *context, void *backend_transaction,
    const pxa_ui_command_view_t *view, void **created_handle) {
    pxa_lvgl_ui_t *ui = (pxa_lvgl_ui_t *)context;
    pxa_lvgl_ui_transaction_t *transaction =
        (pxa_lvgl_ui_transaction_t *)backend_transaction;
    pxa_lvgl_ui_command_t *command;
    size_t size;
    if (ui == NULL || transaction == NULL || transaction->ui != ui ||
        view == NULL || view->value.size > SIZE_MAX - sizeof(*command))
        return PXA_STATUS_INVALID_ARGUMENT;
    if (view->property == PXA_UI_PROPERTY_GRID_COLUMNS ||
        view->property == PXA_UI_PROPERTY_GRID_ROWS ||
        view->property == PXA_UI_PROPERTY_GRID_CELL)
        return PXA_STATUS_UNSUPPORTED;
    if (view->value.size == SIZE_MAX - sizeof(*command))
        return PXA_STATUS_RESOURCE_LIMIT;
    size = sizeof(*command) + view->value.size + 1u;
    command = (pxa_lvgl_ui_command_t *)ui_allocate(ui, size);
    if (command == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    memset(command, 0, sizeof(*command));
    command->view = *view;
    if (view->value.size != 0) {
        memcpy(command->value, view->value.data, view->value.size);
        command->view.value.data = command->value;
    }
    command->value[view->value.size] = 0;
    if (view->command == PXA_UI_COMMAND_CREATE) {
        command->created = (pxa_lvgl_ui_node_t *)ui_allocate(
            ui, sizeof(*command->created));
        if (command->created == NULL) {
            ui_release(ui, command);
            return PXA_STATUS_RESOURCE_LIMIT;
        }
        memset(command->created, 0, sizeof(*command->created));
        command->created->ui = ui;
        command->created->surface = transaction->info.surface;
        command->created->id = view->node;
        command->created->type = view->type;
        command->created->subtype = view->subtype;
        command->created->visible = 1;
        command->created->owner_command = command;
        if (created_handle != NULL) *created_handle = command->created;
    }
    if (transaction->tail == NULL)
        transaction->commands = command;
    else
        transaction->tail->next = command;
    transaction->tail = command;
    return PXA_STATUS_OK;
}

static pxa_status_t backend_commit(void *context, void *backend_transaction) {
    pxa_lvgl_ui_t *ui = (pxa_lvgl_ui_t *)context;
    pxa_lvgl_ui_transaction_t *transaction =
        (pxa_lvgl_ui_transaction_t *)backend_transaction;
    pxa_status_t status;
    if (ui == NULL || transaction == NULL || transaction->ui != ui)
        return PXA_STATUS_INVALID_ARGUMENT;
    status = ui->config.execute(execute_transaction, transaction,
                                ui->config.execute_user_data);
    if (status != PXA_STATUS_OK) return status;
    status = transaction->status;
    if (status == PXA_STATUS_OK) free_transaction(transaction, 1);
    return status;
}

static void backend_cancel(void *context, void *backend_transaction) {
    pxa_lvgl_ui_t *ui = (pxa_lvgl_ui_t *)context;
    pxa_lvgl_ui_transaction_t *transaction =
        (pxa_lvgl_ui_transaction_t *)backend_transaction;
    if (ui != NULL && transaction != NULL && transaction->ui == ui)
        free_transaction(transaction, 0);
}

typedef struct {
    pxa_lvgl_ui_node_t *node;
    pxa_ui_canvas_view_t view;
    pxa_ui_release_fn release;
    void *release_context;
    pxa_status_t status;
    pxa_lvgl_ui_canvas_asset_t *assets;
    lv_area_t *grown_clips;
    size_t clip_capacity;
    lv_image_dsc_t *grown_bitmap_images;
    size_t bitmap_count;
    uint8_t reuse_assets;
} canvas_swap_t;

static size_t canvas_clip_depth(const uint8_t *data, size_t size) {
    size_t offset = 0;
    size_t depth = 0;
    size_t maximum = 0;
    while (offset + 4u <= size) {
        uint8_t type = data[offset];
        uint16_t length = pxa_read_u16(data + offset + 2u);
        offset += 4u;
        if (length > size - offset) return 0;
        if (type == PXA_UI_CANVAS_CLIP_PUSH) {
            ++depth;
            if (depth > maximum) maximum = depth;
        } else if (type == PXA_UI_CANVAS_CLIP_POP && depth != 0) {
            --depth;
        }
        offset += length;
    }
    return maximum;
}

static size_t canvas_bitmap_count(const uint8_t *data, size_t size) {
    size_t offset = 0;
    size_t count = 0;
    while (offset + 4u <= size) {
        uint8_t type = data[offset];
        uint16_t length = pxa_read_u16(data + offset + 2u);
        offset += 4u;
        if (length > size - offset) return 0;
        if (type == PXA_UI_CANVAS_BITMAP_RGB565 && length > 20u) ++count;
        offset += length;
    }
    return count;
}

static void populate_canvas_bitmaps(pxa_lvgl_ui_canvas_t *canvas) {
    size_t offset = 0;
    size_t index = 0;
    while (offset + 4u <= canvas->size && index < canvas->bitmap_count) {
        uint8_t type = canvas->bytes[offset];
        uint16_t length = pxa_read_u16(canvas->bytes + offset + 2u);
        const uint8_t *value;
        offset += 4u;
        if (length > canvas->size - offset) break;
        value = canvas->bytes + offset;
        offset += length;
        if (type == PXA_UI_CANVAS_BITMAP_RGB565 && length > 20u) {
            lv_image_dsc_t *image = &canvas->bitmap_images[index++];
            memset(image, 0, sizeof(*image));
            image->header.magic = LV_IMAGE_HEADER_MAGIC;
            image->header.cf = LV_COLOR_FORMAT_RGB565;
            image->header.w = (uint16_t)pxa_read_u32(value + 8);
            image->header.h = (uint16_t)pxa_read_u32(value + 12);
            image->header.stride = (uint16_t)pxa_read_u32(value + 16);
            image->data_size = length - 20u;
            image->data = value + 20;
        }
    }
}

static pxa_status_t acquire_canvas_assets(
    pxa_lvgl_ui_node_t *node, const uint8_t *data, size_t size,
    pxa_lvgl_ui_canvas_asset_t **output) {
    pxa_lvgl_ui_canvas_asset_t *assets = NULL;
    size_t offset = 0;
    *output = NULL;
    if (node->ui->config.resolve_asset == NULL) return PXA_STATUS_OK;
    while (offset + 4u <= size) {
        uint8_t type = data[offset];
        uint16_t length = pxa_read_u16(data + offset + 2u);
        const uint8_t *value;
        size_t path_size;
        pxa_lvgl_ui_canvas_asset_t *asset;
        offset += 4u;
        if (length > size - offset) {
            release_canvas_assets(node->ui, assets);
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        value = data + offset;
        if (type != PXA_UI_CANVAS_IMAGE || length <= 18u) {
            offset += length;
            continue;
        }
        path_size = length - 18u;
        if (find_canvas_asset(assets, value + 18u, path_size) != NULL) {
            offset += length;
            continue;
        }
        if (path_size > SIZE_MAX - sizeof(*asset)) {
            release_canvas_assets(node->ui, assets);
            return PXA_STATUS_RESOURCE_LIMIT;
        }
        asset = (pxa_lvgl_ui_canvas_asset_t *)ui_allocate(
            node->ui, sizeof(*asset) + path_size);
        if (asset == NULL) {
            release_canvas_assets(node->ui, assets);
            return PXA_STATUS_RESOURCE_LIMIT;
        }
        asset->source = node->ui->config.resolve_asset(
            value + 18u, path_size, node->ui->config.asset_user_data);
        if (asset->source == NULL) {
            ui_release(node->ui, asset);
            release_canvas_assets(node->ui, assets);
            return PXA_STATUS_RESOURCE_LIMIT;
        }
        asset->path_size = path_size;
        memcpy(asset->path, value + 18u, path_size);
        asset->next = assets;
        assets = asset;
        offset += length;
    }
    *output = assets;
    return PXA_STATUS_OK;
}

static int canvas_assets_match(pxa_lvgl_ui_canvas_asset_t *assets,
                                const uint8_t *data, size_t size) {
    pxa_lvgl_ui_canvas_asset_t *asset;
    size_t offset = 0;
    for (asset = assets; asset != NULL; asset = asset->next) asset->seen = 0;
    while (offset + 4u <= size) {
        uint8_t type = data[offset];
        uint16_t length = pxa_read_u16(data + offset + 2u);
        offset += 4u;
        if (length > size - offset) return 0;
        if (type == PXA_UI_CANVAS_IMAGE && length > 18u) {
            asset = find_canvas_asset(assets, data + offset + 18u, length - 18u);
            if (asset == NULL) return 0;
            asset->seen = 1;
        }
        offset += length;
    }
    for (asset = assets; asset != NULL; asset = asset->next)
        if (!asset->seen) return 0;
    return offset == size;
}

static void execute_canvas_swap(void *data) {
    canvas_swap_t *swap = (canvas_swap_t *)data;
    pxa_lvgl_ui_canvas_t *canvas = swap->node->canvas;
    int first_frame = canvas == NULL;
    if (canvas == NULL) {
        canvas = (pxa_lvgl_ui_canvas_t *)ui_allocate(
            swap->node->ui, sizeof(*canvas));
        if (canvas == NULL) {
            swap->status = PXA_STATUS_RESOURCE_LIMIT;
            return;
        }
        memset(canvas, 0, sizeof(*canvas));
    }
    if (canvas->release != NULL && canvas->bytes != NULL)
        canvas->release(canvas->release_context, (void *)canvas->bytes);
    if (!swap->reuse_assets) {
        release_canvas_assets(swap->node->ui, canvas->assets);
        canvas->assets = swap->assets;
        swap->assets = NULL;
    }
    if (swap->grown_clips != NULL) {
        ui_release(swap->node->ui, canvas->clip_stack);
        canvas->clip_stack = swap->grown_clips;
        canvas->clip_capacity = swap->clip_capacity;
        swap->grown_clips = NULL;
    }
    if (swap->grown_bitmap_images != NULL) {
        ui_release(swap->node->ui, canvas->bitmap_images);
        canvas->bitmap_images = swap->grown_bitmap_images;
        canvas->bitmap_capacity = swap->bitmap_count;
        swap->grown_bitmap_images = NULL;
    }
    canvas->bytes = swap->view.display_list.data;
    canvas->size = swap->view.display_list.size;
    canvas->bitmap_count = swap->bitmap_count;
    populate_canvas_bitmaps(canvas);
    canvas->release = swap->release;
    canvas->release_context = swap->release_context;
    swap->node->canvas = canvas;
    if (first_frame || swap->view.dirty_count == 0) {
        lv_obj_invalidate(swap->node->object);
    } else {
        lv_area_t origin;
        uint8_t index;
        lv_obj_get_coords(swap->node->object, &origin);
        for (index = 0; index < swap->view.dirty_count; ++index) {
            const int32_t *rect = swap->view.dirty_rects[index];
            lv_area_t area;
            area.x1 = origin.x1 + canvas_pixels(swap->node->ui, rect[0]);
            area.y1 = origin.y1 + canvas_pixels(swap->node->ui, rect[1]);
            area.x2 = origin.x1 + canvas_end_pixels(swap->node->ui,
                (int64_t)rect[0] + rect[2]) - 1;
            area.y2 = origin.y1 + canvas_end_pixels(swap->node->ui,
                (int64_t)rect[1] + rect[3]) - 1;
            area.x1 = LV_MAX(area.x1, origin.x1);
            area.y1 = LV_MAX(area.y1, origin.y1);
            area.x2 = LV_MIN(area.x2, origin.x2);
            area.y2 = LV_MIN(area.y2, origin.y2);
            if (area.x1 <= area.x2 && area.y1 <= area.y2)
                lv_obj_invalidate_area(swap->node->object, &area);
        }
    }
    swap->status = PXA_STATUS_OK;
}

static pxa_status_t backend_canvas(
    void *context, const pxa_ui_canvas_view_t *canvas,
    pxa_ui_release_fn release, void *release_context) {
    pxa_lvgl_ui_t *ui = (pxa_lvgl_ui_t *)context;
    pxa_lvgl_ui_node_t *node;
    canvas_swap_t swap;
    pxa_status_t status;
    if (ui == NULL || ui->magic != PXA_LVGL_UI_MAGIC || canvas == NULL ||
        release == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    node = (pxa_lvgl_ui_node_t *)canvas->node_handle;
    if (node == NULL || node->ui != ui || node->object == NULL ||
        node->type != PXA_UI_NODE_CANVAS || node->id != canvas->node)
        return PXA_STATUS_NOT_FOUND;
    memset(&swap, 0, sizeof(swap));
    swap.node = node;
    swap.view = *canvas;
    swap.release = release;
    swap.release_context = release_context;
    swap.status = PXA_STATUS_INTERNAL;
    /* Resource I/O and list preparation do not need the LVGL execution lock. */
    swap.reuse_assets = node->canvas != NULL && canvas_assets_match(
        node->canvas->assets, canvas->display_list.data, canvas->display_list.size);
    if (!swap.reuse_assets) {
        status = acquire_canvas_assets(node, canvas->display_list.data,
                                         canvas->display_list.size, &swap.assets);
        if (status != PXA_STATUS_OK) return status;
    }
    swap.clip_capacity = canvas_clip_depth(canvas->display_list.data,
                                            canvas->display_list.size);
    if (node->canvas == NULL || swap.clip_capacity > node->canvas->clip_capacity) {
        if (swap.clip_capacity > SIZE_MAX / sizeof(*swap.grown_clips)) {
            release_canvas_assets(ui, swap.assets);
            return PXA_STATUS_RESOURCE_LIMIT;
        }
        if (swap.clip_capacity != 0) {
            swap.grown_clips = (lv_area_t *)ui_allocate(
                ui, swap.clip_capacity * sizeof(*swap.grown_clips));
            if (swap.grown_clips == NULL) {
                release_canvas_assets(ui, swap.assets);
                return PXA_STATUS_RESOURCE_LIMIT;
            }
        }
    }
    swap.bitmap_count = canvas_bitmap_count(canvas->display_list.data,
                                             canvas->display_list.size);
    if (node->canvas == NULL ||
        swap.bitmap_count > node->canvas->bitmap_capacity) {
        if (swap.bitmap_count > SIZE_MAX / sizeof(*swap.grown_bitmap_images)) {
            release_canvas_assets(ui, swap.assets);
            ui_release(ui, swap.grown_clips);
            return PXA_STATUS_RESOURCE_LIMIT;
        }
        if (swap.bitmap_count != 0) {
            swap.grown_bitmap_images = (lv_image_dsc_t *)ui_allocate(
                ui, swap.bitmap_count * sizeof(*swap.grown_bitmap_images));
            if (swap.grown_bitmap_images == NULL) {
                release_canvas_assets(ui, swap.assets);
                ui_release(ui, swap.grown_clips);
                return PXA_STATUS_RESOURCE_LIMIT;
            }
        }
    }
    status = ui->config.execute(execute_canvas_swap, &swap,
                                ui->config.execute_user_data);
    release_canvas_assets(ui, swap.assets);
    ui_release(ui, swap.grown_clips);
    ui_release(ui, swap.grown_bitmap_images);
    return status == PXA_STATUS_OK ? swap.status : status;
}

static void execute_reset(void *data) {
    pxa_lvgl_ui_t *ui = (pxa_lvgl_ui_t *)data;
    while (ui->nodes != NULL) {
        pxa_lvgl_ui_node_t *node = ui->nodes;
        if (node->object != NULL)
            lv_obj_delete(node->object);
        else {
            unlink_node(node);
            ui_release(ui, node);
        }
    }
    clear_alpha_plane(ui);
    ui->primary_root = NULL;
}

static void backend_reset(void *context) {
    pxa_lvgl_ui_t *ui = (pxa_lvgl_ui_t *)context;
    if (ui != NULL && ui->magic == PXA_LVGL_UI_MAGIC)
        (void)ui->config.execute(execute_reset, ui, ui->config.execute_user_data);
}

typedef struct {
    pxa_lvgl_ui_t *ui;
    pxa_ui_environment_t environment;
} environment_change_t;

static void execute_environment_change(void *data) {
    environment_change_t *change = (environment_change_t *)data;
    change->ui->primary_environment = change->environment;
    if (change->ui->primary_root != NULL &&
        change->ui->primary_root->object != NULL)
        lv_obj_invalidate(change->ui->primary_root->object);
}

static pxa_status_t backend_environment(
    void *context, const pxa_ui_environment_t *environment) {
    pxa_lvgl_ui_t *ui = (pxa_lvgl_ui_t *)context;
    environment_change_t change;
    if (ui == NULL || environment == NULL ||
        environment->surface != PXA_UI_PRIMARY_SURFACE)
        return PXA_STATUS_UNSUPPORTED;
    change.ui = ui;
    change.environment = *environment;
    return ui->config.execute(execute_environment_change, &change,
                              ui->config.execute_user_data);
}

static void intersect_clip(lv_area_t *target, const lv_area_t *clip) {
    if (target->x1 < clip->x1) target->x1 = clip->x1;
    if (target->y1 < clip->y1) target->y1 = clip->y1;
    if (target->x2 > clip->x2) target->x2 = clip->x2;
    if (target->y2 > clip->y2) target->y2 = clip->y2;
}

static void on_canvas_draw(lv_event_t *event) {
    pxa_lvgl_ui_node_t *node =
        (pxa_lvgl_ui_node_t *)lv_event_get_user_data(event);
    lv_layer_t *layer;
    lv_area_t origin;
    lv_area_t saved_clip;
    const uint8_t *data;
    size_t size;
    size_t offset = 0;
    size_t clip_depth = 0;
    size_t bitmap_index = 0;
    if (node == NULL || node->canvas == NULL) return;
    data = node->canvas->bytes;
    size = node->canvas->size;
    layer = lv_event_get_layer(event);
    lv_obj_get_coords(node->object, &origin);
    saved_clip = layer->_clip_area;
    while (offset + 4u <= size) {
        uint8_t type = data[offset];
        uint16_t length = pxa_read_u16(data + offset + 2u);
        const uint8_t *value;
        offset += 4;
        if (length > size - offset) break;
        value = data + offset;
        offset += length;
        lv_image_dsc_t *bitmap_image = NULL;
        if (type == PXA_UI_CANVAS_BITMAP_RGB565 && length > 20u &&
            bitmap_index < node->canvas->bitmap_count)
            bitmap_image = &node->canvas->bitmap_images[bitmap_index++];
        if ((type == PXA_UI_CANVAS_RECT && length == 28) ||
            (type == PXA_UI_CANVAS_ELLIPSE && length == 26) ||
            (type == PXA_UI_CANVAS_IMAGE && length > 18) ||
            (type == PXA_UI_CANVAS_BITMAP_RGB565 && length > 20)) {
            int32_t x = origin.x1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value));
            int32_t y = origin.y1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value + 4));
            int32_t width = canvas_pixels(node->ui, pxa_read_u32(value + 8));
            int32_t height = canvas_pixels(node->ui, pxa_read_u32(value + 12));
            if (x > layer->_clip_area.x2 || y > layer->_clip_area.y2 ||
                (int64_t)x + width <= layer->_clip_area.x1 ||
                (int64_t)y + height <= layer->_clip_area.y1) continue;
        }
        if (type == PXA_UI_CANVAS_RECT && length == 28) {
            lv_area_t area;
            lv_draw_rect_dsc_t descriptor;
            uint32_t fill = pxa_read_u32(value + 16);
            uint32_t border = pxa_read_u32(value + 24);
            area.x1 = origin.x1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value));
            area.y1 = origin.y1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value + 4));
            area.x2 = area.x1 + canvas_pixels(node->ui,
                pxa_read_u32(value + 8)) - 1;
            area.y2 = area.y1 + canvas_pixels(node->ui,
                pxa_read_u32(value + 12)) - 1;
            lv_draw_rect_dsc_init(&descriptor);
            descriptor.bg_color = rgba_color(fill);
            descriptor.bg_opa = rgba_opa(fill);
            descriptor.radius = canvas_pixels(node->ui,
                                               pxa_read_u16(value + 20));
            descriptor.border_width = canvas_pixels(
                node->ui, pxa_read_u16(value + 22));
            descriptor.border_color = rgba_color(border);
            descriptor.border_opa = rgba_opa(border);
            lv_draw_rect(layer, &descriptor, &area);
        } else if (type == PXA_UI_CANVAS_LINE && length == 22) {
            lv_draw_line_dsc_t descriptor;
            uint32_t color = pxa_read_u32(value + 16);
            lv_draw_line_dsc_init(&descriptor);
            descriptor.p1.x = origin.x1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value));
            descriptor.p1.y = origin.y1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value + 4));
            descriptor.p2.x = origin.x1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value + 8));
            descriptor.p2.y = origin.y1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value + 12));
            descriptor.color = rgba_color(color);
            descriptor.opa = rgba_opa(color);
            descriptor.width = canvas_pixels(node->ui,
                                              pxa_read_u16(value + 20));
            lv_draw_line(layer, &descriptor);
        } else if (type == PXA_UI_CANVAS_ELLIPSE && length == 26) {
            lv_area_t area;
            lv_draw_rect_dsc_t descriptor;
            uint32_t fill = pxa_read_u32(value + 16);
            uint32_t border = pxa_read_u32(value + 22);
            area.x1 = origin.x1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value));
            area.y1 = origin.y1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value + 4));
            area.x2 = area.x1 + canvas_pixels(node->ui,
                pxa_read_u32(value + 8)) - 1;
            area.y2 = area.y1 + canvas_pixels(node->ui,
                pxa_read_u32(value + 12)) - 1;
            lv_draw_rect_dsc_init(&descriptor);
            descriptor.bg_color = rgba_color(fill);
            descriptor.bg_opa = rgba_opa(fill);
            descriptor.radius = LV_RADIUS_CIRCLE;
            descriptor.border_width = canvas_pixels(
                node->ui, pxa_read_u16(value + 20));
            descriptor.border_color = rgba_color(border);
            descriptor.border_opa = rgba_opa(border);
            lv_draw_rect(layer, &descriptor, &area);
        } else if (type == PXA_UI_CANVAS_ARC && length == 22) {
            lv_draw_arc_dsc_t descriptor;
            uint32_t color = pxa_read_u32(value + 16);
            lv_draw_arc_dsc_init(&descriptor);
            descriptor.center.x = origin.x1 + canvas_pixels(
                node->ui, (int32_t)pxa_read_u32(value));
            descriptor.center.y = origin.y1 + canvas_pixels(
                node->ui, (int32_t)pxa_read_u32(value + 4));
            descriptor.radius = canvas_pixels(node->ui,
                                               pxa_read_u32(value + 8));
            descriptor.start_angle = pxa_read_u16(value + 12);
            descriptor.end_angle = pxa_read_u16(value + 14);
            descriptor.color = rgba_color(color);
            descriptor.opa = rgba_opa(color);
            descriptor.width = canvas_pixels(node->ui,
                                              pxa_read_u16(value + 20));
            lv_draw_arc(layer, &descriptor);
        } else if (type == PXA_UI_CANVAS_TEXT && length >= 18) {
            lv_area_t area;
            lv_draw_label_dsc_t descriptor;
            uint32_t color = pxa_read_u32(value + 12);
            area.x1 = origin.x1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value));
            area.y1 = origin.y1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value + 4));
            area.x2 = area.x1 + canvas_pixels(node->ui,
                pxa_read_u32(value + 8)) - 1;
            area.y2 = origin.y2;
            lv_draw_label_dsc_init(&descriptor);
            descriptor.text = (const char *)(value + 18);
            descriptor.text_length = length - 18u;
            descriptor.text_local = 1;
            descriptor.color = rgba_color(color);
            descriptor.opa = rgba_opa(color);
            descriptor.font = font_for_role(node->ui, value[16]);
            descriptor.align = value[17] == 1 ? LV_TEXT_ALIGN_CENTER
                               : value[17] == 2 ? LV_TEXT_ALIGN_RIGHT
                                                : LV_TEXT_ALIGN_LEFT;
            lv_draw_label(layer, &descriptor, &area);
        } else if (type == PXA_UI_CANVAS_TEXT_BOX && length >= 24) {
            lv_area_t area;
            lv_draw_label_dsc_t descriptor;
            const lv_font_t *font = font_for_role(node->ui, value[20]);
            int32_t box_height = canvas_pixels(node->ui,
                                                pxa_read_u32(value + 12));
            int32_t text_y = origin.y1 + canvas_pixels(
                node->ui, (int32_t)pxa_read_u32(value + 4));
            uint32_t color = pxa_read_u32(value + 16);
            if (box_height <= 0) continue;
            if (value[22] == PXA_UI_CANVAS_TEXT_ALIGN_MIDDLE &&
                box_height > font->line_height) {
                text_y += (box_height - font->line_height) / 2;
            } else if (value[22] == PXA_UI_CANVAS_TEXT_ALIGN_BOTTOM &&
                       box_height > font->line_height) {
                text_y += box_height - font->line_height;
            }
            area.x1 = origin.x1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value));
            area.y1 = text_y;
            area.x2 = area.x1 + canvas_pixels(node->ui,
                pxa_read_u32(value + 8)) - 1;
            area.y2 = text_y + font->line_height - 1;
            lv_draw_label_dsc_init(&descriptor);
            descriptor.text = (const char *)(value + 24);
            descriptor.text_length = length - 24u;
            descriptor.text_local = 1;
            descriptor.color = rgba_color(color);
            descriptor.opa = rgba_opa(color);
            descriptor.font = font;
            descriptor.align = value[21] == 1 ? LV_TEXT_ALIGN_CENTER
                               : value[21] == 2 ? LV_TEXT_ALIGN_RIGHT
                                                : LV_TEXT_ALIGN_LEFT;
            lv_draw_label(layer, &descriptor, &area);
        } else if (type == PXA_UI_CANVAS_BITMAP_RGB565 && length > 20) {
            lv_area_t target_area;
            lv_area_t image_area;
            lv_area_t saved_image_clip;
            lv_draw_image_dsc_t descriptor;
            uint32_t width = pxa_read_u32(value + 8);
            uint32_t height = pxa_read_u32(value + 12);
            int32_t target_width = canvas_pixels(node->ui, width);
            int32_t target_height = canvas_pixels(node->ui, height);
            int64_t scale_x;
            int64_t scale_y;
            if (width == 0 || height == 0 || target_width <= 0 ||
                target_height <= 0)
                continue;
            if (bitmap_image == NULL) continue;
            target_area.x1 = origin.x1 + canvas_pixels(
                node->ui, (int32_t)pxa_read_u32(value));
            target_area.y1 = origin.y1 + canvas_pixels(
                node->ui, (int32_t)pxa_read_u32(value + 4));
            target_area.x2 = target_area.x1 + target_width - 1;
            target_area.y2 = target_area.y1 + target_height - 1;
            image_area.x1 = target_area.x1;
            image_area.y1 = target_area.y1;
            image_area.x2 = image_area.x1 + (int32_t)width - 1;
            image_area.y2 = image_area.y1 + (int32_t)height - 1;
            lv_draw_image_dsc_init(&descriptor);
            descriptor.src = bitmap_image;
            descriptor.opa = LV_OPA_COVER;
            scale_x = (int64_t)target_width * LV_SCALE_NONE / width;
            scale_y = (int64_t)target_height * LV_SCALE_NONE / height;
            descriptor.scale_x = scale_x < 1 ? 1
                                 : scale_x > INT32_MAX ? INT32_MAX
                                                       : (int32_t)scale_x;
            descriptor.scale_y = scale_y < 1 ? 1
                                 : scale_y > INT32_MAX ? INT32_MAX
                                                       : (int32_t)scale_y;
            descriptor.image_area = image_area;
            saved_image_clip = layer->_clip_area;
            intersect_clip(&layer->_clip_area, &target_area);
            lv_draw_image(layer, &descriptor, &image_area);
            layer->_clip_area = saved_image_clip;
        } else if (type == PXA_UI_CANVAS_IMAGE && length > 18) {
            pxa_lvgl_ui_canvas_asset_t *asset = find_canvas_asset(
                node->canvas->assets, value + 18, length - 18u);
            const void *source = asset == NULL ? NULL : asset->source;
            if (source != NULL) {
                lv_area_t area;
                lv_area_t image_area;
                lv_area_t image_clip;
                lv_draw_image_dsc_t descriptor;
                lv_image_header_t header;
                int32_t target_width;
                int32_t target_height;
                int32_t scale_x = LV_SCALE_NONE;
                int32_t scale_y = LV_SCALE_NONE;
                int32_t drawn_width;
                int32_t drawn_height;
                int64_t raw_scale_x;
                int64_t raw_scale_y;
                area.x1 = origin.x1 + canvas_pixels(node->ui,
                    (int32_t)pxa_read_u32(value));
                area.y1 = origin.y1 + canvas_pixels(node->ui,
                    (int32_t)pxa_read_u32(value + 4));
                area.x2 = area.x1 + canvas_pixels(node->ui,
                    pxa_read_u32(value + 8)) - 1;
                area.y2 = area.y1 + canvas_pixels(node->ui,
                    pxa_read_u32(value + 12)) - 1;
                lv_draw_image_dsc_init(&descriptor);
                descriptor.src = source;
                descriptor.opa = value[16];
                target_width = lv_area_get_width(&area);
                target_height = lv_area_get_height(&area);
                if (lv_image_decoder_get_info(source, &header) == LV_RESULT_OK &&
                    header.w != 0 && header.h != 0) {
                    raw_scale_x = (int64_t)target_width * LV_SCALE_NONE /
                                  header.w;
                    raw_scale_y = (int64_t)target_height * LV_SCALE_NONE /
                                  header.h;
                    scale_x = raw_scale_x > INT32_MAX
                                  ? INT32_MAX : (int32_t)raw_scale_x;
                    scale_y = raw_scale_y > INT32_MAX
                                  ? INT32_MAX : (int32_t)raw_scale_y;
                    if (value[17] != 1) {
                        int32_t uniform = value[17] == 2
                                              ? LV_MAX(scale_x, scale_y)
                                              : LV_MIN(scale_x, scale_y);
                        scale_x = uniform;
                        scale_y = uniform;
                    }
                    if (scale_x < 1) scale_x = 1;
                    if (scale_y < 1) scale_y = 1;
                    drawn_width = (int32_t)((int64_t)header.w * scale_x /
                                            LV_SCALE_NONE);
                    drawn_height = (int32_t)((int64_t)header.h * scale_y /
                                             LV_SCALE_NONE);
                    image_area.x1 = area.x1 + (target_width - drawn_width) / 2;
                    image_area.y1 = area.y1 + (target_height - drawn_height) / 2;
                    image_area.x2 = image_area.x1 + header.w - 1;
                    image_area.y2 = image_area.y1 + header.h - 1;
                    descriptor.scale_x = scale_x;
                    descriptor.scale_y = scale_y;
                    descriptor.image_area = image_area;
                    image_clip = layer->_clip_area;
                    intersect_clip(&layer->_clip_area, &area);
                    lv_draw_image(layer, &descriptor, &image_area);
                    layer->_clip_area = image_clip;
                } else {
                    lv_draw_image(layer, &descriptor, &area);
                }
            }
        } else if (type == PXA_UI_CANVAS_CLIP_PUSH && length == 16 &&
                   clip_depth < node->canvas->clip_capacity) {
            lv_area_t clip;
            node->canvas->clip_stack[clip_depth++] = layer->_clip_area;
            clip.x1 = origin.x1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value));
            clip.y1 = origin.y1 + canvas_pixels(node->ui,
                (int32_t)pxa_read_u32(value + 4));
            clip.x2 = clip.x1 + canvas_pixels(node->ui,
                pxa_read_u32(value + 8)) - 1;
            clip.y2 = clip.y1 + canvas_pixels(node->ui,
                pxa_read_u32(value + 12)) - 1;
            intersect_clip(&clip, &layer->_clip_area);
            layer->_clip_area = clip;
        } else if (type == PXA_UI_CANVAS_CLIP_POP && length == 0 &&
                   clip_depth != 0) {
            layer->_clip_area = node->canvas->clip_stack[--clip_depth];
        }
    }
    layer->_clip_area = saved_clip;
}

static void on_canvas_pointer(lv_event_t *event) {
    pxa_lvgl_ui_node_t *node =
        (pxa_lvgl_ui_node_t *)lv_event_get_user_data(event);
    lv_indev_t *input = lv_event_get_indev(event);
    lv_event_code_t code = lv_event_get_code(event);
    lv_point_t point;
    lv_area_t area;
    uint8_t value[12] = {0};
    uint8_t phase;
    uint16_t flags;
    if (node == NULL || input == NULL) return;
    if (code == LV_EVENT_PRESSED) phase = 0;
    else if (code == LV_EVENT_PRESSING) phase = 1;
    else if (code == LV_EVENT_RELEASED) phase = 2;
    else phase = 3;
    lv_indev_get_point(input, &point);
    lv_obj_get_coords(node->object, &area);
    value[1] = phase;
    pxa_write_u32(value + 4, (uint32_t)canvas_logical_pixels(
        node->ui, point.x - area.x1));
    pxa_write_u32(value + 8, (uint32_t)canvas_logical_pixels(
        node->ui, point.y - area.y1));
    flags = phase == 1 ? 0 : PXA_UI_EVENT_FLAG_RELIABLE;
    emit_event(node, input, PXA_UI_EVENT_POINTER, flags, value,
               sizeof(value));
}

void pxa_lvgl_ui_theme_init(pxa_lvgl_ui_theme_t *theme) {
    static const uint32_t colors[10] = {
        UINT32_C(0x0b1018ff), UINT32_C(0x17212cff),
        UINT32_C(0x23a7d9ff), UINT32_C(0xffffffff),
        UINT32_C(0xf1f5f9ff), UINT32_C(0x91a4b7ff),
        UINT32_C(0x34475aff), UINT32_C(0x34c785ff),
        UINT32_C(0xf5bd4fff), UINT32_C(0xef5d67ff)};
    if (theme == NULL) return;
    memset(theme, 0, sizeof(*theme));
    memcpy(theme->rgba, colors, sizeof(colors));
}

size_t pxa_lvgl_ui_workspace_size(void) {
    return sizeof(pxa_lvgl_ui_t) + PXA_LVGL_UI_ALIGNMENT - 1u;
}

pxa_status_t pxa_lvgl_ui_init(void *workspace, size_t workspace_size,
                                 const pxa_lvgl_ui_config_t *config,
                                 pxa_lvgl_ui_t **output,
                                 pxa_ui_backend_t *backend) {
    uintptr_t address;
    uintptr_t aligned;
    pxa_lvgl_ui_t *ui;
    if (output == NULL || backend == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    memset(backend, 0, sizeof(*backend));
    if (workspace == NULL || workspace_size < pxa_lvgl_ui_workspace_size() ||
        config == NULL || config->struct_size < sizeof(*config) ||
        config->allocate == NULL || config->release == NULL ||
        config->execute == NULL ||
        ((config->resolve_asset == NULL) != (config->release_asset == NULL)))
        return PXA_STATUS_INVALID_ARGUMENT;
    address = (uintptr_t)workspace;
    aligned = (address + PXA_LVGL_UI_ALIGNMENT - 1u) &
              ~(uintptr_t)(PXA_LVGL_UI_ALIGNMENT - 1u);
    ui = (pxa_lvgl_ui_t *)aligned;
    memset(ui, 0, sizeof(*ui));
    ui->config = *config;
    ui->theme = config->theme;
    ui->primary_environment = config->primary_environment;
    if (ui->primary_environment.surface == 0)
        ui->primary_environment.surface = PXA_UI_PRIMARY_SURFACE;
    if (ui->primary_environment.density_q16 == 0)
        ui->primary_environment.density_q16 = UINT32_C(1) << 16;
    if (ui->primary_environment.font_scale_q16 == 0)
        ui->primary_environment.font_scale_q16 = UINT32_C(1) << 16;
    ui->magic = PXA_LVGL_UI_MAGIC;
    backend->struct_size = sizeof(*backend);
    backend->context = ui;
    backend->begin = backend_begin;
    backend->apply = backend_apply;
    backend->commit = backend_commit;
    backend->cancel = backend_cancel;
    backend->present_canvas = backend_canvas;
    backend->reset = backend_reset;
    backend->environment_changed = backend_environment;
    *output = ui;
    return PXA_STATUS_OK;
}

typedef struct {
    pxa_lvgl_ui_t *ui;
    const pxa_lvgl_ui_theme_t *theme;
} theme_update_t;

static void execute_theme(void *data) {
    theme_update_t *update = (theme_update_t *)data;
    pxa_lvgl_ui_node_t *node;
    update->ui->theme = *update->theme;
    for (node = update->ui->nodes; node != NULL; node = node->next) {
        apply_node_colors(node);
        lv_obj_set_style_text_font(node->object,
                                   font_for_role(node->ui, node->font_role), 0);
        if (node->content != NULL)
            lv_obj_set_style_text_font(
                node->content, font_for_role(node->ui, node->font_role), 0);
    }
}

pxa_status_t pxa_lvgl_ui_set_theme(
    pxa_lvgl_ui_t *ui, const pxa_lvgl_ui_theme_t *theme) {
    theme_update_t update;
    if (ui == NULL || ui->magic != PXA_LVGL_UI_MAGIC || theme == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    update.ui = ui;
    update.theme = theme;
    return ui->config.execute(execute_theme, &update,
                              ui->config.execute_user_data);
}

void pxa_lvgl_ui_reset(pxa_lvgl_ui_t *ui) {
    backend_reset(ui);
}

bool pxa_lvgl_ui_alpha_plane(const pxa_lvgl_ui_t *ui,
                              pxa_lvgl_ui_alpha_plane_t *output) {
    if (output == NULL) return false;
    memset(output, 0, sizeof(*output));
    if (ui == NULL || ui->magic != PXA_LVGL_UI_MAGIC ||
        ui->alpha_pixels == NULL || ui->alpha_values == NULL ||
        ui->alpha_width == 0 || ui->alpha_height == 0)
        return false;
    output->pixels = ui->alpha_pixels;
    output->alpha = ui->alpha_values;
    output->pixel_stride_bytes = (uint32_t)ui->alpha_width *
                                 sizeof(*ui->alpha_pixels);
    output->alpha_stride_bytes = ui->alpha_width;
    output->x = ui->alpha_x;
    output->y = ui->alpha_y;
    output->width = ui->alpha_width;
    output->height = ui->alpha_height;
    return true;
}

uint64_t pxa_lvgl_ui_event_timestamp_us(const pxa_lvgl_ui_t *ui) {
    return ui != NULL && ui->magic == PXA_LVGL_UI_MAGIC
               ? ui->event_timestamp_us
               : 0;
}

void pxa_lvgl_ui_deinit(pxa_lvgl_ui_t *ui) {
    if (ui == NULL || ui->magic != PXA_LVGL_UI_MAGIC) return;
    backend_reset(ui);
    ui->magic = 0;
}
