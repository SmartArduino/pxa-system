/* SPDX-License-Identifier: MIT */
#ifndef PXA_LVGL_FILE_PICKER_H
#define PXA_LVGL_FILE_PICKER_H
#include "pxa/file_import.h"
#include "pxa/lvgl/pxa_lvgl_dialog_style.h"

#define PXA_FILE_PICKER_PAGE 12u
typedef struct { uint64_t epoch; uint8_t kind; char *name; } pxa_file_picker_entry_t;
typedef struct pxa_lvgl_file_picker pxa_lvgl_file_picker_t;
typedef struct {
    void *context;
    /* epoch 0 enumerates volumes; otherwise enumerate a volume-relative path.
     * Return only matching files and directories. Each name is malloc-owned
     * and transferred to the picker, including partial results on error.
     * All paths stay Host-side. */
    pxa_status_t (*browse)(void *,uint64_t,const char *,const char *,size_t,
        pxa_file_picker_entry_t *,size_t,size_t *,uint8_t *);
    void (*changed)(void *); /* wake owner; never destroys the picker inline */
    void (*overlay)(lv_obj_t *,int); /* 1 add, 0 refresh, -1 remove; optional */
    pxa_lvgl_dialog_style_t style;
    const lv_font_t *body_font,*title_font,*caption_font;
    const char *app_name,*purpose,*extensions;
    uint8_t multiple,chinese;
} pxa_lvgl_file_picker_config_t;
/* All calls, including destruction/state reads, require the LVGL owner lock.
 * Selection memory is owned by the picker until destroy. No fixed file-count
 * limit; allocation failure leaves the existing selection intact. */
pxa_lvgl_file_picker_t *pxa_lvgl_file_picker_create(const pxa_lvgl_file_picker_config_t *);
void pxa_lvgl_file_picker_destroy(pxa_lvgl_file_picker_t *);
/* 0 browsing, 1 confirmed/copying, 2 cancelled. */
unsigned pxa_lvgl_file_picker_state(const pxa_lvgl_file_picker_t *);
const pxa_file_choice_t *pxa_lvgl_file_picker_selection(const pxa_lvgl_file_picker_t *,size_t *);
void pxa_lvgl_file_picker_progress(pxa_lvgl_file_picker_t *,size_t,size_t,uint64_t);
/* System Back is consumed by the picker even when a native shell owns focus. */
void pxa_lvgl_file_picker_back(pxa_lvgl_file_picker_t *);
/* Desktop controller navigation. Native shells can instead install their
 * existing overlay focus group via config.overlay. */
void pxa_lvgl_file_picker_controller(pxa_lvgl_file_picker_t *,uint32_t,uint64_t);
#endif
