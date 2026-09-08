#include "core/service_registry.h"
#include "core/runtime_internal.h"

#include <string.h>

#define PXA_SERVICE_INDEX_NONE UINT32_MAX

uint32_t pxa_service_registry_bucket_count(uint16_t capacity) {
    uint32_t target = (uint32_t)capacity * 2u;
    uint32_t result = 1;
    while (result < target && result <= UINT32_MAX / 2u) result *= 2u;
    return result;
}

static uint32_t service_hash(const pxa_service_registry_t *registry,
                             uint16_t service_id) {
    return ((uint32_t)service_id * UINT32_C(2654435761)) &
           (registry->bucket_count - 1u);
}

void pxa_service_registry_init(pxa_service_registry_t *registry,
                               pxa_service_slot_t *slots, uint16_t capacity,
                               uint32_t *buckets, uint32_t bucket_count) {
    uint32_t index;
    if (registry == NULL) return;
    memset(registry, 0, sizeof(*registry));
    registry->slots = slots;
    registry->buckets = buckets;
    registry->capacity = capacity;
    registry->bucket_count = bucket_count;
    if (slots != NULL)
        memset(slots, 0, (size_t)capacity * sizeof(*slots));
    if (buckets != NULL) {
        for (index = 0; index < bucket_count; ++index)
            buckets[index] = PXA_SERVICE_INDEX_NONE;
    }
}

const pxa_service_ops_t *pxa_service_registry_find(
    const pxa_service_registry_t *registry, uint16_t service_id) {
    uint32_t bucket;
    uint32_t probes;
    if (registry == NULL || registry->slots == NULL ||
        registry->buckets == NULL || registry->bucket_count == 0 ||
        (registry->bucket_count & (registry->bucket_count - 1u)) != 0 ||
        service_id == 0) {
        return NULL;
    }
    bucket = service_hash(registry, service_id);
    for (probes = 0; probes < registry->bucket_count; ++probes) {
        uint32_t slot_index = registry->buckets[bucket];
        if (slot_index == PXA_SERVICE_INDEX_NONE) return NULL;
        if (slot_index < registry->count &&
            registry->slots[slot_index].occupied &&
            registry->slots[slot_index].operations.service_id == service_id) {
            return &registry->slots[slot_index].operations;
        }
        bucket = (bucket + 1u) & (registry->bucket_count - 1u);
    }
    return NULL;
}

pxa_status_t pxa_service_registry_register(
    pxa_service_registry_t *registry, const pxa_service_ops_t *service) {
    uint32_t bucket;
    uint32_t probes;
    uint32_t slot_index;
    if (registry == NULL || service == NULL || registry->slots == NULL ||
        registry->buckets == NULL || service->struct_size < sizeof(*service) ||
        service->service_id == 0 || service->control == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (pxa_service_registry_find(registry, service->service_id) != NULL)
        return PXA_STATUS_BAD_STATE;
    if (registry->count >= registry->capacity)
        return PXA_STATUS_RESOURCE_LIMIT;
    bucket = service_hash(registry, service->service_id);
    for (probes = 0; probes < registry->bucket_count; ++probes) {
        if (registry->buckets[bucket] == PXA_SERVICE_INDEX_NONE) break;
        bucket = (bucket + 1u) & (registry->bucket_count - 1u);
    }
    if (probes == registry->bucket_count) return PXA_STATUS_RESOURCE_LIMIT;
    slot_index = registry->count++;
    registry->slots[slot_index].operations = *service;
    registry->slots[slot_index].occupied = 1;
    registry->buckets[bucket] = slot_index;
    return PXA_STATUS_OK;
}

void pxa_service_registry_notify_component_stopped(
    const pxa_service_registry_t *registry, pxa_runtime_t *runtime,
    pxa_component_t component) {
    uint16_t index;
    if (registry == NULL) return;
    for (index = 0; index < registry->count; ++index) {
        const pxa_service_slot_t *slot = &registry->slots[index];
        if (slot->occupied && slot->operations.component_stopped != NULL) {
            slot->operations.component_stopped(slot->operations.context,
                                               runtime, component);
        }
    }
}

pxa_status_t pxa_service_register(pxa_runtime_t *runtime,
                                  const pxa_service_ops_t *service) {
    if (!pxa_runtime_is_valid(runtime)) return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_service_registry_register(&runtime->services, service);
}

pxa_status_t pxa_runtime_control(pxa_runtime_t *runtime,
                                 pxa_component_t component_ref,
                                 const void *message_bytes,
                                 size_t message_size) {
    pxa_message_view_t message;
    const pxa_service_ops_t *service;
    pxa_status_t status;
    status = pxa_component_validate_import(runtime, component_ref);
    if (status != PXA_STATUS_OK) return status;
    status = pxa_message_decode((const uint8_t *)message_bytes, message_size,
                                PXA_MAX_CONTROL_MESSAGE, &message);
    if (status != PXA_STATUS_OK) return status;
    if (message.service == PXA_SERVICE_CORE) {
        if (message.opcode == PXA_CORE_CANCEL_REQUEST) {
            if (message.request_id != 0 || message.payload.size != 4) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            return pxa_request_cancel(runtime, component_ref,
                                      pxa_read_u32(message.payload.data));
        }
        if (message.opcode == PXA_CORE_CLOSE_HANDLE) {
            if (message.request_id != 0 || message.payload.size != 4) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            return pxa_handle_close(runtime, component_ref,
                                    pxa_read_u32(message.payload.data));
        }
    }
    service = pxa_service_registry_find(&runtime->services, message.service);
    if (service == NULL) return PXA_STATUS_UNSUPPORTED;
    return service->control(service->context, runtime, component_ref, &message);
}
