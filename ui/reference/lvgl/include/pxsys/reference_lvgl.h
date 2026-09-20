#ifndef PXSYS_REFERENCE_LVGL_H
#define PXSYS_REFERENCE_LVGL_H

#include <stdint.h>

#include "lvgl.h"
#include "pxsys/app_metadata.h"
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
/* Built-in Settings pages, appended so existing values stay stable. */
#define PXSYS_REFERENCE_UI_SOUND_SETTINGS UINT32_C(64)
#define PXSYS_REFERENCE_UI_DEVICE_INFO UINT32_C(128)
#define PXSYS_REFERENCE_UI_APP_MANAGER UINT32_C(256)
#define PXSYS_REFERENCE_UI_FILE_MANAGER UINT32_C(512)
#define PXSYS_REFERENCE_UI_ALL UINT32_C(1023)

typedef enum {
    PXSYS_NAVIGATION_BUTTONS = 0,
    PXSYS_NAVIGATION_GESTURES,
} pxsys_navigation_mode_t;

typedef const lv_font_t* (*pxsys_reference_lvgl_resolve_font_fn)(
    void* context, pxsys_typography_role_t role, uint16_t requested_px,
    const pxsys_locale_snapshot_t* locale);

typedef void (*pxsys_reference_lvgl_release_icon_fn)(void* context);

typedef struct {
    /* LVGL image source borrowed until release is called. */
    const void* source;
    pxsys_reference_lvgl_release_icon_fn release;
    void* release_context;
} pxsys_reference_lvgl_app_icon_t;

typedef bool (*pxsys_reference_lvgl_resolve_app_icon_fn)(
    void* context, const pxsys_app_descriptor_t* app,
    pxsys_reference_lvgl_app_icon_t* icon);

/* Optional platform metadata source for dynamically installed Apps. Native
 * and packaged Apps both return the same system metadata representation. */
typedef bool (*pxsys_reference_lvgl_resolve_app_metadata_fn)(
    void* context, const pxsys_app_descriptor_t* app,
    const pxsys_locale_snapshot_t* locale, pxsys_app_metadata_t* metadata);

typedef void (*pxsys_reference_lvgl_content_insets_fn)(
    void* context, uint16_t top, uint16_t bottom);

typedef void (*pxsys_reference_lvgl_lock_changed_fn)(void* context,
                                                     bool locked);

typedef enum {
    PXSYS_REFERENCE_POWER_ACTION_RESTART = 0,
    PXSYS_REFERENCE_POWER_ACTION_SHUTDOWN,
} pxsys_reference_power_action_t;

typedef void (*pxsys_reference_lvgl_power_action_fn)(
    void* context, pxsys_reference_power_action_t action);

typedef struct {
    pxsys_string_t locale;
    /* Native language name, for example "English" or "简体中文". */
    pxsys_string_t display_name;
} pxsys_reference_language_t;

/* Back-gesture phases. Products may replace the built-in left-edge swipe by
 * installing an override; the reference indicator is then hidden and the
 * override owns the pointer sequence. */
typedef enum {
    PXSYS_REFERENCE_BACK_GESTURE_PRESS = 0,
    PXSYS_REFERENCE_BACK_GESTURE_MOVE,
    PXSYS_REFERENCE_BACK_GESTURE_RELEASE,
    PXSYS_REFERENCE_BACK_GESTURE_CANCEL,
} pxsys_reference_back_gesture_phase_t;

/* Return non-zero to consume the phase. Returning non-zero on PRESS takes
 * ownership of the whole sequence. On RELEASE an override may set *commit to
 * ask the reference UI to run its standard back navigation. */
typedef bool (*pxsys_reference_lvgl_back_gesture_fn)(
    void* context, pxsys_reference_back_gesture_phase_t phase, int32_t x,
    int32_t y, bool* commit);

/* ---- Built-in Settings page providers ---------------------------------- */

/* About device: fixed fields, values supplied by the product. */
#define PXSYS_REFERENCE_DEVICE_VALUE_MAX 48
typedef enum {
    PXSYS_REFERENCE_DEVICE_FIRMWARE_NAME = 0,
    PXSYS_REFERENCE_DEVICE_FIRMWARE_VERSION,
    PXSYS_REFERENCE_DEVICE_SYSTEM_VERSION,
    PXSYS_REFERENCE_DEVICE_DISPLAY,
    PXSYS_REFERENCE_DEVICE_MEMORY,
    PXSYS_REFERENCE_DEVICE_STORAGE,
    PXSYS_REFERENCE_DEVICE_FIELD_COUNT,
} pxsys_reference_device_field_t;

/* Fill values[field] for every requested field; missing fields stay empty. */
typedef void (*pxsys_reference_lvgl_device_info_fn)(
    void* context, char values[PXSYS_REFERENCE_DEVICE_FIELD_COUNT]
                              [PXSYS_REFERENCE_DEVICE_VALUE_MAX]);

/* Current allocatable memory. Values are sampled whenever Recents opens. */
typedef bool (*pxsys_reference_lvgl_memory_info_fn)(
    void* context, uint64_t* available_bytes, uint64_t* total_bytes);

/* Product-owned diagnostics switches. The reference UI only supplies the
 * controls; rendering, logging and persistence remain outside LVGL. */
typedef enum {
    PXSYS_REFERENCE_PERFORMANCE_OVERLAY = 0,
    PXSYS_REFERENCE_PERFORMANCE_LOG,
    PXSYS_REFERENCE_PXADB,
} pxsys_reference_performance_option_t;

typedef bool (*pxsys_reference_lvgl_performance_get_fn)(
    void* context, pxsys_reference_performance_option_t option);
typedef bool (*pxsys_reference_lvgl_performance_set_fn)(
    void* context, pxsys_reference_performance_option_t option, bool enabled);

#define PXSYS_REFERENCE_WIFI_SSID_MAX 33
#define PXSYS_REFERENCE_WIFI_NETWORK_MAX 16
typedef struct {
    char ssid[PXSYS_REFERENCE_WIFI_SSID_MAX];
    int8_t rssi;
    uint8_t secured;
} pxsys_reference_wifi_network_t;

typedef size_t (*pxsys_reference_lvgl_wifi_scan_fn)(
    void* context, pxsys_reference_wifi_network_t* networks, size_t capacity);
typedef bool (*pxsys_reference_lvgl_wifi_connect_fn)(
    void* context, const char* ssid, const char* password);

/* App manager. */
#define PXSYS_REFERENCE_MANAGED_APP_MAX 48
#define PXSYS_REFERENCE_MANAGED_APP_IDENTITY_MAX 130
#define PXSYS_REFERENCE_MANAGED_APP_NAME_MAX 64
#define PXSYS_REFERENCE_MANAGED_APP_VERSION_MAX 32

typedef enum {
    PXSYS_REFERENCE_APP_ACTION_ENABLE = 0,
    PXSYS_REFERENCE_APP_ACTION_DISABLE,
    PXSYS_REFERENCE_APP_ACTION_CLEAR_DATA,
    PXSYS_REFERENCE_APP_ACTION_UNINSTALL,
} pxsys_reference_app_action_t;

typedef struct {
    char identity[PXSYS_REFERENCE_MANAGED_APP_IDENTITY_MAX];
    char name[PXSYS_REFERENCE_MANAGED_APP_NAME_MAX];
    char version[PXSYS_REFERENCE_MANAGED_APP_VERSION_MAX];
    uint8_t built_in;
    uint8_t installed;
    uint8_t enabled;
    uint8_t has_private_data;
    uint8_t active;
} pxsys_reference_managed_app_t;

/* Two-phase enumeration: NULL items returns the required count. */
typedef size_t (*pxsys_reference_lvgl_app_list_fn)(
    void* context, pxsys_reference_managed_app_t* apps, size_t capacity);
typedef bool (*pxsys_reference_lvgl_app_action_fn)(
    void* context, const char* identity, pxsys_reference_app_action_t action);

/* File manager. Paths are '/'-rooted logical paths below the storage root. */
#define PXSYS_REFERENCE_FILE_ENTRY_MAX 64
#define PXSYS_REFERENCE_FILE_NAME_MAX 128
#define PXSYS_REFERENCE_FILE_PATH_MAX 192

typedef enum {
    PXSYS_REFERENCE_FILE_ACTION_DELETE = 0,
} pxsys_reference_file_action_t;

typedef struct {
    char name[PXSYS_REFERENCE_FILE_NAME_MAX];
    char path[PXSYS_REFERENCE_FILE_PATH_MAX];
    uint64_t size;
    uint8_t is_directory;
} pxsys_reference_file_entry_t;

typedef size_t (*pxsys_reference_lvgl_file_list_fn)(
    void* context, const char* path, pxsys_reference_file_entry_t* entries,
    size_t capacity);
typedef bool (*pxsys_reference_lvgl_file_action_fn)(
    void* context, const char* path, pxsys_reference_file_action_t action);

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
/* Built-in left-edge back gesture. Products can disable it and use the
 * runtime override hook instead. */
#ifndef PXSYS_REFERENCE_UI_BACK_GESTURE
#define PXSYS_REFERENCE_UI_BACK_GESTURE 1
#endif
/* Built-in Sound, About, Apps and Files settings pages. Providers supplied by
 * the product fill them; without a provider the page stays unregistered. */
#ifndef PXSYS_REFERENCE_UI_BUILTIN_SETTINGS
#define PXSYS_REFERENCE_UI_BUILTIN_SETTINGS 1
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
    /* Optional backend resource hook. It keeps package/native icon ownership
     * outside the system core and lets products replace the standard source. */
    void* app_icon_context;
    pxsys_reference_lvgl_resolve_app_icon_fn resolve_app_icon;
    /* Compatibility surfaces can reserve visible system chrome without
     * depending on the reference layout implementation. */
    void* content_insets_context;
    pxsys_reference_lvgl_content_insets_fn content_insets_changed;
    /* Borrowed for the UI lifetime. NULL selects the built-in en/zh list. */
    const pxsys_reference_language_t* languages;
    size_t language_count;
    /* Optional borrowed LVGL image source. NULL uses the built-in adaptive
     * wallpaper; the WALLPAPER feature bit or compile option can remove it. */
    const void* wallpaper_source;
    /* Optional dynamic metadata source, appended to preserve older config
     * initializers. Catalog-backed metadata remains the normal fallback. */
    void* app_metadata_context;
    pxsys_reference_lvgl_resolve_app_metadata_fn resolve_app_metadata;
    /* Back gesture, appended to preserve older config initializers. The
     * built-in edge swipe stays active when back_gesture is NULL. edge_width 0
     * selects 20 logical pixels measured from the safe-area edge; shaped or
     * inset panels therefore keep the gesture reachable. */
    uint8_t back_gesture_enabled;
    uint16_t back_gesture_edge_width;
    void* back_gesture_context;
    pxsys_reference_lvgl_back_gesture_fn back_gesture;
    /* Built-in Settings page providers, appended to preserve older config
     * initializers. A NULL provider keeps its page unregistered, so the
     * matching Settings row stays hidden. */
    void* device_info_context;
    pxsys_reference_lvgl_device_info_fn device_info;
    void* app_manager_context;
    pxsys_reference_lvgl_app_list_fn app_list;
    pxsys_reference_lvgl_app_action_fn app_action;
    void* file_manager_context;
    pxsys_reference_lvgl_file_list_fn file_list;
    pxsys_reference_lvgl_file_action_fn file_action;
    /* Optional Recents memory indicator, appended for source compatibility. */
    void* memory_info_context;
    pxsys_reference_lvgl_memory_info_fn memory_info;
    /* Optional product diagnostics controls. A missing getter keeps this
     * section out of Settings. */
    void* performance_context;
    pxsys_reference_lvgl_performance_get_fn performance_get;
    pxsys_reference_lvgl_performance_set_fn performance_set;
    /* Optional station-mode Wi-Fi selection. When present, tapping Wi-Fi in
     * Settings scans access points and opens an on-screen password keyboard. */
    void* wifi_context;
    pxsys_reference_lvgl_wifi_scan_fn wifi_scan;
    pxsys_reference_lvgl_wifi_connect_fn wifi_connect;
} pxsys_reference_lvgl_config_t;

typedef struct pxsys_reference_lvgl pxsys_reference_lvgl_t;

void pxsys_reference_lvgl_config_init(pxsys_reference_lvgl_config_t* config);
pxsys_status_t pxsys_reference_lvgl_create(
    const pxsys_reference_lvgl_config_t* config,
    pxsys_reference_lvgl_t** output);
pxsys_status_t pxsys_reference_lvgl_start(pxsys_reference_lvgl_t* ui);
pxsys_status_t pxsys_reference_lvgl_destroy(pxsys_reference_lvgl_t* ui);
/* Brings the system launcher to the foreground through the role host. */
pxsys_status_t pxsys_reference_lvgl_home(pxsys_reference_lvgl_t* ui);
void pxsys_reference_lvgl_refresh_apps(pxsys_reference_lvgl_t* ui);
pxsys_status_t pxsys_reference_lvgl_set_navigation_mode(
    pxsys_reference_lvgl_t* ui, pxsys_navigation_mode_t mode);
pxsys_status_t pxsys_reference_lvgl_set_animations_enabled(
    pxsys_reference_lvgl_t* ui, bool enabled);
bool pxsys_reference_lvgl_animations_enabled(
    const pxsys_reference_lvgl_t* ui);
bool pxsys_reference_lvgl_dismiss_overlay(pxsys_reference_lvgl_t* ui);
pxsys_status_t pxsys_reference_lvgl_set_locked(pxsys_reference_lvgl_t* ui,
                                               bool locked);
bool pxsys_reference_lvgl_is_locked(const pxsys_reference_lvgl_t* ui);
pxsys_status_t pxsys_reference_lvgl_set_lock_changed_callback(
    pxsys_reference_lvgl_t* ui, void* context,
    pxsys_reference_lvgl_lock_changed_fn callback);
pxsys_status_t pxsys_reference_lvgl_set_power_action_callback(
    pxsys_reference_lvgl_t* ui, void* context,
    pxsys_reference_lvgl_power_action_fn callback);
pxsys_status_t pxsys_reference_lvgl_set_power_menu_changed_callback(
    pxsys_reference_lvgl_t* ui, void* context,
    pxsys_reference_lvgl_lock_changed_fn callback);
pxsys_status_t pxsys_reference_lvgl_show_power_menu(
    pxsys_reference_lvgl_t* ui);
void pxsys_reference_lvgl_hide_power_menu(pxsys_reference_lvgl_t* ui);
bool pxsys_reference_lvgl_power_menu_visible(
    const pxsys_reference_lvgl_t* ui);

#ifdef __cplusplus
}
#endif

#endif
