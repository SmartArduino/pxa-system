#ifndef PXA_CORE_SERVICE_REGISTRY_H
#define PXA_CORE_SERVICE_REGISTRY_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/service.h"

typedef struct {
    pxa_service_ops_t operations;
    uint8_t occupied;
} pxa_service_slot_t;

typedef struct {
    pxa_service_slot_t *slots;
    uint32_t *buckets;
    uint32_t bucket_count;
    uint16_t capacity;
    uint16_t count;
} pxa_service_registry_t;

uint32_t pxa_service_registry_bucket_count(uint16_t capacity);
void pxa_service_registry_init(pxa_service_registry_t *registry,
                               pxa_service_slot_t *slots, uint16_t capacity,
                               uint32_t *buckets, uint32_t bucket_count);
const pxa_service_ops_t *pxa_service_registry_find(
    const pxa_service_registry_t *registry, uint16_t service_id);
pxa_status_t pxa_service_registry_register(
    pxa_service_registry_t *registry, const pxa_service_ops_t *service);
void pxa_service_registry_notify_component_stopped(
    const pxa_service_registry_t *registry, pxa_runtime_t *runtime,
    pxa_component_t component);

#endif
