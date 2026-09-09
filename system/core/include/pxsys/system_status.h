#ifndef PXSYS_SYSTEM_STATUS_H
#define PXSYS_SYSTEM_STATUS_H

#include <stddef.h>
#include <stdint.h>

#include "pxsys/status.h"
#include "pxsys/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PXSYS_NETWORK_NONE = 0,
    PXSYS_NETWORK_WIFI,
    PXSYS_NETWORK_CELLULAR,
    PXSYS_NETWORK_ETHERNET,
} pxsys_network_type_t;

typedef enum {
    PXSYS_LEVEL_CONTROL_VOLUME = 0,
    PXSYS_LEVEL_CONTROL_BRIGHTNESS,
} pxsys_level_control_t;

typedef enum {
    PXSYS_TOGGLE_BLUETOOTH = 0,
    PXSYS_TOGGLE_DO_NOT_DISTURB,
    PXSYS_TOGGLE_FLASHLIGHT,
    PXSYS_TOGGLE_AIRPLANE_MODE,
} pxsys_toggle_control_t;

typedef struct {
    uint32_t struct_size;
    uint64_t generation;
    uint8_t time_valid;
    uint8_t hour;
    uint8_t minute;
    uint8_t battery_valid;
    uint8_t battery_percent;
    uint8_t charging;
    uint8_t network_connected;
    uint8_t network_signal_level;
    pxsys_network_type_t network_type;
    /* Independent radio state. A provider which only implements the legacy
     * network_* fields may leave these zero; consumers must retain the legacy
     * fallback until the capability fields are populated. */
    uint8_t wifi_supported;
    uint8_t wifi_enabled;
    uint8_t wifi_connected;
    uint8_t wifi_signal_level;
    uint8_t cellular_supported;
    uint8_t cellular_enabled;
    uint8_t cellular_connected;
    uint8_t cellular_signal_level;
    /* Calendar and control-center capabilities are append-only so existing
     * providers can leave them zero-initialized. Weekday uses 0=Sunday. */
    uint8_t date_valid;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t volume_supported;
    uint8_t volume_percent;
    uint8_t brightness_supported;
    uint8_t brightness_percent;
    uint8_t bluetooth_supported;
    uint8_t bluetooth_enabled;
    uint8_t do_not_disturb_supported;
    uint8_t do_not_disturb_enabled;
    uint8_t flashlight_supported;
    uint8_t flashlight_enabled;
    uint8_t airplane_mode_supported;
    uint8_t airplane_mode_enabled;
} pxsys_system_status_snapshot_t;

typedef void (*pxsys_system_status_changed_fn)(
    void* context, const pxsys_system_status_snapshot_t* snapshot);

typedef pxsys_status_t (*pxsys_network_set_enabled_fn)(
    void* context, pxsys_network_type_t network, uint8_t enabled);

typedef pxsys_status_t (*pxsys_level_control_set_fn)(
    void* context, pxsys_level_control_t control, uint8_t percent);

typedef pxsys_status_t (*pxsys_toggle_control_set_fn)(
    void* context, pxsys_toggle_control_t control, uint8_t enabled);

typedef struct {
    uint32_t struct_size;
    size_t max_observers;
    pxsys_allocator_t allocator;
    void* network_control_context;
    pxsys_network_set_enabled_fn set_network_enabled;
    void* control_context;
    pxsys_level_control_set_fn set_level;
    pxsys_toggle_control_set_fn set_toggle;
} pxsys_system_status_service_config_t;

typedef struct pxsys_system_status_service pxsys_system_status_service_t;

void pxsys_system_status_snapshot_init(pxsys_system_status_snapshot_t* snapshot);
void pxsys_system_status_service_config_init(
    pxsys_system_status_service_config_t* config);
pxsys_status_t pxsys_system_status_service_create(
    const pxsys_system_status_service_config_t* config,
    const pxsys_system_status_snapshot_t* initial,
    pxsys_system_status_service_t** output);
pxsys_status_t pxsys_system_status_service_destroy(
    pxsys_system_status_service_t* service);
pxsys_status_t pxsys_system_status_service_update(
    pxsys_system_status_service_t* service,
    const pxsys_system_status_snapshot_t* snapshot);
pxsys_status_t pxsys_system_status_service_get(
    const pxsys_system_status_service_t* service,
    pxsys_system_status_snapshot_t* snapshot);
pxsys_status_t pxsys_system_status_service_set_network_enabled(
    pxsys_system_status_service_t* service, pxsys_network_type_t network,
    uint8_t enabled);
pxsys_status_t pxsys_system_status_service_set_level(
    pxsys_system_status_service_t* service, pxsys_level_control_t control,
    uint8_t percent);
pxsys_status_t pxsys_system_status_service_set_toggle(
    pxsys_system_status_service_t* service, pxsys_toggle_control_t control,
    uint8_t enabled);
pxsys_status_t pxsys_system_status_service_subscribe(
    pxsys_system_status_service_t* service, void* context,
    pxsys_system_status_changed_fn callback);
pxsys_status_t pxsys_system_status_service_unsubscribe(
    pxsys_system_status_service_t* service, void* context,
    pxsys_system_status_changed_fn callback);

#ifdef __cplusplus
}
#endif

#endif
