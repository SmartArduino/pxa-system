#ifndef PXSYS_REFERENCE_IME_H
#define PXSYS_REFERENCE_IME_H
#include "lvgl.h"
#include "pxsys/display.h"
#include "pxsys/theme.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct pxsys_reference_ime pxsys_reference_ime_t;
typedef struct {
    const pxsys_display_profile_t *display;
    const pxsys_theme_snapshot_t *theme;
    const lv_font_t *caption_font, *body_font, *icon_font;
    uint8_t layout, language; /* automatic/full/nine; Chinese/English */
    void (*closed)(void *context);
    void *context;
} pxsys_reference_ime_config_t;
/* One local decoder and the same adaptive keyboard used by PXA Providers.
 * Borrowed editor/fonts must outlive their use; editor deletion is observed.
 * The close callback schedules owner cleanup, never destroys during dispatch. */
pxsys_reference_ime_t *pxsys_reference_ime_create(lv_obj_t *parent,lv_obj_t *editor,
    const pxsys_reference_ime_config_t *config);
void pxsys_reference_ime_destroy(pxsys_reference_ime_t *ime);
void pxsys_reference_ime_update(pxsys_reference_ime_t *ime,const pxsys_reference_ime_config_t *config);
bool pxsys_reference_ime_is_visible(const pxsys_reference_ime_t *ime);
lv_obj_t *pxsys_reference_ime_get_root(const pxsys_reference_ime_t *ime);
lv_coord_t pxsys_reference_ime_keyboard_top(const pxsys_reference_ime_t *ime);
#ifdef __cplusplus
}
#endif
#endif
