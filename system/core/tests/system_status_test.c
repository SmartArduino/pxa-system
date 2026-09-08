#include <assert.h>
#include <stdlib.h>

#include "pxsys/system_status.h"

static void* allocate(void* context, size_t size) {
    (void)context;
    return malloc(size);
}

static void release(void* context, void* memory) {
    (void)context;
    free(memory);
}

static void changed(void* context,
                    const pxsys_system_status_snapshot_t* snapshot) {
    unsigned* count = (unsigned*)context;
    assert(snapshot->generation != 0);
    (*count)++;
}

int main(void) {
    pxsys_system_status_snapshot_t snapshot;
    pxsys_system_status_service_config_t config;
    pxsys_system_status_service_t* service = NULL;
    unsigned notifications = 0;
    pxsys_system_status_snapshot_init(&snapshot);
    pxsys_system_status_service_config_init(&config);
    config.allocator.allocate = allocate;
    config.allocator.release = release;
    assert(pxsys_system_status_service_create(&config, &snapshot, &service) ==
           PXSYS_STATUS_OK);
    assert(pxsys_system_status_service_subscribe(service, &notifications, changed) ==
           PXSYS_STATUS_OK);
    assert(notifications == 1);
    snapshot.time_valid = 1;
    snapshot.hour = 9;
    snapshot.minute = 41;
    snapshot.battery_valid = 1;
    snapshot.battery_percent = 75;
    snapshot.network_type = PXSYS_NETWORK_WIFI;
    snapshot.network_connected = 1;
    snapshot.network_signal_level = 3;
    assert(pxsys_system_status_service_update(service, &snapshot) == PXSYS_STATUS_OK);
    assert(notifications == 2);
    snapshot.struct_size = sizeof(snapshot);
    assert(pxsys_system_status_service_get(service, &snapshot) == PXSYS_STATUS_OK);
    assert(snapshot.battery_percent == 75 && snapshot.hour == 9);
    snapshot.battery_percent = 101;
    assert(pxsys_system_status_service_update(service, &snapshot) ==
           PXSYS_STATUS_INVALID_ARGUMENT);
    assert(pxsys_system_status_service_unsubscribe(service, &notifications, changed) ==
           PXSYS_STATUS_OK);
    assert(pxsys_system_status_service_destroy(service) == PXSYS_STATUS_OK);
    return 0;
}
