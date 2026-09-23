#include "pxsys/reference_lvgl.h"
#include "pxsys/reference_ime.h"

/* Temporary bring-up switch: open the input method for the first application
 * text input even before it is focused. */
#ifndef PXSYS_REFERENCE_UI_DEBUG_INPUT_METHOD
#define PXSYS_REFERENCE_UI_DEBUG_INPUT_METHOD 0
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxsys/app_metadata.h"
#include "pxsys/reference_layout.h"

#define REFERENCE_MAGIC UINT32_C(0x50585255)
#define REFERENCE_APP_COUNT 8u
#define TRANSIENT_BAR_TIMEOUT_MS 2500u
#define NAVIGATION_GESTURE_COMMIT_DISTANCE 32
#define NAVIGATION_GESTURE_HOLD_MS 180u
#define NAVIGATION_GESTURE_MOTION_SLOP 4
#define REFERENCE_RESOURCE_NAMESPACE "system.ui"
#define REFERENCE_TRANSLATION_SCRATCH_COUNT 12u
#define REFERENCE_TRANSLATION_SCRATCH_BYTES 96u

#define RESOURCE_ENTRY(key_value, text_value)                              \
    {{key_value, sizeof(key_value) - 1u}, {text_value, sizeof(text_value) - 1u}}

static const pxsys_resource_entry_t reference_en_resources[] = {
    RESOURCE_ENTRY("apps.empty", "No apps installed"),
    RESOURCE_ENTRY("recents.empty", "No recent apps"),
    RESOURCE_ENTRY("settings.title", "Settings"),
    RESOURCE_ENTRY("settings.wifi", "Wi-Fi"),
    RESOURCE_ENTRY("settings.mobile", "Mobile network"),
    RESOURCE_ENTRY("settings.appearance", "Appearance"),
    RESOURCE_ENTRY("settings.display", "Display"),
    RESOURCE_ENTRY("settings.section.connections", "Connections"),
    RESOURCE_ENTRY("settings.section.personalization", "Personalization"),
    RESOURCE_ENTRY("settings.section.device", "Device"),
    RESOURCE_ENTRY("settings.section.system", "System"),
    RESOURCE_ENTRY("settings.section.developer", "Developer"),
    RESOURCE_ENTRY("settings.bluetooth", "Bluetooth audio"),
    RESOURCE_ENTRY("settings.bluetooth.detail", "Speakers, sources and local music"),
    RESOURCE_ENTRY("settings.sound", "Sound & display"),
    RESOURCE_ENTRY("settings.sound.detail", "Volume, brightness and screen timeout"),
    RESOURCE_ENTRY("settings.alarm", "Alarms"),
    RESOURCE_ENTRY("settings.alarm.detail", "Create and manage alarms"),
    RESOURCE_ENTRY("settings.language", "Language"),
    RESOURCE_ENTRY("settings.language.detail", "System display language"),
    RESOURCE_ENTRY("settings.apps", "Apps"),
    RESOURCE_ENTRY("settings.apps.detail", "Disable, uninstall and clear data"),
    RESOURCE_ENTRY("settings.files", "Files & storage"),
    RESOURCE_ENTRY("settings.files.detail", "Browse and organize device files"),
    RESOURCE_ENTRY("settings.about", "About device"),
    RESOURCE_ENTRY("settings.about.detail", "Firmware, storage and hardware information"),
    RESOURCE_ENTRY("settings.performance.overlay", "Performance overlay"),
    RESOURCE_ENTRY("settings.performance.overlay.detail", "Show frame and render timing on screen"),
    RESOURCE_ENTRY("settings.performance.log", "Performance log"),
    RESOURCE_ENTRY("settings.performance.log.detail", "Write frame and render timing to the log"),
    RESOURCE_ENTRY("settings.pxadb", "PXADB"),
    RESOURCE_ENTRY("settings.pxadb.detail", "Allow USB debugging and diagnostics"),
    RESOURCE_ENTRY("settings.wifi.networks", "Available networks"),
    RESOURCE_ENTRY("settings.wifi.scan", "Scan again"),
    RESOURCE_ENTRY("settings.wifi.none", "No networks found"),
    RESOURCE_ENTRY("settings.wifi.password", "Enter Wi-Fi password"),
    RESOURCE_ENTRY("settings.wifi.open", "Open network"),
    RESOURCE_ENTRY("settings.wifi.secured", "Secured"),
    RESOURCE_ENTRY("language.title", "Language"),
    RESOURCE_ENTRY("state.connected", "Connected"),
    RESOURCE_ENTRY("state.on", "On"),
    RESOURCE_ENTRY("state.off", "Off"),
    RESOURCE_ENTRY("theme.light", "Light"),
    RESOURCE_ENTRY("theme.dark", "Dark"),
    RESOURCE_ENTRY("control.mobile", "Mobile"),
    RESOURCE_ENTRY("control.volume", "Volume"),
    RESOURCE_ENTRY("control.brightness", "Brightness"),
    RESOURCE_ENTRY("control.bluetooth", "Bluetooth"),
    RESOURCE_ENTRY("control.focus", "Focus"),
    RESOURCE_ENTRY("control.light", "Flashlight"),
    RESOURCE_ENTRY("control.theme", "Theme"),
    RESOURCE_ENTRY("app.system-home.name", "System Home"),
    RESOURCE_ENTRY("app.system-home.description", "Standard system launcher"),
    RESOURCE_ENTRY("app.system-settings.name", "Settings"),
    RESOURCE_ENTRY("app.system-settings.description", "System and device settings"),
    RESOURCE_ENTRY("app.system-status-bar.name", "System Status Bar"),
    RESOURCE_ENTRY("app.system-navigation-bar.name", "System Navigation Bar"),
    RESOURCE_ENTRY("settings.sound.volume", "Volume"),
    RESOURCE_ENTRY("settings.sound.brightness", "Brightness"),
    RESOURCE_ENTRY("settings.sound.unsupported", "No adjustable levels"),
    RESOURCE_ENTRY("settings.about.section.system", "System"),
    RESOURCE_ENTRY("settings.about.section.memory", "Memory & storage"),
    RESOURCE_ENTRY("settings.about.firmware.name", "Firmware"),
    RESOURCE_ENTRY("settings.about.firmware.version", "Firmware version"),
    RESOURCE_ENTRY("settings.about.system.version", "System version"),
    RESOURCE_ENTRY("settings.about.display", "Display"),
    RESOURCE_ENTRY("settings.about.memory", "Memory"),
    RESOURCE_ENTRY("settings.about.storage", "Storage"),
    RESOURCE_ENTRY("settings.apps.empty", "No installed apps"),
    RESOURCE_ENTRY("settings.apps.system", "System app"),
    RESOURCE_ENTRY("settings.apps.user", "User app"),
    RESOURCE_ENTRY("settings.apps.disabled", "Disabled"),
    RESOURCE_ENTRY("settings.apps.enable", "Enable"),
    RESOURCE_ENTRY("settings.apps.disable", "Disable"),
    RESOURCE_ENTRY("settings.apps.clear", "Clear data"),
    RESOURCE_ENTRY("settings.apps.uninstall", "Uninstall"),
    RESOURCE_ENTRY("settings.apps.close", "Close"),
    RESOURCE_ENTRY("settings.apps.confirm.clear", "Clear all app data?"),
    RESOURCE_ENTRY("settings.apps.confirm.uninstall", "Uninstall this app?"),
    RESOURCE_ENTRY("settings.apps.permissions", "Permissions"),
    RESOURCE_ENTRY("settings.apps.permissions.empty", "No declared permissions"),
    RESOURCE_ENTRY("settings.apps.permissions.required", "Required"),
    RESOURCE_ENTRY("settings.apps.permissions.optional", "Optional"),
    RESOURCE_ENTRY("settings.files.up", "Up one level"),
    RESOURCE_ENTRY("settings.files.empty", "Empty folder"),
    RESOURCE_ENTRY("settings.files.folder", "Folder"),
    RESOURCE_ENTRY("settings.files.delete", "Delete"),
    RESOURCE_ENTRY("settings.files.confirm.delete", "Delete this item?"),
    RESOURCE_ENTRY("settings.confirm", "Confirm"),
    RESOURCE_ENTRY("settings.cancel", "Cancel"),
    RESOURCE_ENTRY("recents.memory.available", "Free"),
    RESOURCE_ENTRY("lock.swipe", "Swipe up to unlock"),
    RESOURCE_ENTRY("lock.time_unavailable", "Time unavailable"),
    RESOURCE_ENTRY("power.title", "Power menu"),
    RESOURCE_ENTRY("power.restart", "Restart"),
    RESOURCE_ENTRY("power.shutdown", "Power off"),
    RESOURCE_ENTRY("power.cancel", "Cancel"),
};

static const pxsys_resource_entry_t reference_zh_resources[] = {
    RESOURCE_ENTRY("apps.empty", "没有已安装的应用"),
    RESOURCE_ENTRY("recents.empty", "没有最近任务"),
    RESOURCE_ENTRY("settings.title", "设置"),
    RESOURCE_ENTRY("settings.wifi", "无线网络"),
    RESOURCE_ENTRY("settings.mobile", "移动网络"),
    RESOURCE_ENTRY("settings.appearance", "外观"),
    RESOURCE_ENTRY("settings.display", "显示"),
    RESOURCE_ENTRY("settings.section.connections", "连接"),
    RESOURCE_ENTRY("settings.section.personalization", "个性化"),
    RESOURCE_ENTRY("settings.section.device", "设备"),
    RESOURCE_ENTRY("settings.section.system", "系统"),
    RESOURCE_ENTRY("settings.section.developer", "开发者选项"),
    RESOURCE_ENTRY("settings.bluetooth", "蓝牙音频"),
    RESOURCE_ENTRY("settings.bluetooth.detail", "音响、音源和本地音乐"),
    RESOURCE_ENTRY("settings.sound", "声音与显示"),
    RESOURCE_ENTRY("settings.sound.detail", "音量、亮度和自动熄屏"),
    RESOURCE_ENTRY("settings.alarm", "闹钟"),
    RESOURCE_ENTRY("settings.alarm.detail", "添加和管理闹钟"),
    RESOURCE_ENTRY("settings.language", "语言"),
    RESOURCE_ENTRY("settings.language.detail", "系统显示语言"),
    RESOURCE_ENTRY("settings.apps", "应用"),
    RESOURCE_ENTRY("settings.apps.detail", "停用、卸载与清除应用数据"),
    RESOURCE_ENTRY("settings.files", "文件与存储"),
    RESOURCE_ENTRY("settings.files.detail", "浏览和整理设备文件"),
    RESOURCE_ENTRY("settings.about", "关于设备"),
    RESOURCE_ENTRY("settings.about.detail", "固件、存储与硬件信息"),
    RESOURCE_ENTRY("settings.performance.overlay", "性能浮层"),
    RESOURCE_ENTRY("settings.performance.overlay.detail", "在屏幕显示帧率和渲染耗时"),
    RESOURCE_ENTRY("settings.performance.log", "性能日志"),
    RESOURCE_ENTRY("settings.performance.log.detail", "在日志中输出帧率和渲染耗时"),
    RESOURCE_ENTRY("settings.pxadb", "PXADB 调试"),
    RESOURCE_ENTRY("settings.pxadb.detail", "允许 USB 调试和诊断连接"),
    RESOURCE_ENTRY("settings.wifi.networks", "可用网络"),
    RESOURCE_ENTRY("settings.wifi.scan", "重新扫描"),
    RESOURCE_ENTRY("settings.wifi.none", "未发现无线网络"),
    RESOURCE_ENTRY("settings.wifi.password", "输入无线网络密码"),
    RESOURCE_ENTRY("settings.wifi.open", "开放网络"),
    RESOURCE_ENTRY("settings.wifi.secured", "已加密"),
    RESOURCE_ENTRY("language.title", "语言"),
    RESOURCE_ENTRY("state.connected", "已连接"),
    RESOURCE_ENTRY("state.on", "已开启"),
    RESOURCE_ENTRY("state.off", "已关闭"),
    RESOURCE_ENTRY("theme.light", "浅色"),
    RESOURCE_ENTRY("theme.dark", "深色"),
    RESOURCE_ENTRY("control.mobile", "移动网络"),
    RESOURCE_ENTRY("control.volume", "音量"),
    RESOURCE_ENTRY("control.brightness", "亮度"),
    RESOURCE_ENTRY("control.bluetooth", "蓝牙"),
    RESOURCE_ENTRY("control.focus", "勿扰"),
    RESOURCE_ENTRY("control.light", "手电筒"),
    RESOURCE_ENTRY("control.theme", "主题"),
    RESOURCE_ENTRY("app.system-home.name", "系统桌面"),
    RESOURCE_ENTRY("app.system-home.description", "标准系统桌面"),
    RESOURCE_ENTRY("app.system-settings.name", "设置"),
    RESOURCE_ENTRY("app.system-settings.description", "系统与设备设置"),
    RESOURCE_ENTRY("app.system-status-bar.name", "系统状态栏"),
    RESOURCE_ENTRY("app.system-navigation-bar.name", "系统导航栏"),
    RESOURCE_ENTRY("settings.sound.volume", "音量"),
    RESOURCE_ENTRY("settings.sound.brightness", "亮度"),
    RESOURCE_ENTRY("settings.sound.unsupported", "暂无可调节项"),
    RESOURCE_ENTRY("settings.about.section.system", "系统"),
    RESOURCE_ENTRY("settings.about.section.memory", "内存与存储"),
    RESOURCE_ENTRY("settings.about.firmware.name", "固件名称"),
    RESOURCE_ENTRY("settings.about.firmware.version", "固件版本"),
    RESOURCE_ENTRY("settings.about.system.version", "系统版本"),
    RESOURCE_ENTRY("settings.about.display", "屏幕分辨率"),
    RESOURCE_ENTRY("settings.about.memory", "内存"),
    RESOURCE_ENTRY("settings.about.storage", "存储空间"),
    RESOURCE_ENTRY("settings.apps.empty", "没有已安装的应用"),
    RESOURCE_ENTRY("settings.apps.system", "系统应用"),
    RESOURCE_ENTRY("settings.apps.user", "用户应用"),
    RESOURCE_ENTRY("settings.apps.disabled", "已停用"),
    RESOURCE_ENTRY("settings.apps.enable", "启用"),
    RESOURCE_ENTRY("settings.apps.disable", "停用"),
    RESOURCE_ENTRY("settings.apps.clear", "清除数据"),
    RESOURCE_ENTRY("settings.apps.uninstall", "卸载"),
    RESOURCE_ENTRY("settings.apps.close", "关闭"),
    RESOURCE_ENTRY("settings.apps.confirm.clear", "确认清除应用数据？"),
    RESOURCE_ENTRY("settings.apps.confirm.uninstall", "确认卸载该应用？"),
    RESOURCE_ENTRY("settings.apps.permissions", "权限"),
    RESOURCE_ENTRY("settings.apps.permissions.empty", "未声明权限"),
    RESOURCE_ENTRY("settings.apps.permissions.required", "必要"),
    RESOURCE_ENTRY("settings.apps.permissions.optional", "可选"),
    RESOURCE_ENTRY("settings.files.up", "返回上级目录"),
    RESOURCE_ENTRY("settings.files.empty", "空文件夹"),
    RESOURCE_ENTRY("settings.files.folder", "文件夹"),
    RESOURCE_ENTRY("settings.files.delete", "删除"),
    RESOURCE_ENTRY("settings.files.confirm.delete", "确认删除该项？"),
    RESOURCE_ENTRY("settings.confirm", "确认"),
    RESOURCE_ENTRY("settings.cancel", "取消"),
    RESOURCE_ENTRY("recents.memory.available", "可用"),
    RESOURCE_ENTRY("lock.swipe", "上滑解锁"),
    RESOURCE_ENTRY("lock.time_unavailable", "时间未同步"),
    RESOURCE_ENTRY("power.title", "电源菜单"),
    RESOURCE_ENTRY("power.restart", "重新启动"),
    RESOURCE_ENTRY("power.shutdown", "关机"),
    RESOURCE_ENTRY("power.cancel", "取消"),
};

static const pxsys_resource_catalog_t reference_catalog_en = {
    sizeof(pxsys_resource_catalog_t),
    {REFERENCE_RESOURCE_NAMESPACE, sizeof(REFERENCE_RESOURCE_NAMESPACE) - 1u},
    {"en", 2}, reference_en_resources,
    sizeof(reference_en_resources) / sizeof(reference_en_resources[0]), -100};
static const pxsys_resource_catalog_t reference_catalog_zh = {
    sizeof(pxsys_resource_catalog_t),
    {REFERENCE_RESOURCE_NAMESPACE, sizeof(REFERENCE_RESOURCE_NAMESPACE) - 1u},
    {"zh", 2}, reference_zh_resources,
    sizeof(reference_zh_resources) / sizeof(reference_zh_resources[0]), -100};

static const pxsys_reference_language_t reference_languages[] = {
    {{"en-US", 5}, {"English", 7}},
    {{"zh-CN", 5}, {"简体中文", sizeof("简体中文") - 1u}},
};

typedef enum {
    REFERENCE_PAGE_HOME = 0,
    REFERENCE_PAGE_SETTINGS,
    REFERENCE_PAGE_STATUS_BAR,
    REFERENCE_PAGE_NAVIGATION_BAR,
    REFERENCE_PAGE_SOUND_SETTINGS,
    REFERENCE_PAGE_DEVICE_INFO,
    REFERENCE_PAGE_APP_MANAGER,
    REFERENCE_PAGE_FILE_MANAGER,
} reference_page_t;

typedef enum {
    NAVIGATION_ICON_BACK = 0,
    NAVIGATION_ICON_HOME,
    NAVIGATION_ICON_RECENTS,
} navigation_icon_t;

typedef struct {
    struct pxsys_reference_lvgl* ui;
    reference_page_t page;
} app_context_t;

typedef struct {
    struct pxsys_reference_lvgl* ui;
    pxsys_app_identity_t identity;
    char app_id[65];
    char runtime_name[12];
    pxsys_reference_lvgl_app_icon_t icon;
} launcher_item_t;

typedef struct {
    struct pxsys_reference_lvgl* ui;
    pxsys_instance_ref_t instance;
    pxsys_app_identity_t identity;
    char app_id[65];
    char label[72];
    int32_t press_y;
    int32_t dismiss_threshold;
    int32_t preview_hit_x;
    int32_t preview_hit_y;
    int32_t preview_hit_width;
    int32_t preview_hit_height;
    uint8_t running;
    uint8_t dragging;
    uint8_t suppress_click;
} recent_item_t;

typedef struct {
    struct pxsys_reference_lvgl* ui;
    pxsys_network_type_t network;
} network_control_t;

typedef struct {
    struct pxsys_reference_lvgl* ui;
    pxsys_level_control_t control;
} level_control_t;

typedef struct {
    struct pxsys_reference_lvgl* ui;
    pxsys_toggle_control_t control;
} toggle_control_t;

typedef struct {
    struct pxsys_reference_lvgl* ui;
    pxsys_reference_performance_option_t option;
} performance_control_t;

typedef struct {
    struct pxsys_reference_lvgl* ui;
    size_t index;
} wifi_network_control_t;

typedef struct {
    struct pxsys_reference_lvgl* ui;
    size_t index;
} app_permission_control_t;

typedef struct {
    struct pxsys_reference_lvgl* ui;
    const char* role;
} settings_action_t;

#define REFERENCE_SETTINGS_ACTION_COUNT 7u

/* Recently used apps, kept after they stop so the switcher can relaunch. */
typedef struct {
    uint8_t publisher_root[PXSYS_PUBLISHER_ROOT_BYTES];
    char app_id[65];
    char label[72];
    uint32_t last_used;
} recent_app_t;

#define REFERENCE_RECENT_HISTORY 8u

#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && LV_USE_SNAPSHOT
typedef struct {
    pxsys_instance_ref_t instance;
    lv_draw_buf_t* image;
    pxsys_rect_t capture_bounds;
    uint32_t last_used;
    uint8_t transition_pending;
} task_preview_t;
#endif

struct pxsys_reference_lvgl {
    uint32_t magic;
    pxsys_standard_system_t* system;
    pxsys_allocator_t allocator;
    lv_obj_t* parent;
    lv_obj_t* root;
    lv_obj_t* content;
    lv_obj_t* title;
    lv_obj_t* page_indicator;
    lv_obj_t* status_bar;
    lv_obj_t* navigation_bar;
    lv_obj_t* toast;
    lv_obj_t* toast_label;
    lv_obj_t* notification_shade;
    lv_obj_t* notification_panel;
    lv_obj_t* notification_content;
    lv_obj_t* task_switcher;
    lv_obj_t* task_switcher_memory;
    lv_obj_t* lock_screen;
    lv_obj_t* lock_time;
    lv_obj_t* lock_date;
    lv_obj_t* lock_hint;
    lv_obj_t* lock_indicator;
    lv_obj_t* power_menu;
#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && LV_USE_SNAPSHOT
    lv_obj_t* task_switcher_empty;
#endif
    lv_obj_t* language_dialog;
    lv_obj_t* navigation_handle;
    lv_obj_t* navigation_back_indicator;
    int32_t navigation_handle_width;
    lv_timer_t* toast_timer;
    /* System input method for application text inputs. */
    lv_obj_t* app_ime;
    lv_obj_t* app_ime_keyboard;
    lv_obj_t* app_ime_target;
    lv_timer_t* app_ime_timer;
    pxsys_reference_ime_t* app_ime_keypad;
    lv_obj_t* app_ime_pinyin;
    uint8_t app_ime_close_requested;
    lv_timer_t* transient_timer;
    const lv_font_t* text_font;
    const lv_font_t* title_font;
    const lv_font_t* fonts[PXSYS_TYPOGRAPHY_ROLE_COUNT];
    void* font_context;
    pxsys_reference_lvgl_resolve_font_fn resolve_font;
    void* app_icon_context;
    pxsys_reference_lvgl_resolve_app_icon_fn resolve_app_icon;
    void* app_metadata_context;
    pxsys_reference_lvgl_resolve_app_metadata_fn resolve_app_metadata;
    void* content_insets_context;
    pxsys_reference_lvgl_content_insets_fn content_insets_changed;
    uint16_t content_inset_top;
    uint16_t content_inset_bottom;
    uint32_t features;
    size_t max_launcher_apps;
    size_t launcher_count;
    launcher_item_t* launcher_items;
    recent_item_t recent_items[12];
    recent_app_t recent_history[REFERENCE_RECENT_HISTORY];
    size_t recent_history_count;
    pxsys_instance_ref_t pending_dismissals[12];
    size_t pending_dismissal_count;
    uint32_t preview_clock;
#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && LV_USE_SNAPSHOT
    task_preview_t task_previews[PXSYS_REFERENCE_UI_TASK_PREVIEW_COUNT];
    lv_obj_t* task_transition_image;
    lv_obj_t* task_transition_target;
    task_preview_t* task_transition_preview;
#endif
    pxsys_app_identity_t identities[REFERENCE_APP_COUNT];
    app_context_t contexts[REFERENCE_APP_COUNT];
    char app_ids[REFERENCE_APP_COUNT][32];
    uint8_t publisher_root[PXSYS_PUBLISHER_ROOT_BYTES];
    uint8_t apps_registered;
    uint8_t roles_registered;
    uint8_t display_subscribed;
    uint8_t theme_subscribed;
    uint8_t status_subscribed;
    uint8_t toast_subscribed;
    uint8_t window_subscribed;
    uint8_t locale_subscribed;
    uint8_t resource_catalog_en_registered;
    uint8_t resource_catalog_zh_registered;
    uint8_t toast_visible;
    pxsys_toast_tone_t toast_tone;
    uint8_t active_chrome;
    uint8_t content_active;
    uint8_t notification_shade_open;
    uint8_t notification_dragging;
    uint8_t notification_drag_moved;
    uint8_t notification_close_armed;
    uint8_t transient_revealed;
    uint8_t navigation_dragging;
    uint8_t navigation_back_ready;
    uint8_t animations_enabled;
    uint8_t navigation_from_home;
    uint8_t task_switcher_handoff;
    uint8_t locked;
    uint8_t lock_dragging;
    int32_t lock_press_y;
    int32_t notification_press_x;
    int32_t status_press_y;
    int32_t notification_panel_height;
    int32_t notification_progress;
    int32_t notification_scroll_y;
    int32_t settings_scroll_y;
    int32_t navigation_press_x;
    int32_t navigation_press_y;
    int32_t navigation_last_x;
    int32_t navigation_last_y;
    int32_t navigation_back_press_x;
    uint32_t navigation_last_motion_tick;
    network_control_t network_controls[2];
    level_control_t level_controls[2];
    toggle_control_t toggle_controls[4];
    performance_control_t performance_controls[3];
    wifi_network_control_t wifi_network_controls[PXSYS_REFERENCE_WIFI_NETWORK_MAX];
    settings_action_t settings_actions[REFERENCE_SETTINGS_ACTION_COUNT];
    const pxsys_reference_language_t* languages;
    size_t language_count;
    const void* wallpaper_source;
    pxsys_navigation_mode_t navigation_mode;
    uint8_t back_gesture_enabled;
    uint8_t back_gesture_override;
    uint16_t back_gesture_edge_width;
    void* back_gesture_context;
    pxsys_reference_lvgl_back_gesture_fn back_gesture;
    void* device_info_context;
    pxsys_reference_lvgl_device_info_fn device_info;
    void* app_manager_context;
    pxsys_reference_lvgl_app_list_fn app_list;
    pxsys_reference_lvgl_app_action_fn app_action;
    void* file_manager_context;
    pxsys_reference_lvgl_file_list_fn file_list;
    pxsys_reference_lvgl_file_action_fn file_action;
    void* memory_info_context;
    pxsys_reference_lvgl_memory_info_fn memory_info;
    void* performance_context;
    pxsys_reference_lvgl_performance_get_fn performance_get;
    pxsys_reference_lvgl_performance_set_fn performance_set;
    void* wifi_context;
    pxsys_reference_lvgl_wifi_scan_fn wifi_scan;
    pxsys_reference_lvgl_wifi_connect_fn wifi_connect;
    void* app_permission_context;
    pxsys_reference_lvgl_app_permission_list_fn app_permission_list;
    pxsys_reference_lvgl_app_permission_set_fn app_permission_set;
    pxsys_reference_wifi_network_t wifi_networks[PXSYS_REFERENCE_WIFI_NETWORK_MAX];
    app_permission_control_t
        app_permission_controls[PXSYS_REFERENCE_APP_PERMISSION_MAX];
    pxsys_reference_app_permission_t
        app_permissions[PXSYS_REFERENCE_APP_PERMISSION_MAX];
    size_t app_permission_count;
    size_t wifi_network_count;
    char pending_wifi_ssid[PXSYS_REFERENCE_WIFI_SSID_MAX];
    lv_obj_t* wifi_password_input;
    void* lock_changed_context;
    pxsys_reference_lvgl_lock_changed_fn lock_changed;
    void* power_action_context;
    pxsys_reference_lvgl_power_action_fn power_action;
    void* power_menu_changed_context;
    pxsys_reference_lvgl_lock_changed_fn power_menu_changed;
    pxsys_reference_managed_app_t managed_apps[PXSYS_REFERENCE_MANAGED_APP_MAX];
    size_t managed_app_count;
    pxsys_reference_file_entry_t file_entries[PXSYS_REFERENCE_FILE_ENTRY_MAX];
    size_t file_entry_count;
    char file_path[PXSYS_REFERENCE_FILE_PATH_MAX];
    char pending_identity[PXSYS_REFERENCE_MANAGED_APP_IDENTITY_MAX];
    char pending_path[PXSYS_REFERENCE_FILE_PATH_MAX];
    uint8_t pending_action;
    uint8_t pending_kind;
    uint8_t managed_apps_loaded;
    uint8_t file_entries_loaded;
    uint8_t developer_unlocked;
    uint8_t system_version_taps;
    uint32_t system_version_last_tap;
    lv_obj_t* app_dialog;
    lv_obj_t* confirm_dialog;
    reference_page_t active_page;
    pxsys_display_profile_t display;
    pxsys_theme_snapshot_t theme;
    pxsys_system_status_snapshot_t system_status;
    pxsys_window_snapshot_t window;
    pxsys_locale_snapshot_t locale;
    char translation_scratch[REFERENCE_TRANSLATION_SCRATCH_COUNT]
                            [REFERENCE_TRANSLATION_SCRATCH_BYTES];
    size_t translation_scratch_cursor;
};

static void rebuild(pxsys_reference_lvgl_t* ui);
static void build_task_switcher(pxsys_reference_lvgl_t* ui);
static void close_task_switcher(pxsys_reference_lvgl_t* ui);
static int task_switcher_is_open(const pxsys_reference_lvgl_t* ui);
static void close_language_dialog(pxsys_reference_lvgl_t* ui);
static void close_notification_shade(pxsys_reference_lvgl_t* ui);
static void toast_restack(pxsys_reference_lvgl_t* ui);
static void capture_current_task(pxsys_reference_lvgl_t* ui,
                                 int transition_pending);
static void dismiss_recent_item(pxsys_reference_lvgl_t* ui,
                                recent_item_t* item);
static void lock_screen_refresh(pxsys_reference_lvgl_t* ui);

static void rebuild_async(void* context) {
    rebuild((pxsys_reference_lvgl_t*)context);
}

static int ui_valid(const pxsys_reference_lvgl_t* ui) {
    return ui != NULL && ui->magic == REFERENCE_MAGIC;
}

static int languages_valid(const pxsys_reference_language_t* languages,
                           size_t count) {
    size_t index;
    if (count == 0) return languages == NULL;
    if (languages == NULL) return 0;
    for (index = 0; index < count; ++index) {
        pxsys_locale_snapshot_t locale;
        if (pxsys_locale_snapshot_init(&locale, languages[index].locale) !=
                PXSYS_STATUS_OK ||
            locale.tag_size != languages[index].locale.size ||
            memcmp(locale.tag, languages[index].locale.data,
                   locale.tag_size) != 0 ||
            !pxsys_display_text_validate(languages[index].display_name, 64))
            return 0;
    }
    return 1;
}

static lv_color_t color_token(const pxsys_reference_lvgl_t* ui,
                              pxsys_color_token_t token) {
    return lv_color_hex(ui->theme.colors[token] & UINT32_C(0x00ffffff));
}

static const lv_font_t* typography_font(const pxsys_reference_lvgl_t* ui,
                                        pxsys_typography_role_t role) {
    const lv_font_t* resolved;
    if (role >= PXSYS_TYPOGRAPHY_ROLE_COUNT) return ui->text_font;
    if (ui->resolve_font != NULL) {
        resolved = ui->resolve_font(
            ui->font_context, role, ui->theme.typography_px[role],
            &ui->locale);
        if (resolved != NULL) return resolved;
    }
    if (ui->fonts[role] != NULL)
        return ui->fonts[role];
    if (role == PXSYS_TYPOGRAPHY_DISPLAY ||
        role == PXSYS_TYPOGRAPHY_HEADLINE || role == PXSYS_TYPOGRAPHY_TITLE)
        return ui->title_font;
    return ui->text_font;
}

static const char* translated(pxsys_reference_lvgl_t* ui, const char* key,
                              const char* fallback) {
    pxsys_string_t value;
    char* output;
    size_t size;
    if (pxsys_resource_resolve(
            pxsys_standard_system_resources(ui->system),
            pxsys_string_from_cstr(REFERENCE_RESOURCE_NAMESPACE),
            pxsys_string_from_cstr(key),
            pxsys_string(ui->locale.tag, ui->locale.tag_size), &value) !=
        PXSYS_STATUS_OK)
        return fallback;
    output = ui->translation_scratch[
        ui->translation_scratch_cursor++ % REFERENCE_TRANSLATION_SCRATCH_COUNT];
    size = value.size < REFERENCE_TRANSLATION_SCRATCH_BYTES - 1u
               ? value.size
               : REFERENCE_TRANSLATION_SCRATCH_BYTES - 1u;
    memcpy(output, value.data, size);
    output[size] = '\0';
    return output;
}

static pxsys_string_t localized_app_name(
    pxsys_reference_lvgl_t* ui, const pxsys_app_descriptor_t* app) {
    pxsys_app_metadata_t catalog_metadata = {0};
    pxsys_app_metadata_t platform_metadata = {0};
    pxsys_status_t catalog_status;
    if (ui == NULL || app == NULL) return pxsys_string(NULL, 0);
    catalog_metadata.struct_size = sizeof(catalog_metadata);
    catalog_status = pxsys_app_metadata_resolve(
        pxsys_standard_system_resources(ui->system), app,
        pxsys_string(ui->locale.tag, ui->locale.tag_size),
        &catalog_metadata);
    if (catalog_status == PXSYS_STATUS_OK &&
        (catalog_metadata.catalog_fields &
         PXSYS_APP_METADATA_CATALOG_DISPLAY_NAME) != 0) {
        return catalog_metadata.display_name;
    }
    platform_metadata.struct_size = sizeof(platform_metadata);
    if (ui->resolve_app_metadata != NULL &&
        ui->resolve_app_metadata(ui->app_metadata_context, app, &ui->locale,
                                 &platform_metadata) &&
        pxsys_display_text_validate(platform_metadata.display_name,
                                    SIZE_MAX)) {
        return platform_metadata.display_name;
    }
    if (catalog_status == PXSYS_STATUS_OK)
        return catalog_metadata.display_name;
    return app->display_name;
}

static const char* borrowed_text(pxsys_reference_lvgl_t* ui,
                                 pxsys_string_t value) {
    char* output = ui->translation_scratch[
        ui->translation_scratch_cursor++ % REFERENCE_TRANSLATION_SCRATCH_COUNT];
    size_t size = value.size < REFERENCE_TRANSLATION_SCRATCH_BYTES - 1u
                      ? value.size
                      : REFERENCE_TRANSLATION_SCRATCH_BYTES - 1u;
    if (size != 0) memcpy(output, value.data, size);
    output[size] = '\0';
    return output;
}

static uint32_t gesture_strip_height(const pxsys_reference_layout_t* layout) {
    return pxsys_reference_layout_gesture_strip_height(layout->size_class);
}

static void expand_content_into_hidden_gesture_area(
    const pxsys_reference_lvgl_t* ui, pxsys_reference_layout_t* layout) {
    if (ui->navigation_mode == PXSYS_NAVIGATION_GESTURES &&
        ui->window.navigation_bar_mode != PXSYS_WINDOW_BAR_HIDDEN) {
        int32_t content_bottom = layout->content.y +
                                 (int32_t)layout->content.height;
        int32_t safe_bottom = layout->safe_area.y +
                              (int32_t)layout->safe_area.height;
        int32_t vertical_padding = (int32_t)layout->outer_padding / 2;
#if PXSYS_REFERENCE_UI_GESTURE_HANDLE
        int32_t wanted_bottom = (int32_t)ui->display.height -
                                (int32_t)gesture_strip_height(layout) -
                                vertical_padding;
        if (wanted_bottom > safe_bottom - vertical_padding)
            wanted_bottom = safe_bottom - vertical_padding;
#else
        int32_t wanted_bottom = safe_bottom - vertical_padding;
#endif
        if (wanted_bottom > content_bottom)
            layout->content.height +=
                (uint32_t)(wanted_bottom - content_bottom);
    }
}

static void style_plain(lv_obj_t* object) {
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_remove_flag(object,
                       LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

static pxsys_status_t open_role(pxsys_reference_lvgl_t* ui, const char* role) {
    pxsys_intent_t intent = {0};
    pxsys_instance_ref_t instance;
    intent.struct_size = sizeof(intent);
    intent.action = pxsys_string_from_cstr("system.intent.main");
    /* Bring the role to the front without clearing the tasks above it: the
     * running applications stay alive and keep their place in recents. */
    intent.flags = PXSYS_INTENT_FLAG_SINGLE_TOP;
    return pxsys_task_manager_start_role(
        pxsys_standard_system_tasks(ui->system),
        pxsys_standard_system_roles(ui->system), NULL,
        pxsys_string_from_cstr(role), &intent, &instance);
}

static void back_clicked(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    if (!ui_valid(ui)) return;
    if (pxsys_reference_lvgl_dismiss_overlay(ui)) return;
    {
        pxsys_back_result_t result;
        (void)pxsys_task_manager_back(pxsys_standard_system_tasks(ui->system),
                                      &result);
    }
}

static void navigation_back_indicator_reset(pxsys_reference_lvgl_t* ui) {
    if (ui->navigation_back_indicator == NULL) return;
    lv_obj_delete(ui->navigation_back_indicator);
    ui->navigation_back_indicator = NULL;
}

/* Left-edge drag pill: grows and rounds as the pointer travels, switching to
 * the accent color once the release would navigate back. */
static int navigation_back_indicator_update(pxsys_reference_lvgl_t* ui,
                                            int32_t edge_x, int32_t press_x,
                                            int32_t x, int32_t y) {
    static const lv_point_precise_t kChevronPoints[] = {
        {8, 1}, {1, 9}, {8, 17},
    };
    lv_obj_t* indicator;
    lv_obj_t* chevron;
    int32_t distance = x - press_x;
    int32_t progress;
    int32_t width;
    int32_t height;
    int32_t radius;
    int32_t indicator_x;
    int32_t indicator_y;
    int ready;
    if (distance < 4) {
        navigation_back_indicator_reset(ui);
        return 0;
    }
    if (ui->navigation_back_indicator == NULL) {
        ui->navigation_back_indicator = lv_obj_create(ui->root);
        style_plain(ui->navigation_back_indicator);
        lv_obj_set_size(ui->navigation_back_indicator, 18, 68);
        lv_obj_set_style_radius(ui->navigation_back_indicator, 5, 0);
        lv_obj_set_style_shadow_width(ui->navigation_back_indicator, 6, 0);
        lv_obj_set_style_shadow_opa(ui->navigation_back_indicator, LV_OPA_30,
                                    0);
        chevron = lv_line_create(ui->navigation_back_indicator);
        lv_line_set_points(chevron, kChevronPoints, 3);
        lv_obj_set_style_line_width(chevron, 3, 0);
        lv_obj_set_style_line_rounded(chevron, true, 0);
        lv_obj_set_size(chevron, 10, 18);
        lv_obj_align(chevron, LV_ALIGN_CENTER, 0, 0);
    }
    indicator = ui->navigation_back_indicator;
    chevron = lv_obj_get_child(indicator, 0);
    progress = distance > 72 ? 72 : distance;
    width = 18 + progress * 20 / 72;
    height = 68 - progress * 16 / 72;
    radius = 5 + progress * 11 / 72;
    if (radius > 16) radius = 16;
    ready = distance >= NAVIGATION_GESTURE_COMMIT_DISTANCE;
    indicator_x = edge_x + distance - width;
    if (indicator_x > edge_x + 8) indicator_x = edge_x + 8;
    if (indicator_x < edge_x - width + 4) indicator_x = edge_x - width + 4;
    indicator_y = y - height / 2;
    if (indicator_y < 0) indicator_y = 0;
    if (indicator_y > (int32_t)ui->display.height - height)
        indicator_y = (int32_t)ui->display.height - height;
    lv_obj_set_size(indicator, width, height);
    lv_obj_set_pos(indicator, indicator_x, indicator_y);
    lv_obj_set_style_radius(indicator, radius, 0);
    lv_obj_set_style_bg_color(
        indicator,
        color_token(ui, ready ? PXSYS_COLOR_ACCENT : PXSYS_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(indicator, (lv_opa_t)242, 0);
    lv_obj_set_style_shadow_color(indicator, color_token(ui, PXSYS_COLOR_SCRIM),
                                  0);
    if (chevron != NULL) {
        if (width < 28)
            lv_obj_add_flag(chevron, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_remove_flag(chevron, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_line_color(
            chevron,
            color_token(ui, ready ? PXSYS_COLOR_ON_ACCENT
                                  : PXSYS_COLOR_TEXT_PRIMARY),
            0);
    }
    lv_obj_remove_flag(indicator, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(indicator);
    return ready;
}

static void navigation_back(pxsys_reference_lvgl_t* ui) {
    pxsys_back_result_t result;
    if (!ui_valid(ui)) return;
    /* A committed edge swipe is an explicit Back action. Do not consume it
     * merely to reveal transient system bars in a fullscreen app. */
    if (ui->locked) return;
    if (ui->power_menu != NULL) {
        pxsys_reference_lvgl_hide_power_menu(ui);
        return;
    }
    if (ui->language_dialog != NULL) {
        close_language_dialog(ui);
        return;
    }
    if (task_switcher_is_open(ui)) {
        close_task_switcher(ui);
        return;
    }
    if (ui->notification_shade_open) {
        close_notification_shade(ui);
        return;
    }
    (void)pxsys_task_manager_back(pxsys_standard_system_tasks(ui->system),
                                  &result);
}

static void navigation_back_gesture_event(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    lv_indev_t* indev = lv_event_get_indev(event);
    lv_point_t point;
    lv_event_code_t code;
    int32_t horizontal;
    int32_t edge_x;
    if (!ui_valid(ui) || indev == NULL) return;
    lv_indev_get_point(indev, &point);
    code = lv_event_get_code(event);
    edge_x = (int32_t)ui->display.safe_insets.left;
    if (ui->back_gesture != NULL) {
        pxsys_reference_back_gesture_phase_t phase;
        bool commit = false;
        switch (code) {
            case LV_EVENT_PRESSED:
                phase = PXSYS_REFERENCE_BACK_GESTURE_PRESS;
                break;
            case LV_EVENT_PRESSING:
                phase = PXSYS_REFERENCE_BACK_GESTURE_MOVE;
                break;
            case LV_EVENT_RELEASED:
                phase = PXSYS_REFERENCE_BACK_GESTURE_RELEASE;
                break;
            case LV_EVENT_PRESS_LOST:
                phase = PXSYS_REFERENCE_BACK_GESTURE_CANCEL;
                break;
            default:
                return;
        }
        if (!ui->back_gesture_override) {
            if (phase != PXSYS_REFERENCE_BACK_GESTURE_PRESS) return;
            if (ui->back_gesture(ui->back_gesture_context, phase, point.x,
                                 point.y, &commit)) {
                ui->back_gesture_override = 1;
            }
            if (ui->back_gesture_override) return;
        } else {
            (void)ui->back_gesture(ui->back_gesture_context, phase, point.x,
                                   point.y, &commit);
            if (phase == PXSYS_REFERENCE_BACK_GESTURE_RELEASE) {
                ui->back_gesture_override = 0;
                if (commit) navigation_back(ui);
            } else if (phase == PXSYS_REFERENCE_BACK_GESTURE_CANCEL) {
                ui->back_gesture_override = 0;
            }
            return;
        }
    }
#if !PXSYS_REFERENCE_UI_BACK_GESTURE
    (void)edge_x;
    return;
#endif
    if (code == LV_EVENT_PRESSED) {
        ui->navigation_back_press_x = point.x;
        ui->navigation_back_ready = 0;
        return;
    }
    if (code == LV_EVENT_PRESSING) {
        ui->navigation_back_ready = (uint8_t)navigation_back_indicator_update(
            ui, edge_x, ui->navigation_back_press_x, point.x, point.y);
        return;
    }
    if (code == LV_EVENT_PRESS_LOST) {
        ui->navigation_back_ready = 0;
        navigation_back_indicator_reset(ui);
        return;
    }
    if (code != LV_EVENT_RELEASED) return;
    /* Some touch drivers retain the last valid point on release while others
     * report a final edge value. Preserve the readiness that was rendered on
     * the last move, but also accept a valid release point crossing the limit. */
    horizontal = point.x - ui->navigation_back_press_x;
    if (horizontal >= NAVIGATION_GESTURE_COMMIT_DISTANCE)
        ui->navigation_back_ready = 1;
    navigation_back_indicator_reset(ui);
    if (!ui->navigation_back_ready) return;
    ui->navigation_back_ready = 0;
    navigation_back(ui);
}

static void theme_clicked(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    pxsys_theme_snapshot_t next = ui->theme;
    pxsys_color_scheme_t scheme =
        ui->theme.effective_scheme == PXSYS_COLOR_SCHEME_DARK
            ? PXSYS_COLOR_SCHEME_LIGHT : PXSYS_COLOR_SCHEME_DARK;
    pxsys_theme_snapshot_init(&next, scheme);
    next.configured_mode = scheme == PXSYS_COLOR_SCHEME_DARK
                               ? PXSYS_THEME_MODE_DARK
                               : PXSYS_THEME_MODE_LIGHT;
    (void)pxsys_theme_service_update(pxsys_standard_system_theme(ui->system),
                                     &next);
}

static void launcher_clicked(lv_event_t* event) {
    launcher_item_t* item = (launcher_item_t*)lv_event_get_user_data(event);
    pxsys_intent_t intent = {0};
    pxsys_instance_ref_t instance;
    if (item == NULL || !ui_valid(item->ui)) return;
    intent.struct_size = sizeof(intent);
    intent.target = &item->identity;
    intent.action = pxsys_string_from_cstr("system.intent.main");
    intent.flags = PXSYS_INTENT_FLAG_CLEAR_TOP;
    (void)pxsys_task_manager_start(pxsys_standard_system_tasks(item->ui->system),
                                   &intent, &instance);
}

static lv_obj_t* make_label(lv_obj_t* parent, const char* text,
                            const lv_font_t* font, lv_color_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_color(label, color, 0);
    if (font != NULL) lv_obj_set_style_text_font(label, font, 0);
    return label;
}

#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_LIST
static lv_obj_t* make_button(pxsys_reference_lvgl_t* ui, lv_obj_t* parent,
                             const char* text, lv_event_cb_t clicked,
                             void* user_data) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_style_radius(button, ui->theme.base_radius_px * 2u, 0);
    lv_obj_set_style_bg_color(button, color_token(ui, PXSYS_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_color(button, color_token(ui, PXSYS_COLOR_BORDER),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, color_token(ui, PXSYS_COLOR_BORDER), 0);
    lv_obj_set_style_pad_all(button, 8, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    make_label(button, text, ui->text_font,
               color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
    lv_obj_set_width(lv_obj_get_child(button, 0), LV_PCT(100));
    lv_obj_set_style_text_align(lv_obj_get_child(button, 0), LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(lv_obj_get_child(button, 0));
    if (clicked != NULL)
        lv_obj_add_event_cb(button, clicked, LV_EVENT_CLICKED, user_data);
    return button;
}
#endif

static lv_obj_t* make_navigation_action(pxsys_reference_lvgl_t* ui,
                                        lv_obj_t* parent,
                                        navigation_icon_t icon,
                                        lv_event_cb_t clicked) {
    static const lv_point_precise_t back_points[] = {
        {10, 0}, {0, 7}, {10, 14}, {10, 0}};
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_t* glyph;
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(button, color_token(ui, PXSYS_COLOR_BORDER),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_40, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_radius(button, ui->theme.base_radius_px, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    if (icon == NAVIGATION_ICON_BACK) {
        glyph = lv_line_create(button);
        lv_line_set_points(glyph, back_points,
                           sizeof(back_points) / sizeof(back_points[0]));
        lv_obj_set_style_line_width(glyph, 2, 0);
        lv_obj_set_style_line_rounded(glyph, true, 0);
        lv_obj_set_style_line_color(
            glyph, color_token(ui, PXSYS_COLOR_TEXT_PRIMARY), 0);
    } else {
        glyph = lv_obj_create(button);
        style_plain(glyph);
        lv_obj_set_size(glyph, icon == NAVIGATION_ICON_HOME ? 15 : 14,
                        icon == NAVIGATION_ICON_HOME ? 15 : 14);
        lv_obj_set_style_bg_opa(glyph, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(glyph, 2, 0);
        lv_obj_set_style_border_color(
            glyph, color_token(ui, PXSYS_COLOR_TEXT_PRIMARY), 0);
        lv_obj_set_style_radius(
            glyph, icon == NAVIGATION_ICON_HOME ? LV_RADIUS_CIRCLE : 2, 0);
    }
    lv_obj_center(glyph);
    lv_obj_add_event_cb(button, clicked, LV_EVENT_CLICKED, ui);
    return button;
}

static lv_obj_t* application_root(const pxsys_reference_lvgl_t* ui) {
    lv_display_t* display = lv_obj_get_display(ui->root);
    if (ui->content_active && ui->content != NULL) return ui->content;
    return display == NULL ? NULL : lv_display_get_screen_active(display);
}

static void application_backdrop_apply(pxsys_reference_lvgl_t* ui) {
    lv_display_t* display = lv_obj_get_display(ui->root);
    lv_obj_t* app =
        display == NULL ? NULL : lv_display_get_screen_active(display);
    lv_obj_t* backdrop;
    if (app == NULL) return;
    backdrop = lv_display_get_layer_bottom(display);
    if (backdrop != NULL) {
        lv_obj_set_style_bg_color(
            backdrop, color_token(ui, PXSYS_COLOR_BACKGROUND), 0);
        lv_obj_set_style_bg_opa(backdrop, LV_OPA_COVER, 0);
    }
    lv_obj_set_style_bg_color(app, color_token(ui, PXSYS_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(app, LV_OPA_COVER, 0);
}

static void application_scale_set(void* object, int32_t value) {
    lv_obj_set_style_transform_scale_x((lv_obj_t*)object, value, 0);
    lv_obj_set_style_transform_scale_y((lv_obj_t*)object, value, 0);
}

static void application_y_set(void* object, int32_t value) {
    lv_obj_t* app = (lv_obj_t*)object;
    if (lv_obj_get_parent(app) == NULL)
        lv_obj_set_y(app, value);
    else
        lv_obj_set_style_translate_y(app, value, 0);
}

static void application_x_set(void* object, int32_t value) {
    lv_obj_t* app = (lv_obj_t*)object;
    if (lv_obj_get_parent(app) == NULL)
        lv_obj_set_x(app, value);
    else
        lv_obj_set_style_translate_x(app, value, 0);
}

#if PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS || \
    (PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && \
     LV_USE_SNAPSHOT)
static int32_t application_x_get(const lv_obj_t* app) {
    return lv_obj_get_parent(app) == NULL
               ? lv_obj_get_style_x(app, LV_PART_MAIN)
               : lv_obj_get_style_translate_x(app, LV_PART_MAIN);
}

static int32_t application_y_get(const lv_obj_t* app) {
    return lv_obj_get_parent(app) == NULL
               ? lv_obj_get_style_y(app, LV_PART_MAIN)
               : lv_obj_get_style_translate_y(app, LV_PART_MAIN);
}
#endif

static void application_motion_set(pxsys_reference_lvgl_t* ui, int32_t scale,
                                   int32_t translate_x, int32_t translate_y,
                                   int32_t radius) {
    lv_obj_t* app = application_root(ui);
    if (app == NULL) return;
    lv_obj_set_style_transform_pivot_x(app, lv_obj_get_width(app) / 2, 0);
    lv_obj_set_style_transform_pivot_y(app, lv_obj_get_height(app) / 2, 0);
    application_scale_set(app, scale);
    application_x_set(app, translate_x);
    application_y_set(app, translate_y);
    lv_obj_set_style_radius(app, radius, 0);
    lv_obj_set_style_clip_corner(app, radius > 0, 0);
}

#if PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS && \
    PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && \
    LV_USE_SNAPSHOT
static void object_y_set(void* object, int32_t value) {
    lv_obj_set_y((lv_obj_t*)object, value);
}

static void object_x_set(void* object, int32_t value) {
    lv_obj_set_x((lv_obj_t*)object, value);
}

static void image_scale_set(void* object, int32_t value) {
    lv_image_set_scale((lv_obj_t*)object, value);
}
#endif

static void application_scale_reset(pxsys_reference_lvgl_t* ui) {
    lv_obj_t* app = application_root(ui);
    if (app == NULL) return;
    lv_anim_delete(app, application_scale_set);
    lv_anim_delete(app, application_x_set);
    lv_anim_delete(app, application_y_set);
    application_motion_set(ui, 256, 0, 0, 0);
}

static void home_animation_completed(lv_anim_t* animation) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_anim_get_user_data(animation);
    if (!ui_valid(ui)) return;
    /* Reset while the leaving application's screen is still the active root;
     * after open_role the root points at the launcher content and the app
     * screen would keep the fly-away transform for its next launch. */
    application_scale_reset(ui);
    (void)open_role(ui, PXSYS_ROLE_HOME);
    application_scale_reset(ui);
}

static void animate_application(pxsys_reference_lvgl_t* ui,
                                int32_t target_scale, int32_t target_x,
                                int32_t target_y,
                                lv_anim_completed_cb_t completed) {
    lv_obj_t* app = application_root(ui);
    if (app == NULL) return;
#if PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS
    if (ui->animations_enabled) {
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, app);
        lv_anim_set_exec_cb(&animation, application_scale_set);
        lv_anim_set_values(
            &animation, lv_obj_get_style_transform_scale_x(app, 0),
            target_scale);
        lv_anim_set_duration(&animation, 180);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
        lv_anim_set_user_data(&animation, ui);
        if (completed != NULL) lv_anim_set_completed_cb(&animation, completed);
        lv_anim_start(&animation);
        lv_anim_set_exec_cb(&animation, application_x_set);
        lv_anim_set_values(&animation, application_x_get(app), target_x);
        lv_anim_set_completed_cb(&animation, NULL);
        lv_anim_start(&animation);
        lv_anim_set_exec_cb(&animation, application_y_set);
        lv_anim_set_values(&animation, application_y_get(app), target_y);
        lv_anim_set_completed_cb(&animation, NULL);
        lv_anim_start(&animation);
        return;
    }
#else
    (void)target_scale;
    (void)target_x;
    (void)target_y;
#endif
    if (completed == home_animation_completed) {
        application_scale_reset(ui);
        (void)open_role(ui, PXSYS_ROLE_HOME);
    } else {
        application_scale_reset(ui);
    }
}

static int app_is_system_home(
    pxsys_reference_lvgl_t* ui, const pxsys_app_descriptor_t* app) {
    const pxsys_app_descriptor_t* home = NULL;
    if (app == NULL ||
        pxsys_role_resolve(pxsys_standard_system_roles(ui->system),
                           pxsys_string_from_cstr(PXSYS_ROLE_HOME), &home) !=
            PXSYS_STATUS_OK ||
        home == NULL)
        return 0;
    return pxsys_app_identity_equal(&app->identity, &home->identity);
}

#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && LV_USE_SNAPSHOT
static int instance_equal(pxsys_instance_ref_t left,
                          pxsys_instance_ref_t right) {
    return left.slot == right.slot && left.generation == right.generation;
}

static task_preview_t* find_task_preview(pxsys_reference_lvgl_t* ui,
                                         pxsys_instance_ref_t instance) {
    size_t index;
    for (index = 0; index < PXSYS_REFERENCE_UI_TASK_PREVIEW_COUNT; ++index) {
        if (ui->task_previews[index].image != NULL &&
            instance_equal(ui->task_previews[index].instance, instance))
            return &ui->task_previews[index];
    }
    return NULL;
}

static lv_draw_buf_t* capture_transformed_application(
    pxsys_reference_lvgl_t* ui, pxsys_rect_t* capture_bounds) {
    lv_obj_t* app = application_root(ui);
    lv_area_t transformed;
    lv_area_t display_area;
    lv_area_t clipped;
    lv_draw_buf_t* source;
    lv_draw_buf_t* result;
    int32_t scale;
    int32_t translate_x;
    int32_t translate_y;
    int32_t radius;
    int32_t source_width;
    int32_t source_height;
    int32_t ext;
    int32_t transformed_width;
    int32_t transformed_height;
    int32_t result_width;
    int32_t result_height;
    int32_t x;
    int32_t y;
    if (app == NULL || capture_bounds == NULL) return NULL;

    lv_obj_update_layout(app);
    lv_obj_get_coords(app, &transformed);
    lv_obj_get_transformed_area(app, &transformed,
                                LV_OBJ_POINT_TRANSFORM_FLAG_NONE);
    display_area.x1 = 0;
    display_area.y1 = 0;
    display_area.x2 = (int32_t)ui->display.width - 1;
    display_area.y2 = (int32_t)ui->display.height - 1;
    clipped = transformed;
    if (clipped.x1 < display_area.x1) clipped.x1 = display_area.x1;
    if (clipped.y1 < display_area.y1) clipped.y1 = display_area.y1;
    if (clipped.x2 > display_area.x2) clipped.x2 = display_area.x2;
    if (clipped.y2 > display_area.y2) clipped.y2 = display_area.y2;
    if (clipped.x1 > clipped.x2 || clipped.y1 > clipped.y2) return NULL;

    scale = lv_obj_get_style_transform_scale_x(app, LV_PART_MAIN);
    translate_x = application_x_get(app);
    translate_y = application_y_get(app);
    radius = lv_obj_get_style_radius(app, LV_PART_MAIN);
    source_width = lv_obj_get_width(app);
    source_height = lv_obj_get_height(app);

    /* Snapshot rendering is synchronous. Resetting only for the off-screen
     * draw and restoring before returning means the live display never sees
     * an untransformed frame. */
    application_motion_set(ui, 256, 0, 0, 0);
    source = lv_snapshot_take(app, LV_COLOR_FORMAT_RGB565);
    application_motion_set(ui, scale, translate_x, translate_y, radius);
    if (source == NULL || source_width <= 0 || source_height <= 0) return NULL;
    ext = ((int32_t)source->header.w - source_width) / 2;
    if (ext < 0) ext = 0;

    transformed_width = lv_area_get_width(&transformed);
    transformed_height = lv_area_get_height(&transformed);
    result_width = lv_area_get_width(&clipped);
    result_height = lv_area_get_height(&clipped);
    result = lv_draw_buf_create((uint32_t)result_width,
                                (uint32_t)result_height,
                                LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    if (result == NULL) {
        lv_draw_buf_destroy(source);
        return NULL;
    }

    for (y = 0; y < result_height; ++y) {
        int32_t source_y = ext +
            (int32_t)(((int64_t)(clipped.y1 + y - transformed.y1) *
                       source_height) / transformed_height);
        uint16_t* destination_row =
            (uint16_t*)lv_draw_buf_goto_xy(result, 0, (uint32_t)y);
        const uint16_t* source_row;
        if (source_y < ext) source_y = ext;
        if (source_y >= ext + source_height)
            source_y = ext + source_height - 1;
        source_row = (const uint16_t*)lv_draw_buf_goto_xy(
            source, 0, (uint32_t)source_y);
        for (x = 0; x < result_width; ++x) {
            int32_t source_x = ext +
                (int32_t)(((int64_t)(clipped.x1 + x - transformed.x1) *
                           source_width) / transformed_width);
            if (source_x < ext) source_x = ext;
            if (source_x >= ext + source_width)
                source_x = ext + source_width - 1;
            destination_row[x] = source_row[source_x];
        }
    }
    lv_draw_buf_destroy(source);
    capture_bounds->x = clipped.x1;
    capture_bounds->y = clipped.y1;
    capture_bounds->width = (uint32_t)result_width;
    capture_bounds->height = (uint32_t)result_height;
    return result;
}
#endif

static void record_recent_history(pxsys_reference_lvgl_t* ui,
                                  const pxsys_app_descriptor_t* app) {
    char app_id[65];
    pxsys_string_t display_name;
    size_t index;
    size_t match;
    if (!ui_valid(ui) || app == NULL) return;
    if (app->identity.app_id.size == 0 ||
        app->identity.app_id.size >= sizeof(app_id))
        return;
    memcpy(app_id, app->identity.app_id.data, app->identity.app_id.size);
    app_id[app->identity.app_id.size] = '\0';
    match = ui->recent_history_count;
    for (index = 0; index < ui->recent_history_count; ++index) {
        if (memcmp(ui->recent_history[index].publisher_root,
                   app->identity.publisher_root,
                   PXSYS_PUBLISHER_ROOT_BYTES) == 0 &&
            strcmp(ui->recent_history[index].app_id, app_id) == 0) {
            match = index;
            break;
        }
    }
    if (match == ui->recent_history_count) {
        if (ui->recent_history_count < REFERENCE_RECENT_HISTORY) {
            ui->recent_history_count++;
        } else {
            match = REFERENCE_RECENT_HISTORY - 1u; /* reuse the oldest slot */
        }
    }
    if (match != 0) {
        recent_app_t entry = ui->recent_history[match];
        memmove(&ui->recent_history[1], &ui->recent_history[0],
                match * sizeof(entry));
        ui->recent_history[0] = entry;
    }
    memcpy(ui->recent_history[0].publisher_root, app->identity.publisher_root,
           PXSYS_PUBLISHER_ROOT_BYTES);
    memcpy(ui->recent_history[0].app_id, app_id, sizeof(app_id));
    display_name = localized_app_name(ui, app);
    snprintf(ui->recent_history[0].label, sizeof(ui->recent_history[0].label),
             "%.*s", (int)display_name.size, display_name.data);
    ui->recent_history[0].last_used = ++ui->preview_clock;
}

static void capture_current_task(pxsys_reference_lvgl_t* ui,
                                 int transition_pending) {
    pxsys_task_manager_t* tasks;
    pxsys_instance_ref_t current;
    pxsys_instance_snapshot_t snapshot = {0};
    if (!ui_valid(ui)) return;
    tasks = pxsys_standard_system_tasks(ui->system);
    snapshot.struct_size = sizeof(snapshot);
    if (pxsys_task_manager_task(tasks, 0, &current, &snapshot) !=
            PXSYS_STATUS_OK ||
        snapshot.app == NULL || app_is_system_home(ui, snapshot.app))
        return;
    record_recent_history(ui, snapshot.app);
#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && \
    LV_USE_SNAPSHOT
    {
        task_preview_t* preview = find_task_preview(ui, current);
        lv_draw_buf_t* image;
        pxsys_rect_t capture_bounds = {0};
        size_t index;
        for (index = 0; index < PXSYS_REFERENCE_UI_TASK_PREVIEW_COUNT;
             ++index)
            ui->task_previews[index].transition_pending = 0;
        if (preview == NULL) {
            for (index = 0; index < PXSYS_REFERENCE_UI_TASK_PREVIEW_COUNT;
                 ++index) {
                if (ui->task_previews[index].image == NULL) {
                    preview = &ui->task_previews[index];
                    break;
                }
                if (preview == NULL || ui->task_previews[index].last_used <
                                           preview->last_used)
                    preview = &ui->task_previews[index];
            }
        }
        if (preview == NULL) return;
        if (preview->image != NULL) {
            lv_draw_buf_destroy(preview->image);
            preview->image = NULL;
        }
        image = capture_transformed_application(ui, &capture_bounds);
        if (image == NULL) return;
        preview->image = image;
        preview->capture_bounds = capture_bounds;
        preview->instance = current;
        preview->last_used = ++ui->preview_clock;
        preview->transition_pending = transition_pending != 0;
    }
#else
    (void)transition_pending;
#endif
}

#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && LV_USE_SNAPSHOT
static void clear_task_transition(pxsys_reference_lvgl_t* ui) {
    if (ui->task_transition_preview != NULL)
        ui->task_transition_preview->transition_pending = 0;
    ui->task_transition_image = NULL;
    ui->task_transition_target = NULL;
    ui->task_transition_preview = NULL;
}

#if PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS
static void task_transition_completed(lv_anim_t* animation) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_anim_get_user_data(animation);
    if (!ui_valid(ui)) return;
    if (ui->task_transition_target != NULL)
        lv_obj_remove_flag(ui->task_transition_target, LV_OBJ_FLAG_HIDDEN);
    if (ui->task_transition_image != NULL)
        lv_obj_delete(ui->task_transition_image);
    clear_task_transition(ui);
}
#endif

static int start_task_transition(pxsys_reference_lvgl_t* ui) {
    lv_obj_t* target = ui->task_transition_target;
    task_preview_t* preview = ui->task_transition_preview;
    lv_area_t target_area;
    lv_obj_t* image;
    int32_t target_scale;
    if (target == NULL || preview == NULL || preview->image == NULL) {
        clear_task_transition(ui);
        return 0;
    }
#if PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS
    if (ui->animations_enabled) {
        lv_anim_t animation;
        lv_obj_update_layout(ui->task_switcher);
        lv_obj_get_coords(target, &target_area);
        lv_obj_get_transformed_area(target, &target_area,
                                    LV_OBJ_POINT_TRANSFORM_FLAG_NONE);
        target_scale = lv_image_get_scale(target);
        image = lv_image_create(ui->task_switcher);
        lv_image_set_src(image, preview->image);
        lv_image_set_pivot(image, 0, 0);
        lv_obj_set_pos(image, preview->capture_bounds.x,
                       preview->capture_bounds.y);
        lv_obj_move_foreground(image);
        ui->task_transition_image = image;

        lv_anim_init(&animation);
        lv_anim_set_var(&animation, image);
        lv_anim_set_duration(&animation, 180);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&animation, object_x_set);
        lv_anim_set_values(&animation, preview->capture_bounds.x,
                           target_area.x1);
        lv_anim_start(&animation);
        lv_anim_set_exec_cb(&animation, image_scale_set);
        lv_anim_set_values(&animation, 256, target_scale);
        lv_anim_start(&animation);
        lv_anim_set_exec_cb(&animation, object_y_set);
        lv_anim_set_values(&animation, preview->capture_bounds.y,
                           target_area.y1);
        lv_anim_set_user_data(&animation, ui);
        lv_anim_set_completed_cb(&animation, task_transition_completed);
        lv_anim_start(&animation);
        return 1;
    }
#else
    (void)target_area;
    (void)image;
    (void)target_scale;
#endif
    lv_obj_remove_flag(target, LV_OBJ_FLAG_HIDDEN);
    clear_task_transition(ui);
    return 0;
}
#endif

static void close_task_switcher(pxsys_reference_lvgl_t* ui) {
#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && \
    LV_USE_SNAPSHOT
    clear_task_transition(ui);
#endif
    /* Do not delete this tree while the LVGL input/event machinery may still
     * reference it. Some LVGL event stacks become cyclic during deletion,
     * making lv_event_mark_deleted() spin until the task watchdog fires.
     * Keep the bounded switcher tree and reuse it on its next invocation. */
    if (ui->task_switcher != NULL)
        lv_obj_add_flag(ui->task_switcher, LV_OBJ_FLAG_HIDDEN);
    application_scale_reset(ui);
}

static int task_switcher_is_open(const pxsys_reference_lvgl_t* ui) {
    return ui->task_switcher != NULL &&
           !lv_obj_has_flag(ui->task_switcher, LV_OBJ_FLAG_HIDDEN);
}

static void recent_clicked(lv_event_t* event) {
    recent_item_t* item = (recent_item_t*)lv_event_get_user_data(event);
    if (item == NULL || !ui_valid(item->ui)) return;
    if (item->suppress_click) {
        item->suppress_click = 0;
        return;
    }
#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && LV_USE_SNAPSHOT
    {
        lv_indev_t* indev = lv_event_get_indev(event);
        lv_obj_t* card = lv_event_get_current_target(event);
        lv_area_t area;
        lv_point_t point;
        if (indev != NULL && card != NULL) {
            lv_indev_get_point(indev, &point);
            lv_obj_get_coords(card, &area);
            point.x -= area.x1;
            point.y -= area.y1;
            if (point.x < item->preview_hit_x ||
                point.x >= item->preview_hit_x + item->preview_hit_width ||
                point.y < item->preview_hit_y ||
                point.y >= item->preview_hit_y + item->preview_hit_height) {
                close_task_switcher(item->ui);
                return;
            }
        }
    }
#endif
    if (item->running && pxsys_task_manager_activate(
                             pxsys_standard_system_tasks(item->ui->system),
                             item->instance) == PXSYS_STATUS_OK) {
        close_task_switcher(item->ui);
        return;
    }
    /* A retained card can outlive its instance. Fall back to a cold start
     * rather than trying to activate an invalid instance reference. */
    {
        pxsys_intent_t intent = {0};
        pxsys_instance_ref_t instance;
        pxsys_status_t status;
        intent.struct_size = sizeof(intent);
        intent.target = &item->identity;
        intent.action = pxsys_string_from_cstr("system.intent.main");
        status = pxsys_task_manager_start(
            pxsys_standard_system_tasks(item->ui->system), &intent, &instance);
        if (status != PXSYS_STATUS_OK && status != PXSYS_STATUS_PENDING)
            return;
    }
    close_task_switcher(item->ui);
}

static int instance_same(pxsys_instance_ref_t left,
                         pxsys_instance_ref_t right) {
    return left.slot == right.slot && left.generation == right.generation;
}

static int dismissal_pending(const pxsys_reference_lvgl_t* ui,
                             pxsys_instance_ref_t instance) {
    size_t index;
    for (index = 0; index < ui->pending_dismissal_count; ++index) {
        if (instance_same(ui->pending_dismissals[index], instance)) return 1;
    }
    return 0;
}

static void remember_pending_dismissal(pxsys_reference_lvgl_t* ui,
                                       pxsys_instance_ref_t instance) {
    size_t capacity = sizeof(ui->pending_dismissals) /
                      sizeof(ui->pending_dismissals[0]);
    if (dismissal_pending(ui, instance)) return;
    if (ui->pending_dismissal_count < capacity) {
        ui->pending_dismissals[ui->pending_dismissal_count++] = instance;
        return;
    }
    memmove(&ui->pending_dismissals[0], &ui->pending_dismissals[1],
            (capacity - 1u) * sizeof(ui->pending_dismissals[0]));
    ui->pending_dismissals[capacity - 1u] = instance;
}

static void recent_history_remove(pxsys_reference_lvgl_t* ui,
                                  const recent_item_t* item) {
    size_t index;
    for (index = 0; index < ui->recent_history_count; ++index) {
        if (memcmp(ui->recent_history[index].publisher_root,
                   item->identity.publisher_root,
                   PXSYS_PUBLISHER_ROOT_BYTES) == 0 &&
            strcmp(ui->recent_history[index].app_id, item->app_id) == 0) {
            memmove(&ui->recent_history[index], &ui->recent_history[index + 1u],
                    (ui->recent_history_count - index - 1u) *
                        sizeof(recent_app_t));
            ui->recent_history_count--;
            return;
        }
    }
}

static void refresh_task_switcher_async(void* context) {
    pxsys_reference_lvgl_t* ui = (pxsys_reference_lvgl_t*)context;
    if (!ui_valid(ui) || ui->task_switcher == NULL) return;
    build_task_switcher(ui);
}

static void dismiss_recent_item(pxsys_reference_lvgl_t* ui,
                                recent_item_t* item) {
    if (!ui_valid(ui) || item == NULL) return;
    if (item->running) {
        pxsys_status_t status = pxsys_task_manager_finish_instance(
            pxsys_standard_system_tasks(ui->system), item->instance,
            PXSYS_STOP_NORMAL);
        if (status != PXSYS_STATUS_OK && status != PXSYS_STATUS_PENDING)
            return;
        if (status == PXSYS_STATUS_PENDING)
            remember_pending_dismissal(ui, item->instance);
    }
    recent_history_remove(ui, item);
    /* Refresh the bounded card slots in place. This removes the dismissed
     * card without deleting the switcher tree during an input event. Delay
     * the refresh so a trailing CLICKED event still sees suppress_click. */
    ui->task_switcher_handoff = 1;
    if (lv_async_call(refresh_task_switcher_async, ui) != LV_RESULT_OK)
        close_task_switcher(ui);
}

#if !(PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && \
      LV_USE_SNAPSHOT)
static void close_clicked(lv_event_t* event) {
    recent_item_t* item = (recent_item_t*)lv_event_get_user_data(event);
    if (item == NULL || !ui_valid(item->ui)) return;
    dismiss_recent_item(item->ui, item);
}
#endif

static void home_clicked(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    if (!ui_valid(ui)) return;
    if (ui->notification_shade_open) close_notification_shade(ui);
    capture_current_task(ui, 0);
    animate_application(ui, 212, 0, -(int32_t)ui->display.height / 3,
                        home_animation_completed);
}

static void recents_clicked(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    if (!ui_valid(ui)) return;
    if (ui->notification_shade_open) close_notification_shade(ui);
    capture_current_task(ui, 0);
    build_task_switcher(ui);
    animate_application(ui, 232, 0, -18, NULL);
}

static void task_switcher_background_clicked(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    if (ui_valid(ui) &&
        lv_event_get_target(event) == lv_event_get_current_target(event))
        close_task_switcher(ui);
}

#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && LV_USE_SNAPSHOT
static void recent_card_event(lv_event_t* event) {
    recent_item_t* item = (recent_item_t*)lv_event_get_user_data(event);
    lv_obj_t* card;
    lv_indev_t* indev;
    lv_point_t point;
    int32_t drag;
    if (item == NULL || !ui_valid(item->ui)) return;
    card = lv_event_get_current_target(event);
    indev = lv_event_get_indev(event);
    if (indev == NULL) return;
    lv_indev_get_point(indev, &point);
    switch (lv_event_get_code(event)) {
    case LV_EVENT_PRESSED:
        item->press_y = point.y;
        item->dragging = 1;
        item->suppress_click = 0;
        break;
    case LV_EVENT_PRESSING:
        if (!item->dragging) break;
        drag = item->press_y - point.y;
        if (drag < 0) drag = 0;
        lv_obj_set_style_translate_y(card, (lv_coord_t)-drag, 0);
        break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        item->dragging = 0;
        drag = item->press_y - point.y;
        if (drag >= item->dismiss_threshold) {
            item->suppress_click = 1;
            dismiss_recent_item(item->ui, item);
        } else {
            lv_obj_set_style_translate_y(card, 0, 0);
        }
        break;
    default:
        break;
    }
}

static void update_recent_card(pxsys_reference_lvgl_t* ui, lv_obj_t* card,
                               recent_item_t* item, int32_t card_width,
                               int32_t card_height, uint32_t padding) {
    task_preview_t* preview = find_task_preview(ui, item->instance);
    lv_obj_t* preview_area = lv_obj_get_child(card, 0);
    lv_obj_t* label = lv_obj_get_child(card, 1);
    lv_obj_t* image = lv_obj_get_child(preview_area, 0);
    int32_t label_height = ui->text_font != NULL
                               ? (int32_t)ui->text_font->line_height + 6
                               : 22;
    int32_t preview_height = card_height - label_height - (int32_t)padding;
    lv_obj_remove_flag(card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_translate_y(card, 0, 0);
    lv_obj_set_size(card, card_width, card_height);
    lv_obj_set_size(preview_area, card_width, preview_height);
    lv_obj_set_width(label, card_width);
    lv_obj_set_height(label, label_height);
    lv_label_set_text(label, item->label);
    item->preview_hit_x = 0;
    item->preview_hit_y = 0;
    item->preview_hit_width = card_width;
    item->preview_hit_height = card_height;
    item->dismiss_threshold = card_height / 4;
    if (item->dismiss_threshold < 32) item->dismiss_threshold = 32;
    if (preview != NULL && preview->image != NULL &&
        preview->image->header.w != 0 && preview->image->header.h != 0) {
        uint32_t scale_x = (uint32_t)card_width * 256u /
                           preview->image->header.w;
        uint32_t scale_y = (uint32_t)preview_height * 256u /
                           preview->image->header.h;
        uint32_t scale = scale_x < scale_y ? scale_x : scale_y;
        int32_t rendered_width =
            (int32_t)((uint32_t)preview->image->header.w * scale / 256u);
        int32_t rendered_height =
            (int32_t)((uint32_t)preview->image->header.h * scale / 256u);
        lv_image_set_src(image, preview->image);
        lv_image_set_scale(image, scale);
        lv_obj_center(image);
        lv_obj_remove_flag(image, LV_OBJ_FLAG_HIDDEN);
        item->preview_hit_x = (card_width - rendered_width) / 2;
        item->preview_hit_y = (preview_height - rendered_height) / 2;
        item->preview_hit_width = rendered_width;
        /* Keep the app name associated with the card actionable, while the
         * letterboxed area to either side remains background and closes the
         * switcher. */
        item->preview_hit_height = card_height - item->preview_hit_y;
        if (preview->transition_pending) {
            ui->task_transition_target = image;
            ui->task_transition_preview = preview;
            lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t* make_recent_card(pxsys_reference_lvgl_t* ui,
                                  lv_obj_t* parent, recent_item_t* item,
                                  int32_t card_width, int32_t card_height,
                                  uint32_t padding) {
    lv_obj_t* card = lv_button_create(parent);
    lv_obj_t* preview_area;
    lv_obj_t* image;
    lv_obj_t* label;
    int32_t label_height = ui->text_font != NULL
                               ? (int32_t)ui->text_font->line_height + 6
                               : 22;
    int32_t preview_height = card_height - label_height - (int32_t)padding;
    lv_obj_set_size(card, card_width, card_height);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_opa(card, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_SNAPPABLE);

    preview_area = lv_obj_create(card);
    style_plain(preview_area);
    lv_obj_set_pos(preview_area, 0, 0);
    lv_obj_set_size(preview_area, card_width, preview_height);
    lv_obj_set_style_radius(preview_area, ui->theme.base_radius_px * 3u, 0);
    lv_obj_set_style_bg_color(preview_area,
                              color_token(ui, PXSYS_COLOR_SURFACE), 0);
    lv_obj_set_style_clip_corner(preview_area, true, 0);
    image = lv_image_create(preview_area);
    lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);

    label = make_label(card, item->label, ui->text_font,
                       color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
    lv_obj_set_width(label, card_width);
    lv_obj_set_height(label, label_height);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(card, recent_clicked, LV_EVENT_CLICKED, item);
    lv_obj_add_event_cb(card, recent_card_event, LV_EVENT_PRESSED, item);
    lv_obj_add_event_cb(card, recent_card_event, LV_EVENT_PRESSING, item);
    lv_obj_add_event_cb(card, recent_card_event, LV_EVENT_RELEASED, item);
    lv_obj_add_event_cb(card, recent_card_event, LV_EVENT_PRESS_LOST, item);
    update_recent_card(ui, card, item, card_width, card_height, padding);
    return card;
}
#endif

static int format_recents_memory(pxsys_reference_lvgl_t* ui, char* output,
                                 size_t capacity) {
    uint64_t available = 0;
    uint64_t total = 0;
    double divisor;
    const char* unit;
    if (output == NULL || capacity == 0) return 0;
    output[0] = '\0';
    if (ui->memory_info == NULL ||
        !ui->memory_info(ui->memory_info_context, &available, &total) ||
        total == 0)
        return 0;
    if (available > total) available = total;
    if (total >= UINT64_C(1024) * 1024 * 1024) {
        divisor = 1024.0 * 1024.0 * 1024.0;
        unit = "GB";
    } else if (total >= UINT64_C(1024) * 1024) {
        divisor = 1024.0 * 1024.0;
        unit = "MB";
    } else {
        divisor = 1024.0;
        unit = "KB";
    }
    snprintf(output, capacity, "%s %.1f / %.1f %s",
             translated(ui, "recents.memory.available", "Free"),
             (double)available / divisor, (double)total / divisor, unit);
    return output[0] != '\0';
}

static void build_task_switcher(pxsys_reference_lvgl_t* ui) {
    pxsys_reference_layout_t layout;
    pxsys_task_manager_t* tasks;
    lv_obj_t* panel;
    char memory_text[64];
    int32_t panel_x;
    int32_t panel_y;
    int32_t panel_width;
    int32_t panel_height;
    int32_t header_height = 0;
    size_t count;
    size_t index;
    int handoff;
#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && LV_USE_SNAPSHOT
    int retained;
#endif
    if (!ui_valid(ui) ||
        pxsys_reference_layout_compute(&ui->display, &layout) != PXSYS_STATUS_OK)
        return;
    handoff = ui->task_switcher_handoff != 0;
    ui->task_switcher_handoff = 0;
#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && \
    LV_USE_SNAPSHOT
    clear_task_transition(ui);
    retained = ui->task_switcher != NULL;
#else
    if (ui->task_switcher != NULL) {
        lv_obj_delete(ui->task_switcher);
        ui->task_switcher = NULL;
        ui->task_switcher_memory = NULL;
    }
#endif
    tasks = pxsys_standard_system_tasks(ui->system);
    count = pxsys_task_manager_count(tasks);
    if (count > sizeof(ui->recent_items) / sizeof(ui->recent_items[0]))
        count = sizeof(ui->recent_items) / sizeof(ui->recent_items[0]);
    if (ui->task_switcher == NULL) {
        ui->task_switcher = lv_obj_create(ui->parent);
        style_plain(ui->task_switcher);
        lv_obj_set_size(ui->task_switcher, (lv_coord_t)ui->display.width,
                        (lv_coord_t)ui->display.height);
        /* Solid backdrop so the cards do not blend with the page behind them;
         * a product may replace this with a wallpaper later. */
        lv_obj_set_style_bg_color(ui->task_switcher,
                                  color_token(ui, PXSYS_COLOR_BACKGROUND), 0);
        lv_obj_set_style_bg_opa(ui->task_switcher, LV_OPA_COVER, 0);
        lv_obj_add_flag(ui->task_switcher, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(ui->task_switcher,
                            task_switcher_background_clicked,
                            LV_EVENT_CLICKED, ui);
        panel = lv_obj_create(ui->task_switcher);
        style_plain(panel);
        lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(panel, task_switcher_background_clicked,
                            LV_EVENT_CLICKED, ui);
    } else {
        panel = lv_obj_get_child(ui->task_switcher, 0);
        lv_obj_remove_flag(ui->task_switcher, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_size(ui->task_switcher, (lv_coord_t)ui->display.width,
                    (lv_coord_t)ui->display.height);
    panel_x = (int32_t)layout.safe_area.x + (int32_t)layout.outer_padding;
    panel_y = (int32_t)layout.safe_area.y + (int32_t)layout.outer_padding;
    panel_width = (int32_t)layout.safe_area.width -
                  (int32_t)layout.outer_padding * 2;
    panel_height = (int32_t)layout.safe_area.height -
                   (int32_t)layout.outer_padding * 2;
    if (format_recents_memory(ui, memory_text, sizeof(memory_text))) {
        const lv_font_t* font = typography_font(ui, PXSYS_TYPOGRAPHY_CAPTION);
        header_height = font != NULL ? (int32_t)font->line_height + 6 : 18;
        if (ui->task_switcher_memory == NULL) {
            ui->task_switcher_memory = make_label(
                ui->task_switcher, memory_text, font,
                color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
        } else {
            lv_label_set_text(ui->task_switcher_memory, memory_text);
            lv_obj_remove_flag(ui->task_switcher_memory, LV_OBJ_FLAG_HIDDEN);
        }
        if (ui->task_switcher_memory != NULL) {
            lv_obj_set_pos(ui->task_switcher_memory, panel_x, panel_y);
            lv_obj_set_size(ui->task_switcher_memory, panel_width,
                            header_height);
            lv_obj_set_style_text_align(ui->task_switcher_memory,
                                        LV_TEXT_ALIGN_RIGHT, 0);
        }
    } else if (ui->task_switcher_memory != NULL) {
        lv_obj_add_flag(ui->task_switcher_memory, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_pos(panel, panel_x, panel_y + header_height);
    lv_obj_set_size(panel, panel_width, panel_height - header_height);
    {
#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && LV_USE_SNAPSHOT
        lv_obj_t* strip = NULL;
#endif
        uint32_t added = 0;
        size_t item_capacity =
            sizeof(ui->recent_items) / sizeof(ui->recent_items[0]);
        size_t hindex;
#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && LV_USE_SNAPSHOT
        int32_t card_width = panel_width * 4 / 5;
        int32_t card_height = panel_height;
        card_height -= header_height;
        uint32_t card_padding = layout.outer_padding > 8
                                    ? 8u : layout.outer_padding;
        if (card_width < 96) card_width = 96;
        if (card_height < 96) card_height = 96;
        if (retained) {
            strip = lv_obj_get_child(panel, 0);
        } else {
            strip = lv_obj_create(panel);
            style_plain(strip);
            lv_obj_set_style_bg_opa(strip, LV_OPA_TRANSP, 0);
            lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
            lv_obj_set_scroll_dir(strip, LV_DIR_HOR);
            lv_obj_set_scroll_snap_x(strip, LV_SCROLL_SNAP_CENTER);
            lv_obj_add_flag(strip,
                            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                                LV_OBJ_FLAG_SCROLL_ONE);
            lv_obj_add_event_cb(strip, task_switcher_background_clicked,
                                LV_EVENT_CLICKED, ui);
        }
        lv_obj_set_pos(strip, 0, 0);
        lv_obj_set_size(strip, panel_width, card_height);
        lv_obj_set_style_pad_left(strip, (panel_width - card_width) / 2, 0);
        lv_obj_set_style_pad_right(strip, (panel_width - card_width) / 2, 0);
        lv_obj_set_style_pad_column(strip, layout.item_gap, 0);
#else
        lv_obj_set_style_pad_top(panel, 0, 0);
        lv_obj_set_style_pad_row(panel, layout.item_gap, 0);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
#endif
        for (index = 0; index < count && added < item_capacity; ++index) {
            pxsys_instance_snapshot_t snapshot = {0};
            recent_item_t* item = &ui->recent_items[added];
            pxsys_string_t display_name;
            snapshot.struct_size = sizeof(snapshot);
            memset(item, 0, sizeof(*item));
            item->ui = ui;
            if (pxsys_task_manager_task(tasks, index, &item->instance,
                                        &snapshot) != PXSYS_STATUS_OK ||
                snapshot.app == NULL || app_is_system_home(ui, snapshot.app) ||
                dismissal_pending(ui, item->instance))
                continue;
            display_name = localized_app_name(ui, snapshot.app);
            snprintf(item->label, sizeof(item->label), "%.*s",
                     (int)display_name.size, display_name.data);
            memcpy(item->app_id, snapshot.app->identity.app_id.data,
                   snapshot.app->identity.app_id.size);
            item->app_id[snapshot.app->identity.app_id.size] = '\0';
            item->identity = snapshot.app->identity;
            item->identity.app_id = pxsys_string_from_cstr(item->app_id);
            item->running = 1;
            added++;
        }
        /* Then recently used apps which are not running anymore; tapping them
         * performs a cold start. */
        for (hindex = 0;
             hindex < ui->recent_history_count && added < item_capacity;
             ++hindex) {
            recent_app_t* entry = &ui->recent_history[hindex];
            recent_item_t* item;
            const pxsys_app_descriptor_t* app;
            pxsys_string_t display_name;
            int duplicate = 0;
            for (index = 0; index < added; ++index) {
                recent_item_t* known = &ui->recent_items[index];
                if (memcmp(known->identity.publisher_root,
                           entry->publisher_root,
                           PXSYS_PUBLISHER_ROOT_BYTES) == 0 &&
                    strcmp(known->app_id, entry->app_id) == 0) {
                    duplicate = 1;
                    break;
                }
            }
            if (duplicate) continue;
            item = &ui->recent_items[added];
            memset(item, 0, sizeof(*item));
            item->ui = ui;
            item->instance = pxsys_instance_ref_invalid();
            memcpy(item->app_id, entry->app_id, sizeof(item->app_id));
            memcpy(item->identity.publisher_root, entry->publisher_root,
                   PXSYS_PUBLISHER_ROOT_BYTES);
            item->identity.app_id = pxsys_string_from_cstr(item->app_id);
            app = pxsys_app_registry_find(
                pxsys_standard_system_apps(ui->system), &item->identity);
            display_name = app == NULL ? pxsys_string_from_cstr(entry->label)
                                       : localized_app_name(ui, app);
            snprintf(item->label, sizeof(item->label), "%.*s",
                     (int)display_name.size, display_name.data);
            item->running = 0;
            added++;
        }
        for (index = 0; index < added; ++index) {
            recent_item_t* item = &ui->recent_items[index];
#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && LV_USE_SNAPSHOT
            lv_obj_t* card = lv_obj_get_child(strip, (int32_t)index);
            if (card == NULL)
                card = make_recent_card(ui, strip, item, card_width,
                                        card_height, card_padding);
            else
                update_recent_card(ui, card, item, card_width, card_height,
                                   card_padding);
#else
            {
                lv_obj_t* row = lv_obj_create(panel);
                lv_obj_t* button;
                lv_obj_t* close;
                lv_obj_t* close_label;
                int32_t row_height =
                    layout.size_class == PXSYS_UI_SIZE_COMPACT ? 38 : 48;
                style_plain(row);
                lv_obj_set_size(row, LV_PCT(100), (lv_coord_t)row_height);
                lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                                      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
                lv_obj_set_style_pad_column(row, layout.item_gap, 0);
                button = make_button(ui, row, item->label, recent_clicked, item);
                lv_obj_set_flex_grow(button, 1);
                lv_obj_set_height(button, LV_PCT(100));
                close = make_button(ui, row, "X", NULL, NULL);
                lv_obj_set_size(close, (lv_coord_t)row_height,
                                (lv_coord_t)row_height);
                close_label = lv_obj_get_child(close, 0);
                if (close_label != NULL)
                    lv_obj_set_style_text_color(
                        close_label,
                        color_token(ui, PXSYS_COLOR_TEXT_SECONDARY), 0);
                lv_obj_add_event_cb(close, close_clicked, LV_EVENT_CLICKED,
                                    item);
            }
#endif
        }
#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && LV_USE_SNAPSHOT
        for (index = added; index < lv_obj_get_child_count(strip); ++index) {
            lv_obj_t* card = lv_obj_get_child(strip, (int32_t)index);
            if (card != NULL) lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
        }
        if (added == 0) {
            if (ui->task_switcher_empty == NULL) {
                ui->task_switcher_empty = make_label(
                    panel, translated(ui, "recents.empty", "No recent apps"),
                    typography_font(ui, PXSYS_TYPOGRAPHY_BODY),
                    color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
                lv_obj_center(ui->task_switcher_empty);
            } else {
                lv_label_set_text(
                    ui->task_switcher_empty,
                    translated(ui, "recents.empty", "No recent apps"));
                lv_obj_remove_flag(ui->task_switcher_empty,
                                   LV_OBJ_FLAG_HIDDEN);
            }
        } else if (ui->task_switcher_empty != NULL) {
            lv_obj_add_flag(ui->task_switcher_empty, LV_OBJ_FLAG_HIDDEN);
        }
#else
        if (added == 0) {
            lv_obj_t* empty;
            lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);
            empty = make_label(panel,
                               translated(ui, "recents.empty",
                                          "No recent apps"),
                               typography_font(ui, PXSYS_TYPOGRAPHY_BODY),
                               color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
            lv_obj_center(empty);
        }
#endif
    }
    lv_obj_move_foreground(ui->task_switcher);
#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && \
    LV_USE_SNAPSHOT
    if (start_task_transition(ui)) handoff = 1;
#endif
#if PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS
    if (ui->animations_enabled && !handoff) {
        lv_obj_set_style_opa(ui->task_switcher, LV_OPA_TRANSP, 0);
        lv_obj_fade_in(ui->task_switcher, 160, 0);
    }
#else
    (void)handoff;
#endif
}

static lv_obj_t* make_launcher_tile(pxsys_reference_lvgl_t* ui,
                                    const char* name,
                                    launcher_item_t* item) {
    lv_obj_t* tile = lv_button_create(ui->content);
    lv_obj_t* marker;
    lv_obj_t* label;
    pxsys_color_token_t icon_token;
    uint32_t hash = UINT32_C(2166136261);
    size_t index;
    int32_t icon_size = ui->display.width < 360 ? 42 : 50;
    int32_t label_gap = ui->display.width < 360 ? 5 : 6;
    const char* symbol = LV_SYMBOL_LIST;
    if (item != NULL) {
        for (index = 0; item->app_id[index] != '\0'; ++index) {
            hash ^= (uint8_t)item->app_id[index];
            hash *= UINT32_C(16777619);
        }
    }
    switch (hash % 4u) {
    case 1:
        icon_token = PXSYS_COLOR_SUCCESS;
        break;
    case 2:
        icon_token = PXSYS_COLOR_WARNING;
        break;
    case 3:
        icon_token = PXSYS_COLOR_ERROR;
        break;
    default:
        icon_token = PXSYS_COLOR_ACCENT;
        break;
    }
    lv_obj_set_style_radius(tile, ui->theme.base_radius_px * 2u, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(tile, color_token(ui, PXSYS_COLOR_SURFACE),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(tile, LV_OPA_40, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(tile, 0, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 3, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    marker = lv_obj_create(tile);
    style_plain(marker);
    lv_obj_set_size(marker, icon_size, icon_size);
    lv_obj_set_style_radius(marker, icon_size / 4, 0);
    lv_obj_set_style_bg_color(marker, color_token(ui, icon_token), 0);
    lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(marker, 8, 0);
    lv_obj_set_style_shadow_opa(marker, LV_OPA_20, 0);
    lv_obj_set_style_shadow_offset_y(marker, 3, 0);
    if (item != NULL && item->icon.source != NULL) {
        lv_obj_t* image = lv_image_create(marker);
        lv_obj_set_size(image, LV_PCT(100), LV_PCT(100));
        lv_image_set_src(image, item->icon.source);
        lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CONTAIN);
        lv_obj_center(image);
    } else {
        const char* app_id = item != NULL ? item->app_id : "";
        if (strstr(app_id, "ai-chat") != NULL) symbol = LV_SYMBOL_AUDIO;
        else if (strstr(app_id, "camera") != NULL) symbol = LV_SYMBOL_IMAGE;
        else if (strstr(app_id, "weather") != NULL) symbol = LV_SYMBOL_TINT;
        else if (strstr(app_id, "garden") != NULL) symbol = LV_SYMBOL_EYE_OPEN;
        else if (strstr(app_id, "arcade") != NULL) symbol = LV_SYMBOL_PLAY;
        else if (strstr(app_id, "lab") != NULL) symbol = LV_SYMBOL_SETTINGS;
        else if (strstr(app_id, "plane") != NULL) symbol = LV_SYMBOL_GPS;
        else if (strstr(app_id, "settings") != NULL) symbol = LV_SYMBOL_SETTINGS;
        else if (strstr(app_id, "file") != NULL) symbol = LV_SYMBOL_DIRECTORY;
        else if (strstr(app_id, "alarm") != NULL) symbol = LV_SYMBOL_BELL;
        else if (strstr(app_id, "bluetooth") != NULL) symbol = LV_SYMBOL_BLUETOOTH;
        make_label(marker, symbol, NULL,
                   color_token(ui, PXSYS_COLOR_ON_ACCENT));
        lv_obj_center(lv_obj_get_child(marker, 0));
    }
    lv_obj_align(marker, LV_ALIGN_TOP_MID, 0, 0);
    label = make_label(tile, name, typography_font(ui, PXSYS_TYPOGRAPHY_CAPTION),
                       color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_height(label,
                      typography_font(ui, PXSYS_TYPOGRAPHY_CAPTION) != NULL
                          ? (lv_coord_t)typography_font(
                                ui, PXSYS_TYPOGRAPHY_CAPTION)->line_height + 2
                          : 16);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, icon_size + label_gap);
    lv_obj_add_event_cb(tile, launcher_clicked, LV_EVENT_CLICKED, item);
    return tile;
}

static void release_launcher_icons(pxsys_reference_lvgl_t* ui) {
    size_t index;
    if (ui == NULL || ui->launcher_items == NULL) return;
    for (index = 0; index < ui->launcher_count; ++index) {
        pxsys_reference_lvgl_app_icon_t* icon = &ui->launcher_items[index].icon;
        if (icon->release != NULL) icon->release(icon->release_context);
        memset(icon, 0, sizeof(*icon));
    }
    ui->launcher_count = 0;
}

static int radio_has_explicit_capabilities(
    const pxsys_system_status_snapshot_t* status) {
    return status->wifi_supported || status->cellular_supported;
}

static int radio_supported(const pxsys_reference_lvgl_t* ui,
                           pxsys_network_type_t network) {
    if (radio_has_explicit_capabilities(&ui->system_status)) {
        return network == PXSYS_NETWORK_WIFI
                   ? ui->system_status.wifi_supported
                   : ui->system_status.cellular_supported;
    }
    return ui->system_status.network_type == network;
}

static int radio_enabled(const pxsys_reference_lvgl_t* ui,
                         pxsys_network_type_t network) {
    if (radio_has_explicit_capabilities(&ui->system_status)) {
        return network == PXSYS_NETWORK_WIFI
                   ? ui->system_status.wifi_enabled
                   : ui->system_status.cellular_enabled;
    }
    return ui->system_status.network_type == network;
}

static int radio_connected(const pxsys_reference_lvgl_t* ui,
                           pxsys_network_type_t network) {
    if (radio_has_explicit_capabilities(&ui->system_status)) {
        return network == PXSYS_NETWORK_WIFI
                   ? ui->system_status.wifi_connected
                   : ui->system_status.cellular_connected;
    }
    return ui->system_status.network_type == network &&
           ui->system_status.network_connected;
}

static uint8_t radio_signal_level(const pxsys_reference_lvgl_t* ui,
                                  pxsys_network_type_t network) {
    if (radio_has_explicit_capabilities(&ui->system_status)) {
        return network == PXSYS_NETWORK_WIFI
                   ? ui->system_status.wifi_signal_level
                   : ui->system_status.cellular_signal_level;
    }
    return ui->system_status.network_type == network
               ? ui->system_status.network_signal_level : 0;
}

static int settings_role_available(pxsys_reference_lvgl_t* ui,
                                   const char* role) {
    const pxsys_app_descriptor_t* app;
    return role != NULL &&
           pxsys_role_resolve(pxsys_standard_system_roles(ui->system),
                              pxsys_string_from_cstr(role), &app) ==
               PXSYS_STATUS_OK;
}

static lv_obj_t* make_settings_row(pxsys_reference_lvgl_t* ui,
                                   const pxsys_reference_layout_t* layout,
                                   lv_obj_t* section, const char* symbol,
                                   pxsys_color_token_t icon_color,
                                   const char* title, const char* subtitle,
                                   lv_event_cb_t clicked, void* user_data);
static void wifi_settings_clicked(lv_event_t* event);

static void settings_action_clicked(lv_event_t* event) {
    settings_action_t* action =
        (settings_action_t*)lv_event_get_user_data(event);
    if (action == NULL || !ui_valid(action->ui) || action->role == NULL) return;
    (void)open_role(action->ui, action->role);
}

static void performance_switch_changed(lv_event_t* event) {
    performance_control_t* control =
        (performance_control_t*)lv_event_get_user_data(event);
    lv_obj_t* toggle = lv_event_get_current_target(event);
    const bool enabled = toggle != NULL &&
        lv_obj_has_state(toggle, LV_STATE_CHECKED);
    if (control == NULL || !ui_valid(control->ui) ||
        control->ui->performance_set == NULL ||
        !control->ui->performance_set(control->ui->performance_context,
                                      control->option, enabled)) {
        if (toggle != NULL) {
            if (enabled) lv_obj_remove_state(toggle, LV_STATE_CHECKED);
            else lv_obj_add_state(toggle, LV_STATE_CHECKED);
        }
    }
}

static void make_performance_setting(
    pxsys_reference_lvgl_t* ui, const pxsys_reference_layout_t* layout,
    lv_obj_t* section, pxsys_reference_performance_option_t option,
    const char* title, const char* subtitle, performance_control_t* control) {
    lv_obj_t* row = make_settings_row(ui, layout, section, LV_SYMBOL_SETTINGS,
                                      PXSYS_COLOR_WARNING, title, subtitle,
                                      NULL, NULL);
    lv_obj_t* toggle = lv_switch_create(row);
    lv_obj_set_size(toggle, 42, 24);
    lv_obj_align(toggle, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(toggle, color_token(ui, PXSYS_COLOR_BORDER),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_color(toggle, color_token(ui, PXSYS_COLOR_ACCENT),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(toggle, color_token(ui, PXSYS_COLOR_ON_ACCENT),
                              LV_PART_KNOB);
    if (ui->performance_get != NULL &&
        ui->performance_get(ui->performance_context, option))
        lv_obj_add_state(toggle, LV_STATE_CHECKED);
    control->ui = ui;
    control->option = option;
    lv_obj_add_event_cb(toggle, performance_switch_changed,
                        LV_EVENT_VALUE_CHANGED, control);
}

static lv_obj_t* make_settings_section(pxsys_reference_lvgl_t* ui,
                                       const char* title) {
    lv_obj_t* group = lv_obj_create(ui->content);
    lv_obj_t* label = make_label(
        group, title, typography_font(ui, PXSYS_TYPOGRAPHY_CAPTION),
        color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
    lv_obj_t* section = lv_obj_create(group);
    style_plain(group);
    lv_obj_set_width(group, LV_PCT(100));
    lv_obj_set_height(group, LV_SIZE_CONTENT);
    lv_obj_set_layout(group, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(group, 4, 0);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_pad_left(label, 8, 0);
    style_plain(section);
    lv_obj_set_width(section, LV_PCT(100));
    lv_obj_set_height(section, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(section, color_token(ui, PXSYS_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(section, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(section, 1, 0);
    lv_obj_set_style_border_color(section, color_token(ui, PXSYS_COLOR_BORDER),
                                  0);
    lv_obj_set_style_border_opa(section, LV_OPA_70, 0);
    lv_obj_set_style_radius(section, 8, 0);
    /* Rows stay inside the section bounds, so clipping every child through a
     * rounded mask only adds software blending work while the list scrolls. */
    lv_obj_set_style_clip_corner(section, false, 0);
    lv_obj_set_layout(section, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(section, 0, 0);
    return section;
}

static void make_cellular_glyph(lv_obj_t* parent, int32_t x, int32_t y,
                                lv_color_t color) {
    uint8_t index;
    for (index = 0; index < 4; ++index) {
        const int32_t bar_height = 5 + (int32_t)index * 3;
        lv_obj_t* bar = lv_obj_create(parent);
        style_plain(bar);
        lv_obj_set_pos(bar, x + (int32_t)index * 4, y + 14 - bar_height);
        lv_obj_set_size(bar, 3, bar_height);
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_set_style_bg_color(bar, color, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    }
}

static lv_obj_t* make_settings_row(pxsys_reference_lvgl_t* ui,
                                   const pxsys_reference_layout_t* layout,
                                   lv_obj_t* section, const char* symbol,
                                   pxsys_color_token_t icon_color,
                                   const char* title, const char* subtitle,
                                   lv_event_cb_t clicked, void* user_data) {
    const lv_font_t* title_font =
        typography_font(ui, PXSYS_TYPOGRAPHY_LABEL);
    const lv_font_t* subtitle_font =
        typography_font(ui, PXSYS_TYPOGRAPHY_CAPTION);
    int32_t title_height = title_font != NULL ? title_font->line_height + 2 : 18;
    int32_t subtitle_height =
        subtitle_font != NULL ? subtitle_font->line_height + 2 : 16;
    int32_t text_height =
        subtitle == NULL ? title_height : title_height + subtitle_height;
    int32_t minimum_height =
        layout->size_class == PXSYS_UI_SIZE_COMPACT ? 50 : 56;
    int32_t height = text_height + 10 > minimum_height ? text_height + 10
                                                     : minimum_height;
    int32_t icon_size = layout->size_class == PXSYS_UI_SIZE_COMPACT ? 30 : 32;
    lv_obj_t* row = lv_obj_create(section);
    lv_obj_t* icon_background;
    lv_obj_t* text;
    lv_obj_t* label;
    style_plain(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, height);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(row, color_token(ui, PXSYS_COLOR_BORDER),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(
        row, lv_obj_get_child_count(section) > 1 ? LV_BORDER_SIDE_TOP
                                                 : LV_BORDER_SIDE_NONE,
        0);
    lv_obj_set_style_border_color(row, color_token(ui, PXSYS_COLOR_BORDER), 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(row, 10, 0);
    lv_obj_set_style_pad_column(row, 10, 0);
    if (clicked != NULL) {
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, clicked, LV_EVENT_CLICKED, user_data);
    }
    icon_background = lv_obj_create(row);
    style_plain(icon_background);
    lv_obj_set_size(icon_background, icon_size, icon_size);
    lv_obj_set_style_min_width(icon_background, icon_size, 0);
    lv_obj_set_style_radius(icon_background, 8, 0);
    lv_obj_set_style_bg_color(icon_background, color_token(ui, icon_color), 0);
    lv_obj_set_style_bg_opa(icon_background, LV_OPA_30, 0);
    if (symbol != NULL) {
        label = make_label(icon_background, symbol, NULL,
                           color_token(ui, icon_color));
        lv_obj_center(label);
    } else {
        make_cellular_glyph(icon_background, (icon_size - 15) / 2,
                            (icon_size - 14) / 2,
                            color_token(ui, icon_color));
    }
    text = lv_obj_create(row);
    style_plain(text);
    lv_obj_set_width(text, 0);
    lv_obj_set_height(text, text_height);
    lv_obj_set_flex_grow(text, 1);
    label = make_label(text, title, title_font,
                       color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_height(label, title_height);
    lv_obj_align(label, subtitle == NULL ? LV_ALIGN_LEFT_MID
                                         : LV_ALIGN_TOP_LEFT,
                 0, 0);
    if (subtitle != NULL) {
        label = make_label(text, subtitle, subtitle_font,
                           color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_height(label, subtitle_height);
        lv_obj_align(label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
    return row;
}

static void add_settings_chevron(pxsys_reference_lvgl_t* ui, lv_obj_t* row) {
    lv_obj_t* chevron = make_label(
        row, LV_SYMBOL_RIGHT, NULL,
        color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
    lv_obj_set_style_min_width(chevron, 10, 0);
}

static int add_settings_role(pxsys_reference_lvgl_t* ui,
                             const pxsys_reference_layout_t* layout,
                             lv_obj_t* section, size_t action_index,
                             const char* role, const char* symbol,
                             pxsys_color_token_t icon_color,
                             const char* title, const char* subtitle) {
    lv_obj_t* row;
    settings_action_t* action;
    if (action_index >= REFERENCE_SETTINGS_ACTION_COUNT ||
        !settings_role_available(ui, role))
        return 0;
    action = &ui->settings_actions[action_index];
    action->ui = ui;
    action->role = role;
    row = make_settings_row(ui, layout, section, symbol, icon_color, title,
                            subtitle, settings_action_clicked, action);
    add_settings_chevron(ui, row);
    return 1;
}

#if PXSYS_REFERENCE_UI_WIFI || PXSYS_REFERENCE_UI_CELLULAR
static void network_switch_changed(lv_event_t* event) {
    network_control_t* control =
        (network_control_t*)lv_event_get_user_data(event);
    lv_obj_t* toggle = lv_event_get_current_target(event);
    uint8_t enabled;
    pxsys_status_t status;
    if (control == NULL || !ui_valid(control->ui) || toggle == NULL) return;
    enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED) ? 1 : 0;
    status = pxsys_system_status_service_set_network_enabled(
        pxsys_standard_system_status(control->ui->system), control->network,
        enabled);
    if (status != PXSYS_STATUS_OK) {
        if (enabled)
            lv_obj_remove_state(toggle, LV_STATE_CHECKED);
        else
            lv_obj_add_state(toggle, LV_STATE_CHECKED);
    }
}

static void make_network_setting(pxsys_reference_lvgl_t* ui,
                                 const pxsys_reference_layout_t* layout,
                                 lv_obj_t* section,
                                 pxsys_network_type_t network,
                                 network_control_t* control,
                                 settings_action_t* details) {
    lv_obj_t* row;
    lv_obj_t* toggle;
    const char* title;
    const char* state;
    int connected = radio_connected(ui, network);
    title = network == PXSYS_NETWORK_WIFI
                ? translated(ui, "settings.wifi", "Wi-Fi")
                : translated(ui, "settings.mobile", "Mobile network");
    state = connected ? translated(ui, "state.connected", "Connected")
                      : radio_enabled(ui, network)
                            ? translated(ui, "state.on", "On")
                            : translated(ui, "state.off", "Off");
    row = make_settings_row(
        ui, layout, section,
        network == PXSYS_NETWORK_WIFI ? LV_SYMBOL_WIFI : NULL,
        PXSYS_COLOR_ACCENT, title, state,
        network == PXSYS_NETWORK_WIFI && ui->wifi_scan != NULL &&
                ui->wifi_connect != NULL
            ? wifi_settings_clicked
            : details == NULL ? NULL : settings_action_clicked,
        network == PXSYS_NETWORK_WIFI && ui->wifi_scan != NULL &&
                ui->wifi_connect != NULL
            ? ui
            : (void*)details);
    toggle = lv_switch_create(row);
    lv_obj_set_size(toggle, 42, 24);
    lv_obj_align(toggle, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(toggle, color_token(ui, PXSYS_COLOR_BORDER),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_color(toggle, color_token(ui, PXSYS_COLOR_ACCENT),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(toggle,
                              color_token(ui, PXSYS_COLOR_ON_ACCENT),
                              LV_PART_KNOB);
    if (radio_enabled(ui, network))
        lv_obj_add_state(toggle, LV_STATE_CHECKED);
    control->ui = ui;
    control->network = network;
    lv_obj_add_event_cb(toggle, network_switch_changed,
                        LV_EVENT_VALUE_CHANGED, control);
}
#endif

static const pxsys_reference_language_t* current_language(
    const pxsys_reference_lvgl_t* ui) {
    size_t index;
    for (index = 0; index < ui->language_count; ++index) {
        const pxsys_reference_language_t* language = &ui->languages[index];
        if (language->locale.size == ui->locale.tag_size &&
            memcmp(language->locale.data, ui->locale.tag,
                   ui->locale.tag_size) == 0)
            return language;
    }
    return NULL;
}

static void close_language_dialog(pxsys_reference_lvgl_t* ui) {
    lv_obj_t* dialog;
    if (!ui_valid(ui) || ui->language_dialog == NULL) return;
    dialog = ui->language_dialog;
    ui->language_dialog = NULL;
    lv_obj_delete(dialog);
}

static void language_dialog_close_clicked(lv_event_t* event) {
    close_language_dialog(
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event));
}

static void language_selected(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    lv_obj_t* row = lv_event_get_current_target(event);
    size_t index;
    pxsys_locale_snapshot_t locale;
    if (!ui_valid(ui) || row == NULL) return;
    index = (size_t)(uintptr_t)lv_obj_get_user_data(row);
    if (index >= ui->language_count ||
        pxsys_locale_snapshot_init(&locale, ui->languages[index].locale) !=
            PXSYS_STATUS_OK)
        return;
    close_language_dialog(ui);
    (void)pxsys_locale_service_update(
        pxsys_standard_system_locale(ui->system), &locale);
}

static void language_clicked(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    pxsys_reference_layout_t layout;
    lv_obj_t* panel;
    lv_obj_t* header;
    lv_obj_t* list;
    lv_obj_t* action;
    size_t index;
    int32_t panel_height;
    if (!ui_valid(ui) || ui->language_dialog != NULL ||
        pxsys_reference_layout_compute(&ui->display, &layout) !=
            PXSYS_STATUS_OK)
        return;
    expand_content_into_hidden_gesture_area(ui, &layout);
    ui->language_dialog = lv_obj_create(ui->root);
    style_plain(ui->language_dialog);
    lv_obj_set_size(ui->language_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(ui->language_dialog,
                              color_token(ui, PXSYS_COLOR_SCRIM), 0);
    lv_obj_set_style_bg_opa(ui->language_dialog, LV_OPA_60, 0);
    lv_obj_add_flag(ui->language_dialog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui->language_dialog, language_dialog_close_clicked,
                        LV_EVENT_CLICKED, ui);

    panel_height = (int32_t)layout.content.height;
    panel = lv_obj_create(ui->language_dialog);
    style_plain(panel);
    lv_obj_set_pos(panel, layout.content.x, layout.content.y);
    lv_obj_set_size(panel, (lv_coord_t)layout.content.width, panel_height);
    lv_obj_set_style_bg_color(panel, color_token(ui, PXSYS_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, ui->theme.base_radius_px * 3u, 0);
    lv_obj_set_style_clip_corner(panel, true, 0);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);

    header = lv_obj_create(panel);
    style_plain(header);
    lv_obj_set_size(header, LV_PCT(100), 42);
    lv_obj_set_style_pad_hor(header, 12, 0);
    lv_obj_set_layout(header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    action = lv_button_create(header);
    lv_obj_set_size(action, 32, 32);
    lv_obj_set_style_radius(action, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(action, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(action, color_token(ui, PXSYS_COLOR_BORDER),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(action, LV_OPA_40, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(action, 0, 0);
    lv_obj_set_style_border_width(action, 0, 0);
    make_label(action, LV_SYMBOL_LEFT, NULL,
               color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
    lv_obj_center(lv_obj_get_child(action, 0));
    lv_obj_add_event_cb(action, language_dialog_close_clicked,
                        LV_EVENT_CLICKED, ui);
    action = make_label(header,
                        translated(ui, "language.title", "Language"),
                        typography_font(ui, PXSYS_TYPOGRAPHY_TITLE),
                        color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
    lv_obj_set_flex_grow(action, 1);

    list = lv_obj_create(panel);
    style_plain(list);
    lv_obj_set_pos(list, 0, 42);
    lv_obj_set_size(list, LV_PCT(100), panel_height - 42);
    lv_obj_set_style_pad_all(list, 8, 0);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    for (index = 0; index < ui->language_count; ++index) {
        const pxsys_reference_language_t* language = &ui->languages[index];
        int selected = language->locale.size == ui->locale.tag_size &&
                       memcmp(language->locale.data, ui->locale.tag,
                              ui->locale.tag_size) == 0;
        lv_obj_t* row = make_settings_row(
            ui, &layout, list, LV_SYMBOL_EDIT, PXSYS_COLOR_ACCENT,
            borrowed_text(ui, language->display_name),
            borrowed_text(ui, language->locale), language_selected, ui);
        lv_obj_set_user_data(row, (void*)(uintptr_t)index);
        lv_obj_set_style_bg_color(row, color_token(ui, PXSYS_COLOR_ACCENT), 0);
        lv_obj_set_style_bg_opa(row, selected ? LV_OPA_30 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_radius(row, ui->theme.base_radius_px * 2u, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        if (selected) {
            action = make_label(row, LV_SYMBOL_OK, NULL,
                                color_token(ui, PXSYS_COLOR_ACCENT));
            lv_obj_set_style_min_width(action, 14, 0);
        }
    }
    lv_obj_move_foreground(ui->language_dialog);
    if (ui->status_bar != NULL) lv_obj_move_foreground(ui->status_bar);
    if (ui->navigation_bar != NULL) lv_obj_move_foreground(ui->navigation_bar);
}

#if PXSYS_REFERENCE_UI_ENABLE_WALLPAPER
static void make_wallpaper_shape(pxsys_reference_lvgl_t* ui,
                                 lv_obj_t* wallpaper, int32_t x, int32_t y,
                                 int32_t width, int32_t height,
                                 int32_t rotation,
                                 pxsys_color_token_t color, lv_opa_t opacity) {
    lv_obj_t* shape = lv_obj_create(wallpaper);
    style_plain(shape);
    lv_obj_set_pos(shape, x, y);
    lv_obj_set_size(shape, width, height);
    lv_obj_set_style_radius(shape, ui->theme.base_radius_px * 4u, 0);
    lv_obj_set_style_bg_color(shape, color_token(ui, color), 0);
    lv_obj_set_style_bg_opa(shape, opacity, 0);
    lv_obj_set_style_transform_pivot_x(shape, width / 2, 0);
    lv_obj_set_style_transform_pivot_y(shape, height / 2, 0);
    lv_obj_set_style_transform_rotation(shape, rotation, 0);
}

static void build_home_wallpaper(pxsys_reference_lvgl_t* ui) {
    lv_obj_t* wallpaper = lv_obj_create(ui->root);
    style_plain(wallpaper);
    lv_obj_set_pos(wallpaper, 0, 0);
    lv_obj_set_size(wallpaper, (lv_coord_t)ui->display.width,
                    (lv_coord_t)ui->display.height);
    lv_obj_set_style_bg_color(wallpaper,
                              color_token(ui, PXSYS_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(wallpaper, LV_OPA_COVER, 0);
    lv_obj_set_style_clip_corner(wallpaper, true, 0);
    if (ui->wallpaper_source != NULL) {
        lv_obj_t* image = lv_image_create(wallpaper);
        lv_obj_set_size(image, LV_PCT(100), LV_PCT(100));
        lv_image_set_src(image, ui->wallpaper_source);
        lv_image_set_inner_align(image, LV_IMAGE_ALIGN_COVER);
        lv_obj_remove_flag(image,
                           LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        return;
    }
    make_wallpaper_shape(
        ui, wallpaper, -(int32_t)ui->display.width / 3,
        -(int32_t)ui->display.height / 5,
        (int32_t)ui->display.width, (int32_t)ui->display.height / 2,
        120, PXSYS_COLOR_ACCENT, LV_OPA_20);
    make_wallpaper_shape(
        ui, wallpaper, (int32_t)ui->display.width / 2,
        (int32_t)ui->display.height / 4,
        (int32_t)ui->display.width * 3 / 4,
        (int32_t)ui->display.height * 2 / 3,
        -180, PXSYS_COLOR_SUCCESS, LV_OPA_10);
    make_wallpaper_shape(
        ui, wallpaper, -(int32_t)ui->display.width / 5,
        (int32_t)ui->display.height * 3 / 4,
        (int32_t)ui->display.width * 4 / 5,
        (int32_t)ui->display.height / 4,
        -80, PXSYS_COLOR_SURFACE, LV_OPA_50);
}
#endif

static void build_home(pxsys_reference_lvgl_t* ui,
                       const pxsys_reference_layout_t* layout) {
    pxsys_app_registry_t* registry = pxsys_standard_system_apps(ui->system);
    size_t index;
    uint32_t row_height;
    uint32_t columns;
    uint32_t column_width;
    uint32_t gap = layout->size_class == PXSYS_UI_SIZE_COMPACT ? 4u : 8u;
    release_launcher_icons(ui);
    lv_obj_set_layout(ui->content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ui->content, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(ui->content, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    columns = layout->content.width < 180u
                  ? 3u
                  : layout->content.width < 500u ? 4u : 5u;
    lv_obj_set_style_pad_column(ui->content, gap, 0);
    lv_obj_set_style_pad_row(ui->content, gap, 0);
    column_width = (layout->content.width -
                    (columns - 1u) * gap) / columns;
    row_height = layout->size_class == PXSYS_UI_SIZE_COMPACT ? 64u : 76u;
    lv_obj_add_flag(ui->content,
                    LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(ui->content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui->content, LV_SCROLLBAR_MODE_AUTO);

    for (index = 0; index < pxsys_app_registry_count(registry) &&
                    ui->launcher_count < ui->max_launcher_apps; ++index) {
        const pxsys_app_descriptor_t* app = pxsys_app_registry_at(registry, index);
        launcher_item_t* item;
        lv_obj_t* tile;
        char name[96];
        pxsys_string_t display_name;
        size_t name_size;
        if (app == NULL || !(app->flags & PXSYS_APP_FLAG_ENABLED) ||
            !(app->flags & PXSYS_APP_FLAG_LAUNCHER))
            continue;
        item = &ui->launcher_items[ui->launcher_count];
        memset(item, 0, sizeof(*item));
        if (app->identity.app_id.size >= sizeof(item->app_id)) continue;
        item->ui = ui;
        memcpy(item->identity.publisher_root, app->identity.publisher_root,
               PXSYS_PUBLISHER_ROOT_BYTES);
        memcpy(item->app_id, app->identity.app_id.data, app->identity.app_id.size);
        item->app_id[app->identity.app_id.size] = '\0';
        item->identity.app_id = pxsys_string_from_cstr(item->app_id);
        snprintf(item->runtime_name, sizeof(item->runtime_name), "%s",
                 app->runtime_id.size == strlen(PXSYS_NATIVE_RUNTIME_ID) &&
                         memcmp(app->runtime_id.data, PXSYS_NATIVE_RUNTIME_ID,
                                app->runtime_id.size) == 0
                     ? "Native" : "PXA");
        display_name = localized_app_name(ui, app);
        name_size = display_name.size < sizeof(name) - 1u
                        ? display_name.size : sizeof(name) - 1u;
        memcpy(name, display_name.data, name_size);
        name[name_size] = '\0';
        if (ui->resolve_app_icon != NULL &&
            !ui->resolve_app_icon(ui->app_icon_context, app, &item->icon))
            memset(&item->icon, 0, sizeof(item->icon));
        tile = make_launcher_tile(ui, name, item);
        lv_obj_set_size(tile, (lv_coord_t)column_width, (lv_coord_t)row_height);
        ui->launcher_count++;
    }
    if (ui->launcher_count == 0) {
        lv_obj_t* empty = make_label(
            ui->content, translated(ui, "apps.empty", "No apps installed"),
            typography_font(ui, PXSYS_TYPOGRAPHY_BODY),
                                    color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
        lv_obj_center(empty);
    }
    lv_obj_update_layout(ui->content);
    lv_obj_scroll_to_y(ui->content, 0, LV_ANIM_OFF);
}

static void build_settings(pxsys_reference_lvgl_t* ui,
                           const pxsys_reference_layout_t* layout) {
    lv_obj_t* section;
    lv_obj_t* row;
    settings_action_t* network_action = NULL;
    const pxsys_reference_language_t* selected_language;
    const char* appearance_detail;
    const char* language_detail;
    int has_wifi = 0;
    int has_cellular = 0;
    int has_bluetooth = settings_role_available(ui, PXSYS_ROLE_BLUETOOTH_SETTINGS);
    int has_sound = settings_role_available(ui, PXSYS_ROLE_SOUND_SETTINGS);
    int has_alarm = settings_role_available(ui, PXSYS_ROLE_ALARM_SETTINGS);
    int has_apps = settings_role_available(ui, PXSYS_ROLE_APP_MANAGER);
    int has_files = settings_role_available(ui, PXSYS_ROLE_FILE_MANAGER);
    int has_about = settings_role_available(ui, PXSYS_ROLE_DEVICE_INFO);
    lv_obj_set_layout(ui->content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ui->content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(
        ui->content,
        layout->size_class == PXSYS_UI_SIZE_COMPACT ? 8 : 10, 0);
    lv_obj_set_style_pad_bottom(ui->content, 8, 0);
    /* Keep the scroll container itself hit-testable. Non-interactive rows and
     * gaps otherwise fall through to the root, so scrolling only starts when
     * the pointer happens to land on a button or switch. */
    lv_obj_add_flag(ui->content,
                    LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(ui->content, LV_DIR_VER);
#if PXSYS_REFERENCE_UI_WIFI
    has_wifi = radio_supported(ui, PXSYS_NETWORK_WIFI);
#endif
#if PXSYS_REFERENCE_UI_CELLULAR
    has_cellular = radio_supported(ui, PXSYS_NETWORK_CELLULAR);
#endif
    if ((has_wifi || has_cellular) &&
        settings_role_available(ui, PXSYS_ROLE_NETWORK_SETTINGS)) {
        network_action = &ui->settings_actions[0];
        network_action->ui = ui;
        network_action->role = PXSYS_ROLE_NETWORK_SETTINGS;
    }
    if (has_wifi || has_cellular || has_bluetooth) {
        section = make_settings_section(
            ui, translated(ui, "settings.section.connections", "Connections"));
#if PXSYS_REFERENCE_UI_WIFI
        if (has_wifi)
            make_network_setting(ui, layout, section, PXSYS_NETWORK_WIFI,
                                 &ui->network_controls[0], network_action);
#endif
#if PXSYS_REFERENCE_UI_CELLULAR
        if (has_cellular)
            make_network_setting(ui, layout, section, PXSYS_NETWORK_CELLULAR,
                                 &ui->network_controls[1], network_action);
#endif
        (void)add_settings_role(
            ui, layout, section, 1, PXSYS_ROLE_BLUETOOTH_SETTINGS,
            LV_SYMBOL_BLUETOOTH, PXSYS_COLOR_ACCENT,
            translated(ui, "settings.bluetooth", "Bluetooth audio"),
            translated(ui, "settings.bluetooth.detail",
                       "Speakers, sources and local music"));
    }

    section = make_settings_section(
        ui, translated(ui, "settings.section.personalization",
                       "Personalization"));
    appearance_detail =
        ui->theme.configured_mode == PXSYS_THEME_MODE_CUSTOM
            ? borrowed_text(ui,
                            pxsys_string(ui->theme.theme_id,
                                         ui->theme.theme_id_size))
            : ui->theme.effective_scheme == PXSYS_COLOR_SCHEME_DARK
                  ? translated(ui, "theme.dark", "Dark")
                  : translated(ui, "theme.light", "Light");
    row = make_settings_row(
        ui, layout, section, LV_SYMBOL_TINT, PXSYS_COLOR_WARNING,
        translated(ui, "settings.appearance", "Appearance"), appearance_detail,
        theme_clicked, ui);
    add_settings_chevron(ui, row);
    selected_language = current_language(ui);
    language_detail = selected_language == NULL
                          ? borrowed_text(ui, pxsys_string(
                                                  ui->locale.tag,
                                                  ui->locale.tag_size))
                          : borrowed_text(ui, selected_language->display_name);
    row = make_settings_row(
        ui, layout, section, LV_SYMBOL_EDIT, PXSYS_COLOR_ACCENT,
        translated(ui, "settings.language", "Language"), language_detail,
        language_clicked, ui);
    add_settings_chevron(ui, row);

    if (has_sound || has_alarm) {
        section = make_settings_section(
            ui, translated(ui, "settings.section.device", "Device"));
        (void)add_settings_role(
            ui, layout, section, 2, PXSYS_ROLE_SOUND_SETTINGS,
            LV_SYMBOL_VOLUME_MAX, PXSYS_COLOR_SUCCESS,
            translated(ui, "settings.sound", "Sound & display"),
            translated(ui, "settings.sound.detail",
                       "Volume, brightness and screen timeout"));
        (void)add_settings_role(
            ui, layout, section, 3, PXSYS_ROLE_ALARM_SETTINGS,
            LV_SYMBOL_BELL, PXSYS_COLOR_WARNING,
            translated(ui, "settings.alarm", "Alarms"),
            translated(ui, "settings.alarm.detail",
                       "Create and manage alarms"));
    }

    if (has_apps || has_files || has_about) {
        section = make_settings_section(
            ui, translated(ui, "settings.section.system", "System"));
        (void)add_settings_role(
            ui, layout, section, 4, PXSYS_ROLE_APP_MANAGER, LV_SYMBOL_LIST,
            PXSYS_COLOR_ACCENT, translated(ui, "settings.apps", "Apps"),
            translated(ui, "settings.apps.detail",
                       "Disable, uninstall and clear data"));
        (void)add_settings_role(
            ui, layout, section, 5, PXSYS_ROLE_FILE_MANAGER,
            LV_SYMBOL_DIRECTORY, PXSYS_COLOR_SUCCESS,
            translated(ui, "settings.files", "Files & storage"),
            translated(ui, "settings.files.detail",
                       "Browse and organize device files"));
        (void)add_settings_role(
            ui, layout, section, 6, PXSYS_ROLE_DEVICE_INFO,
            LV_SYMBOL_SETTINGS, PXSYS_COLOR_WARNING,
            translated(ui, "settings.about", "About device"),
            translated(ui, "settings.about.detail",
                        "Firmware, storage and hardware information"));
    }
    lv_obj_update_layout(ui->content);
    lv_obj_scroll_to_y(ui->content, ui->settings_scroll_y, LV_ANIM_OFF);
}

static void notification_shade_progress_set(void* object, int32_t progress) {
    pxsys_reference_lvgl_t* ui = (pxsys_reference_lvgl_t*)object;
    if (!ui_valid(ui) || ui->notification_shade == NULL ||
        ui->notification_panel == NULL || ui->notification_panel_height <= 0)
        return;
    if (progress < 0) progress = 0;
    if (progress > 256) progress = 256;
    ui->notification_progress = progress;
    lv_obj_set_y(ui->notification_panel,
                 (lv_coord_t)(-ui->notification_panel_height +
                              (int64_t)ui->notification_panel_height *
                                  progress / 256));
    /* No dimming scrim: a translucent full-screen overlay made every drag
     * frame repaint the whole display underneath it. Without it the drag
     * only repaints the moving panel. */
}

static void close_notification_shade(pxsys_reference_lvgl_t* ui) {
    if (!ui_valid(ui) || ui->notification_shade == NULL) return;
#if PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS
    lv_anim_delete(ui, notification_shade_progress_set);
#endif
    ui->notification_shade_open = 0;
    ui->notification_dragging = 0;
    ui->notification_drag_moved = 0;
    ui->notification_close_armed = 0;
    ui->notification_progress = 0;
    ui->notification_scroll_y = 0;
    lv_obj_add_flag(ui->notification_shade, LV_OBJ_FLAG_HIDDEN);
}

#if PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS
static void notification_shade_closed(lv_anim_t* animation) {
    close_notification_shade(
        (pxsys_reference_lvgl_t*)lv_anim_get_user_data(animation));
}
#endif

static void settle_notification_shade(pxsys_reference_lvgl_t* ui,
                                      int32_t target_progress) {
    if (!ui_valid(ui) || ui->notification_shade == NULL) return;
    target_progress = target_progress == 0 ? 0 : 256;
    ui->notification_shade_open = target_progress != 0;
#if PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS
    if (ui->animations_enabled &&
        ui->notification_progress != target_progress) {
        lv_anim_t animation;
        int32_t delta = ui->notification_progress - target_progress;
        uint32_t distance = (uint32_t)(delta < 0 ? -delta : delta);
        lv_anim_delete(ui, notification_shade_progress_set);
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, ui);
        lv_anim_set_exec_cb(&animation, notification_shade_progress_set);
        lv_anim_set_values(&animation, ui->notification_progress,
                           target_progress);
        lv_anim_set_duration(&animation, 100u + distance * 120u / 256u);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
        lv_anim_set_user_data(&animation, ui);
        if (target_progress == 0)
            lv_anim_set_completed_cb(&animation, notification_shade_closed);
        lv_anim_start(&animation);
        return;
    }
#endif
    notification_shade_progress_set(ui, target_progress);
    if (target_progress == 0) close_notification_shade(ui);
}

static int has_transient_chrome(const pxsys_reference_lvgl_t* ui) {
    return ui->window.status_bar_mode == PXSYS_WINDOW_BAR_TRANSIENT ||
           ui->window.navigation_bar_mode == PXSYS_WINDOW_BAR_TRANSIENT;
}

static int bar_is_visible(const pxsys_reference_lvgl_t* ui,
                          pxsys_window_bar_mode_t mode) {
    return mode == PXSYS_WINDOW_BAR_VISIBLE ||
           (mode == PXSYS_WINDOW_BAR_TRANSIENT && ui->transient_revealed);
}

static void transient_timeout(lv_timer_t* timer) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_timer_get_user_data(timer);
    if (!ui_valid(ui)) return;
    /* Keep the gesture target alive until the held pointer is released.
     * Rebuilding the transient navigation bar here would emit PRESS_LOST and
     * discard the pending Home/Recents action. */
    if (ui->navigation_dragging || ui->notification_dragging) {
        lv_timer_reset(timer);
        return;
    }
    lv_timer_pause(timer);
    if (!ui->transient_revealed) return;
    ui->transient_revealed = 0;
    close_notification_shade(ui);
    rebuild(ui);
}

static void reveal_transient_chrome(pxsys_reference_lvgl_t* ui) {
    if (!ui_valid(ui) || !has_transient_chrome(ui)) return;
    ui->transient_revealed = 1;
    if (ui->transient_timer == NULL) {
        ui->transient_timer =
            lv_timer_create(transient_timeout, TRANSIENT_BAR_TIMEOUT_MS, ui);
    } else {
        lv_timer_set_period(ui->transient_timer, TRANSIENT_BAR_TIMEOUT_MS);
        lv_timer_reset(ui->transient_timer);
        lv_timer_resume(ui->transient_timer);
    }
    if (lv_async_call(rebuild_async, ui) != LV_RESULT_OK) rebuild(ui);
}

static void shade_close_clicked(lv_event_t* event) {
    settle_notification_shade(
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event), 0);
}

static void shade_panel_event(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    lv_indev_t* indev = lv_event_get_indev(event);
    lv_point_t point;
    if (!ui_valid(ui) || indev == NULL) return;
    lv_indev_get_point(indev, &point);
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
        ui->status_press_y = point.y;
        ui->notification_dragging = 1;
        ui->notification_drag_moved = 0;
        ui->notification_close_armed = 1;
    } else if (lv_event_get_code(event) == LV_EVENT_PRESSING) {
        int32_t distance = point.y - ui->status_press_y;
        int32_t progress;
        if (distance > -4) return;
        ui->notification_drag_moved = 1;
        progress = 256 +
            (int32_t)((int64_t)distance * 256 /
                      ui->notification_panel_height);
        notification_shade_progress_set(ui, progress);
    } else if (lv_event_get_code(event) == LV_EVENT_RELEASED ||
               lv_event_get_code(event) == LV_EVENT_PRESS_LOST) {
        int32_t target = ui->notification_drag_moved &&
                                 ui->notification_progress < 216
                             ? 0 : 256;
        ui->notification_dragging = 0;
        ui->notification_close_armed = 0;
        settle_notification_shade(ui, target);
    }
}

static void shade_body_event(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    lv_obj_t* body = lv_event_get_current_target(event);
    lv_indev_t* indev = lv_event_get_indev(event);
    lv_point_t point;
    lv_event_code_t code = lv_event_get_code(event);
    if (!ui_valid(ui) || body == NULL || indev == NULL) return;
    lv_indev_get_point(indev, &point);
    if (code == LV_EVENT_PRESSED) {
        ui->notification_close_armed =
            lv_obj_get_scroll_bottom(body) <= 1;
        ui->notification_dragging = 0;
        ui->notification_drag_moved = 0;
        ui->notification_scroll_y = lv_obj_get_scroll_y(body);
        ui->notification_press_x = point.x;
        ui->status_press_y = point.y;
        return;
    }
    if (code == LV_EVENT_PRESSING && ui->notification_close_armed) {
        int32_t distance = ui->status_press_y - point.y;
        int32_t horizontal = point.x - ui->notification_press_x;
        int32_t progress;
        if (horizontal < 0) horizontal = -horizontal;
        if (distance <= 6 || distance <= horizontal) return;
        ui->notification_dragging = 1;
        ui->notification_drag_moved = 1;
        lv_obj_scroll_to_y(body, ui->notification_scroll_y, LV_ANIM_OFF);
        progress = 256 -
            (int32_t)((int64_t)distance * 256 /
                      ui->notification_panel_height);
        notification_shade_progress_set(ui, progress);
        return;
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        int32_t target = ui->notification_close_armed &&
                                 ui->notification_drag_moved &&
                                 code == LV_EVENT_RELEASED &&
                                 ui->notification_progress < 216
                             ? 0 : 256;
        ui->notification_dragging = 0;
        ui->notification_close_armed = 0;
        if (ui->notification_drag_moved)
            settle_notification_shade(ui, target);
        ui->notification_drag_moved = 0;
    }
}

static void shade_enable_pointer_bubble(lv_obj_t* parent) {
    uint32_t index;
    uint32_t count = lv_obj_get_child_count(parent);
    for (index = 0; index < count; ++index) {
        lv_obj_t* child = lv_obj_get_child(parent, (int32_t)index);
        lv_obj_add_flag(child, LV_OBJ_FLAG_EVENT_BUBBLE);
        shade_enable_pointer_bubble(child);
    }
}

static void shade_home_gesture_event(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    lv_indev_t* indev = lv_event_get_indev(event);
    lv_point_t point;
    lv_event_code_t code = lv_event_get_code(event);
    int32_t distance;
    if (!ui_valid(ui) || indev == NULL) return;
    lv_indev_get_point(indev, &point);
    if (code == LV_EVENT_PRESSED) {
        ui->status_press_y = point.y;
        ui->notification_dragging = 1;
        ui->notification_drag_moved = 0;
        return;
    }
    distance = ui->status_press_y - point.y;
    if (code == LV_EVENT_PRESSING) {
        if (distance <= 0) return;
        ui->notification_drag_moved = distance > 4;
        notification_shade_progress_set(
            ui, 256 - (int32_t)((int64_t)distance * 256 /
                                ui->notification_panel_height));
        return;
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        int close = code == LV_EVENT_RELEASED &&
                    distance > NAVIGATION_GESTURE_COMMIT_DISTANCE;
        ui->notification_dragging = 0;
        ui->notification_drag_moved = 0;
        settle_notification_shade(ui, close ? 0 : 256);
    }
}

static void shade_settings_clicked(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    if (!ui_valid(ui)) return;
    close_notification_shade(ui);
    (void)open_role(ui, PXSYS_ROLE_SETTINGS);
}

static void control_network_clicked(lv_event_t* event) {
    network_control_t* control =
        (network_control_t*)lv_event_get_user_data(event);
    if (control == NULL || !ui_valid(control->ui)) return;
    (void)pxsys_system_status_service_set_network_enabled(
        pxsys_standard_system_status(control->ui->system), control->network,
        radio_enabled(control->ui, control->network) ? 0 : 1);
}

static void control_level_released(lv_event_t* event) {
    level_control_t* control =
        (level_control_t*)lv_event_get_user_data(event);
    lv_obj_t* slider = lv_event_get_current_target(event);
    if (control == NULL || !ui_valid(control->ui) || slider == NULL) return;
    (void)pxsys_system_status_service_set_level(
        pxsys_standard_system_status(control->ui->system), control->control,
        (uint8_t)lv_slider_get_value(slider));
}

static int toggle_enabled(const pxsys_reference_lvgl_t* ui,
                          pxsys_toggle_control_t control) {
    switch (control) {
    case PXSYS_TOGGLE_BLUETOOTH:
        return ui->system_status.bluetooth_enabled;
    case PXSYS_TOGGLE_DO_NOT_DISTURB:
        return ui->system_status.do_not_disturb_enabled;
    case PXSYS_TOGGLE_FLASHLIGHT:
        return ui->system_status.flashlight_enabled;
    case PXSYS_TOGGLE_AIRPLANE_MODE:
        return ui->system_status.airplane_mode_enabled;
    }
    return 0;
}

static void control_toggle_clicked(lv_event_t* event) {
    toggle_control_t* control =
        (toggle_control_t*)lv_event_get_user_data(event);
    if (control == NULL || !ui_valid(control->ui)) return;
    (void)pxsys_system_status_service_set_toggle(
        pxsys_standard_system_status(control->ui->system), control->control,
        toggle_enabled(control->ui, control->control) ? 0 : 1);
}

static lv_obj_t* make_control_tile(pxsys_reference_lvgl_t* ui,
                                   lv_obj_t* parent, int32_t x, int32_t y,
                                   int32_t width, int32_t height,
                                   const char* symbol, const char* title,
                                   const char* state, int active,
                                   lv_event_cb_t clicked, void* user_data) {
    lv_obj_t* tile = lv_button_create(parent);
    lv_obj_t* icon_background;
    lv_obj_t* icon;
    lv_obj_t* label;
    lv_color_t foreground = color_token(
        ui, active ? PXSYS_COLOR_ON_ACCENT : PXSYS_COLOR_TEXT_PRIMARY);
    lv_color_t icon_foreground = color_token(
        ui, active ? PXSYS_COLOR_ON_ACCENT : PXSYS_COLOR_ACCENT);
    const int32_t title_y = height / 2 - 18;
    const int32_t state_y = height / 2 + 4;
    lv_obj_set_pos(tile, x, y);
    lv_obj_set_size(tile, width, height);
    lv_obj_set_style_bg_color(
        tile, color_token(ui, active ? PXSYS_COLOR_ACCENT
                                    : PXSYS_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_color(tile, color_token(ui, PXSYS_COLOR_BORDER),
                              LV_STATE_PRESSED);
    lv_obj_set_style_border_width(tile, active ? 0 : 1, 0);
    lv_obj_set_style_border_color(tile, color_token(ui, PXSYS_COLOR_BORDER), 0);
    lv_obj_set_style_border_opa(tile, LV_OPA_70, 0);
    lv_obj_set_style_shadow_width(tile, 0, 0);
    lv_obj_set_style_radius(tile, 8, 0);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_set_style_opa(tile, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    icon_background = lv_obj_create(tile);
    style_plain(icon_background);
    lv_obj_set_pos(icon_background, 10, (height - 30) / 2);
    lv_obj_set_size(icon_background, 30, 30);
    lv_obj_set_style_radius(icon_background, 8, 0);
    lv_obj_set_style_bg_color(icon_background, icon_foreground, 0);
    lv_obj_set_style_bg_opa(icon_background, active ? LV_OPA_20 : LV_OPA_10,
                            0);
    if (symbol != NULL) {
        icon = make_label(icon_background, symbol, NULL, icon_foreground);
        lv_obj_center(icon);
    } else {
        make_cellular_glyph(icon_background, 7, 8, icon_foreground);
    }
    label = make_label(tile, title,
                       typography_font(ui, PXSYS_TYPOGRAPHY_BODY), foreground);
    lv_obj_set_pos(label, 50, title_y);
    lv_obj_set_width(label, width - 60);
    label = make_label(tile, state,
                       typography_font(ui, PXSYS_TYPOGRAPHY_CAPTION),
                       active ? foreground
                              : color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
    lv_obj_set_pos(label, 50, state_y);
    lv_obj_set_width(label, width - 60);
    lv_obj_add_event_cb(tile, clicked, LV_EVENT_CLICKED, user_data);
    return tile;
}

static void make_control_slider(pxsys_reference_lvgl_t* ui, lv_obj_t* parent,
                                int32_t x, int32_t y, int32_t width,
                                const char* symbol, const char* title,
                                uint8_t value,
                                pxsys_level_control_t control,
                                level_control_t* context) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_t* icon;
    lv_obj_t* slider;
    style_plain(row);
    lv_obj_set_pos(row, x, y);
    lv_obj_set_size(row, width, 48);
    lv_obj_set_style_bg_color(row, color_token(ui, PXSYS_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 8, 0);
    icon = make_label(row, symbol, NULL,
                      color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
    lv_obj_set_pos(icon, 12, 8);
    icon = make_label(row, title,
                      typography_font(ui, PXSYS_TYPOGRAPHY_LABEL),
                      color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
    lv_obj_set_pos(icon, 38, 5);
    slider = lv_slider_create(row);
    lv_obj_set_pos(slider, 38, 30);
    lv_obj_set_size(slider, width - 52, 7);
    lv_slider_set_range(slider,
                        control == PXSYS_LEVEL_CONTROL_BRIGHTNESS ? 2 : 0,
                        100);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, color_token(ui, PXSYS_COLOR_BORDER),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, color_token(ui, PXSYS_COLOR_ACCENT),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, color_token(ui, PXSYS_COLOR_ACCENT),
                              LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_width(slider, 15, LV_PART_KNOB);
    lv_obj_set_style_height(slider, 15, LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 2, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, color_token(ui, PXSYS_COLOR_SURFACE),
                                  LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider, 3, LV_PART_KNOB);
    lv_obj_set_style_shadow_opa(slider, LV_OPA_20, LV_PART_KNOB);
    context->ui = ui;
    context->control = control;
    lv_obj_add_event_cb(slider, control_level_released, LV_EVENT_RELEASED,
                        context);
}

static void make_round_control(pxsys_reference_lvgl_t* ui, lv_obj_t* parent,
                               int32_t slot_x, int32_t y, int32_t slot_width,
                               int32_t diameter, const char* symbol,
                               const char* title, int active,
                               lv_event_cb_t clicked, void* user_data) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_t* icon;
    lv_obj_t* label;
    int32_t x = slot_x + (slot_width - diameter) / 2;
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, diameter, diameter);
    lv_obj_set_style_bg_color(
        button, color_token(ui, active ? PXSYS_COLOR_ACCENT
                                      : PXSYS_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_color(button, color_token(ui, PXSYS_COLOR_BORDER),
                              LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, active ? 0 : 1, 0);
    lv_obj_set_style_border_color(button,
                                  color_token(ui, PXSYS_COLOR_BORDER), 0);
    lv_obj_set_style_border_opa(button, LV_OPA_70, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_opa(button, LV_OPA_80, LV_STATE_PRESSED);
    icon = make_label(button, symbol, NULL,
                      color_token(ui, active ? PXSYS_COLOR_ON_ACCENT
                                             : PXSYS_COLOR_TEXT_PRIMARY));
    lv_obj_center(icon);
    lv_obj_add_event_cb(button, clicked, LV_EVENT_CLICKED, user_data);
    label = make_label(parent, title,
                       typography_font(ui, PXSYS_TYPOGRAPHY_CAPTION),
                       color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
    lv_obj_set_pos(label, slot_x, y + diameter + 6);
    lv_obj_set_width(label, slot_width);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

static void build_notification_shade(pxsys_reference_lvgl_t* ui,
                                     int32_t initial_progress) {
    pxsys_reference_layout_t layout;
    lv_obj_t* body;
    lv_obj_t* home_catcher;
    lv_obj_t* scroll_mask;
    lv_obj_t* settings;
    lv_obj_t* time;
    lv_obj_t* label;
    char time_text[16];
    char date_text[40];
    uint32_t panel_height;
    int32_t content_x;
    int32_t content_width;
    int32_t header_y;
    int32_t time_height;
    int32_t date_height;
    int32_t body_y;
    int32_t body_bottom;
    int32_t tile_y;
    int32_t tile_gap;
    int32_t tile_width;
    int32_t tile_height;
    int32_t slider_y;
    int32_t quick_y;
    int32_t quick_size;
    int32_t quick_count = 0;
    int32_t quick_columns;
    int32_t quick_index = 0;
    int32_t quick_slot_width;
    int32_t network_count = 0;
    int32_t network_index = 0;
    int wifi_visible = 0;
    int cellular_visible = 0;
    int32_t right_inset;
    const lv_font_t* time_font;
    const lv_font_t* date_font;
    if (!ui_valid(ui) ||
        pxsys_reference_layout_compute(&ui->display, &layout) != PXSYS_STATUS_OK)
        return;
    if (ui->notification_shade_open && ui->notification_content != NULL)
        ui->notification_scroll_y =
            lv_obj_get_scroll_y(ui->notification_content);
    if (ui->notification_shade != NULL)
        lv_obj_delete(ui->notification_shade);
    ui->notification_content = NULL;
    ui->notification_shade = lv_obj_create(ui->root);
    style_plain(ui->notification_shade);
    lv_obj_set_pos(ui->notification_shade, 0, 0);
    lv_obj_set_size(ui->notification_shade, (lv_coord_t)ui->display.width,
                    (lv_coord_t)ui->display.height);
    /* Intentionally transparent: no dimming scrim over the content behind. */
    lv_obj_add_flag(ui->notification_shade, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui->notification_shade, shade_close_clicked,
                        LV_EVENT_CLICKED, ui);

    panel_height = ui->display.height;
    if (ui->navigation_mode == PXSYS_NAVIGATION_BUTTONS &&
        ui->navigation_bar != NULL &&
        bar_is_visible(ui, ui->window.navigation_bar_mode))
        panel_height = (uint32_t)layout.navigation_bar.y;
    ui->notification_panel_height = (int32_t)panel_height;
    ui->notification_panel = lv_obj_create(ui->notification_shade);
    style_plain(ui->notification_panel);
    lv_obj_set_pos(ui->notification_panel, 0, 0);
    lv_obj_set_size(ui->notification_panel,
                    (lv_coord_t)ui->display.width,
                    (lv_coord_t)panel_height);
    lv_obj_set_style_bg_color(ui->notification_panel,
                              color_token(ui, PXSYS_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(ui->notification_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ui->notification_panel,
                            ui->theme.base_radius_px * 3u, 0);
    /* The panel background keeps its rounded corners, but the children are
     * not clipped to them: the mask pass costs a lot per child in software
     * rendering and the panel is screen sized, so its corners are at the
     * display corners where no child sits. */
    lv_obj_set_style_clip_corner(ui->notification_panel, false, 0);
    lv_obj_clear_flag(ui->notification_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ui->notification_panel,
                    LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(ui->notification_panel, shade_panel_event,
                        LV_EVENT_PRESSED, ui);
    lv_obj_add_event_cb(ui->notification_panel, shade_panel_event,
                        LV_EVENT_PRESSING, ui);
    lv_obj_add_event_cb(ui->notification_panel, shade_panel_event,
                        LV_EVENT_RELEASED, ui);
    lv_obj_add_event_cb(ui->notification_panel, shade_panel_event,
                        LV_EVENT_PRESS_LOST, ui);

    content_x = layout.safe_area.x + (int32_t)layout.outer_padding;
    content_width = (int32_t)layout.safe_area.width -
                    (int32_t)layout.outer_padding * 2;
    if (content_width < 80) {
        content_x = (int32_t)layout.outer_padding;
        content_width = (int32_t)ui->display.width -
                        (int32_t)layout.outer_padding * 2;
    }
    header_y = layout.safe_area.y + (int32_t)layout.outer_padding;
    right_inset = (int32_t)ui->display.width - content_x - content_width;
    snprintf(time_text, sizeof(time_text),
             ui->system_status.time_valid ? "%02u:%02u" : "--:--",
             (unsigned)ui->system_status.hour,
             (unsigned)ui->system_status.minute);
    time_font = typography_font(ui, PXSYS_TYPOGRAPHY_DISPLAY);
    date_font = typography_font(ui, PXSYS_TYPOGRAPHY_LABEL);
    time_height = time_font != NULL ? time_font->line_height + 2 : 32;
    date_height = date_font != NULL ? date_font->line_height + 2 : 16;
    time = make_label(ui->notification_panel, time_text, time_font,
                      color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
    lv_obj_set_pos(time, content_x, header_y);
    lv_obj_set_size(time, content_width - 48, time_height);
    if (ui->system_status.date_valid)
        snprintf(date_text, sizeof(date_text), "%04u-%02u-%02u",
                 (unsigned)ui->system_status.year,
                 (unsigned)ui->system_status.month,
                 (unsigned)ui->system_status.day);
    else
        snprintf(date_text, sizeof(date_text), "---- -- --");
    label = make_label(ui->notification_panel, date_text, date_font,
                       color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
    lv_obj_set_pos(label, content_x, header_y + time_height - 1);
    lv_obj_set_size(label, content_width - 48, date_height);

    settings = lv_button_create(ui->notification_panel);
    lv_obj_set_pos(settings, (int32_t)ui->display.width - right_inset - 34,
                   header_y);
    lv_obj_set_size(settings, 34, 34);
    lv_obj_set_style_bg_color(settings, color_token(ui, PXSYS_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_color(settings, color_token(ui, PXSYS_COLOR_BORDER),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(settings, 0, 0);
    lv_obj_set_style_border_width(settings, 0, 0);
    lv_obj_set_style_radius(settings, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(settings, 0, 0);
    lv_obj_clear_flag(settings, LV_OBJ_FLAG_SCROLLABLE);
    label = make_label(settings, LV_SYMBOL_SETTINGS, NULL,
                       color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
    lv_obj_center(label);
    lv_obj_add_event_cb(settings, shade_settings_clicked, LV_EVENT_CLICKED, ui);

    body_y = header_y + time_height + date_height + 5;
    body_bottom = (int32_t)panel_height;
    if (body_bottom < body_y) body_bottom = body_y;
    body = lv_obj_create(ui->notification_panel);
    ui->notification_content = body;
    style_plain(body);
    lv_obj_set_pos(body, 0, body_y);
    lv_obj_set_size(body, (lv_coord_t)ui->display.width,
                    body_bottom - body_y);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(body, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(body, color_token(ui, PXSYS_COLOR_BORDER),
                              LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(body, LV_RADIUS_CIRCLE, LV_PART_SCROLLBAR);
    lv_obj_add_event_cb(body, shade_body_event, LV_EVENT_PRESSED, ui);
    lv_obj_add_event_cb(body, shade_body_event, LV_EVENT_PRESSING, ui);
    lv_obj_add_event_cb(body, shade_body_event, LV_EVENT_RELEASED, ui);
    lv_obj_add_event_cb(body, shade_body_event, LV_EVENT_PRESS_LOST, ui);

    tile_y = 6;
    tile_gap = (int32_t)layout.item_gap;
    tile_height = layout.size_class == PXSYS_UI_SIZE_COMPACT ? 58 : 64;
#if PXSYS_REFERENCE_UI_WIFI
    wifi_visible = radio_supported(ui, PXSYS_NETWORK_WIFI);
#endif
#if PXSYS_REFERENCE_UI_CELLULAR
    cellular_visible = radio_supported(ui, PXSYS_NETWORK_CELLULAR);
#endif
    network_count = wifi_visible + cellular_visible;
    tile_width = network_count > 1 ? (content_width - tile_gap) / 2
                                   : content_width;
    if (wifi_visible) {
        ui->network_controls[0].ui = ui;
        ui->network_controls[0].network = PXSYS_NETWORK_WIFI;
        make_control_tile(
            ui, body,
            content_x + network_index * (tile_width + tile_gap), tile_y,
            tile_width, tile_height, LV_SYMBOL_WIFI,
            translated(ui, "settings.wifi", "Wi-Fi"),
            radio_connected(ui, PXSYS_NETWORK_WIFI)
                ? translated(ui, "state.connected", "Connected")
                : radio_enabled(ui, PXSYS_NETWORK_WIFI)
                      ? translated(ui, "state.on", "On")
                      : translated(ui, "state.off", "Off"),
            radio_enabled(ui, PXSYS_NETWORK_WIFI), control_network_clicked,
            &ui->network_controls[0]);
        network_index++;
    }
    if (cellular_visible) {
        ui->network_controls[1].ui = ui;
        ui->network_controls[1].network = PXSYS_NETWORK_CELLULAR;
        make_control_tile(
            ui, body,
            content_x + network_index * (tile_width + tile_gap), tile_y,
            tile_width, tile_height, NULL,
            translated(ui, "control.mobile", "Mobile"),
            radio_connected(ui, PXSYS_NETWORK_CELLULAR)
                ? translated(ui, "state.connected", "Connected")
                : radio_enabled(ui, PXSYS_NETWORK_CELLULAR)
                      ? translated(ui, "state.on", "On")
                      : translated(ui, "state.off", "Off"),
            radio_enabled(ui, PXSYS_NETWORK_CELLULAR),
            control_network_clicked, &ui->network_controls[1]);
    }

    slider_y = tile_y + (network_count ? tile_height + 10 : 0);
    if (ui->system_status.volume_supported) {
        make_control_slider(ui, body, content_x, slider_y, content_width,
                            LV_SYMBOL_VOLUME_MAX,
                            translated(ui, "control.volume", "Volume"),
                            ui->system_status.volume_percent,
                            PXSYS_LEVEL_CONTROL_VOLUME,
                            &ui->level_controls[0]);
        slider_y += 56;
    }
    if (ui->system_status.brightness_supported) {
        make_control_slider(ui, body, content_x, slider_y, content_width,
                            LV_SYMBOL_EYE_OPEN,
                            translated(ui, "control.brightness", "Brightness"),
                            ui->system_status.brightness_percent,
                            PXSYS_LEVEL_CONTROL_BRIGHTNESS,
                            &ui->level_controls[1]);
        slider_y += 56;
    }

    quick_y = slider_y + 8;
    quick_size = layout.size_class == PXSYS_UI_SIZE_COMPACT ? 48 : 52;
    quick_count = 1 + ui->system_status.bluetooth_supported +
                  ui->system_status.do_not_disturb_supported +
                  ui->system_status.flashlight_supported;
    if (quick_count < 1) quick_count = 1;
    quick_columns = layout.size_class == PXSYS_UI_SIZE_COMPACT ? 3 : 4;
    if (quick_columns > quick_count) quick_columns = quick_count;
    quick_slot_width = content_width / quick_columns;
    if (ui->system_status.bluetooth_supported) {
        ui->toggle_controls[0].ui = ui;
        ui->toggle_controls[0].control = PXSYS_TOGGLE_BLUETOOTH;
        make_round_control(ui, body, content_x, quick_y, quick_slot_width,
                           quick_size, LV_SYMBOL_BLUETOOTH,
                           translated(ui, "control.bluetooth", "Bluetooth"),
                           ui->system_status.bluetooth_enabled,
                           control_toggle_clicked, &ui->toggle_controls[0]);
        quick_index++;
    }
    if (ui->system_status.do_not_disturb_supported) {
        ui->toggle_controls[1].ui = ui;
        ui->toggle_controls[1].control = PXSYS_TOGGLE_DO_NOT_DISTURB;
        make_round_control(
            ui, body,
            content_x + (quick_index % quick_columns) * quick_slot_width,
            quick_y + (quick_index / quick_columns) * 76,
            quick_slot_width, quick_size, LV_SYMBOL_BELL,
            translated(ui, "control.focus", "Focus"),
            ui->system_status.do_not_disturb_enabled,
            control_toggle_clicked, &ui->toggle_controls[1]);
        quick_index++;
    }
    if (ui->system_status.flashlight_supported) {
        ui->toggle_controls[2].ui = ui;
        ui->toggle_controls[2].control = PXSYS_TOGGLE_FLASHLIGHT;
        make_round_control(
            ui, body,
            content_x + (quick_index % quick_columns) * quick_slot_width,
            quick_y + (quick_index / quick_columns) * 76,
            quick_slot_width, quick_size, LV_SYMBOL_CHARGE,
            translated(ui, "control.light", "Flashlight"),
            ui->system_status.flashlight_enabled, control_toggle_clicked,
            &ui->toggle_controls[2]);
        quick_index++;
    }
    make_round_control(
        ui, body,
        content_x + (quick_index % quick_columns) * quick_slot_width,
        quick_y + (quick_index / quick_columns) * 76,
        quick_slot_width, quick_size,
        LV_SYMBOL_TINT,
        translated(ui, "control.theme", "Theme"),
        ui->theme.effective_scheme == PXSYS_COLOR_SCHEME_DARK,
        theme_clicked, ui);
    quick_index++;
    {
        lv_obj_t* scroll_extent = lv_obj_create(body);
        style_plain(scroll_extent);
        lv_obj_set_pos(scroll_extent, content_x,
                       quick_y + ((quick_index - 1) / quick_columns) * 76 +
                           quick_size + 26);
        lv_obj_set_size(scroll_extent, 1, 1);
    }
    shade_enable_pointer_bubble(body);
    lv_obj_update_layout(body);
    lv_obj_scroll_to_y(body, ui->notification_scroll_y, LV_ANIM_OFF);
    scroll_mask = lv_obj_create(ui->notification_panel);
    style_plain(scroll_mask);
    lv_obj_set_pos(scroll_mask, 0, body_y);
    lv_obj_set_size(scroll_mask, (lv_coord_t)ui->display.width, 6);
    lv_obj_set_style_bg_color(scroll_mask,
                              color_token(ui, PXSYS_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(scroll_mask, LV_OPA_COVER, 0);
    if (ui->navigation_mode == PXSYS_NAVIGATION_GESTURES) {
        home_catcher = lv_obj_create(ui->notification_panel);
        style_plain(home_catcher);
        lv_obj_set_pos(
            home_catcher, 0,
            (int32_t)panel_height - (int32_t)gesture_strip_height(&layout));
        lv_obj_set_size(home_catcher, (lv_coord_t)ui->display.width,
                        (lv_coord_t)gesture_strip_height(&layout));
        lv_obj_set_style_bg_opa(home_catcher, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(home_catcher,
                        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(home_catcher, shade_home_gesture_event,
                            LV_EVENT_PRESSED, ui);
        lv_obj_add_event_cb(home_catcher, shade_home_gesture_event,
                            LV_EVENT_PRESSING, ui);
        lv_obj_add_event_cb(home_catcher, shade_home_gesture_event,
                            LV_EVENT_RELEASED, ui);
        lv_obj_add_event_cb(home_catcher, shade_home_gesture_event,
                            LV_EVENT_PRESS_LOST, ui);
    }
    lv_obj_remove_flag(ui->notification_shade, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui->notification_shade);
    if (ui->navigation_mode == PXSYS_NAVIGATION_BUTTONS &&
        ui->navigation_bar != NULL)
        lv_obj_move_foreground(ui->navigation_bar);
    ui->notification_shade_open = 1;
    notification_shade_progress_set(ui, initial_progress);
    if (ui->toast_visible && ui->toast != NULL) toast_restack(ui);
}

static void status_bar_event(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    lv_indev_t* indev = lv_event_get_indev(event);
    lv_point_t point;
    if (!ui_valid(ui) || indev == NULL) return;
    lv_indev_get_point(indev, &point);
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
        ui->status_press_y = point.y;
        ui->notification_dragging = 1;
        ui->notification_drag_moved = 0;
    } else if (lv_event_get_code(event) == LV_EVENT_PRESSING) {
        int32_t distance = point.y - ui->status_press_y;
        int32_t progress;
        if (!bar_is_visible(ui, ui->window.status_bar_mode) || distance <= 0)
            return;
        if (distance >= 6) ui->notification_drag_moved = 1;
        if (ui->notification_shade == NULL ||
            !ui->notification_shade_open) {
            build_notification_shade(ui, 0);
            ui->notification_dragging = 1;
            ui->notification_drag_moved = distance >= 6;
        }
        progress = (int32_t)((int64_t)point.y * 256 /
                             ui->notification_panel_height);
        notification_shade_progress_set(ui, progress);
    } else if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
        int32_t distance = point.y - ui->status_press_y;
        if (!ui->transient_revealed && has_transient_chrome(ui) &&
            distance >= 8) {
            reveal_transient_chrome(ui);
        } else if (ui->notification_shade_open) {
            int32_t open = !ui->notification_drag_moved ||
                           ui->notification_progress >= 64;
            ui->notification_dragging = 0;
            settle_notification_shade(ui, open ? 256 : 0);
        } else if (point.y >= ui->status_press_y &&
                   bar_is_visible(ui, ui->window.status_bar_mode)) {
            build_notification_shade(ui, 0);
            settle_notification_shade(ui, 256);
        }
        ui->notification_dragging = 0;
    } else if (lv_event_get_code(event) == LV_EVENT_PRESS_LOST) {
        ui->notification_dragging = 0;
        if (ui->notification_shade_open && ui->notification_progress < 256)
            settle_notification_shade(ui, 0);
    }
}

static void navigation_handle_reset(pxsys_reference_lvgl_t* ui) {
    if (ui->navigation_handle == NULL) return;
    lv_obj_set_width(ui->navigation_handle,
                     (lv_coord_t)ui->navigation_handle_width);
    lv_obj_align(ui->navigation_handle, LV_ALIGN_BOTTOM_MID, 0, -5);
}

static void navigation_gesture_event(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    lv_indev_t* indev = lv_event_get_indev(event);
    lv_point_t point;
    if (!ui_valid(ui) || indev == NULL) return;
    lv_indev_get_point(indev, &point);
    lv_event_code_t code = lv_event_get_code(event);
    int32_t distance;
    if (code == LV_EVENT_PRESSED) {
        ui->navigation_press_x = point.x;
        ui->navigation_press_y = point.y;
        ui->navigation_last_x = point.x;
        ui->navigation_last_y = point.y;
        ui->navigation_last_motion_tick = lv_tick_get();
        ui->navigation_dragging = 1;
        ui->navigation_from_home =
            ui->content_active && ui->active_page == REFERENCE_PAGE_HOME;
        return;
    }
    if (code == LV_EVENT_PRESS_LOST) {
        /* The drag was interrupted (for example by a rebuild); undo whatever
         * motion it applied so the application does not stay transformed. */
        ui->navigation_dragging = 0;
        ui->navigation_from_home = 0;
        navigation_handle_reset(ui);
        application_scale_reset(ui);
        return;
    }
    distance = ui->navigation_press_y - point.y;
    if (code == LV_EVENT_PRESSING || code == LV_EVENT_GESTURE) {
        int32_t delta_x = point.x - ui->navigation_last_x;
        int32_t delta_y = point.y - ui->navigation_last_y;
        if (delta_x < 0) delta_x = -delta_x;
        if (delta_y < 0) delta_y = -delta_y;
        if (delta_x >= NAVIGATION_GESTURE_MOTION_SLOP ||
            delta_y >= NAVIGATION_GESTURE_MOTION_SLOP) {
            ui->navigation_last_x = point.x;
            ui->navigation_last_y = point.y;
            ui->navigation_last_motion_tick = lv_tick_get();
        }
#if PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS
        if (ui->animations_enabled && distance > 0) {
            int32_t max_progress = (int32_t)ui->display.height * 2 / 3;
            int32_t progress = distance > max_progress ? max_progress : distance;
            int32_t horizontal = (point.x - ui->navigation_press_x) / 2;
            int32_t scale = 256 - progress * 36 / max_progress;
            int32_t radius = progress / 4;
            int32_t translate_y = -progress / 3;
            int32_t abs_x = horizontal < 0 ? -horizontal : horizontal;
            int32_t abs_y = translate_y < 0 ? -translate_y : translate_y;
            int32_t scale_x = 256 -
                (int32_t)((int64_t)(abs_x + 4) * 512 / ui->display.width);
            int32_t scale_y = 256 -
                (int32_t)((int64_t)(abs_y + 4) * 512 / ui->display.height);
            int32_t inset_x;
            int32_t inset_y;
            int32_t min_x;
            int32_t max_x;
            int32_t min_y;
            int32_t max_y;
            if (scale_x < scale) scale = scale_x;
            if (scale_y < scale) scale = scale_y;
            if (scale < 144) scale = 144;
            inset_x = (int32_t)((int64_t)ui->display.width *
                                (256 - scale) / 512);
            inset_y = (int32_t)((int64_t)ui->display.height *
                                (256 - scale) / 512);
            min_x = (int32_t)ui->display.safe_insets.left + 4 - inset_x;
            max_x = inset_x - (int32_t)ui->display.safe_insets.right - 4;
            min_y = (int32_t)ui->display.safe_insets.top + 4 - inset_y;
            max_y = inset_y - (int32_t)ui->display.safe_insets.bottom - 4;
            if (min_x > max_x) {
                min_x = 0;
                max_x = 0;
            }
            if (min_y > max_y) {
                min_y = 0;
                max_y = 0;
            }
            if (horizontal > max_x) horizontal = max_x;
            if (horizontal < min_x) horizontal = min_x;
            if (radius > 18) radius = 18;
            if (translate_y < min_y) translate_y = min_y;
            if (translate_y > max_y) translate_y = max_y;
            /* The launcher page already is the system home; moving or scaling
             * it only produces an empty backdrop. */
            if (!ui->navigation_from_home)
                application_motion_set(ui, scale, horizontal, translate_y,
                                       radius);
            if (ui->navigation_handle != NULL) {
                /* Feedback only: widen the pill. Moving it vertically would
                 * clip it against the gesture strip on this LVGL version. */
                lv_obj_set_width(ui->navigation_handle, 56 + progress / 2);
            }
        }
#endif
        return;
    }
    if (code != LV_EVENT_RELEASED) return;
    ui->navigation_dragging = 0;
    navigation_handle_reset(ui);
    if (distance > NAVIGATION_GESTURE_COMMIT_DISTANCE) {
        if (ui->navigation_from_home) {
            application_scale_reset(ui);
            ui->task_switcher_handoff = 1;
            build_task_switcher(ui);
        } else if (lv_tick_elaps(ui->navigation_last_motion_tick) >=
                   NAVIGATION_GESTURE_HOLD_MS) {
            ui->task_switcher_handoff = 1;
            capture_current_task(ui, 1);
            build_task_switcher(ui);
            application_scale_reset(ui);
        } else {
            capture_current_task(ui, 0);
            animate_application(ui, 212, 0,
                                -(int32_t)ui->display.height / 3,
                                home_animation_completed);
        }
    } else {
        animate_application(ui, 256, 0, 0, NULL);
    }
    ui->navigation_from_home = 0;
}

static void transient_navigation_reveal_event(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    lv_indev_t* indev = lv_event_get_indev(event);
    lv_point_t point;
    lv_event_code_t code = lv_event_get_code(event);
    if (!ui_valid(ui) || indev == NULL) return;
    lv_indev_get_point(indev, &point);
    if (code == LV_EVENT_PRESSED) {
        ui->navigation_press_y = point.y;
        ui->navigation_dragging = 1;
        return;
    }
    if (code == LV_EVENT_RELEASED) {
        int32_t distance = ui->navigation_press_y - point.y;
        ui->navigation_dragging = 0;
        if (distance >= 8) reveal_transient_chrome(ui);
    } else if (code == LV_EVENT_PRESS_LOST) {
        ui->navigation_dragging = 0;
    }
}

#if PXSYS_REFERENCE_UI_CELLULAR
static void make_signal_icon(pxsys_reference_lvgl_t* ui, lv_obj_t* parent,
                             int32_t x, int32_t center_y) {
    uint8_t index;
    uint8_t level = radio_signal_level(ui, PXSYS_NETWORK_CELLULAR);
    for (index = 0; index < 4; ++index) {
        lv_obj_t* bar = lv_obj_create(parent);
        int32_t height = 3 + (int32_t)index * 3;
        style_plain(bar);
        lv_obj_set_size(bar, 2, height);
        lv_obj_set_pos(bar, x + (int32_t)index * 4,
                       center_y + 6 - height);
        lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(
            bar,
            color_token(ui,
                        radio_connected(ui, PXSYS_NETWORK_CELLULAR) &&
                                index < level
                            ? PXSYS_COLOR_TEXT_PRIMARY : PXSYS_COLOR_BORDER),
            0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    }
}
#endif

#if PXSYS_REFERENCE_UI_WIFI
static void make_wifi_icon(pxsys_reference_lvgl_t* ui, lv_obj_t* parent,
                           int32_t x, int32_t center_y) {
    lv_obj_t* icon = make_label(
        parent, LV_SYMBOL_WIFI, NULL,
        color_token(ui, radio_connected(ui, PXSYS_NETWORK_WIFI)
                            ? PXSYS_COLOR_TEXT_PRIMARY : PXSYS_COLOR_BORDER));
    uint8_t level = radio_signal_level(ui, PXSYS_NETWORK_WIFI);
    lv_obj_set_pos(icon, x, center_y - 8);
    lv_obj_set_style_opa(icon,
                         level >= 3 ? LV_OPA_COVER :
                         level == 2 ? LV_OPA_80 :
                         level == 1 ? LV_OPA_60 : LV_OPA_40, 0);
}
#endif

static void make_battery_icon(pxsys_reference_lvgl_t* ui, lv_obj_t* parent,
                              int32_t x, int32_t center_y) {
    lv_obj_t* body = lv_obj_create(parent);
    lv_obj_t* fill;
    lv_obj_t* cap;
    uint32_t percent = ui->system_status.battery_valid
                           ? ui->system_status.battery_percent : 0;
    int32_t fill_width = (int32_t)(16u * percent / 100u);
    style_plain(body);
    lv_obj_set_pos(body, x, center_y - 6);
    lv_obj_set_size(body, 22, 12);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 1, 0);
    lv_obj_set_style_border_color(body,
                                  color_token(ui, PXSYS_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_radius(body, 3, 0);
    fill = lv_obj_create(body);
    style_plain(fill);
    if (fill_width < 2 && percent != 0) fill_width = 2;
    lv_obj_set_size(fill, fill_width, 6);
    lv_obj_align(fill, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_style_radius(fill, 1, 0);
    lv_obj_set_style_bg_color(
        fill, color_token(ui, ui->system_status.charging
                                 ? PXSYS_COLOR_SUCCESS
                                 : percent <= 15 && ui->system_status.battery_valid
                                       ? PXSYS_COLOR_ERROR
                                       : PXSYS_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    cap = lv_obj_create(parent);
    style_plain(cap);
    lv_obj_set_pos(cap, x + 23, center_y - 3);
    lv_obj_set_size(cap, 2, 6);
    lv_obj_set_style_radius(cap, 1, 0);
    lv_obj_set_style_bg_color(cap,
                              color_token(ui, PXSYS_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
}

static void build_status_bar(pxsys_reference_lvgl_t* ui,
                             const pxsys_reference_layout_t* layout) {
    lv_obj_t* status = lv_obj_create(ui->root);
    lv_obj_t* time_label;
#if PXSYS_REFERENCE_UI_BATTERY_PERCENT
    lv_obj_t* percent_label;
    const lv_font_t* percent_font;
    int32_t percent_width;
#endif
    char time_text[8];
#if PXSYS_REFERENCE_UI_BATTERY_PERCENT
    char percent_text[8];
#endif
    int32_t safe_right = layout->safe_area.x +
                         (int32_t)layout->safe_area.width;
    int32_t safe_top = layout->safe_area.y;
    int32_t battery_x = safe_right - (int32_t)layout->outer_padding - 25;
#if PXSYS_REFERENCE_UI_WIFI || PXSYS_REFERENCE_UI_CELLULAR
    int32_t indicator_x;
#endif
    int32_t center_y = safe_top +
        ((int32_t)layout->status_bar.height - safe_top) / 2;
    int32_t align_y = center_y - (int32_t)layout->status_bar.height / 2;
    ui->status_bar = status;
    style_plain(status);
    lv_obj_set_pos(status, layout->status_bar.x, layout->status_bar.y);
    lv_obj_set_size(status, (lv_coord_t)layout->status_bar.width,
                    (lv_coord_t)layout->status_bar.height);
    lv_obj_set_style_bg_color(status,
                              color_token(ui, PXSYS_COLOR_BACKGROUND), 0);
    if (ui->window.status_bar_mode == PXSYS_WINDOW_BAR_TRANSIENT)
        lv_obj_set_style_bg_opa(status, (lv_opa_t)224, 0);
    if (ui->features & PXSYS_REFERENCE_UI_NOTIFICATION_SHADE) {
        lv_obj_add_flag(status, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(status, status_bar_event, LV_EVENT_PRESSED, ui);
        lv_obj_add_event_cb(status, status_bar_event, LV_EVENT_PRESSING, ui);
        lv_obj_add_event_cb(status, status_bar_event, LV_EVENT_RELEASED, ui);
        lv_obj_add_event_cb(status, status_bar_event, LV_EVENT_PRESS_LOST, ui);
    }
    snprintf(time_text, sizeof(time_text),
             ui->system_status.time_valid ? "%02u:%02u" : "--:--",
             (unsigned)ui->system_status.hour,
             (unsigned)ui->system_status.minute);
    time_label = make_label(status, time_text, ui->text_font,
                            color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
    lv_obj_align(time_label, LV_ALIGN_LEFT_MID,
                 layout->safe_area.x + (int32_t)layout->outer_padding,
                 align_y);
#if PXSYS_REFERENCE_UI_BATTERY_PERCENT
    percent_width = 0;
    if (ui->system_status.battery_valid) {
        snprintf(percent_text, sizeof(percent_text), "%u%%",
                 (unsigned)ui->system_status.battery_percent);
        percent_font = typography_font(ui, PXSYS_TYPOGRAPHY_CAPTION);
        percent_label = make_label(status, percent_text, percent_font,
                                   color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
        lv_label_set_long_mode(percent_label, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_width(percent_label, LV_SIZE_CONTENT);
        lv_obj_set_height(percent_label,
                          percent_font != NULL ? percent_font->line_height
                                               : LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(percent_label, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(percent_label, LV_ALIGN_RIGHT_MID,
                     -((int32_t)layout->viewport.width - safe_right) -
                         (int32_t)layout->outer_padding - 29,
                     align_y);
        lv_obj_update_layout(status);
        percent_width = lv_obj_get_width(percent_label);
    }
#endif
#if PXSYS_REFERENCE_UI_WIFI || PXSYS_REFERENCE_UI_CELLULAR
#if PXSYS_REFERENCE_UI_BATTERY_PERCENT
    indicator_x = ui->system_status.battery_valid
                      ? battery_x - 4 - percent_width - 8 - 16
                      : safe_right - (int32_t)layout->outer_padding - 16;
#else
    indicator_x = ui->system_status.battery_valid
                      ? safe_right - (int32_t)layout->outer_padding - 45
                      : safe_right - (int32_t)layout->outer_padding - 16;
#endif
#endif
#if PXSYS_REFERENCE_UI_CELLULAR
    if (radio_supported(ui, PXSYS_NETWORK_CELLULAR) &&
        radio_enabled(ui, PXSYS_NETWORK_CELLULAR)) {
        make_signal_icon(ui, status, indicator_x, center_y);
        indicator_x -= 22;
    }
#endif
#if PXSYS_REFERENCE_UI_WIFI
    if (radio_supported(ui, PXSYS_NETWORK_WIFI) &&
        radio_enabled(ui, PXSYS_NETWORK_WIFI))
        make_wifi_icon(ui, status, indicator_x, center_y);
#endif
    if (ui->system_status.battery_valid)
        make_battery_icon(ui, status, battery_x, center_y);
}

static void toast_apply_theme(pxsys_reference_lvgl_t* ui,
                              pxsys_toast_tone_t tone) {
    pxsys_color_token_t border = PXSYS_COLOR_BORDER;
    if (ui->toast == NULL) return;
    if (tone == PXSYS_TOAST_SUCCESS) border = PXSYS_COLOR_SUCCESS;
    else if (tone == PXSYS_TOAST_WARNING) border = PXSYS_COLOR_WARNING;
    else if (tone == PXSYS_TOAST_ERROR) border = PXSYS_COLOR_ERROR;
    lv_obj_set_style_bg_color(ui->toast,
                              color_token(ui, PXSYS_COLOR_SURFACE), 0);
    lv_obj_set_style_border_color(ui->toast, color_token(ui, border), 0);
    lv_obj_set_style_shadow_color(ui->toast,
                                  color_token(ui, PXSYS_COLOR_SCRIM), 0);
    lv_obj_set_style_text_color(ui->toast_label,
                                color_token(ui, PXSYS_COLOR_TEXT_PRIMARY), 0);
}

static void toast_reposition(pxsys_reference_lvgl_t* ui) {
    pxsys_reference_layout_t layout;
    int32_t bottom_offset;
    int32_t max_width;
    if (ui->toast == NULL ||
        pxsys_reference_layout_compute(&ui->display, &layout) != PXSYS_STATUS_OK)
        return;
    max_width = (int32_t)layout.safe_area.width -
                2 * (int32_t)layout.outer_padding;
    if (max_width > 420) max_width = 420;
    if (max_width < 96) max_width = 96;
    lv_obj_set_style_max_width(ui->toast, max_width, 0);
    lv_obj_set_style_max_width(ui->toast_label, max_width - 24, 0);
    if (!(ui->active_chrome & PXSYS_REFERENCE_UI_NAVIGATION_BAR) ||
        !bar_is_visible(ui, ui->window.navigation_bar_mode)) {
        bottom_offset = 16;
    } else if (ui->navigation_mode == PXSYS_NAVIGATION_GESTURES) {
        bottom_offset = layout.size_class == PXSYS_UI_SIZE_COMPACT ? 26 : 34;
    } else {
        bottom_offset = (int32_t)layout.navigation_bar.height + 10;
    }
    lv_obj_align(ui->toast, LV_ALIGN_BOTTOM_MID, 0, -bottom_offset);
}

static void toast_restack(pxsys_reference_lvgl_t* ui) {
    lv_obj_t* top_layer;
    int32_t root_index;
    if (ui->toast == NULL || ui->root == NULL) return;
    top_layer = lv_display_get_layer_top(lv_obj_get_display(ui->root));
    if (ui->parent != top_layer) {
        lv_obj_move_foreground(ui->toast);
        return;
    }
    root_index = lv_obj_get_index(ui->root);
    if (root_index >= 0)
        lv_obj_move_to_index(ui->toast, root_index + 1);
}

static void toast_hide(lv_timer_t* timer) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_timer_get_user_data(timer);
    if (!ui_valid(ui) || ui->toast == NULL || !ui->toast_visible) return;
    ui->toast_visible = 0;
    lv_obj_fade_out(ui->toast, 160, 0);
}

static void toast_posted(void* context, const pxsys_toast_message_t* toast) {
    pxsys_reference_lvgl_t* ui = (pxsys_reference_lvgl_t*)context;
    uint32_t duration;
    if (!ui_valid(ui) || toast == NULL) return;
    if (ui->toast == NULL) {
        ui->toast = lv_obj_create(ui->parent);
        lv_obj_set_size(ui->toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_min_width(ui->toast, 120, 0);
        lv_obj_set_style_min_height(ui->toast, 42, 0);
        lv_obj_set_style_radius(ui->toast, ui->theme.base_radius_px * 3u, 0);
        lv_obj_set_style_border_width(ui->toast, 1, 0);
        lv_obj_set_style_pad_hor(ui->toast, 12, 0);
        lv_obj_set_style_pad_ver(ui->toast, 9, 0);
        lv_obj_set_style_shadow_width(ui->toast, 14, 0);
        lv_obj_set_style_shadow_opa(ui->toast, LV_OPA_30, 0);
        lv_obj_set_style_shadow_offset_y(ui->toast, 4, 0);
        lv_obj_clear_flag(ui->toast, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(ui->toast, LV_OBJ_FLAG_CLICKABLE);
        ui->toast_label = make_label(ui->toast, "",
                                     typography_font(ui, PXSYS_TYPOGRAPHY_BODY),
                                     color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
        lv_label_set_long_mode(ui->toast_label, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_align(ui->toast_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(ui->toast_label);
    }
    lv_label_set_text_fmt(ui->toast_label, "%.*s", (int)toast->message.size,
                          toast->message.data);
    toast_apply_theme(ui, toast->tone);
    ui->toast_tone = toast->tone;
    toast_reposition(ui);
    lv_obj_remove_flag(ui->toast, LV_OBJ_FLAG_HIDDEN);
    toast_restack(ui);
    ui->toast_visible = 1;
    lv_anim_delete(ui->toast, NULL);
    lv_obj_set_style_opa(ui->toast, LV_OPA_TRANSP, 0);
    lv_obj_fade_in(ui->toast, 140, 0);
    duration = toast->duration_ms == 0 ? 2500 : toast->duration_ms;
    if (ui->toast_timer == NULL)
        ui->toast_timer = lv_timer_create(toast_hide, duration, ui);
    else {
        lv_timer_set_period(ui->toast_timer, duration);
        lv_timer_reset(ui->toast_timer);
    }
}

#if PXSYS_REFERENCE_UI_BUILTIN_SETTINGS

static void page_content_init(pxsys_reference_lvgl_t* ui,
                              const pxsys_reference_layout_t* layout) {
    lv_obj_set_layout(ui->content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ui->content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(
        ui->content,
        layout->size_class == PXSYS_UI_SIZE_COMPACT ? 8 : 10, 0);
    lv_obj_set_style_pad_bottom(ui->content, 8, 0);
    lv_obj_add_flag(ui->content,
                    LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(ui->content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui->content, LV_SCROLLBAR_MODE_AUTO);
}

static lv_obj_t* make_page_button(pxsys_reference_lvgl_t* ui, lv_obj_t* parent,
                                  const char* text,
                                  pxsys_color_token_t background,
                                  pxsys_color_token_t foreground,
                                  lv_event_cb_t clicked, void* user_data) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_t* label;
    lv_obj_set_width(button, LV_PCT(100));
    lv_obj_set_height(button, 42);
    lv_obj_set_style_radius(button, ui->theme.base_radius_px * 2u, 0);
    lv_obj_set_style_bg_color(button, color_token(ui, background), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    label = make_label(button, text, typography_font(ui, PXSYS_TYPOGRAPHY_LABEL),
                       color_token(ui, foreground));
    lv_obj_center(label);
    if (clicked != NULL)
        lv_obj_add_event_cb(button, clicked, LV_EVENT_CLICKED, user_data);
    return button;
}

/* ---------------------------------------------------------------- Sound */

static void make_level_control_block(pxsys_reference_lvgl_t* ui,
                                     lv_obj_t* section, const char* title,
                                     pxsys_level_control_t control,
                                     uint8_t value,
                                     level_control_t* context) {
    lv_obj_t* block = lv_obj_create(section);
    lv_obj_t* label;
    lv_obj_t* slider;
    int32_t minimum = control == PXSYS_LEVEL_CONTROL_BRIGHTNESS ? 2 : 0;
    style_plain(block);
    lv_obj_set_width(block, LV_PCT(100));
    lv_obj_set_height(block, LV_SIZE_CONTENT);
    lv_obj_set_layout(block, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(block, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(block, 10, 0);
    lv_obj_set_style_pad_row(block, 8, 0);
    lv_obj_add_flag(block, LV_OBJ_FLAG_CLICKABLE);
    label = make_label(block, title,
                       typography_font(ui, PXSYS_TYPOGRAPHY_LABEL),
                       color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
    lv_obj_set_width(label, LV_PCT(100));
    slider = lv_slider_create(block);
    lv_obj_set_width(slider, LV_PCT(100));
    lv_slider_set_range(slider, minimum, 100);
    lv_slider_set_value(slider, value < minimum ? minimum : value,
                        LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, color_token(ui, PXSYS_COLOR_BORDER),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, color_token(ui, PXSYS_COLOR_ACCENT),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, color_token(ui, PXSYS_COLOR_ACCENT),
                              LV_PART_KNOB);
    context->ui = ui;
    context->control = control;
    lv_obj_add_event_cb(slider, control_level_released, LV_EVENT_RELEASED,
                        context);
}

static void build_sound_settings(pxsys_reference_lvgl_t* ui,
                                 const pxsys_reference_layout_t* layout) {
    page_content_init(ui, layout);
    if (ui->system_status.volume_supported ||
        ui->system_status.brightness_supported) {
        lv_obj_t* section = make_settings_section(
            ui, translated(ui, "settings.section.device", "Device"));
        if (ui->system_status.volume_supported) {
            make_level_control_block(
                ui, section, translated(ui, "settings.sound.volume", "Volume"),
                PXSYS_LEVEL_CONTROL_VOLUME, ui->system_status.volume_percent,
                &ui->level_controls[0]);
        }
        if (ui->system_status.brightness_supported) {
            make_level_control_block(
                ui, section,
                translated(ui, "settings.sound.brightness", "Brightness"),
                PXSYS_LEVEL_CONTROL_BRIGHTNESS,
                ui->system_status.brightness_percent, &ui->level_controls[1]);
        }
    } else {
        lv_obj_t* label = make_label(
            ui->content,
            translated(ui, "settings.sound.unsupported",
                       "No adjustable levels"),
            typography_font(ui, PXSYS_TYPOGRAPHY_BODY),
            color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
        lv_obj_set_width(label, LV_PCT(100));
    }
}

/* ---------------------------------------------------------------- About */

typedef struct {
    const char* section_key;
    const char* section_default;
    const char* label_key;
    const char* label_default;
    const char* symbol;
    pxsys_reference_device_field_t field;
} device_info_row_t;

static const device_info_row_t kDeviceInfoRows[] = {
    {"settings.about.section.system", "System", "settings.about.firmware.name",
     "Firmware", LV_SYMBOL_SETTINGS, PXSYS_REFERENCE_DEVICE_FIRMWARE_NAME},
    {"settings.about.section.system", "System",
     "settings.about.firmware.version", "Firmware version", LV_SYMBOL_UPLOAD,
     PXSYS_REFERENCE_DEVICE_FIRMWARE_VERSION},
    {"settings.about.section.system", "System",
     "settings.about.system.version", "System version", LV_SYMBOL_LIST,
     PXSYS_REFERENCE_DEVICE_SYSTEM_VERSION},
    {"settings.about.section.system", "System", "settings.about.display",
     "Display", LV_SYMBOL_IMAGE, PXSYS_REFERENCE_DEVICE_DISPLAY},
    {"settings.about.section.memory", "Memory & storage",
     "settings.about.memory", "Memory", LV_SYMBOL_DRIVE,
     PXSYS_REFERENCE_DEVICE_MEMORY},
    {"settings.about.section.memory", "Memory & storage",
     "settings.about.storage", "Storage", LV_SYMBOL_SD_CARD,
     PXSYS_REFERENCE_DEVICE_STORAGE},
};

static void system_version_clicked(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    const uint32_t now = lv_tick_get();
    if (!ui_valid(ui) || ui->developer_unlocked) return;
    if (ui->system_version_last_tap == 0 ||
        now - ui->system_version_last_tap > 800)
        ui->system_version_taps = 0;
    ui->system_version_last_tap = now;
    if (++ui->system_version_taps < 7) return;
    ui->developer_unlocked = 1;
    rebuild(ui);
}

static void build_device_info_page(pxsys_reference_lvgl_t* ui,
                                   const pxsys_reference_layout_t* layout) {
    char values[PXSYS_REFERENCE_DEVICE_FIELD_COUNT]
               [PXSYS_REFERENCE_DEVICE_VALUE_MAX];
    const char* section_key = NULL;
    lv_obj_t* section = NULL;
    size_t index;
    page_content_init(ui, layout);
    if (ui->device_info == NULL) return;
    memset(values, 0, sizeof(values));
    ui->device_info(ui->device_info_context, values);
    for (index = 0; index < sizeof(kDeviceInfoRows) / sizeof(kDeviceInfoRows[0]);
         ++index) {
        const device_info_row_t* row = &kDeviceInfoRows[index];
        if (values[row->field][0] == '\0') continue;
        if (section_key == NULL || strcmp(section_key, row->section_key) != 0) {
            section = make_settings_section(
                ui, translated(ui, row->section_key, row->section_default));
            section_key = row->section_key;
        }
        (void)make_settings_row(
            ui, layout, section, row->symbol, PXSYS_COLOR_ACCENT,
            translated(ui, row->label_key, row->label_default),
            values[row->field],
            row->field == PXSYS_REFERENCE_DEVICE_SYSTEM_VERSION
                ? system_version_clicked : NULL,
            row->field == PXSYS_REFERENCE_DEVICE_SYSTEM_VERSION ? ui : NULL);
    }
    if (!ui->developer_unlocked || ui->performance_get == NULL ||
        ui->performance_set == NULL)
        return;
    section = make_settings_section(
        ui, translated(ui, "settings.section.developer", "Developer"));
    make_performance_setting(
        ui, layout, section, PXSYS_REFERENCE_PERFORMANCE_OVERLAY,
        translated(ui, "settings.performance.overlay", "Performance overlay"),
        translated(ui, "settings.performance.overlay.detail",
                   "Show frame and render timing on screen"),
        &ui->performance_controls[0]);
    make_performance_setting(
        ui, layout, section, PXSYS_REFERENCE_PERFORMANCE_LOG,
        translated(ui, "settings.performance.log", "Performance log"),
        translated(ui, "settings.performance.log.detail",
                   "Write frame and render timing to the log"),
        &ui->performance_controls[1]);
    make_performance_setting(
        ui, layout, section, PXSYS_REFERENCE_PXADB,
        translated(ui, "settings.pxadb", "PXADB"),
        translated(ui, "settings.pxadb.detail",
                   "Allow USB debugging and diagnostics"),
        &ui->performance_controls[2]);
}

/* ---------------------------------------------------------- Page dialogs */

static void page_close_dialogs(pxsys_reference_lvgl_t* ui) {
    if (!ui_valid(ui)) return;
    if (ui->confirm_dialog != NULL) {
        lv_obj_delete(ui->confirm_dialog);
        ui->confirm_dialog = NULL;
    }
    if (ui->app_dialog != NULL) {
        lv_obj_delete(ui->app_dialog);
        ui->app_dialog = NULL;
    }
    ui->wifi_password_input = NULL;
}

static lv_obj_t* make_page_dialog(pxsys_reference_lvgl_t* ui,
                                  const pxsys_reference_layout_t* layout,
                                  lv_obj_t** out_panel) {
    lv_obj_t* dialog = lv_obj_create(ui->root);
    lv_obj_t* panel;
    style_plain(dialog);
    lv_obj_set_size(dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(dialog, color_token(ui, PXSYS_COLOR_SCRIM), 0);
    lv_obj_set_style_bg_opa(dialog, LV_OPA_60, 0);
    lv_obj_add_flag(dialog, LV_OBJ_FLAG_CLICKABLE);
    panel = lv_obj_create(dialog);
    style_plain(panel);
    lv_obj_set_width(panel, LV_PCT(88));
    lv_obj_set_height(panel, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(panel, (lv_coord_t)layout->content.height, 0);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, color_token(ui, PXSYS_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, ui->theme.base_radius_px * 3u, 0);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_set_style_pad_row(panel, 8, 0);
    lv_obj_set_layout(panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(panel,
                    LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    *out_panel = panel;
    lv_obj_move_foreground(dialog);
    if (ui->status_bar != NULL) lv_obj_move_foreground(ui->status_bar);
    if (ui->navigation_bar != NULL) lv_obj_move_foreground(ui->navigation_bar);
    return dialog;
}

static void page_dialog_scrim_clicked(lv_event_t* event);
static void show_wifi_networks(pxsys_reference_lvgl_t* ui);

static void wifi_scan_clicked(lv_event_t* event) {
    show_wifi_networks((pxsys_reference_lvgl_t*)lv_event_get_user_data(event));
}

static void wifi_connect_selected(pxsys_reference_lvgl_t* ui,
                                  const char* password) {
    if (!ui_valid(ui) || ui->wifi_connect == NULL ||
        ui->pending_wifi_ssid[0] == '\0')
        return;
    (void)ui->wifi_connect(ui->wifi_context, ui->pending_wifi_ssid,
                           password != NULL ? password : "");
    page_close_dialogs(ui);
    rebuild(ui);
}

static void wifi_keyboard_event(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    const lv_event_code_t code = lv_event_get_code(event);
    if (!ui_valid(ui)) return;
    if (code == LV_EVENT_READY && ui->wifi_password_input != NULL) {
        wifi_connect_selected(ui, lv_textarea_get_text(ui->wifi_password_input));
    } else if (code == LV_EVENT_CANCEL) {
        page_close_dialogs(ui);
        rebuild(ui);
    }
}

static void show_wifi_password(pxsys_reference_lvgl_t* ui) {
    pxsys_reference_layout_t layout;
    lv_obj_t* panel;
    lv_obj_t* keyboard;
    lv_obj_t* label;
    if (!ui_valid(ui) || ui->app_dialog != NULL ||
        pxsys_reference_layout_compute(&ui->display, &layout) !=
            PXSYS_STATUS_OK)
        return;
    expand_content_into_hidden_gesture_area(ui, &layout);
    ui->app_dialog = make_page_dialog(ui, &layout, &panel);
    lv_obj_add_event_cb(ui->app_dialog, page_dialog_scrim_clicked,
                        LV_EVENT_CLICKED, ui);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 8);
    label = make_label(panel,
                       translated(ui, "settings.wifi.password",
                                  "Enter Wi-Fi password"),
                       typography_font(ui, PXSYS_TYPOGRAPHY_TITLE),
                       color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
    lv_obj_set_width(label, LV_PCT(100));
    label = make_label(panel, ui->pending_wifi_ssid,
                       typography_font(ui, PXSYS_TYPOGRAPHY_BODY),
                       color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
    lv_obj_set_width(label, LV_PCT(100));
    ui->wifi_password_input = lv_textarea_create(panel);
    lv_obj_set_width(ui->wifi_password_input, LV_PCT(100));
    lv_textarea_set_one_line(ui->wifi_password_input, true);
    lv_textarea_set_password_mode(ui->wifi_password_input, true);
    lv_textarea_set_max_length(ui->wifi_password_input, 64);
    lv_textarea_set_placeholder_text(ui->wifi_password_input,
                                     translated(ui, "settings.wifi.password",
                                                "Enter Wi-Fi password"));
    lv_obj_set_style_text_font(ui->wifi_password_input,
                              typography_font(ui, PXSYS_TYPOGRAPHY_BODY), 0);
    keyboard = lv_keyboard_create(ui->app_dialog);
    lv_obj_set_size(keyboard, LV_PCT(100), LV_PCT(48));
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(keyboard,
                              typography_font(ui, PXSYS_TYPOGRAPHY_BODY), 0);
    lv_keyboard_set_textarea(keyboard, ui->wifi_password_input);
    lv_obj_add_event_cb(keyboard, wifi_keyboard_event, LV_EVENT_READY, ui);
    lv_obj_add_event_cb(keyboard, wifi_keyboard_event, LV_EVENT_CANCEL, ui);
    lv_obj_move_foreground(keyboard);
}

/* --- system input method for application text inputs ------------------- */

/* The keyboard the Guest draws for itself is not shown here; a Host input
 * method lets the same text input be edited with the system keyboard, and the
 * edits reach the Guest as text events. */

static int object_is_within(const lv_obj_t* object, const lv_obj_t* ancestor) {
    while (object != NULL) {
        if (object == ancestor) return 1;
        object = lv_obj_get_parent(object);
    }
    return 0;
}

static lv_obj_t* find_any_textarea(lv_obj_t* object) {
    uint32_t count;
    uint32_t index;
    if (object == NULL) return NULL;
    if (lv_obj_check_type(object, &lv_textarea_class) &&
        !lv_obj_has_state(object, LV_STATE_DISABLED))
        return object;
    count = lv_obj_get_child_count(object);
    for (index = 0; index < count; ++index) {
        lv_obj_t* found = find_any_textarea(lv_obj_get_child(object, index));
        if (found != NULL) return found;
    }
    return NULL;
}

static lv_obj_t* find_focused_textarea(lv_obj_t* object) {
    uint32_t count;
    uint32_t index;
    if (object == NULL) return NULL;
    if (lv_obj_check_type(object, &lv_textarea_class) &&
        lv_obj_has_state(object, LV_STATE_FOCUSED))
        return object;
    count = lv_obj_get_child_count(object);
    for (index = 0; index < count; ++index) {
        lv_obj_t* found = find_focused_textarea(lv_obj_get_child(object, index));
        if (found != NULL) return found;
    }
    return NULL;
}

static void app_input_method_target_deleted(lv_event_t* event);

static void app_input_method_close(pxsys_reference_lvgl_t* ui) {
    if (ui->app_ime_keypad != NULL) {
        pxsys_reference_ime_destroy(ui->app_ime_keypad);
        ui->app_ime_keypad = NULL;
        ui->app_ime = NULL;
        ui->app_ime_keyboard = NULL;
        ui->app_ime_target = NULL;
        return;
    }
    ui->app_ime_pinyin = NULL;
    if (ui->app_ime_keyboard != NULL && ui->app_ime_target != NULL) {
        /* The delete notification clears the target, so it is alive here. */
        lv_obj_t* target = ui->app_ime_target;
        ui->app_ime_target = NULL;
        lv_obj_remove_event_cb_with_user_data(
            target, app_input_method_target_deleted, ui);
        if (lv_obj_has_state(target, LV_STATE_FOCUSED))
            lv_obj_remove_state(target, LV_STATE_FOCUSED);
    }
    if (ui->app_ime != NULL) {
        lv_obj_delete(ui->app_ime);
        ui->app_ime = NULL;
        ui->app_ime_keyboard = NULL;
    }
    ui->app_ime_target = NULL;
}

static void app_input_method_event(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    if (ui_valid(ui)) app_input_method_close(ui);
}

static void app_input_method_closed(void* context) {
    pxsys_reference_lvgl_t* ui = (pxsys_reference_lvgl_t*)context;
    if (ui_valid(ui)) ui->app_ime_close_requested = 1;
}

/* The Guest deletes its input when it replaces the surface; the delete
 * notification keeps the pointer from going stale. */
static void app_input_method_target_deleted(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    if (!ui_valid(ui)) return;
    ui->app_ime_target = NULL;
    ui->app_ime_close_requested = 1;
}

static void app_input_method_open(pxsys_reference_lvgl_t* ui,
                                  lv_obj_t* target) {
    lv_obj_t* panel;
    if (!ui_valid(ui) || ui->parent == NULL || target == NULL) return;
    app_input_method_close(ui);
#if LV_USE_IME_PINYIN && LV_IME_PINYIN_USE_K9_MODE
    /* Narrow panels get the nine key pad: bigger targets, pinyin candidates
     * and symbol pages. */
    if (ui->display.width < 480u) {
        ui->app_ime_keypad = pxsys_reference_ime_create(ui->parent, target);
        if (ui->app_ime_keypad == NULL) return;
        pxsys_reference_ime_set_font(
            ui->app_ime_keypad, typography_font(ui, PXSYS_TYPOGRAPHY_BODY));
        pxsys_reference_ime_set_icon_font(
            ui->app_ime_keypad, typography_font(ui, PXSYS_TYPOGRAPHY_TITLE));
        pxsys_reference_ime_set_close_callback(ui->app_ime_keypad,
                                              app_input_method_closed, ui);
        lv_obj_add_event_cb(target, app_input_method_target_deleted,
                            LV_EVENT_DELETE, ui);
        ui->app_ime = pxsys_reference_ime_get_root(ui->app_ime_keypad);
        ui->app_ime_keyboard = NULL;
        ui->app_ime_target = target;
        return;
    }
#endif
    panel = lv_obj_create(ui->parent);
    if (panel == NULL) return;
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(panel, 0, 0);
    ui->app_ime = panel;
    ui->app_ime_keyboard = lv_keyboard_create(panel);
    if (ui->app_ime_keyboard == NULL) {
        lv_obj_delete(panel);
        ui->app_ime = NULL;
        return;
    }
    lv_obj_set_size(ui->app_ime_keyboard, LV_PCT(100), LV_PCT(46));
    lv_obj_align(ui->app_ime_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(ui->app_ime_keyboard,
                              typography_font(ui, PXSYS_TYPOGRAPHY_BODY), 0);
    lv_obj_add_event_cb(target, app_input_method_target_deleted,
                        LV_EVENT_DELETE, ui);
    lv_keyboard_set_textarea(ui->app_ime_keyboard, target);
#if LV_USE_IME_PINYIN
    ui->app_ime_pinyin = lv_ime_pinyin_create(panel);
    if (ui->app_ime_pinyin != NULL) {
        lv_ime_pinyin_set_keyboard(ui->app_ime_pinyin, ui->app_ime_keyboard);
        lv_ime_pinyin_set_mode(ui->app_ime_pinyin, LV_IME_PINYIN_MODE_K26);
        lv_obj_set_style_text_font(
            lv_ime_pinyin_get_cand_panel(ui->app_ime_pinyin),
            typography_font(ui, PXSYS_TYPOGRAPHY_BODY), 0);
    }
#endif
    lv_obj_add_event_cb(ui->app_ime_keyboard, app_input_method_event,
                        LV_EVENT_READY, ui);
    lv_obj_add_event_cb(ui->app_ime_keyboard, app_input_method_event,
                        LV_EVENT_CANCEL, ui);
    lv_obj_move_foreground(panel);
    ui->app_ime_target = target;
}

/* A text input keeps the keyboard until the input is submitted, cancelled or
 * the Guest deletes it. */
static void app_input_method_poll(lv_timer_t* timer) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_timer_get_user_data(timer);
    lv_obj_t* target;
    if (!ui_valid(ui) || ui->parent == NULL) return;
    if (ui->app_ime_close_requested) {
        ui->app_ime_close_requested = 0;
        app_input_method_close(ui);
        return;
    }
    /* The Wi-Fi page owns its dialog and keyboard. */
    if (ui->app_dialog != NULL || ui->confirm_dialog != NULL) {
        if (ui->app_ime != NULL) app_input_method_close(ui);
        return;
    }
    target = find_focused_textarea(lv_screen_active());
#if PXSYS_REFERENCE_UI_DEBUG_INPUT_METHOD
    if (target == NULL) target = find_any_textarea(lv_screen_active());
#endif
    if (target == NULL || target == ui->app_ime_target) return;
    if (ui->app_ime != NULL && object_is_within(target, ui->app_ime)) return;
    if (ui->app_ime_target != NULL &&
        object_is_within(target, ui->app_ime_target)) return;
    app_input_method_open(ui, target);
}

static void wifi_network_clicked(lv_event_t* event) {
    wifi_network_control_t* control =
        (wifi_network_control_t*)lv_event_get_user_data(event);
    pxsys_reference_lvgl_t* ui;
    const pxsys_reference_wifi_network_t* network;
    if (control == NULL || !ui_valid(control->ui) ||
        control->index >= control->ui->wifi_network_count)
        return;
    ui = control->ui;
    network = &ui->wifi_networks[control->index];
    snprintf(ui->pending_wifi_ssid, sizeof(ui->pending_wifi_ssid), "%s",
             network->ssid);
    page_close_dialogs(ui);
    if (network->secured)
        show_wifi_password(ui);
    else
        wifi_connect_selected(ui, "");
}

static void show_wifi_networks(pxsys_reference_lvgl_t* ui) {
    pxsys_reference_layout_t layout;
    lv_obj_t* panel;
    lv_obj_t* label;
    lv_obj_t* button;
    size_t count = 0;
    if (!ui_valid(ui) || ui->wifi_scan == NULL || ui->wifi_connect == NULL ||
        pxsys_reference_layout_compute(&ui->display, &layout) !=
            PXSYS_STATUS_OK)
        return;
    page_close_dialogs(ui);
    count = ui->wifi_scan(ui->wifi_context, ui->wifi_networks,
                          PXSYS_REFERENCE_WIFI_NETWORK_MAX);
    if (count > PXSYS_REFERENCE_WIFI_NETWORK_MAX)
        count = PXSYS_REFERENCE_WIFI_NETWORK_MAX;
    ui->wifi_network_count = count;
    expand_content_into_hidden_gesture_area(ui, &layout);
    ui->app_dialog = make_page_dialog(ui, &layout, &panel);
    lv_obj_add_event_cb(ui->app_dialog, page_dialog_scrim_clicked,
                        LV_EVENT_CLICKED, ui);
    label = make_label(panel,
                       translated(ui, "settings.wifi.networks",
                                  "Available networks"),
                       typography_font(ui, PXSYS_TYPOGRAPHY_TITLE),
                       color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
    lv_obj_set_width(label, LV_PCT(100));
    button = make_page_button(
        ui, panel, translated(ui, "settings.wifi.scan", "Scan again"),
        PXSYS_COLOR_ACCENT, PXSYS_COLOR_ON_ACCENT, wifi_scan_clicked, ui);
    lv_obj_set_width(button, LV_PCT(100));
    if (count == 0) {
        label = make_label(panel,
                           translated(ui, "settings.wifi.none",
                                      "No networks found"),
                           typography_font(ui, PXSYS_TYPOGRAPHY_BODY),
                           color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
        lv_obj_set_width(label, LV_PCT(100));
    }
    for (size_t index = 0; index < count; ++index) {
        char detail[48];
        const pxsys_reference_wifi_network_t* network =
            &ui->wifi_networks[index];
        snprintf(detail, sizeof(detail), "%d dBm  %s", network->rssi,
                 translated(ui, network->secured ? "settings.wifi.secured"
                                                 : "settings.wifi.open",
                            network->secured ? "Secured" : "Open network"));
        ui->wifi_network_controls[index].ui = ui;
        ui->wifi_network_controls[index].index = index;
        (void)make_settings_row(ui, &layout, panel, LV_SYMBOL_WIFI,
                                PXSYS_COLOR_ACCENT, network->ssid, detail,
                                wifi_network_clicked,
                                &ui->wifi_network_controls[index]);
    }
}

static void wifi_settings_clicked(lv_event_t* event) {
    show_wifi_networks((pxsys_reference_lvgl_t*)lv_event_get_user_data(event));
}

static void page_dialog_scrim_clicked(lv_event_t* event) {
    page_close_dialogs(
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event));
    rebuild((pxsys_reference_lvgl_t*)lv_event_get_user_data(event));
}

static void page_confirm_clicked(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    lv_obj_t* button = lv_event_get_current_target(event);
    if (!ui_valid(ui) || button == NULL) return;
    if ((uintptr_t)lv_obj_get_user_data(button) != 0) {
        if (ui->pending_kind == 0 && ui->app_action != NULL) {
            (void)ui->app_action(
                ui->app_manager_context, ui->pending_identity,
                (pxsys_reference_app_action_t)ui->pending_action);
            ui->managed_apps_loaded = 0;
        } else if (ui->pending_kind == 1 && ui->file_action != NULL) {
            (void)ui->file_action(
                ui->file_manager_context, ui->pending_path,
                (pxsys_reference_file_action_t)ui->pending_action);
            ui->file_entries_loaded = 0;
        }
    }
    page_close_dialogs(ui);
    rebuild(ui);
}

static void show_page_confirm(pxsys_reference_lvgl_t* ui,
                              const char* message) {
    pxsys_reference_layout_t layout;
    lv_obj_t* panel;
    lv_obj_t* label;
    lv_obj_t* row;
    lv_obj_t* confirm;
    lv_obj_t* cancel;
    if (!ui_valid(ui) || ui->confirm_dialog != NULL ||
        pxsys_reference_layout_compute(&ui->display, &layout) !=
            PXSYS_STATUS_OK)
        return;
    expand_content_into_hidden_gesture_area(ui, &layout);
    ui->confirm_dialog = make_page_dialog(ui, &layout, &panel);
    lv_obj_add_event_cb(ui->confirm_dialog, page_dialog_scrim_clicked,
                        LV_EVENT_CLICKED, ui);
    label = make_label(panel, message,
                       typography_font(ui, PXSYS_TYPOGRAPHY_BODY),
                       color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
    lv_obj_set_width(label, LV_PCT(100));
    row = lv_obj_create(panel);
    style_plain(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 42);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    confirm = make_page_button(
        ui, row, translated(ui, "settings.confirm", "Confirm"),
        PXSYS_COLOR_ERROR, PXSYS_COLOR_ON_ACCENT, page_confirm_clicked, ui);
    lv_obj_set_width(confirm, 0);
    lv_obj_set_flex_grow(confirm, 1);
    lv_obj_set_user_data(confirm, (void*)(uintptr_t)1);
    cancel = make_page_button(
        ui, row, translated(ui, "settings.cancel", "Cancel"),
        PXSYS_COLOR_BORDER, PXSYS_COLOR_TEXT_PRIMARY, page_confirm_clicked, ui);
    lv_obj_set_width(cancel, 0);
    lv_obj_set_flex_grow(cancel, 1);
    lv_obj_set_user_data(cancel, (void*)0);
}

/* ----------------------------------------------------------- App manager */

static void app_row_clicked(lv_event_t* event);

static void app_action_clicked(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    lv_obj_t* button = lv_event_get_current_target(event);
    uintptr_t action = button == NULL
                           ? 0
                           : (uintptr_t)lv_obj_get_user_data(button);
    if (!ui_valid(ui) || ui->app_action == NULL) return;
    if (action == PXSYS_REFERENCE_APP_ACTION_CLEAR_DATA) {
        ui->pending_kind = 0;
        ui->pending_action = PXSYS_REFERENCE_APP_ACTION_CLEAR_DATA;
        show_page_confirm(
            ui, translated(ui, "settings.apps.confirm.clear",
                           "Clear all app data?"));
        return;
    }
    if (action == PXSYS_REFERENCE_APP_ACTION_UNINSTALL) {
        ui->pending_kind = 0;
        ui->pending_action = PXSYS_REFERENCE_APP_ACTION_UNINSTALL;
        show_page_confirm(
            ui, translated(ui, "settings.apps.confirm.uninstall",
                           "Uninstall this app?"));
        return;
    }
    (void)ui->app_action(ui->app_manager_context, ui->pending_identity,
                         (pxsys_reference_app_action_t)action);
    ui->managed_apps_loaded = 0;
    page_close_dialogs(ui);
    rebuild(ui);
}

static void app_permission_changed(lv_event_t* event) {
    app_permission_control_t* control =
        (app_permission_control_t*)lv_event_get_user_data(event);
    lv_obj_t* toggle = lv_event_get_current_target(event);
    bool granted;
    if (control == NULL || !ui_valid(control->ui) || toggle == NULL) return;
    granted = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    if (control->ui->app_permission_set == NULL ||
        !control->ui->app_permission_set(control->ui->app_permission_context,
                                         control->ui->pending_identity,
                                         control->index, granted)) {
        if (granted) lv_obj_remove_state(toggle, LV_STATE_CHECKED);
        else lv_obj_add_state(toggle, LV_STATE_CHECKED);
        return;
    }
    if (control->index < control->ui->app_permission_count)
        control->ui->app_permissions[control->index].granted = granted ? 1 : 0;
}

/* Permission rows reuse the settings row idiom, so the dialog keeps the
 * reference spacing, typography and switch styling. */
static void add_app_permissions(pxsys_reference_lvgl_t* ui,
                                const pxsys_reference_layout_t* layout,
                                lv_obj_t* panel,
                                const pxsys_reference_managed_app_t* app) {
    lv_obj_t* label;
    size_t count;
    size_t index;
    if (ui->app_permission_list == NULL) return;
    count = ui->app_permission_list(ui->app_permission_context, app->identity,
                                    NULL, 0);
    if (count > PXSYS_REFERENCE_APP_PERMISSION_MAX)
        count = PXSYS_REFERENCE_APP_PERMISSION_MAX;
    ui->app_permission_count = 0;
    if (count != 0) {
        count = ui->app_permission_list(ui->app_permission_context,
                                        app->identity, ui->app_permissions,
                                        count);
        if (count > PXSYS_REFERENCE_APP_PERMISSION_MAX)
            count = PXSYS_REFERENCE_APP_PERMISSION_MAX;
        ui->app_permission_count = count;
    }
    label = make_label(panel,
                       translated(ui, "settings.apps.permissions",
                                  "Permissions"),
                       typography_font(ui, PXSYS_TYPOGRAPHY_CAPTION),
                       color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_pad_left(label, 8, 0);
    if (count == 0) {
        label = make_label(panel,
                           translated(ui, "settings.apps.permissions.empty",
                                      "No declared permissions"),
                           typography_font(ui, PXSYS_TYPOGRAPHY_CAPTION),
                           color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_pad_left(label, 8, 0);
        return;
    }
    for (index = 0; index < count; ++index) {
        pxsys_reference_app_permission_t* permission =
            &ui->app_permissions[index];
        char subtitle[PXSYS_REFERENCE_APP_PERMISSION_TEXT_MAX * 2u + 8u];
        lv_obj_t* row;
        lv_obj_t* toggle;
        snprintf(subtitle, sizeof(subtitle), "%s  %s", permission->scope,
                 translated(ui,
                            permission->required
                                ? "settings.apps.permissions.required"
                                : "settings.apps.permissions.optional",
                            permission->required ? "Required" : "Optional"));
        row = make_settings_row(ui, layout, panel, LV_SYMBOL_EYE_OPEN,
                                PXSYS_COLOR_ACCENT, permission->name, subtitle,
                                NULL, NULL);
        toggle = lv_switch_create(row);
        lv_obj_set_size(toggle, 42, 24);
        lv_obj_set_style_bg_color(toggle, color_token(ui, PXSYS_COLOR_BORDER),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_color(toggle, color_token(ui, PXSYS_COLOR_ACCENT),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(toggle,
                                  color_token(ui, PXSYS_COLOR_ON_ACCENT),
                                  LV_PART_KNOB);
        if (permission->granted) lv_obj_add_state(toggle, LV_STATE_CHECKED);
        ui->app_permission_controls[index].ui = ui;
        ui->app_permission_controls[index].index = index;
        lv_obj_add_event_cb(toggle, app_permission_changed,
                            LV_EVENT_VALUE_CHANGED,
                            &ui->app_permission_controls[index]);
    }
}

static void show_app_detail(pxsys_reference_lvgl_t* ui,
                            const pxsys_reference_managed_app_t* app) {
    pxsys_reference_layout_t layout;
    lv_obj_t* panel;
    lv_obj_t* info;
    lv_obj_t* label;
    lv_obj_t* button;
    char buffer[160];
    if (!ui_valid(ui) || ui->app_dialog != NULL || app == NULL ||
        pxsys_reference_layout_compute(&ui->display, &layout) !=
            PXSYS_STATUS_OK)
        return;
    expand_content_into_hidden_gesture_area(ui, &layout);
    snprintf(ui->pending_identity, sizeof(ui->pending_identity), "%s",
             app->identity);
    ui->app_dialog = make_page_dialog(ui, &layout, &panel);
    lv_obj_add_event_cb(ui->app_dialog, page_dialog_scrim_clicked,
                        LV_EVENT_CLICKED, ui);

    label = make_label(panel, app->name,
                       typography_font(ui, PXSYS_TYPOGRAPHY_TITLE),
                       color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
    lv_obj_set_width(label, LV_PCT(100));

    info = lv_obj_create(panel);
    style_plain(info);
    lv_obj_set_width(info, LV_PCT(100));
    lv_obj_set_height(info, LV_SIZE_CONTENT);
    lv_obj_set_layout(info, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(info, 4, 0);
    snprintf(buffer, sizeof(buffer), "%s: %s",
             translated(ui, "settings.apps.version", "Version"),
             app->version);
    label = make_label(info, buffer,
                       typography_font(ui, PXSYS_TYPOGRAPHY_CAPTION),
                       color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
    lv_obj_set_width(label, LV_PCT(100));
    snprintf(buffer, sizeof(buffer), "%s: %s",
             translated(ui, "settings.apps.identifier", "Identifier"),
             app->identity);
    label = make_label(info, buffer,
                       typography_font(ui, PXSYS_TYPOGRAPHY_CAPTION),
                       color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
    lv_obj_set_width(label, LV_PCT(100));
    snprintf(buffer, sizeof(buffer), "%s%s",
             translated(ui,
                        app->built_in ? "settings.apps.system"
                                      : "settings.apps.user",
                        app->built_in ? "System app" : "User app"),
             app->enabled ? ""
                          : translated(ui, "settings.apps.disabled",
                                       "Disabled"));
    label = make_label(info, buffer,
                       typography_font(ui, PXSYS_TYPOGRAPHY_CAPTION),
                       color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
    lv_obj_set_width(label, LV_PCT(100));

    add_app_permissions(ui, &layout, panel, app);

    button = make_page_button(
        ui, panel,
        translated(ui,
                   app->enabled ? "settings.apps.disable"
                                : "settings.apps.enable",
                   app->enabled ? "Disable" : "Enable"),
        PXSYS_COLOR_ACCENT, PXSYS_COLOR_ON_ACCENT, app_action_clicked, ui);
    lv_obj_set_user_data(
        button, (void*)(uintptr_t)(app->enabled
                                       ? PXSYS_REFERENCE_APP_ACTION_DISABLE
                                       : PXSYS_REFERENCE_APP_ACTION_ENABLE));
    if (app->has_private_data) {
        button = make_page_button(
            ui, panel, translated(ui, "settings.apps.clear", "Clear data"),
            PXSYS_COLOR_WARNING, PXSYS_COLOR_ON_ACCENT, app_action_clicked, ui);
        lv_obj_set_user_data(
            button, (void*)(uintptr_t)PXSYS_REFERENCE_APP_ACTION_CLEAR_DATA);
    }
    if (app->installed && !app->built_in) {
        button = make_page_button(
            ui, panel, translated(ui, "settings.apps.uninstall", "Uninstall"),
            PXSYS_COLOR_ERROR, PXSYS_COLOR_ON_ACCENT, app_action_clicked, ui);
        lv_obj_set_user_data(
            button, (void*)(uintptr_t)PXSYS_REFERENCE_APP_ACTION_UNINSTALL);
    }
    button = make_page_button(ui, panel,
                              translated(ui, "settings.apps.close", "Close"),
                              PXSYS_COLOR_BORDER,
                              PXSYS_COLOR_TEXT_PRIMARY,
                              page_dialog_scrim_clicked, ui);
    (void)button;
}

static void app_row_clicked(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    lv_obj_t* row = lv_event_get_current_target(event);
    size_t index =
        row == NULL ? 0 : (size_t)(uintptr_t)lv_obj_get_user_data(row);
    if (!ui_valid(ui) || index >= ui->managed_app_count) return;
    show_app_detail(ui, &ui->managed_apps[index]);
}

static void build_app_manager(pxsys_reference_lvgl_t* ui,
                              const pxsys_reference_layout_t* layout) {
    lv_obj_t* section;
    size_t index;
    page_content_init(ui, layout);
    if (!ui->managed_apps_loaded) {
        ui->managed_apps_loaded = 1;
        ui->managed_app_count = 0;
        if (ui->app_list != NULL) {
            size_t count = ui->app_list(ui->app_manager_context, NULL, 0);
            if (count > PXSYS_REFERENCE_MANAGED_APP_MAX)
                count = PXSYS_REFERENCE_MANAGED_APP_MAX;
            if (count != 0) {
                ui->managed_app_count = ui->app_list(
                    ui->app_manager_context, ui->managed_apps, count);
                if (ui->managed_app_count > count)
                    ui->managed_app_count = count;
            }
        }
    }
    if (ui->managed_app_count == 0) {
        lv_obj_t* label = make_label(
            ui->content, translated(ui, "settings.apps.empty",
                                    "No installed apps"),
            typography_font(ui, PXSYS_TYPOGRAPHY_BODY),
            color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
        lv_obj_set_width(label, LV_PCT(100));
        return;
    }
    section = make_settings_section(
        ui, translated(ui, "settings.section.system", "System"));
    for (index = 0; index < ui->managed_app_count; ++index) {
        pxsys_reference_managed_app_t* app = &ui->managed_apps[index];
        char subtitle[128];
        lv_obj_t* row;
        snprintf(subtitle, sizeof(subtitle), "%s%s%s", app->version,
                 app->built_in
                     ? translated(ui, "settings.apps.system", "System app")
                     : translated(ui, "settings.apps.user", "User app"),
                 app->enabled
                     ? ""
                     : translated(ui, "settings.apps.disabled", "Disabled"));
        row = make_settings_row(ui, layout, section, LV_SYMBOL_LIST,
                                PXSYS_COLOR_ACCENT, app->name, subtitle,
                                app_row_clicked, ui);
        lv_obj_set_user_data(row, (void*)(uintptr_t)index);
        add_settings_chevron(ui, row);
    }
}

/* ---------------------------------------------------------- File manager */

static int file_entry_compare(const void* left, const void* right) {
    const pxsys_reference_file_entry_t* a =
        (const pxsys_reference_file_entry_t*)left;
    const pxsys_reference_file_entry_t* b =
        (const pxsys_reference_file_entry_t*)right;
    if (a->is_directory != b->is_directory)
        return a->is_directory ? -1 : 1;
    return strcmp(a->name, b->name);
}

static void format_file_size(uint64_t size, char* buffer, size_t capacity) {
    if (size >= UINT64_C(1024) * 1024) {
        snprintf(buffer, capacity, "%.1f MB",
                 (double)size / (1024.0 * 1024.0));
    } else if (size >= UINT64_C(1024)) {
        snprintf(buffer, capacity, "%.1f KB", (double)size / 1024.0);
    } else {
        snprintf(buffer, capacity, "%u B", (unsigned)size);
    }
}

static void file_manager_reload(pxsys_reference_lvgl_t* ui) {
    ui->file_entry_count = 0;
    if (ui->file_list == NULL) return;
    ui->file_entry_count = ui->file_list(
        ui->file_manager_context, ui->file_path, ui->file_entries,
        PXSYS_REFERENCE_FILE_ENTRY_MAX);
    if (ui->file_entry_count > PXSYS_REFERENCE_FILE_ENTRY_MAX)
        ui->file_entry_count = PXSYS_REFERENCE_FILE_ENTRY_MAX;
    if (ui->file_entry_count > 1) {
        qsort(ui->file_entries, ui->file_entry_count,
              sizeof(ui->file_entries[0]), file_entry_compare);
    }
}

static void file_entry_clicked(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    lv_obj_t* row = lv_event_get_current_target(event);
    size_t index =
        row == NULL ? 0 : (size_t)(uintptr_t)lv_obj_get_user_data(row);
    pxsys_reference_file_entry_t* entry;
    if (!ui_valid(ui) || index >= ui->file_entry_count) return;
    entry = &ui->file_entries[index];
    if (entry->is_directory) {
        snprintf(ui->file_path, sizeof(ui->file_path), "%s", entry->path);
        ui->file_entries_loaded = 0;
        rebuild(ui);
        return;
    }
    snprintf(ui->pending_path, sizeof(ui->pending_path), "%s", entry->path);
    ui->pending_kind = 1;
    ui->pending_action = PXSYS_REFERENCE_FILE_ACTION_DELETE;
    show_page_confirm(
        ui, translated(ui, "settings.files.confirm.delete",
                       "Delete this item?"));
}

static void file_manager_up_clicked(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    char* slash;
    if (!ui_valid(ui)) return;
    slash = strrchr(ui->file_path, '/');
    if (slash != NULL) *slash = '\0';
    ui->file_entries_loaded = 0;
    rebuild(ui);
}

static void build_file_manager(pxsys_reference_lvgl_t* ui,
                               const pxsys_reference_layout_t* layout) {
    lv_obj_t* section;
    lv_obj_t* label;
    size_t index;
    page_content_init(ui, layout);
    if (!ui->file_entries_loaded) {
        ui->file_entries_loaded = 1;
        file_manager_reload(ui);
    }
    if (ui->file_path[0] != '\0') {
        lv_obj_t* button = make_page_button(
            ui, ui->content,
            translated(ui, "settings.files.up", "Up one level"),
            PXSYS_COLOR_SURFACE, PXSYS_COLOR_TEXT_PRIMARY,
            file_manager_up_clicked, ui);
        lv_obj_set_style_border_width(button, 1, 0);
        lv_obj_set_style_border_color(button, color_token(ui, PXSYS_COLOR_BORDER),
                                      0);
        (void)button;
    }
    label = make_label(ui->content, ui->file_path[0] == '\0' ? "/" : ui->file_path,
                       typography_font(ui, PXSYS_TYPOGRAPHY_CAPTION),
                       color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
    lv_obj_set_width(label, LV_PCT(100));
    if (ui->file_entry_count == 0) {
        label = make_label(ui->content,
                           translated(ui, "settings.files.empty",
                                      "Empty folder"),
                           typography_font(ui, PXSYS_TYPOGRAPHY_BODY),
                           color_token(ui, PXSYS_COLOR_TEXT_SECONDARY));
        lv_obj_set_width(label, LV_PCT(100));
        return;
    }
    section = make_settings_section(
        ui, translated(ui, "settings.files", "Files & storage"));
    for (index = 0; index < ui->file_entry_count; ++index) {
        pxsys_reference_file_entry_t* entry = &ui->file_entries[index];
        char subtitle[64];
        lv_obj_t* row;
        if (entry->is_directory) {
            snprintf(subtitle, sizeof(subtitle), "%s",
                     translated(ui, "settings.files.folder", "Folder"));
        } else {
            format_file_size(entry->size, subtitle, sizeof(subtitle));
        }
        row = make_settings_row(
            ui, layout, section,
            entry->is_directory ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE,
            entry->is_directory ? PXSYS_COLOR_WARNING : PXSYS_COLOR_ACCENT,
            entry->name, subtitle, file_entry_clicked, ui);
        lv_obj_set_user_data(row, (void*)(uintptr_t)index);
        if (entry->is_directory) add_settings_chevron(ui, row);
    }
}

#endif /* PXSYS_REFERENCE_UI_BUILTIN_SETTINGS */

static void rebuild(pxsys_reference_lvgl_t* ui) {
    pxsys_reference_layout_t layout;
    lv_obj_t* navigation;
    lv_obj_t* action;
    lv_obj_t* back_gesture = NULL;
    if (!ui_valid(ui) || ui->root == NULL ||
        pxsys_reference_layout_compute(&ui->display, &layout) != PXSYS_STATUS_OK)
        return;
    /* The touch target overlays content. Only a visible gesture handle needs
     * an exclusive strip; otherwise return the reservation to the page. */
    expand_content_into_hidden_gesture_area(ui, &layout);
    if (ui->content_insets_changed != NULL) {
        uint16_t top = 0;
        uint16_t bottom = 0;
        if ((ui->active_chrome & PXSYS_REFERENCE_UI_STATUS_BAR) &&
            ui->window.status_bar_mode != PXSYS_WINDOW_BAR_HIDDEN &&
            bar_is_visible(ui, ui->window.status_bar_mode))
            top = (uint16_t)layout.status_bar.height;
        if ((ui->active_chrome & PXSYS_REFERENCE_UI_NAVIGATION_BAR) &&
            ui->navigation_mode == PXSYS_NAVIGATION_BUTTONS &&
            ui->window.navigation_bar_mode != PXSYS_WINDOW_BAR_HIDDEN &&
            bar_is_visible(ui, ui->window.navigation_bar_mode))
            bottom = (uint16_t)layout.navigation_bar.height;
        if (top != ui->content_inset_top ||
            bottom != ui->content_inset_bottom) {
            ui->content_inset_top = top;
            ui->content_inset_bottom = bottom;
            ui->content_insets_changed(ui->content_insets_context, top, bottom);
        }
    }
    application_backdrop_apply(ui);
    if (ui->content_active && ui->active_page == REFERENCE_PAGE_SETTINGS &&
        ui->content != NULL)
        ui->settings_scroll_y = lv_obj_get_scroll_y(ui->content);
    /* The shade shares the chrome root so the real navigation bar can remain
     * above it. lv_obj_clean deletes it; clear cached child pointers first. */
    if (ui->notification_shade != NULL &&
        lv_obj_get_parent(ui->notification_shade) == ui->root) {
        if (ui->notification_shade_open && ui->notification_content != NULL)
            ui->notification_scroll_y =
                lv_obj_get_scroll_y(ui->notification_content);
        ui->notification_shade = NULL;
        ui->notification_panel = NULL;
        ui->notification_content = NULL;
    }
    /* Dialogs are children of root and are deleted by lv_obj_clean(). */
    ui->language_dialog = NULL;
    ui->app_dialog = NULL;
    ui->confirm_dialog = NULL;
    lv_obj_clean(ui->root);
    ui->status_bar = NULL;
    ui->navigation_bar = NULL;
    ui->navigation_handle = NULL;
    ui->navigation_back_indicator = NULL;
    lv_obj_set_pos(ui->root, 0, 0);
    lv_obj_set_size(ui->root, (lv_coord_t)ui->display.width,
                    (lv_coord_t)ui->display.height);
    lv_obj_set_style_bg_color(ui->root,
                              color_token(ui, PXSYS_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(ui->root,
                            ui->content_active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
#if PXSYS_REFERENCE_UI_ENABLE_WALLPAPER
    if (ui->content_active && ui->active_page == REFERENCE_PAGE_HOME &&
        (ui->features & PXSYS_REFERENCE_UI_WALLPAPER))
        build_home_wallpaper(ui);
#endif

    if ((ui->active_chrome & PXSYS_REFERENCE_UI_STATUS_BAR) &&
        ui->window.status_bar_mode != PXSYS_WINDOW_BAR_HIDDEN) {
        if (bar_is_visible(ui, ui->window.status_bar_mode)) {
            build_status_bar(ui, &layout);
        } else {
            lv_obj_t* catcher = lv_obj_create(ui->root);
            int32_t height = layout.safe_area.y + 10;
            if (height < 14) height = 14;
            style_plain(catcher);
            ui->status_bar = catcher;
            lv_obj_set_pos(catcher, 0, 0);
            lv_obj_set_size(catcher, (lv_coord_t)ui->display.width, height);
            lv_obj_set_style_bg_opa(catcher, LV_OPA_TRANSP, 0);
            lv_obj_add_flag(catcher,
                            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
            lv_obj_add_event_cb(catcher, status_bar_event, LV_EVENT_PRESSED, ui);
            lv_obj_add_event_cb(catcher, status_bar_event, LV_EVENT_PRESSING, ui);
            lv_obj_add_event_cb(catcher, status_bar_event, LV_EVENT_RELEASED, ui);
            lv_obj_add_event_cb(catcher, status_bar_event, LV_EVENT_PRESS_LOST, ui);
        }
    }

    ui->content = NULL;
    ui->title = NULL;
    if (ui->content_active) {
        ui->content = lv_obj_create(ui->root);
        style_plain(ui->content);
        lv_obj_set_pos(ui->content, layout.content.x, layout.content.y);
        lv_obj_set_size(ui->content, (lv_coord_t)layout.content.width,
                        (lv_coord_t)layout.content.height);
        lv_obj_set_style_bg_opa(ui->content, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_column(ui->content, layout.item_gap, 0);
        lv_obj_set_style_pad_row(ui->content, layout.item_gap, 0);
        if (ui->active_page == REFERENCE_PAGE_HOME) {
            /* The launcher grid already fills the whole content area; a title
             * row would only waste vertical space on small displays. */
            ui->title = NULL;
        } else {
            const char* title_key = "settings.title";
            const char* title_default = "Settings";
            const lv_font_t* title_font =
                typography_font(ui, PXSYS_TYPOGRAPHY_HEADLINE);
            int32_t title_height =
                title_font != NULL ? (int32_t)title_font->line_height + 6 : 34;
            int32_t minimum_title_height =
                layout.size_class == PXSYS_UI_SIZE_COMPACT ? 34 : 38;
            if (ui->active_page == REFERENCE_PAGE_SOUND_SETTINGS) {
                title_key = "settings.sound";
                title_default = "Sound & display";
            } else if (ui->active_page == REFERENCE_PAGE_DEVICE_INFO) {
                title_key = "settings.about";
                title_default = "About device";
            } else if (ui->active_page == REFERENCE_PAGE_APP_MANAGER) {
                title_key = "settings.apps";
                title_default = "Apps";
            } else if (ui->active_page == REFERENCE_PAGE_FILE_MANAGER) {
                title_key = "settings.files";
                title_default = "Files & storage";
            }
            if (title_height < minimum_title_height)
                title_height = minimum_title_height;
            ui->title = make_label(ui->content,
                                   translated(ui, title_key, title_default),
                                   title_font,
                                   color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
            lv_obj_set_width(ui->title, LV_PCT(100));
            lv_obj_set_height(ui->title, title_height);
        }
        if (ui->active_page == REFERENCE_PAGE_HOME)
            build_home(ui, &layout);
        else if (ui->active_page == REFERENCE_PAGE_SETTINGS)
            build_settings(ui, &layout);
#if PXSYS_REFERENCE_UI_BUILTIN_SETTINGS
        else if (ui->active_page == REFERENCE_PAGE_SOUND_SETTINGS)
            build_sound_settings(ui, &layout);
        else if (ui->active_page == REFERENCE_PAGE_DEVICE_INFO)
            build_device_info_page(ui, &layout);
        else if (ui->active_page == REFERENCE_PAGE_APP_MANAGER)
            build_app_manager(ui, &layout);
        else if (ui->active_page == REFERENCE_PAGE_FILE_MANAGER)
            build_file_manager(ui, &layout);
#endif
    }

    if ((ui->active_chrome & PXSYS_REFERENCE_UI_NAVIGATION_BAR) &&
        ui->window.navigation_bar_mode != PXSYS_WINDOW_BAR_HIDDEN) {
        int visible = bar_is_visible(ui, ui->window.navigation_bar_mode);
        int gesture_height = (int)gesture_strip_height(&layout);
        navigation = lv_obj_create(ui->root);
        ui->navigation_bar = navigation;
        style_plain(navigation);
        if (ui->navigation_mode == PXSYS_NAVIGATION_GESTURES) {
            lv_obj_set_pos(navigation, 0,
                           (int32_t)ui->display.height - gesture_height);
            lv_obj_set_size(navigation, (lv_coord_t)ui->display.width,
                            gesture_height);
            lv_obj_set_style_bg_opa(navigation, LV_OPA_TRANSP, 0);
#if PXSYS_REFERENCE_UI_GESTURE_HANDLE
            if (visible) {
                action = lv_obj_create(navigation);
                style_plain(action);
                lv_obj_set_size(
                    action,
                    layout.size_class == PXSYS_UI_SIZE_COMPACT ? 56 : 84, 4);
                lv_obj_set_style_radius(action, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_bg_color(
                    action, color_token(ui, PXSYS_COLOR_TEXT_PRIMARY), 0);
                lv_obj_align(action, LV_ALIGN_BOTTOM_MID, 0, -5);
                ui->navigation_handle = action;
                ui->navigation_handle_width =
                    layout.size_class == PXSYS_UI_SIZE_COMPACT ? 56 : 84;
            }
#endif
            lv_obj_add_flag(navigation,
                            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
            lv_obj_add_event_cb(navigation, navigation_gesture_event,
                                LV_EVENT_PRESSED, ui);
            lv_obj_add_event_cb(navigation, navigation_gesture_event,
                                LV_EVENT_PRESSING, ui);
            lv_obj_add_event_cb(navigation, navigation_gesture_event,
                                LV_EVENT_RELEASED, ui);
            lv_obj_add_event_cb(navigation, navigation_gesture_event,
                                LV_EVENT_GESTURE, ui);
        } else if (!visible) {
            lv_obj_set_pos(navigation, 0,
                           (int32_t)ui->display.height - gesture_height);
            lv_obj_set_size(navigation, (lv_coord_t)ui->display.width,
                            gesture_height);
            lv_obj_set_style_bg_opa(navigation, LV_OPA_TRANSP, 0);
            lv_obj_add_flag(navigation,
                            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
            lv_obj_add_event_cb(navigation, transient_navigation_reveal_event,
                                LV_EVENT_PRESSED, ui);
            lv_obj_add_event_cb(navigation, transient_navigation_reveal_event,
                                LV_EVENT_RELEASED, ui);
            lv_obj_add_event_cb(navigation, transient_navigation_reveal_event,
                                LV_EVENT_PRESS_LOST, ui);
        } else {
            int32_t action_width;
            int32_t action_height;
            lv_obj_set_pos(navigation, layout.navigation_bar.x,
                           layout.navigation_bar.y);
            lv_obj_set_size(navigation, (lv_coord_t)layout.navigation_bar.width,
                            (lv_coord_t)layout.navigation_bar.height);
            lv_obj_set_style_bg_color(navigation,
                                      color_token(ui, PXSYS_COLOR_SURFACE), 0);
            if (ui->window.navigation_bar_mode == PXSYS_WINDOW_BAR_TRANSIENT)
                lv_obj_set_style_bg_opa(navigation, (lv_opa_t)224, 0);
            action_width = (int32_t)layout.navigation_bar.width / 3;
            action_height = layout.navigation_bar.height > 8
                                ? (int32_t)layout.navigation_bar.height - 8 : 1;
            action = make_navigation_action(ui, navigation,
                                            NAVIGATION_ICON_BACK, back_clicked);
            lv_obj_set_size(action, action_width, action_height);
            lv_obj_align(action, LV_ALIGN_LEFT_MID, 0, 0);
            action = make_navigation_action(ui, navigation,
                                            NAVIGATION_ICON_HOME, home_clicked);
            lv_obj_set_size(action, action_width, action_height);
            lv_obj_center(action);
            action = make_navigation_action(
                ui, navigation, NAVIGATION_ICON_RECENTS, recents_clicked);
            lv_obj_set_size(action, action_width, action_height);
            lv_obj_align(action, LV_ALIGN_RIGHT_MID, 0, 0);
        }
    }
    if (ui->navigation_mode == PXSYS_NAVIGATION_GESTURES &&
        ui->content_active && ui->back_gesture_enabled &&
        (PXSYS_REFERENCE_UI_BACK_GESTURE || ui->back_gesture != NULL)) {
        int32_t top = (int32_t)layout.safe_area.y;
        int32_t bottom = (int32_t)ui->display.height -
                         (int32_t)gesture_strip_height(&layout);
        int32_t edge_x = (int32_t)layout.safe_area.x;
        uint32_t edge_width = ui->back_gesture_edge_width != 0
                                  ? ui->back_gesture_edge_width
                                  : pxsys_reference_layout_back_gesture_width();
        if ((ui->active_chrome & PXSYS_REFERENCE_UI_STATUS_BAR) &&
            ui->window.status_bar_mode != PXSYS_WINDOW_BAR_HIDDEN &&
            bar_is_visible(ui, ui->window.status_bar_mode))
            top += (int32_t)layout.status_bar.height;
        if (bottom > top) {
            ui->back_gesture_override = 0;
            back_gesture = lv_obj_create(ui->root);
            style_plain(back_gesture);
            lv_obj_set_pos(back_gesture, edge_x, top);
            lv_obj_set_size(back_gesture, edge_width, bottom - top);
            lv_obj_set_style_bg_opa(back_gesture, LV_OPA_TRANSP, 0);
            lv_obj_add_flag(back_gesture,
                            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
            lv_obj_add_event_cb(back_gesture, navigation_back_gesture_event,
                                LV_EVENT_PRESSED, ui);
            lv_obj_add_event_cb(back_gesture, navigation_back_gesture_event,
                                LV_EVENT_PRESSING, ui);
            lv_obj_add_event_cb(back_gesture, navigation_back_gesture_event,
                                LV_EVENT_RELEASED, ui);
            lv_obj_add_event_cb(back_gesture, navigation_back_gesture_event,
                                LV_EVENT_PRESS_LOST, ui);
        }
    }
    /* Content is constructed after the status bar, so creation order alone
     * would put Home/Settings above system chrome. Reassert the invariant at
     * the end of every rebuild; this also protects against scroll overflow and
     * page transition transforms. */
    if (ui->status_bar != NULL) lv_obj_move_foreground(ui->status_bar);
    if (back_gesture != NULL) lv_obj_move_foreground(back_gesture);
    if (ui->navigation_bar != NULL) lv_obj_move_foreground(ui->navigation_bar);
    /* A root hosted directly on layer_top is already above application
     * surfaces. Reordering that root on every status/locale refresh would
     * jump it over newer global overlays such as the lock screen. Custom
     * parents still need the old foreground behavior. */
    if (lv_obj_get_parent(ui->root) !=
        lv_display_get_layer_top(lv_obj_get_display(ui->root)))
        lv_obj_move_foreground(ui->root);
    /* Rebuilds can be triggered while an application is being dismissed. The
     * switcher is a sibling of root, so keep an open switcher above the rebuilt
     * system chrome until the dismiss interaction explicitly closes it. */
    if (task_switcher_is_open(ui)) lv_obj_move_foreground(ui->task_switcher);
    toast_reposition(ui);
    if (ui->toast_visible) toast_restack(ui);
    if (ui->notification_shade_open)
        build_notification_shade(ui, ui->notification_dragging
                                         ? ui->notification_progress : 256);
    lock_screen_refresh(ui);
}

static void lock_screen_refresh(pxsys_reference_lvgl_t* ui) {
    char time_text[8];
    char date_text[20];
    int32_t text_width;
    int32_t bottom_inset;
    if (!ui_valid(ui) || ui->lock_screen == NULL) return;
    lv_obj_set_size(ui->lock_screen, (lv_coord_t)ui->display.width,
                    (lv_coord_t)ui->display.height);
    lv_obj_set_style_bg_color(ui->lock_screen,
                              color_token(ui, PXSYS_COLOR_BACKGROUND), 0);
    text_width = (int32_t)ui->display.width -
                 (int32_t)ui->display.safe_insets.left -
                 (int32_t)ui->display.safe_insets.right - 24;
    if (text_width < 96) text_width = (int32_t)ui->display.width - 24;
    bottom_inset = ui->display.safe_insets.bottom;

    if (ui->system_status.time_valid) {
        snprintf(time_text, sizeof(time_text), "%02u:%02u",
                 (unsigned)ui->system_status.hour,
                 (unsigned)ui->system_status.minute);
    } else {
        snprintf(time_text, sizeof(time_text), "--:--");
    }
    if (ui->system_status.date_valid) {
        snprintf(date_text, sizeof(date_text), "%04u-%02u-%02u",
                 (unsigned)ui->system_status.year,
                 (unsigned)ui->system_status.month,
                 (unsigned)ui->system_status.day);
    } else {
        snprintf(date_text, sizeof(date_text), "%s",
                 translated(ui, "lock.time_unavailable", "Time unavailable"));
    }
    lv_label_set_text(ui->lock_time, time_text);
    lv_obj_set_width(ui->lock_time, text_width);
    lv_obj_set_style_text_align(ui->lock_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(ui->lock_time,
                                color_token(ui, PXSYS_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(
        ui->lock_time, typography_font(ui, PXSYS_TYPOGRAPHY_DISPLAY), 0);
    lv_obj_align(ui->lock_time, LV_ALIGN_CENTER, 0, -24);

    lv_label_set_text(ui->lock_date, date_text);
    lv_obj_set_width(ui->lock_date, text_width);
    lv_obj_set_style_text_align(ui->lock_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(ui->lock_date,
                                color_token(ui, PXSYS_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(
        ui->lock_date, typography_font(ui, PXSYS_TYPOGRAPHY_BODY), 0);
    lv_obj_align(ui->lock_date, LV_ALIGN_CENTER, 0, 20);

    lv_label_set_text(ui->lock_hint,
                      translated(ui, "lock.swipe", "Swipe up to unlock"));
    lv_obj_set_width(ui->lock_hint, text_width);
    lv_obj_set_style_text_align(ui->lock_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(ui->lock_hint,
                                color_token(ui, PXSYS_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(
        ui->lock_hint, typography_font(ui, PXSYS_TYPOGRAPHY_LABEL), 0);
    lv_obj_align(ui->lock_hint, LV_ALIGN_BOTTOM_MID, 0,
                 -(bottom_inset + 26));

    lv_obj_set_style_bg_color(ui->lock_indicator,
                              color_token(ui, PXSYS_COLOR_ACCENT), 0);
    lv_obj_align(ui->lock_indicator, LV_ALIGN_BOTTOM_MID, 0,
                 -(bottom_inset + 12));
    lv_obj_move_foreground(ui->lock_screen);
}

static void lock_screen_event(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    lv_indev_t* indev;
    lv_point_t point;
    lv_event_code_t code;
    int32_t offset;
    int32_t threshold;
    if (!ui_valid(ui) || ui->lock_screen == NULL) return;
    indev = lv_event_get_indev(event);
    if (indev == NULL) indev = lv_indev_active();
    if (indev == NULL) return;
    lv_event_stop_bubbling(event);
    lv_indev_get_point(indev, &point);
    code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        ui->lock_dragging = 1;
        ui->lock_press_y = point.y;
        return;
    }
    if (!ui->lock_dragging) return;
    offset = point.y - ui->lock_press_y;
    if (code == LV_EVENT_PRESSING) return;
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        ui->lock_dragging = 0;
        threshold = (int32_t)ui->display.height / 5;
        if (threshold < 48) threshold = 48;
        if (code == LV_EVENT_RELEASED && offset <= -threshold) {
            (void)pxsys_reference_lvgl_set_locked(ui, false);
        }
    }
}

static int lock_screen_create(pxsys_reference_lvgl_t* ui) {
    if (ui->lock_screen != NULL) {
        lock_screen_refresh(ui);
        return 1;
    }
    ui->lock_screen = lv_obj_create(ui->parent);
    if (ui->lock_screen == NULL) return 0;
    style_plain(ui->lock_screen);
    lv_obj_set_pos(ui->lock_screen, 0, 0);
    lv_obj_set_style_bg_opa(ui->lock_screen, LV_OPA_COVER, 0);
    lv_obj_add_flag(ui->lock_screen,
                    LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(ui->lock_screen, lock_screen_event, LV_EVENT_PRESSED, ui);
    lv_obj_add_event_cb(ui->lock_screen, lock_screen_event, LV_EVENT_PRESSING, ui);
    lv_obj_add_event_cb(ui->lock_screen, lock_screen_event, LV_EVENT_RELEASED, ui);
    lv_obj_add_event_cb(ui->lock_screen, lock_screen_event, LV_EVENT_PRESS_LOST, ui);
    ui->lock_time = lv_label_create(ui->lock_screen);
    ui->lock_date = lv_label_create(ui->lock_screen);
    ui->lock_hint = lv_label_create(ui->lock_screen);
    ui->lock_indicator = lv_obj_create(ui->lock_screen);
    if (ui->lock_time == NULL || ui->lock_date == NULL ||
        ui->lock_hint == NULL || ui->lock_indicator == NULL) {
        lv_obj_delete(ui->lock_screen);
        ui->lock_screen = NULL;
        ui->lock_time = NULL;
        ui->lock_date = NULL;
        ui->lock_hint = NULL;
        ui->lock_indicator = NULL;
        return 0;
    }
    style_plain(ui->lock_indicator);
    lv_obj_set_size(ui->lock_indicator, 44, 4);
    lv_obj_set_style_bg_opa(ui->lock_indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ui->lock_indicator, LV_RADIUS_CIRCLE, 0);
    lock_screen_refresh(ui);
    return 1;
}

static void lock_screen_destroy(pxsys_reference_lvgl_t* ui) {
    if (ui->lock_screen != NULL) lv_obj_delete(ui->lock_screen);
    ui->lock_screen = NULL;
    ui->lock_time = NULL;
    ui->lock_date = NULL;
    ui->lock_hint = NULL;
    ui->lock_indicator = NULL;
    ui->lock_dragging = 0;
}

static void power_menu_cancel_clicked(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    pxsys_reference_lvgl_hide_power_menu(ui);
}

static void power_menu_action_clicked(lv_event_t* event) {
    pxsys_reference_lvgl_t* ui =
        (pxsys_reference_lvgl_t*)lv_event_get_user_data(event);
    lv_obj_t* target = lv_event_get_current_target(event);
    pxsys_reference_lvgl_power_action_fn callback;
    void* callback_context;
    uintptr_t encoded_action;
    if (!ui_valid(ui) || target == NULL) return;
    encoded_action = (uintptr_t)lv_obj_get_user_data(target);
    if (encoded_action == 0) return;
    callback = ui->power_action;
    callback_context = ui->power_action_context;
    pxsys_reference_lvgl_hide_power_menu(ui);
    if (callback != NULL) {
        callback(callback_context,
                 (pxsys_reference_power_action_t)(encoded_action - 1u));
    }
}

static lv_obj_t* power_menu_button(pxsys_reference_lvgl_t* ui,
                                   lv_obj_t* panel, const char* text,
                                   pxsys_color_token_t background,
                                   pxsys_color_token_t foreground) {
    lv_obj_t* button = lv_button_create(panel);
    lv_obj_t* label;
    if (button == NULL) return NULL;
    lv_obj_set_width(button, LV_PCT(100));
    lv_obj_set_height(button, 44);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_bg_color(button, color_token(ui, background), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    label = make_label(button, text,
                       typography_font(ui, PXSYS_TYPOGRAPHY_LABEL),
                       color_token(ui, foreground));
    if (label != NULL) lv_obj_center(label);
    return button;
}

static int power_menu_create(pxsys_reference_lvgl_t* ui) {
    lv_obj_t* panel;
    lv_obj_t* title;
    lv_obj_t* button;
    int32_t panel_width;
    if (!ui_valid(ui)) return 0;
    if (ui->power_menu != NULL) {
        lv_obj_move_foreground(ui->power_menu);
        return 1;
    }
    ui->power_menu = lv_obj_create(ui->parent);
    if (ui->power_menu == NULL) return 0;
    style_plain(ui->power_menu);
    lv_obj_set_pos(ui->power_menu, 0, 0);
    lv_obj_set_size(ui->power_menu, (lv_coord_t)ui->display.width,
                    (lv_coord_t)ui->display.height);
    lv_obj_set_style_bg_color(ui->power_menu,
                              color_token(ui, PXSYS_COLOR_SCRIM), 0);
    lv_obj_set_style_bg_opa(ui->power_menu, LV_OPA_60, 0);
    lv_obj_add_flag(ui->power_menu, LV_OBJ_FLAG_CLICKABLE);

    panel_width = (int32_t)ui->display.width -
                  (int32_t)ui->display.safe_insets.left -
                  (int32_t)ui->display.safe_insets.right - 32;
    if (panel_width > 280) panel_width = 280;
    if (panel_width < 184) panel_width = 184;
    panel = lv_obj_create(ui->power_menu);
    if (panel == NULL) goto failed;
    lv_obj_set_size(panel, panel_width, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(panel, 222, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_bg_color(panel, color_token(ui, PXSYS_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(
        panel, color_token(ui, PXSYS_COLOR_BORDER), 0);
    lv_obj_set_style_shadow_width(panel, 20, 0);
    lv_obj_set_style_shadow_opa(panel, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(panel, 14, 0);
    lv_obj_set_style_pad_row(panel, 10, 0);
    lv_obj_set_layout(panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(panel);

    title = make_label(panel, translated(ui, "power.title", "Power menu"),
                       typography_font(ui, PXSYS_TYPOGRAPHY_TITLE),
                       color_token(ui, PXSYS_COLOR_TEXT_PRIMARY));
    if (title == NULL) goto failed;
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    button = power_menu_button(
        ui, panel, translated(ui, "power.restart", "Restart"),
        PXSYS_COLOR_ACCENT, PXSYS_COLOR_ON_ACCENT);
    if (button == NULL) goto failed;
    lv_obj_set_user_data(
        button, (void*)(uintptr_t)(PXSYS_REFERENCE_POWER_ACTION_RESTART + 1u));
    lv_obj_add_event_cb(button, power_menu_action_clicked, LV_EVENT_CLICKED, ui);

    button = power_menu_button(
        ui, panel, translated(ui, "power.shutdown", "Power off"),
        PXSYS_COLOR_ERROR, PXSYS_COLOR_ON_ACCENT);
    if (button == NULL) goto failed;
    lv_obj_set_user_data(
        button, (void*)(uintptr_t)(PXSYS_REFERENCE_POWER_ACTION_SHUTDOWN + 1u));
    lv_obj_add_event_cb(button, power_menu_action_clicked, LV_EVENT_CLICKED, ui);

    button = power_menu_button(
        ui, panel, translated(ui, "power.cancel", "Cancel"),
        PXSYS_COLOR_BACKGROUND, PXSYS_COLOR_TEXT_PRIMARY);
    if (button == NULL) goto failed;
    lv_obj_add_event_cb(button, power_menu_cancel_clicked, LV_EVENT_CLICKED, ui);
    lv_obj_move_foreground(ui->power_menu);
    if (ui->power_menu_changed != NULL)
        ui->power_menu_changed(ui->power_menu_changed_context, true);
    return 1;

failed:
    lv_obj_delete(ui->power_menu);
    ui->power_menu = NULL;
    return 0;
}

static void display_changed(void* context,
                            const pxsys_display_profile_t* profile) {
    pxsys_reference_lvgl_t* ui = (pxsys_reference_lvgl_t*)context;
    if (!ui_valid(ui)) return;
    ui->display = *profile;
    rebuild(ui);
}

static void theme_changed(void* context,
                          const pxsys_theme_snapshot_t* theme) {
    pxsys_reference_lvgl_t* ui = (pxsys_reference_lvgl_t*)context;
    if (!ui_valid(ui)) return;
    ui->theme = *theme;
    rebuild(ui);
    toast_apply_theme(ui, ui->toast_tone);
}

static void locale_changed(void* context,
                           const pxsys_locale_snapshot_t* locale) {
    pxsys_reference_lvgl_t* ui = (pxsys_reference_lvgl_t*)context;
    if (!ui_valid(ui) || locale == NULL) return;
    ui->locale = *locale;
    rebuild(ui);
}

static void system_status_changed(
    void* context, const pxsys_system_status_snapshot_t* status) {
    pxsys_reference_lvgl_t* ui = (pxsys_reference_lvgl_t*)context;
    if (!ui_valid(ui) || status == NULL) return;
    ui->system_status = *status;
    rebuild(ui);
}

static void window_changed(void* context,
                           const pxsys_window_snapshot_t* window) {
    pxsys_reference_lvgl_t* ui = (pxsys_reference_lvgl_t*)context;
    int task_switcher_open;
    if (!ui_valid(ui) || window == NULL) return;
    task_switcher_open = task_switcher_is_open(ui);
    ui->window = *window;
    ui->transient_revealed = 0;
    if (ui->transient_timer != NULL) lv_timer_pause(ui->transient_timer);
    if (!task_switcher_open) close_task_switcher(ui);
    close_notification_shade(ui);
    rebuild(ui);
}

static pxsys_status_t app_create(void* context,
                                 const pxsys_app_descriptor_t* app,
                                 uint64_t instance_id, void** instance) {
    (void)app;
    (void)instance_id;
    if (context == NULL || instance == NULL) return PXSYS_STATUS_INVALID_ARGUMENT;
    *instance = context;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t app_start(void* context, void* instance,
                                const pxsys_message_t* launch) {
    (void)context;
    (void)instance;
    (void)launch;
    return PXSYS_STATUS_OK;
}

static int page_is_content(reference_page_t page) {
    if (page == REFERENCE_PAGE_HOME || page == REFERENCE_PAGE_SETTINGS)
        return 1;
#if PXSYS_REFERENCE_UI_BUILTIN_SETTINGS
    if (page == REFERENCE_PAGE_SOUND_SETTINGS ||
        page == REFERENCE_PAGE_DEVICE_INFO ||
        page == REFERENCE_PAGE_APP_MANAGER ||
        page == REFERENCE_PAGE_FILE_MANAGER)
        return 1;
#endif
    return 0;
}

static pxsys_status_t app_foreground(void* context, void* instance) {
    app_context_t* app = (app_context_t*)instance;
    (void)context;
    if (app == NULL || !ui_valid(app->ui)) return PXSYS_STATUS_BAD_STATE;
    if (page_is_content(app->page)) {
        app->ui->active_page = app->page;
        app->ui->content_active = 1;
        /* Storage-backed pages re-read their listing whenever they come
         * forward instead of on every status rebuild. */
        if (app->page == REFERENCE_PAGE_APP_MANAGER)
            app->ui->managed_apps_loaded = 0;
        else if (app->page == REFERENCE_PAGE_FILE_MANAGER)
            app->ui->file_entries_loaded = 0;
    } else {
        app->ui->active_chrome |= (uint8_t)(1u << app->page);
    }
    lv_obj_remove_flag(app->ui->root, LV_OBJ_FLAG_HIDDEN);
    rebuild(app->ui);
    return PXSYS_STATUS_OK;
}

static pxsys_status_t app_background(void* context, void* instance) {
    app_context_t* app = (app_context_t*)instance;
    (void)context;
    if (app == NULL || !ui_valid(app->ui)) return PXSYS_STATUS_BAD_STATE;
    if (page_is_content(app->page))
        app->ui->content_active = 0;
    else
        app->ui->active_chrome &= (uint8_t)~(1u << app->page);
    rebuild(app->ui);
    return PXSYS_STATUS_OK;
}

static pxsys_status_t app_event(void* context, void* instance,
                                const pxsys_message_t* event) {
    (void)context;
    (void)instance;
    (void)event;
    return PXSYS_STATUS_OK;
}

static pxsys_back_result_t app_back(void* context, void* instance) {
    app_context_t* app = (app_context_t*)instance;
    (void)context;
    /* Back on the launcher page must not pop it and surface an older task. */
    if (app != NULL && app->page == REFERENCE_PAGE_HOME)
        return PXSYS_BACK_HANDLED;
    return PXSYS_BACK_UNHANDLED;
}

static void app_stop(void* context, void* instance,
                     pxsys_stop_reason_t reason) {
    app_context_t* app = (app_context_t*)instance;
    (void)context;
    (void)reason;
    if (app == NULL || !ui_valid(app->ui)) return;
    if (page_is_content(app->page)) {
        if (app->ui->active_page == app->page)
            app->ui->content_active = 0;
    } else {
        app->ui->active_chrome &= (uint8_t)~(1u << app->page);
    }
    rebuild(app->ui);
}

static void app_destroy(void* context, void* instance) {
    (void)context;
    (void)instance;
}

void pxsys_reference_lvgl_config_init(pxsys_reference_lvgl_config_t* config) {
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->features = PXSYS_REFERENCE_UI_ALL;
    config->max_launcher_apps = 24;
    config->navigation_mode = PXSYS_NAVIGATION_BUTTONS;
    config->animations_enabled = PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS ? 1 : 0;
    config->back_gesture_enabled = 1;
    config->allocator.struct_size = sizeof(config->allocator);
}

static int page_registration_enabled(const pxsys_reference_lvgl_t* ui,
                                     size_t index) {
    static const uint32_t kPageFeatures[REFERENCE_APP_COUNT] = {
        PXSYS_REFERENCE_UI_HOME,
        PXSYS_REFERENCE_UI_SETTINGS,
        PXSYS_REFERENCE_UI_STATUS_BAR,
        PXSYS_REFERENCE_UI_NAVIGATION_BAR,
        PXSYS_REFERENCE_UI_SOUND_SETTINGS,
        PXSYS_REFERENCE_UI_DEVICE_INFO,
        PXSYS_REFERENCE_UI_APP_MANAGER,
        PXSYS_REFERENCE_UI_FILE_MANAGER,
    };
    if (index >= REFERENCE_APP_COUNT ||
        !(ui->features & kPageFeatures[index]))
        return 0;
#if PXSYS_REFERENCE_UI_BUILTIN_SETTINGS
    switch ((reference_page_t)index) {
        case REFERENCE_PAGE_DEVICE_INFO:
            return ui->device_info != NULL;
        case REFERENCE_PAGE_APP_MANAGER:
            return ui->app_list != NULL && ui->app_action != NULL;
        case REFERENCE_PAGE_FILE_MANAGER:
            return ui->file_list != NULL;
        case REFERENCE_PAGE_SOUND_SETTINGS:
            return 1;
        default:
            break;
    }
#else
    if (index >= REFERENCE_PAGE_STATUS_BAR) return 0;
#endif
    return 1;
}

static pxsys_status_t register_apps(pxsys_reference_lvgl_t* ui) {
    static const char* const app_ids[] = {
        "system.home", "system.settings", "system.status-bar",
        "system.navigation-bar", "system.sound-settings",
        "system.device-info", "system.app-manager", "system.file-manager"};
    static const char* const names[] = {
        "System Home", "System Settings", "System Status Bar",
        "System Navigation Bar", "Sound & display", "About device",
        "Apps", "Files & storage"};
    static const char* const name_resource_keys[] = {
        "app.system-home.name", "app.system-settings.name",
        "app.system-status-bar.name", "app.system-navigation-bar.name",
        "settings.sound", "settings.about", "settings.apps", "settings.files"};
    static const char* const descriptions[] = {
        "Standard system launcher", "System and device settings", "", "",
        "Volume and brightness", "Firmware and hardware information",
        "Installed applications", "Browse device storage"};
    static const char* const description_resource_keys[] = {
        "app.system-home.description", "app.system-settings.description", "",
        "", "settings.sound.detail", "settings.about.detail",
        "settings.apps.detail", "settings.files.detail"};
    static const char* const roles[] = {
        PXSYS_ROLE_HOME, PXSYS_ROLE_SETTINGS, PXSYS_ROLE_STATUS_BAR,
        PXSYS_ROLE_NAVIGATION_BAR, PXSYS_ROLE_SOUND_SETTINGS,
        PXSYS_ROLE_DEVICE_INFO, PXSYS_ROLE_APP_MANAGER,
        PXSYS_ROLE_FILE_MANAGER};
    size_t index;
    for (index = 0; index < REFERENCE_APP_COUNT; ++index) {
        pxsys_app_descriptor_t descriptor = {0};
        pxsys_native_app_t native = {0};
        pxsys_role_candidate_t role = {0};
        pxsys_status_t status;
        snprintf(ui->app_ids[index], sizeof(ui->app_ids[index]), "%s", app_ids[index]);
        memcpy(ui->identities[index].publisher_root, ui->publisher_root,
               PXSYS_PUBLISHER_ROOT_BYTES);
        ui->identities[index].app_id = pxsys_string_from_cstr(ui->app_ids[index]);
        ui->contexts[index].ui = ui;
        ui->contexts[index].page = (reference_page_t)index;
        if (!page_registration_enabled(ui, index)) continue;
        descriptor.struct_size = sizeof(descriptor);
        descriptor.identity = ui->identities[index];
        descriptor.display_name = pxsys_string_from_cstr(names[index]);
        descriptor.description = pxsys_string_from_cstr(descriptions[index]);
        descriptor.resource_namespace =
            pxsys_string_from_cstr(REFERENCE_RESOURCE_NAMESPACE);
        descriptor.display_name_resource_key =
            pxsys_string_from_cstr(name_resource_keys[index]);
        descriptor.description_resource_key =
            pxsys_string_from_cstr(description_resource_keys[index]);
        descriptor.version = pxsys_string_from_cstr("1.0.0");
        descriptor.runtime_id = pxsys_string_from_cstr(PXSYS_NATIVE_RUNTIME_ID);
        descriptor.flags = PXSYS_APP_FLAG_SYSTEM | PXSYS_APP_FLAG_ENABLED |
                           PXSYS_APP_FLAG_SINGLE_INSTANCE;
        /* Settings is a normal launcher app; the chrome apps stay hidden. */
        if (index == 1) descriptor.flags |= PXSYS_APP_FLAG_LAUNCHER;
        status = pxsys_app_registry_register(
            pxsys_standard_system_apps(ui->system), &descriptor);
        if (status != PXSYS_STATUS_OK) return status;
        ui->apps_registered |= (uint8_t)(1u << index);
        native.struct_size = sizeof(native);
        native.identity = ui->identities[index];
        native.context = &ui->contexts[index];
        native.create = app_create;
        native.start = app_start;
        native.foreground = app_foreground;
        native.background = app_background;
        native.event = app_event;
        native.back = app_back;
        native.stop = app_stop;
        native.destroy = app_destroy;
        status = pxsys_native_runtime_register_app(
            pxsys_standard_system_native_runtime(ui->system), &native);
        if (status != PXSYS_STATUS_OK) return status;
        role.struct_size = sizeof(role);
        role.role_id = pxsys_string_from_cstr(roles[index]);
        role.app = ui->identities[index];
        role.priority = 100;
        status = pxsys_role_candidate_register(
            pxsys_standard_system_roles(ui->system), &role);
        if (status != PXSYS_STATUS_OK) return status;
        ui->roles_registered |= (uint8_t)(1u << index);
    }
    return PXSYS_STATUS_OK;
}

static void unregister_apps(pxsys_reference_lvgl_t* ui) {
    size_t index = REFERENCE_APP_COUNT;
    while (index != 0) {
        index--;
        if (ui->roles_registered & (uint8_t)(1u << index))
            (void)pxsys_role_candidates_unregister(
                pxsys_standard_system_roles(ui->system), &ui->identities[index]);
        if (ui->apps_registered & (uint8_t)(1u << index)) {
            (void)pxsys_native_runtime_unregister_app(
                pxsys_standard_system_native_runtime(ui->system), &ui->identities[index]);
            (void)pxsys_app_registry_unregister(
                pxsys_standard_system_apps(ui->system), &ui->identities[index]);
        }
    }
    ui->apps_registered = 0;
    ui->roles_registered = 0;
}

pxsys_status_t pxsys_reference_lvgl_create(
    const pxsys_reference_lvgl_config_t* config,
    pxsys_reference_lvgl_t** output) {
    pxsys_reference_lvgl_t* ui;
    pxsys_status_t status;
    if (output == NULL) return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (config == NULL || config->struct_size < sizeof(*config) ||
        config->system == NULL || config->parent == NULL ||
        config->max_launcher_apps == 0 ||
        config->navigation_mode > PXSYS_NAVIGATION_GESTURES ||
        !languages_valid(config->languages, config->language_count) ||
        config->allocator.struct_size < sizeof(config->allocator) ||
        config->allocator.allocate == NULL || config->allocator.release == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    ui = (pxsys_reference_lvgl_t*)config->allocator.allocate(
        config->allocator.context, sizeof(*ui));
    if (ui == NULL) return PXSYS_STATUS_NO_MEMORY;
    memset(ui, 0, sizeof(*ui));
    ui->launcher_items = (launcher_item_t*)config->allocator.allocate(
        config->allocator.context,
        config->max_launcher_apps * sizeof(*ui->launcher_items));
    if (ui->launcher_items == NULL) {
        config->allocator.release(config->allocator.context, ui);
        return PXSYS_STATUS_NO_MEMORY;
    }
    memset(ui->launcher_items, 0,
           config->max_launcher_apps * sizeof(*ui->launcher_items));
    ui->system = config->system;
    ui->allocator = config->allocator;
    ui->parent = config->parent;
    ui->text_font = config->text_font;
    ui->title_font = config->title_font != NULL ? config->title_font
                                                : config->text_font;
    memcpy(ui->fonts, config->fonts, sizeof(ui->fonts));
    ui->font_context = config->font_context;
    ui->resolve_font = config->resolve_font;
    ui->app_icon_context = config->app_icon_context;
    ui->resolve_app_icon = config->resolve_app_icon;
    ui->app_metadata_context = config->app_metadata_context;
    ui->resolve_app_metadata = config->resolve_app_metadata;
    ui->content_insets_context = config->content_insets_context;
    ui->content_insets_changed = config->content_insets_changed;
    ui->languages = config->language_count == 0 ? reference_languages
                                                 : config->languages;
    ui->language_count = config->language_count == 0
                             ? sizeof(reference_languages) /
                                   sizeof(reference_languages[0])
                             : config->language_count;
    ui->wallpaper_source = config->wallpaper_source;
    if (ui->fonts[PXSYS_TYPOGRAPHY_BODY] == NULL)
        ui->fonts[PXSYS_TYPOGRAPHY_BODY] = ui->text_font;
    if (ui->fonts[PXSYS_TYPOGRAPHY_TITLE] == NULL)
        ui->fonts[PXSYS_TYPOGRAPHY_TITLE] = ui->title_font;
    ui->features = config->features;
    ui->max_launcher_apps = config->max_launcher_apps;
    ui->navigation_mode = config->navigation_mode;
    ui->back_gesture_enabled = config->back_gesture_enabled ? 1 : 0;
    ui->back_gesture_edge_width = config->back_gesture_edge_width;
    ui->back_gesture_context = config->back_gesture_context;
    ui->back_gesture = config->back_gesture;
    ui->device_info_context = config->device_info_context;
    ui->device_info = config->device_info;
    ui->app_manager_context = config->app_manager_context;
    ui->app_list = config->app_list;
    ui->app_action = config->app_action;
    ui->file_manager_context = config->file_manager_context;
    ui->file_list = config->file_list;
    ui->file_action = config->file_action;
    ui->memory_info_context = config->memory_info_context;
    ui->memory_info = config->memory_info;
    ui->performance_context = config->performance_context;
    ui->performance_get = config->performance_get;
    ui->performance_set = config->performance_set;
    ui->wifi_context = config->wifi_context;
    ui->wifi_scan = config->wifi_scan;
    ui->wifi_connect = config->wifi_connect;
    ui->app_permission_context = config->app_permission_context;
    ui->app_permission_list = config->app_permission_list;
    ui->app_permission_set = config->app_permission_set;
    ui->animations_enabled =
        PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS && config->animations_enabled;
    memcpy(ui->publisher_root, config->publisher_root,
           PXSYS_PUBLISHER_ROOT_BYTES);
    ui->magic = REFERENCE_MAGIC;
    ui->root = lv_obj_create(ui->parent);
    style_plain(ui->root);
    ui->app_ime_timer = lv_timer_create(app_input_method_poll, 200, ui);
    /* The transparent system-chrome root sits above application surfaces.
     * Only its concrete bars and overlays may participate in hit testing. */
    lv_obj_remove_flag(ui->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(ui->root, LV_OBJ_FLAG_HIDDEN);
    ui->display.struct_size = sizeof(ui->display);
    status = pxsys_display_service_get(
        pxsys_standard_system_display(ui->system), &ui->display);
    if (status != PXSYS_STATUS_OK) goto failed;
    ui->theme.struct_size = sizeof(ui->theme);
    status = pxsys_theme_service_get(
        pxsys_standard_system_theme(ui->system), &ui->theme);
    if (status != PXSYS_STATUS_OK) goto failed;
    ui->system_status.struct_size = sizeof(ui->system_status);
    status = pxsys_system_status_service_get(
        pxsys_standard_system_status(ui->system), &ui->system_status);
    if (status != PXSYS_STATUS_OK) goto failed;
    ui->window.struct_size = sizeof(ui->window);
    status = pxsys_window_service_get(
        pxsys_standard_system_window(ui->system), &ui->window);
    if (status != PXSYS_STATUS_OK) goto failed;
    status = pxsys_resource_catalog_register(
        pxsys_standard_system_resources(ui->system), &reference_catalog_en);
    if (status != PXSYS_STATUS_OK && status != PXSYS_STATUS_ALREADY_EXISTS)
        goto failed;
    ui->resource_catalog_en_registered = status == PXSYS_STATUS_OK;
    status = pxsys_resource_catalog_register(
        pxsys_standard_system_resources(ui->system), &reference_catalog_zh);
    if (status != PXSYS_STATUS_OK && status != PXSYS_STATUS_ALREADY_EXISTS)
        goto failed;
    ui->resource_catalog_zh_registered = status == PXSYS_STATUS_OK;
    ui->locale.struct_size = sizeof(ui->locale);
    status = pxsys_locale_service_get(
        pxsys_standard_system_locale(ui->system), &ui->locale);
    if (status != PXSYS_STATUS_OK) goto failed;
    status = register_apps(ui);
    if (status != PXSYS_STATUS_OK) goto failed;
    status = pxsys_display_service_subscribe(
        pxsys_standard_system_display(ui->system), ui, display_changed);
    if (status != PXSYS_STATUS_OK) goto failed;
    ui->display_subscribed = 1;
    status = pxsys_theme_service_subscribe(
        pxsys_standard_system_theme(ui->system), ui, theme_changed);
    if (status != PXSYS_STATUS_OK) goto failed;
    ui->theme_subscribed = 1;
    status = pxsys_locale_service_subscribe(
        pxsys_standard_system_locale(ui->system), ui, locale_changed);
    if (status != PXSYS_STATUS_OK) goto failed;
    ui->locale_subscribed = 1;
    status = pxsys_system_status_service_subscribe(
        pxsys_standard_system_status(ui->system), ui, system_status_changed);
    if (status != PXSYS_STATUS_OK) goto failed;
    ui->status_subscribed = 1;
    status = pxsys_window_service_subscribe(
        pxsys_standard_system_window(ui->system), ui, window_changed);
    if (status != PXSYS_STATUS_OK) goto failed;
    ui->window_subscribed = 1;
    status = pxsys_toast_service_subscribe(
        pxsys_standard_system_toasts(ui->system), ui, toast_posted);
    if (status != PXSYS_STATUS_OK) goto failed;
    ui->toast_subscribed = 1;
    *output = ui;
    return PXSYS_STATUS_OK;

failed:
    (void)pxsys_reference_lvgl_destroy(ui);
    return status;
}

pxsys_status_t pxsys_reference_lvgl_start(pxsys_reference_lvgl_t* ui) {
    pxsys_intent_t intent = {0};
    pxsys_instance_ref_t instance;
    pxsys_status_t status;
    if (!ui_valid(ui)) return PXSYS_STATUS_INVALID_ARGUMENT;
    status = pxsys_reference_lvgl_home(ui);
    if (status != PXSYS_STATUS_OK && status != PXSYS_STATUS_PENDING)
        return status;
    intent.struct_size = sizeof(intent);
    intent.action = pxsys_string_from_cstr("system.intent.main");
    if (ui->features & PXSYS_REFERENCE_UI_STATUS_BAR) {
        status = pxsys_role_host_start(
            pxsys_standard_system_role_host(ui->system), NULL,
            pxsys_string_from_cstr(PXSYS_ROLE_STATUS_BAR), &intent, &instance);
        if (status != PXSYS_STATUS_OK && status != PXSYS_STATUS_PENDING)
            return status;
    }
    if (ui->features & PXSYS_REFERENCE_UI_NAVIGATION_BAR) {
        status = pxsys_role_host_start(
            pxsys_standard_system_role_host(ui->system), NULL,
            pxsys_string_from_cstr(PXSYS_ROLE_NAVIGATION_BAR), &intent, &instance);
        if (status != PXSYS_STATUS_OK && status != PXSYS_STATUS_PENDING)
            return status;
    }
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_reference_lvgl_home(pxsys_reference_lvgl_t* ui) {
    if (!ui_valid(ui)) return PXSYS_STATUS_INVALID_ARGUMENT;
    if (ui->locked) return PXSYS_STATUS_DENIED;
    if (!(ui->features & PXSYS_REFERENCE_UI_HOME))
        return PXSYS_STATUS_NOT_FOUND;
    return open_role(ui, PXSYS_ROLE_HOME);
}

void pxsys_reference_lvgl_refresh_apps(pxsys_reference_lvgl_t* ui) {
    if (ui_valid(ui) && ui->active_page == REFERENCE_PAGE_HOME) rebuild(ui);
}

pxsys_status_t pxsys_reference_lvgl_set_navigation_mode(
    pxsys_reference_lvgl_t* ui, pxsys_navigation_mode_t mode) {
    if (!ui_valid(ui) || mode > PXSYS_NAVIGATION_GESTURES)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (ui->navigation_mode != mode) {
        ui->navigation_mode = mode;
        rebuild(ui);
    }
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_reference_lvgl_set_animations_enabled(
    pxsys_reference_lvgl_t* ui, bool enabled) {
    if (!ui_valid(ui)) return PXSYS_STATUS_INVALID_ARGUMENT;
#if PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS
    ui->animations_enabled = enabled ? 1 : 0;
#else
    (void)enabled;
    ui->animations_enabled = 0;
#endif
    return PXSYS_STATUS_OK;
}

bool pxsys_reference_lvgl_animations_enabled(
    const pxsys_reference_lvgl_t* ui) {
    return ui_valid(ui) && ui->animations_enabled;
}

bool pxsys_reference_lvgl_dismiss_overlay(pxsys_reference_lvgl_t* ui) {
    if (!ui_valid(ui)) return false;
    if (ui->power_menu != NULL) {
        pxsys_reference_lvgl_hide_power_menu(ui);
        return true;
    }
    if (ui->locked) return true;
    if (ui->language_dialog != NULL) {
        close_language_dialog(ui);
        return true;
    }
    if (task_switcher_is_open(ui)) {
        close_task_switcher(ui);
        return true;
    }
    if (ui->notification_shade_open) {
        settle_notification_shade(ui, 0);
        return true;
    }
    if (has_transient_chrome(ui) && !ui->transient_revealed) {
        reveal_transient_chrome(ui);
        return true;
    }
    return false;
}

pxsys_status_t pxsys_reference_lvgl_set_locked(pxsys_reference_lvgl_t* ui,
                                               bool locked) {
    if (!ui_valid(ui)) return PXSYS_STATUS_INVALID_ARGUMENT;
    if ((ui->locked != 0) == locked) {
        if (locked) lock_screen_refresh(ui);
        return PXSYS_STATUS_OK;
    }
    if (locked) {
        close_task_switcher(ui);
        close_notification_shade(ui);
        if (!lock_screen_create(ui)) return PXSYS_STATUS_NO_MEMORY;
        ui->locked = 1;
    } else {
        ui->locked = 0;
        lock_screen_destroy(ui);
    }
    if (ui->lock_changed != NULL)
        ui->lock_changed(ui->lock_changed_context, locked);
    return PXSYS_STATUS_OK;
}

bool pxsys_reference_lvgl_is_locked(const pxsys_reference_lvgl_t* ui) {
    return ui_valid(ui) && ui->locked;
}

pxsys_status_t pxsys_reference_lvgl_set_lock_changed_callback(
    pxsys_reference_lvgl_t* ui, void* context,
    pxsys_reference_lvgl_lock_changed_fn callback) {
    if (!ui_valid(ui)) return PXSYS_STATUS_INVALID_ARGUMENT;
    ui->lock_changed_context = context;
    ui->lock_changed = callback;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_reference_lvgl_set_power_action_callback(
    pxsys_reference_lvgl_t* ui, void* context,
    pxsys_reference_lvgl_power_action_fn callback) {
    if (!ui_valid(ui)) return PXSYS_STATUS_INVALID_ARGUMENT;
    ui->power_action_context = context;
    ui->power_action = callback;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_reference_lvgl_set_power_menu_changed_callback(
    pxsys_reference_lvgl_t* ui, void* context,
    pxsys_reference_lvgl_lock_changed_fn callback) {
    if (!ui_valid(ui)) return PXSYS_STATUS_INVALID_ARGUMENT;
    ui->power_menu_changed_context = context;
    ui->power_menu_changed = callback;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_reference_lvgl_show_power_menu(
    pxsys_reference_lvgl_t* ui) {
    if (!ui_valid(ui)) return PXSYS_STATUS_INVALID_ARGUMENT;
    close_task_switcher(ui);
    close_notification_shade(ui);
    return power_menu_create(ui) ? PXSYS_STATUS_OK : PXSYS_STATUS_NO_MEMORY;
}

void pxsys_reference_lvgl_hide_power_menu(pxsys_reference_lvgl_t* ui) {
    if (!ui_valid(ui) || ui->power_menu == NULL) return;
    lv_obj_delete(ui->power_menu);
    ui->power_menu = NULL;
    if (ui->power_menu_changed != NULL)
        ui->power_menu_changed(ui->power_menu_changed_context, false);
}

bool pxsys_reference_lvgl_power_menu_visible(
    const pxsys_reference_lvgl_t* ui) {
    return ui_valid(ui) && ui->power_menu != NULL;
}

pxsys_status_t pxsys_reference_lvgl_destroy(pxsys_reference_lvgl_t* ui) {
    pxsys_allocator_t allocator;
#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && LV_USE_SNAPSHOT
    size_t preview_index;
#endif
    if (!ui_valid(ui)) return PXSYS_STATUS_INVALID_ARGUMENT;
    if (ui->content_insets_changed != NULL)
        ui->content_insets_changed(ui->content_insets_context, 0, 0);
    (void)lv_async_call_cancel(rebuild_async, ui);
    (void)lv_async_call_cancel(refresh_task_switcher_async, ui);
    if (ui->toast_subscribed)
        (void)pxsys_toast_service_unsubscribe(
            pxsys_standard_system_toasts(ui->system), ui, toast_posted);
    if (ui->window_subscribed)
        (void)pxsys_window_service_unsubscribe(
            pxsys_standard_system_window(ui->system), ui, window_changed);
    if (ui->status_subscribed)
        (void)pxsys_system_status_service_unsubscribe(
            pxsys_standard_system_status(ui->system), ui,
            system_status_changed);
    if (ui->theme_subscribed)
        (void)pxsys_theme_service_unsubscribe(
            pxsys_standard_system_theme(ui->system), ui, theme_changed);
    if (ui->locale_subscribed)
        (void)pxsys_locale_service_unsubscribe(
            pxsys_standard_system_locale(ui->system), ui, locale_changed);
    if (ui->display_subscribed)
        (void)pxsys_display_service_unsubscribe(
            pxsys_standard_system_display(ui->system), ui, display_changed);
    if (ui->features & PXSYS_REFERENCE_UI_NAVIGATION_BAR)
        (void)pxsys_role_host_stop(pxsys_standard_system_role_host(ui->system),
                                   pxsys_string_from_cstr(PXSYS_ROLE_NAVIGATION_BAR),
                                   PXSYS_STOP_SHUTDOWN);
    if (ui->features & PXSYS_REFERENCE_UI_STATUS_BAR)
        (void)pxsys_role_host_stop(pxsys_standard_system_role_host(ui->system),
                                   pxsys_string_from_cstr(PXSYS_ROLE_STATUS_BAR),
                                   PXSYS_STOP_SHUTDOWN);
    (void)pxsys_task_manager_finish_all(pxsys_standard_system_tasks(ui->system),
                                        PXSYS_STOP_SHUTDOWN);
    unregister_apps(ui);
    if (ui->resource_catalog_zh_registered)
        (void)pxsys_resource_catalog_unregister(
            pxsys_standard_system_resources(ui->system),
            &reference_catalog_zh);
    if (ui->resource_catalog_en_registered)
        (void)pxsys_resource_catalog_unregister(
            pxsys_standard_system_resources(ui->system),
            &reference_catalog_en);
    if (ui->app_ime_keypad != NULL) pxsys_reference_ime_destroy(ui->app_ime_keypad);
    if (ui->app_ime != NULL) lv_obj_delete(ui->app_ime);
    if (ui->app_ime_timer != NULL) lv_timer_delete(ui->app_ime_timer);
    if (ui->toast_timer != NULL) lv_timer_delete(ui->toast_timer);
    if (ui->transient_timer != NULL) lv_timer_delete(ui->transient_timer);
    pxsys_reference_lvgl_hide_power_menu(ui);
    lock_screen_destroy(ui);
    if (ui->toast != NULL) lv_obj_delete(ui->toast);
    if (ui->notification_shade != NULL) lv_obj_delete(ui->notification_shade);
    if (ui->task_switcher != NULL) lv_obj_delete(ui->task_switcher);
    if (ui->root != NULL) lv_obj_delete(ui->root);
    release_launcher_icons(ui);
#if PXSYS_REFERENCE_UI_TASK_SWITCHER == PXSYS_TASK_SWITCHER_CARDS && LV_USE_SNAPSHOT
    for (preview_index = 0;
         preview_index < PXSYS_REFERENCE_UI_TASK_PREVIEW_COUNT;
         ++preview_index) {
        if (ui->task_previews[preview_index].image != NULL)
            lv_draw_buf_destroy(ui->task_previews[preview_index].image);
    }
#endif
    allocator = ui->allocator;
    ui->magic = 0;
    allocator.release(allocator.context, ui->launcher_items);
    allocator.release(allocator.context, ui);
    return PXSYS_STATUS_OK;
}
