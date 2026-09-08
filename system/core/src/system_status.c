#include "pxsys/system_status.h"

#include <string.h>

#define PXSYS_SYSTEM_STATUS_MAGIC UINT32_C(0x50585342)

typedef struct {
    void* context;
    pxsys_system_status_changed_fn callback;
} status_observer_t;

struct pxsys_system_status_service {
    uint32_t magic;
    size_t observer_capacity;
    size_t observer_count;
    uint8_t notifying;
    pxsys_allocator_t allocator;
    pxsys_system_status_snapshot_t current;
    status_observer_t* observers;
};

static int service_valid(const pxsys_system_status_service_t* service) {
    return service != NULL && service->magic == PXSYS_SYSTEM_STATUS_MAGIC;
}

static int snapshot_valid(const pxsys_system_status_snapshot_t* snapshot) {
    return snapshot != NULL && snapshot->struct_size >= sizeof(*snapshot) &&
           snapshot->time_valid <= 1 && snapshot->hour < 24 &&
           snapshot->minute < 60 && snapshot->battery_valid <= 1 &&
           snapshot->battery_percent <= 100 && snapshot->charging <= 1 &&
           snapshot->network_connected <= 1 &&
           snapshot->network_signal_level <= 4 &&
           snapshot->network_type <= PXSYS_NETWORK_ETHERNET;
}

static int snapshot_equal(const pxsys_system_status_snapshot_t* left,
                          const pxsys_system_status_snapshot_t* right) {
    return left->time_valid == right->time_valid && left->hour == right->hour &&
           left->minute == right->minute &&
           left->battery_valid == right->battery_valid &&
           left->battery_percent == right->battery_percent &&
           left->charging == right->charging &&
           left->network_connected == right->network_connected &&
           left->network_signal_level == right->network_signal_level &&
           left->network_type == right->network_type;
}

void pxsys_system_status_snapshot_init(pxsys_system_status_snapshot_t* snapshot) {
    if (snapshot == NULL) return;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->struct_size = sizeof(*snapshot);
}

void pxsys_system_status_service_config_init(
    pxsys_system_status_service_config_t* config) {
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->max_observers = 16;
    config->allocator.struct_size = sizeof(config->allocator);
}

pxsys_status_t pxsys_system_status_service_create(
    const pxsys_system_status_service_config_t* config,
    const pxsys_system_status_snapshot_t* initial,
    pxsys_system_status_service_t** output) {
    pxsys_system_status_service_t* service;
    if (output == NULL) return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (config == NULL || config->struct_size < sizeof(*config) ||
        config->max_observers == 0 ||
        config->max_observers > SIZE_MAX / sizeof(status_observer_t) ||
        config->allocator.struct_size < sizeof(config->allocator) ||
        config->allocator.allocate == NULL || config->allocator.release == NULL ||
        !snapshot_valid(initial)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    service = (pxsys_system_status_service_t*)config->allocator.allocate(
        config->allocator.context, sizeof(*service));
    if (service == NULL) return PXSYS_STATUS_NO_MEMORY;
    memset(service, 0, sizeof(*service));
    service->observers = (status_observer_t*)config->allocator.allocate(
        config->allocator.context,
        config->max_observers * sizeof(*service->observers));
    if (service->observers == NULL) {
        config->allocator.release(config->allocator.context, service);
        return PXSYS_STATUS_NO_MEMORY;
    }
    memset(service->observers, 0,
           config->max_observers * sizeof(*service->observers));
    service->observer_capacity = config->max_observers;
    service->allocator = config->allocator;
    service->current = *initial;
    service->current.struct_size = sizeof(service->current);
    service->current.generation = 1;
    service->magic = PXSYS_SYSTEM_STATUS_MAGIC;
    *output = service;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_system_status_service_destroy(
    pxsys_system_status_service_t* service) {
    pxsys_allocator_t allocator;
    if (!service_valid(service)) return PXSYS_STATUS_INVALID_ARGUMENT;
    if (service->notifying) return PXSYS_STATUS_BUSY;
    allocator = service->allocator;
    service->magic = 0;
    allocator.release(allocator.context, service->observers);
    allocator.release(allocator.context, service);
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_system_status_service_update(
    pxsys_system_status_service_t* service,
    const pxsys_system_status_snapshot_t* snapshot) {
    size_t index;
    uint64_t generation;
    if (!service_valid(service) || !snapshot_valid(snapshot))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (service->notifying) return PXSYS_STATUS_BUSY;
    if (snapshot_equal(&service->current, snapshot)) return PXSYS_STATUS_OK;
    generation = service->current.generation == UINT64_MAX
                     ? 1 : service->current.generation + 1u;
    service->current = *snapshot;
    service->current.struct_size = sizeof(service->current);
    service->current.generation = generation;
    service->notifying = 1;
    for (index = 0; index < service->observer_capacity; ++index) {
        if (service->observers[index].callback != NULL) {
            service->observers[index].callback(
                service->observers[index].context, &service->current);
        }
    }
    service->notifying = 0;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_system_status_service_get(
    const pxsys_system_status_service_t* service,
    pxsys_system_status_snapshot_t* snapshot) {
    if (!service_valid(service) || snapshot == NULL ||
        snapshot->struct_size < sizeof(*snapshot))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *snapshot = service->current;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_system_status_service_subscribe(
    pxsys_system_status_service_t* service, void* context,
    pxsys_system_status_changed_fn callback) {
    size_t index;
    if (!service_valid(service) || callback == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (service->notifying) return PXSYS_STATUS_BUSY;
    for (index = 0; index < service->observer_capacity; ++index) {
        if (service->observers[index].callback == callback &&
            service->observers[index].context == context)
            return PXSYS_STATUS_ALREADY_EXISTS;
    }
    if (service->observer_count == service->observer_capacity)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    for (index = 0; index < service->observer_capacity; ++index) {
        if (service->observers[index].callback == NULL) break;
    }
    service->observers[index].context = context;
    service->observers[index].callback = callback;
    service->observer_count++;
    service->notifying = 1;
    callback(context, &service->current);
    service->notifying = 0;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_system_status_service_unsubscribe(
    pxsys_system_status_service_t* service, void* context,
    pxsys_system_status_changed_fn callback) {
    size_t index;
    if (!service_valid(service) || callback == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (service->notifying) return PXSYS_STATUS_BUSY;
    for (index = 0; index < service->observer_capacity; ++index) {
        if (service->observers[index].callback == callback &&
            service->observers[index].context == context) {
            memset(&service->observers[index], 0, sizeof(service->observers[index]));
            service->observer_count--;
            return PXSYS_STATUS_OK;
        }
    }
    return PXSYS_STATUS_NOT_FOUND;
}
