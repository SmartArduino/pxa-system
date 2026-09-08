#include "lvgl.h"

#include <stdlib.h>

lv_color_t lv_color_hex(uint32_t value) {
    return value & UINT32_C(0x00ffffff);
}

lv_obj_t* lv_obj_create(lv_obj_t* parent) {
    lv_obj_t* object = (lv_obj_t*)calloc(1, sizeof(*object));
    if (object != NULL)
        object->parent = parent;
    return object;
}

void lv_obj_delete(lv_obj_t* object) {
    if (object != NULL) {
        object->deleted = 1;
        free(object);
    }
}

void lv_obj_set_pos(lv_obj_t* object, int32_t x, int32_t y) {
    (void)object;
    (void)x;
    (void)y;
}

void lv_obj_set_size(lv_obj_t* object, int32_t width, int32_t height) {
    object->width = width;
    object->height = height;
}

void lv_obj_set_style_pad_all(lv_obj_t* object, int32_t value, int32_t selector) {
    (void)object;
    (void)value;
    (void)selector;
}

void lv_obj_set_style_border_width(lv_obj_t* object, int32_t value, int32_t selector) {
    (void)object;
    (void)value;
    (void)selector;
}

void lv_obj_set_style_radius(lv_obj_t* object, int32_t value, int32_t selector) {
    (void)object;
    (void)value;
    (void)selector;
}

void lv_obj_set_style_bg_color(lv_obj_t* object, lv_color_t color, int32_t selector) {
    (void)selector;
    object->background = color;
}

void lv_obj_set_style_bg_opa(lv_obj_t* object, lv_opa_t opacity, int32_t selector) {
    (void)selector;
    object->opacity = opacity;
}

void lv_obj_set_style_text_color(lv_obj_t* object, lv_color_t color, int32_t selector) {
    (void)selector;
    object->text = color;
}

void lv_obj_set_style_border_color(lv_obj_t* object, lv_color_t color, int32_t selector) {
    (void)selector;
    object->border = color;
}

void lv_obj_set_scrollbar_mode(lv_obj_t* object, int32_t mode) {
    (void)object;
    (void)mode;
}

void lv_obj_add_flag(lv_obj_t* object, uint32_t flags) {
    object->flags |= flags;
}

void lv_obj_remove_flag(lv_obj_t* object, uint32_t flags) {
    object->flags &= ~flags;
}

void lv_obj_move_foreground(lv_obj_t* object) {
    (void)object;
}
