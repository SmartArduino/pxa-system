#ifndef PXA_LVGL_IME_BUILTIN_H
#define PXA_LVGL_IME_BUILTIN_H
#include "pxa/ime.h"
typedef struct pxa_lvgl_ime_builtin pxa_lvgl_ime_builtin_t;
/* Immutable, independently selectable simplified/traditional tables. Only a
 * bounded composition state is allocated; no Guest, dictionary load or cache. */
unsigned pxa_lvgl_ime_builtin_scripts(void); /* bit 0 simplified, bit 1 traditional */
bool pxa_lvgl_ime_builtin_set_traditional(pxa_lvgl_ime_builtin_t *state,bool traditional);
pxa_lvgl_ime_builtin_t *pxa_lvgl_ime_builtin_create(void *parent);
void pxa_lvgl_ime_builtin_destroy(pxa_lvgl_ime_builtin_t *state);
void pxa_lvgl_ime_builtin_reset(pxa_lvgl_ime_builtin_t *state);
pxa_status_t pxa_lvgl_ime_builtin_input(pxa_lvgl_ime_builtin_t *state,
    uint16_t action,uint8_t layout,uint32_t value,pxa_bytes_t text,
    pxa_ime_present_fn present,void *context);
#endif
