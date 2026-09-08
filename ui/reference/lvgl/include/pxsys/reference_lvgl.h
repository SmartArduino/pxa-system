#ifndef PXSYS_REFERENCE_LVGL_H
#define PXSYS_REFERENCE_LVGL_H

#include <stdint.h>

#include "lvgl.h"
#include "pxsys/standard_system.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXSYS_REFERENCE_UI_HOME UINT32_C(1)
#define PXSYS_REFERENCE_UI_SETTINGS UINT32_C(2)
#define PXSYS_REFERENCE_UI_STATUS_BAR UINT32_C(4)
#define PXSYS_REFERENCE_UI_NAVIGATION_BAR UINT32_C(8)
#define PXSYS_REFERENCE_UI_NOTIFICATION_SHADE UINT32_C(16)
#define PXSYS_REFERENCE_UI_ALL UINT32_C(31)

typedef enum {
    PXSYS_NAVIGATION_BUTTONS = 0,
    PXSYS_NAVIGATION_GESTURES,
} pxsys_navigation_mode_t;

#ifndef PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS
#define PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS 1
#endif

#define PXSYS_TASK_SWITCHER_LIST 0
#define PXSYS_TASK_SWITCHER_CARDS 1
#ifndef PXSYS_REFERENCE_UI_TASK_SWITCHER
#define PXSYS_REFERENCE_UI_TASK_SWITCHER PXSYS_TASK_SWITCHER_CARDS
#endif
#ifndef PXSYS_REFERENCE_UI_TASK_PREVIEW_COUNT
#define PXSYS_REFERENCE_UI_TASK_PREVIEW_COUNT 4
#endif

typedef struct {
    uint32_t struct_size;
    pxsys_standard_system_t* system;
    lv_obj_t* parent;
    const lv_font_t* text_font;
    const lv_font_t* title_font;
    uint8_t publisher_root[PXSYS_PUBLISHER_ROOT_BYTES];
    uint32_t features;
    size_t max_launcher_apps;
    pxsys_allocator_t allocator;
    pxsys_navigation_mode_t navigation_mode;
    uint8_t animations_enabled;
} pxsys_reference_lvgl_config_t;

typedef struct pxsys_reference_lvgl pxsys_reference_lvgl_t;

void pxsys_reference_lvgl_config_init(pxsys_reference_lvgl_config_t* config);
pxsys_status_t pxsys_reference_lvgl_create(
    const pxsys_reference_lvgl_config_t* config,
    pxsys_reference_lvgl_t** output);
pxsys_status_t pxsys_reference_lvgl_start(pxsys_reference_lvgl_t* ui);
pxsys_status_t pxsys_reference_lvgl_destroy(pxsys_reference_lvgl_t* ui);
void pxsys_reference_lvgl_refresh_apps(pxsys_reference_lvgl_t* ui);
pxsys_status_t pxsys_reference_lvgl_set_navigation_mode(
    pxsys_reference_lvgl_t* ui, pxsys_navigation_mode_t mode);
pxsys_status_t pxsys_reference_lvgl_set_animations_enabled(
    pxsys_reference_lvgl_t* ui, bool enabled);
bool pxsys_reference_lvgl_animations_enabled(
    const pxsys_reference_lvgl_t* ui);
bool pxsys_reference_lvgl_dismiss_overlay(pxsys_reference_lvgl_t* ui);

#ifdef __cplusplus
}
#endif

#endif
