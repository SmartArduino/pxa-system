#ifndef PXSYS_LVGL_FLAGS_H
#define PXSYS_LVGL_FLAGS_H

#include "lvgl.h"

/* LVGL 9.6 exposes per-flag setters. Keep composite masks local to the
 * reference UI, where several controls configure multiple flags together. */
static inline void pxsys_lvgl_set_flags(lv_obj_t *object,
                                        lv_obj_flag_t flags, bool enabled) {
    if (flags & LV_OBJ_FLAG_HIDDEN) lv_obj_set_hidden(object, enabled);
    if (flags & LV_OBJ_FLAG_CLICKABLE) lv_obj_set_clickable(object, enabled);
    if (flags & LV_OBJ_FLAG_SCROLLABLE) lv_obj_set_scrollable(object, enabled);
    if (flags & LV_OBJ_FLAG_SCROLL_ELASTIC)
        lv_obj_set_scroll_elastic(object, enabled);
    if (flags & LV_OBJ_FLAG_SCROLL_ONE) lv_obj_set_scroll_one(object, enabled);
    if (flags & LV_OBJ_FLAG_SCROLL_ON_FOCUS)
        lv_obj_set_scroll_on_focus(object, enabled);
    if (flags & LV_OBJ_FLAG_SNAPPABLE) lv_obj_set_snappable(object, enabled);
    if (flags & LV_OBJ_FLAG_PRESS_LOCK) lv_obj_set_press_lock(object, enabled);
    if (flags & LV_OBJ_FLAG_EVENT_BUBBLE)
        lv_obj_set_event_bubble(object, enabled);
    if (flags & LV_OBJ_FLAG_ADV_HITTEST)
        lv_obj_set_adv_hittest(object, enabled);
    if (flags & LV_OBJ_FLAG_USER_1) lv_obj_set_user_flag(object, 0, enabled);
}

static inline void pxsys_lvgl_add_flags(lv_obj_t *object,
                                        lv_obj_flag_t flags) {
    pxsys_lvgl_set_flags(object, flags, true);
}

static inline void pxsys_lvgl_remove_flags(lv_obj_t *object,
                                           lv_obj_flag_t flags) {
    pxsys_lvgl_set_flags(object, flags, false);
}

#endif
