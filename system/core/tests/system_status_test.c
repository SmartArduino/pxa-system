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

static pxsys_status_t set_network(void* context,
                                  pxsys_network_type_t network,
                                  uint8_t enabled) {
    unsigned* calls = (unsigned*)context;
    assert(network == PXSYS_NETWORK_WIFI);
    assert(enabled == 0);
    (*calls)++;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t set_level(void* context, pxsys_level_control_t control,
                                uint8_t percent) {
    unsigned* calls = (unsigned*)context;
    assert(control == PXSYS_LEVEL_CONTROL_BRIGHTNESS);
    assert(percent == 42);
    (*calls)++;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t set_toggle(void* context,
                                 pxsys_toggle_control_t control,
                                 uint8_t enabled) {
    unsigned* calls = (unsigned*)context;
    assert(control == PXSYS_TOGGLE_BLUETOOTH);
    assert(enabled == 1);
    (*calls)++;
    return PXSYS_STATUS_OK;
}

int main(void) {
    pxsys_system_status_snapshot_t snapshot;
    pxsys_system_status_service_config_t config;
    pxsys_system_status_service_t* service = NULL;
    unsigned notifications = 0;
    unsigned control_calls = 0;
    pxsys_system_status_snapshot_init(&snapshot);
    pxsys_system_status_service_config_init(&config);
    config.allocator.allocate = allocate;
    config.allocator.release = release;
    config.network_control_context = &control_calls;
    config.set_network_enabled = set_network;
    config.control_context = &control_calls;
    config.set_level = set_level;
    config.set_toggle = set_toggle;
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
    assert(pxsys_system_status_service_set_network_enabled(
               service, PXSYS_NETWORK_WIFI, 0) == PXSYS_STATUS_OK);
    assert(control_calls == 1);
    assert(pxsys_system_status_service_set_level(
               service, PXSYS_LEVEL_CONTROL_BRIGHTNESS, 42) ==
           PXSYS_STATUS_OK);
    assert(pxsys_system_status_service_set_toggle(
               service, PXSYS_TOGGLE_BLUETOOTH, 1) == PXSYS_STATUS_OK);
    assert(control_calls == 3);
    snapshot.battery_percent = 101;
    assert(pxsys_system_status_service_update(service, &snapshot) ==
           PXSYS_STATUS_INVALID_ARGUMENT);
    assert(pxsys_system_status_service_unsubscribe(service, &notifications, changed) ==
           PXSYS_STATUS_OK);
    assert(pxsys_system_status_service_destroy(service) == PXSYS_STATUS_OK);
    return 0;
}
