#ifndef PXA_LVGL_DIALOG_STYLE_H
#define PXA_LVGL_DIALOG_STYLE_H

#include "lvgl.h"

/* Native shell and host overlays use the same presentation rules. Colors and
 * metrics come from their current theme; this header owns no fonts or objects. */
typedef struct {
    lv_color_t surface, container, panel, text, muted, outline;
    lv_color_t primary, on_primary, primary_container, on_primary_container;
    lv_color_t error_container, on_error_container;
    int32_t spacing, radius;
    const lv_font_t *label_font;
} pxa_lvgl_dialog_style_t;

typedef enum {
    PXA_LVGL_ACTION_PRIMARY,
    PXA_LVGL_ACTION_SECONDARY,
    PXA_LVGL_ACTION_DANGER,
} pxa_lvgl_action_t;

static inline void pxa_lvgl_dialog_panel_style(
    lv_obj_t *panel, const pxa_lvgl_dialog_style_t *style) {
    lv_obj_set_style_bg_color(panel, style->panel, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(panel, style->text, 0);
    lv_obj_set_style_radius(panel, style->radius * 3, 0);
    lv_obj_set_style_border_color(panel, style->outline, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_shadow_width(panel, 0, 0);
    lv_obj_set_style_outline_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, style->spacing * 3, 0);
    lv_obj_set_style_pad_row(panel, style->spacing * 2, 0);
}

static inline void pxa_lvgl_dialog_focus_style(
    lv_obj_t *object, lv_color_t accent) {
    const lv_style_selector_t states[] = {
        LV_STATE_FOCUS_KEY, LV_STATE_EDITED,
        LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY,
    };
    lv_obj_set_style_outline_width(object, 0, 0);
    lv_obj_set_style_outline_width(object, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(object, accent, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(object, accent, LV_STATE_EDITED);
    lv_obj_set_style_border_color(object, accent, LV_STATE_CHECKED);
    for(unsigned i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
        /* A combined selector also overrides board keypad decorations. The
         * inset stroke stays inside compact rows and scrolling viewports. */
        lv_obj_set_style_outline_color(object, accent, states[i]);
        lv_obj_set_style_outline_width(object, 2, states[i]);
        lv_obj_set_style_outline_pad(object, -2, states[i]);
    }
}

static inline void pxa_lvgl_dialog_action_style(
    lv_obj_t *button, const pxa_lvgl_dialog_style_t *style,
    pxa_lvgl_action_t action) {
    const bool secondary = action == PXA_LVGL_ACTION_SECONDARY;
    const bool danger = action == PXA_LVGL_ACTION_DANGER;
    const lv_color_t foreground = secondary ? style->text :
        danger ? style->on_error_container : style->on_primary;
    const lv_color_t background = secondary ? style->container :
        danger ? style->error_container : style->primary;
    const lv_color_t pressed_text = danger ? style->error_container :
        style->on_primary_container;
    const lv_color_t pressed_background = danger ? style->on_error_container :
        style->primary_container;

    /* Remove the default theme's transitions and palette, retaining caller
     * geometry, callbacks and the board's navigation styles. */
    lv_obj_remove_theme(button, LV_PART_MAIN | LV_STATE_ANY);
    lv_obj_set_scrollable(button, false);
    lv_obj_set_style_radius(button, style->radius * 2, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, style->spacing, 0);
    lv_obj_set_style_text_align(button, LV_TEXT_ALIGN_CENTER, 0);
    if(style->label_font) lv_obj_set_style_text_font(button, style->label_font, 0);
    lv_obj_set_style_bg_opa(button, secondary ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, background, 0);
    lv_obj_set_style_text_color(button, foreground, 0);
    lv_obj_set_style_border_width(button, secondary ? 1 : 0, 0);
    lv_obj_set_style_border_width(button, secondary ? 1 : 0, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(button, style->outline, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, pressed_background, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(button, pressed_text, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(button, style->container, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(button, style->muted, LV_STATE_DISABLED);
    lv_obj_set_style_border_color(button, style->outline, LV_STATE_DISABLED);
    lv_obj_set_style_bg_grad_dir(button, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_anim_duration(button, 0, 0);
    pxa_lvgl_dialog_focus_style(button, secondary ? style->primary : foreground);
    /* Pressed colors may differ from resting colors. Keep the focus stroke
     * legible, including combinations installed by a physical keypad. */
    const lv_style_selector_t focus[] = {
        LV_STATE_FOCUS_KEY, LV_STATE_EDITED,
        LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY,
    };
    for(unsigned i = 0; i < sizeof(focus) / sizeof(focus[0]); ++i) {
        lv_obj_set_style_outline_color(button, pressed_text, focus[i] | LV_STATE_PRESSED);
        lv_obj_set_style_outline_width(button, 0, focus[i] | LV_STATE_DISABLED);
    }
}

#endif
