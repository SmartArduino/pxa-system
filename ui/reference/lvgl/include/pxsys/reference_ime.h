#ifndef PXSYS_REFERENCE_IME_H
#define PXSYS_REFERENCE_IME_H

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Nine key input method used on narrow panels: multi tap keys, pinyin
 * candidates, numbers and symbol pages. A wide panel uses the full keyboard
 * instead, see reference_lvgl.c. */
typedef enum {
    PXSYS_REFERENCE_IME_MODE_CHINESE = 0,
    PXSYS_REFERENCE_IME_MODE_ENGLISH,
    PXSYS_REFERENCE_IME_MODE_NUMBER,
    PXSYS_REFERENCE_IME_MODE_SYMBOLS,
} pxsys_reference_ime_mode_t;

typedef struct pxsys_reference_ime pxsys_reference_ime_t;

/* Creates the keypad inside `parent` and binds it to `text_area`. The caller
 * owns both. */
pxsys_reference_ime_t* pxsys_reference_ime_create(lv_obj_t* parent,
                                                  lv_obj_t* text_area);
void pxsys_reference_ime_destroy(pxsys_reference_ime_t* input_method);

void pxsys_reference_ime_set_text_area(pxsys_reference_ime_t* input_method,
                                       lv_obj_t* text_area);
void pxsys_reference_ime_set_font(pxsys_reference_ime_t* input_method,
                                  const lv_font_t* font);
void pxsys_reference_ime_set_icon_font(pxsys_reference_ime_t* input_method,
                                       const lv_font_t* font);
void pxsys_reference_ime_set_layout(pxsys_reference_ime_t* input_method,
                                    lv_coord_t width, lv_coord_t height,
                                    lv_align_t align, lv_coord_t offset_x,
                                    lv_coord_t offset_y);
/* Called when a key hides the input method itself, so the owner can release
 * it on its next event loop turn. */
void pxsys_reference_ime_set_close_callback(pxsys_reference_ime_t* input_method,
                                            void (*callback)(void* context),
                                            void* context);
void pxsys_reference_ime_set_mode(pxsys_reference_ime_t* input_method,
                                  pxsys_reference_ime_mode_t mode);
pxsys_reference_ime_mode_t pxsys_reference_ime_get_mode(
    const pxsys_reference_ime_t* input_method);

void pxsys_reference_ime_show(pxsys_reference_ime_t* input_method);
void pxsys_reference_ime_hide(pxsys_reference_ime_t* input_method);
bool pxsys_reference_ime_is_visible(
    const pxsys_reference_ime_t* input_method);
lv_obj_t* pxsys_reference_ime_get_root(
    const pxsys_reference_ime_t* input_method);

#ifdef __cplusplus
}
#endif

#endif
