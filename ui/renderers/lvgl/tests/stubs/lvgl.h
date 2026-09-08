#ifndef LVGL_H
#define LVGL_H

#include <stdint.h>

typedef uint32_t lv_color_t;
typedef uint8_t lv_opa_t;

typedef struct lv_obj_t {
    struct lv_obj_t* parent;
    int32_t width;
    int32_t height;
    uint32_t flags;
    lv_color_t background;
    lv_color_t text;
    lv_color_t border;
    lv_opa_t opacity;
    uint8_t deleted;
} lv_obj_t;

#define LV_OBJ_FLAG_HIDDEN UINT32_C(1)
#define LV_OBJ_FLAG_SCROLLABLE UINT32_C(2)
#define LV_SCROLLBAR_MODE_OFF 0

lv_color_t lv_color_hex(uint32_t value);
lv_obj_t* lv_obj_create(lv_obj_t* parent);
void lv_obj_delete(lv_obj_t* object);
void lv_obj_set_pos(lv_obj_t* object, int32_t x, int32_t y);
void lv_obj_set_size(lv_obj_t* object, int32_t width, int32_t height);
void lv_obj_set_style_pad_all(lv_obj_t* object, int32_t value, int32_t selector);
void lv_obj_set_style_border_width(lv_obj_t* object, int32_t value, int32_t selector);
void lv_obj_set_style_radius(lv_obj_t* object, int32_t value, int32_t selector);
void lv_obj_set_style_bg_color(lv_obj_t* object, lv_color_t color, int32_t selector);
void lv_obj_set_style_bg_opa(lv_obj_t* object, lv_opa_t opacity, int32_t selector);
void lv_obj_set_style_text_color(lv_obj_t* object, lv_color_t color, int32_t selector);
void lv_obj_set_style_border_color(lv_obj_t* object, lv_color_t color, int32_t selector);
void lv_obj_set_scrollbar_mode(lv_obj_t* object, int32_t mode);
void lv_obj_add_flag(lv_obj_t* object, uint32_t flags);
void lv_obj_remove_flag(lv_obj_t* object, uint32_t flags);
void lv_obj_move_foreground(lv_obj_t* object);

#endif
