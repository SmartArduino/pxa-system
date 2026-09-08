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
} pxsys_system_status_snapshot_t;

typedef void (*pxsys_system_status_changed_fn)(
    void* context, const pxsys_system_status_snapshot_t* snapshot);

typedef struct {
    uint32_t struct_size;
    size_t max_observers;
    pxsys_allocator_t allocator;
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
