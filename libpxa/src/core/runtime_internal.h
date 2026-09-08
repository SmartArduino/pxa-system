#ifndef PXA_CORE_RUNTIME_INTERNAL_H
#define PXA_CORE_RUNTIME_INTERNAL_H

#include <stdint.h>

#include "pxa/runtime.h"

#include "core/event_internal.h"
#include "core/handle_internal.h"
#include "core/request_internal.h"
#include "core/service_registry.h"

#define PXA_RUNTIME_MAGIC UINT32_C(0x50584152)
#define PXA_CORE_INDEX_NONE UINT32_MAX

typedef struct {
    uint64_t instance_id;
    uint32_t next_free;
    pxa_request_owner_t requests;
    pxa_resource_owner_t resources;
    pxa_event_mailbox_t mailbox;
    uint16_t generation;
    uint16_t revoked_count;
    uint8_t occupied;
    uint8_t retired;
    pxa_component_state_t state;
    pxa_guest_callback_t callback;
    pxa_stop_reason_t stop_reason;
} pxa_component_slot_t;

struct pxa_runtime {
    uint32_t magic;
    pxa_runtime_limits_t limits;
    uint32_t component_free_head;
    pxa_component_slot_t *components;
    pxa_request_table_t requests;
    pxa_resource_table_t resources;
    pxa_event_pool_t event_pool;
    pxa_service_registry_t services;
    uint64_t *revoked_authorities;
    uint16_t component_count;
    uint16_t component_peak;
};

int pxa_runtime_is_valid(const pxa_runtime_t *runtime);
pxa_component_slot_t *pxa_runtime_find_component(
    pxa_runtime_t *runtime, pxa_component_t component, uint32_t *index_out);
const pxa_component_slot_t *pxa_runtime_find_component_const(
    const pxa_runtime_t *runtime, pxa_component_t component,
    uint32_t *index_out);
int pxa_runtime_authority_is_revoked(const pxa_runtime_t *runtime,
                                     uint32_t component_index,
                                     pxa_authority_t authority);
pxa_status_t pxa_runtime_release_authority(pxa_runtime_t *runtime,
                                           pxa_component_t component,
                                           pxa_authority_t authority);
void pxa_runtime_cleanup_component(pxa_runtime_t *runtime,
                                   uint32_t component_index);

#endif
