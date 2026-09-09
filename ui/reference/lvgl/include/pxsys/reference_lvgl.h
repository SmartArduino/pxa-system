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
#define PXSYS_REFERENCE_UI_WALLPAPER UINT32_C(32)
#define PXSYS_REFERENCE_UI_ALL UINT32_C(63)

typedef enum {
    PXSYS_NAVIGATION_BUTTONS = 0,
    PXSYS_NAVIGATION_GESTURES,
} pxsys_navigation_mode_t;

typedef const lv_font_t* (*pxsys_reference_lvgl_resolve_font_fn)(
    void* context, pxsys_typography_role_t role, uint16_t requested_px,
    const pxsys_locale_snapshot_t* locale);

typedef struct {
    pxsys_string_t locale;
    /* Native language name, for example "English" or "简体中文". */
    pxsys_string_t display_name;
} pxsys_reference_language_t;

#ifndef PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS
#define PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS 1
#endif
#ifndef PXSYS_REFERENCE_UI_WIFI
#define PXSYS_REFERENCE_UI_WIFI 1
#endif
#ifndef PXSYS_REFERENCE_UI_CELLULAR
#define PXSYS_REFERENCE_UI_CELLULAR 1
#endif
#ifndef PXSYS_REFERENCE_UI_BATTERY_PERCENT
#define PXSYS_REFERENCE_UI_BATTERY_PERCENT 1
#endif
#ifndef PXSYS_REFERENCE_UI_GESTURE_HANDLE
#define PXSYS_REFERENCE_UI_GESTURE_HANDLE 0
#endif
#ifndef PXSYS_REFERENCE_UI_ENABLE_WALLPAPER
#define PXSYS_REFERENCE_UI_ENABLE_WALLPAPER 1
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
    /* Optional semantic font mapping. Missing roles fall back to text_font or
     * title_font, preserving small product ports with only one or two fonts. */
    const lv_font_t* fonts[PXSYS_TYPOGRAPHY_ROLE_COUNT];
    void* font_context;
    pxsys_reference_lvgl_resolve_font_fn resolve_font;
    /* Borrowed for the UI lifetime. NULL selects the built-in en/zh list. */
    const pxsys_reference_language_t* languages;
    size_t language_count;
    /* Optional borrowed LVGL image source. NULL uses the built-in adaptive
     * wallpaper; the WALLPAPER feature bit or compile option can remove it. */
    const void* wallpaper_source;
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
