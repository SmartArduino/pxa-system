#include "core/runtime_internal.h"

#include "core/slot_token.h"

#include <string.h>

pxa_component_slot_t *pxa_runtime_find_component(
    pxa_runtime_t *runtime, pxa_component_t component, uint32_t *index_out) {
    uint32_t index;
    uint16_t generation;
    pxa_component_slot_t *slot;
    if (!pxa_runtime_is_valid(runtime) ||
        !pxa_internal_slot_token_decode(component,
                                        runtime->limits.max_components,
                                        &index, &generation)) {
        return NULL;
    }
    slot = &runtime->components[index];
    if (!slot->occupied || slot->generation != generation) return NULL;
    if (index_out != NULL) *index_out = index;
    return slot;
}

const pxa_component_slot_t *pxa_runtime_find_component_const(
    const pxa_runtime_t *runtime, pxa_component_t component,
    uint32_t *index_out) {
    return pxa_runtime_find_component((pxa_runtime_t *)runtime, component,
                                      index_out);
}

int pxa_runtime_authority_is_revoked(const pxa_runtime_t *runtime,
                                     uint32_t component_index,
                                     pxa_authority_t authority) {
    const pxa_component_slot_t *component =
        &runtime->components[component_index];
    const uint64_t *values = runtime->revoked_authorities +
        (size_t)component_index *
            runtime->limits.max_revoked_authorities_per_component;
    uint16_t index;
    for (index = 0; index < component->revoked_count; ++index) {
        if (values[index] == authority) return 1;
    }
    return 0;
}

void pxa_runtime_cleanup_component(pxa_runtime_t *runtime,
                                   uint32_t component_index) {
    pxa_component_slot_t *component = &runtime->components[component_index];
    pxa_request_table_clear_owner(&runtime->requests, &component->requests,
                                  component_index, &runtime->event_pool);
    pxa_event_mailbox_clear(&runtime->event_pool, &component->mailbox);
    component->revoked_count = 0;
    component->callback = PXA_GUEST_CALLBACK_NONE;
    component->state = PXA_COMPONENT_STOPPED;
    pxa_resource_table_detach_owner(&runtime->resources,
                                    &component->resources, component_index);
    pxa_resource_table_finish_owner(&runtime->resources, component_index);
    pxa_service_registry_notify_component_stopped(
        &runtime->services, runtime,
        pxa_internal_slot_token_encode(component_index,
                                       component->generation));
}

pxa_status_t pxa_component_create(pxa_runtime_t *runtime, uint64_t instance_id,
                                  pxa_component_t *output) {
    uint32_t index;
    uint32_t scan;
    pxa_component_slot_t *component;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = PXA_COMPONENT_INVALID;
    if (!pxa_runtime_is_valid(runtime) || instance_id == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    for (scan = 0; scan < runtime->limits.max_components; ++scan) {
        if (runtime->components[scan].occupied &&
            runtime->components[scan].instance_id == instance_id) {
            return PXA_STATUS_BAD_STATE;
        }
    }
    if (runtime->component_free_head == PXA_CORE_INDEX_NONE) {
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    index = runtime->component_free_head;
    component = &runtime->components[index];
    runtime->component_free_head = component->next_free;
    component->instance_id = instance_id;
    component->occupied = 1;
    component->state = PXA_COMPONENT_CREATED;
    component->callback = PXA_GUEST_CALLBACK_NONE;
    component->stop_reason = PXA_STOP_NORMAL;
    pxa_request_owner_init(&component->requests);
    pxa_resource_owner_init(&component->resources);
    pxa_event_mailbox_init(&component->mailbox);
    component->revoked_count = 0;
    ++runtime->component_count;
    if (runtime->component_count > runtime->component_peak)
        runtime->component_peak = runtime->component_count;
    *output = pxa_internal_slot_token_encode(index, component->generation);
    return PXA_STATUS_OK;
}

pxa_status_t pxa_component_remove(pxa_runtime_t *runtime,
                                  pxa_component_t component_ref) {
    uint32_t index;
    pxa_component_slot_t *component =
        pxa_runtime_find_component(runtime, component_ref, &index);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state != PXA_COMPONENT_STOPPED) return PXA_STATUS_BAD_STATE;
    component->occupied = 0;
    component->instance_id = 0;
    if (runtime->component_count != 0) --runtime->component_count;
    if (component->generation == UINT16_MAX) {
        component->retired = 1;
    } else {
        component->generation++;
        component->next_free = runtime->component_free_head;
        runtime->component_free_head = index;
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_component_begin_start(pxa_runtime_t *runtime,
                                       pxa_component_t component_ref) {
    pxa_component_slot_t *component =
        pxa_runtime_find_component(runtime, component_ref, NULL);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state != PXA_COMPONENT_CREATED ||
        component->callback != PXA_GUEST_CALLBACK_NONE) {
        return PXA_STATUS_BAD_STATE;
    }
    component->state = PXA_COMPONENT_STARTING;
    component->callback = PXA_GUEST_CALLBACK_START;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_component_finish_start(pxa_runtime_t *runtime,
                                        pxa_component_t component_ref,
                                        pxa_status_t result) {
    uint32_t index;
    pxa_component_slot_t *component;
    if (!pxa_status_is_known(result)) return PXA_STATUS_INVALID_ARGUMENT;
    component = pxa_runtime_find_component(runtime, component_ref, &index);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state != PXA_COMPONENT_STARTING ||
        component->callback != PXA_GUEST_CALLBACK_START) {
        return PXA_STATUS_BAD_STATE;
    }
    component->callback = PXA_GUEST_CALLBACK_NONE;
    if (result == PXA_STATUS_OK) {
        component->state = PXA_COMPONENT_RUNNING;
    } else {
        component->stop_reason = PXA_STOP_FAULT;
        pxa_runtime_cleanup_component(runtime, index);
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_component_begin_event(pxa_runtime_t *runtime,
                                       pxa_component_t component_ref) {
    pxa_component_slot_t *component =
        pxa_runtime_find_component(runtime, component_ref, NULL);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state != PXA_COMPONENT_RUNNING ||
        component->callback != PXA_GUEST_CALLBACK_NONE) {
        return PXA_STATUS_BAD_STATE;
    }
    component->callback = PXA_GUEST_CALLBACK_EVENT;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_component_finish_event(pxa_runtime_t *runtime,
                                        pxa_component_t component_ref,
                                        int32_t result) {
    pxa_component_slot_t *component =
        pxa_runtime_find_component(runtime, component_ref, NULL);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if ((component->state != PXA_COMPONENT_RUNNING &&
         component->state != PXA_COMPONENT_STOP_REQUESTED) ||
        component->callback != PXA_GUEST_CALLBACK_EVENT || result > 1 ||
        (result < 0 && !pxa_status_is_known(result))) {
        return PXA_STATUS_BAD_STATE;
    }
    component->callback = PXA_GUEST_CALLBACK_NONE;
    if (result < 0 && component->state == PXA_COMPONENT_RUNNING) {
        component->state = PXA_COMPONENT_STOP_REQUESTED;
        component->stop_reason = PXA_STOP_FAULT;
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_component_request_stop(pxa_runtime_t *runtime,
                                        pxa_component_t component_ref,
                                        pxa_stop_reason_t reason) {
    pxa_component_slot_t *component =
        pxa_runtime_find_component(runtime, component_ref, NULL);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state == PXA_COMPONENT_STOP_REQUESTED) return PXA_STATUS_OK;
    if (component->state != PXA_COMPONENT_RUNNING) return PXA_STATUS_BAD_STATE;
    component->state = PXA_COMPONENT_STOP_REQUESTED;
    component->stop_reason = reason;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_component_begin_stop(pxa_runtime_t *runtime,
                                      pxa_component_t component_ref) {
    pxa_component_slot_t *component =
        pxa_runtime_find_component(runtime, component_ref, NULL);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state != PXA_COMPONENT_STOP_REQUESTED ||
        component->callback != PXA_GUEST_CALLBACK_NONE) {
        return PXA_STATUS_BAD_STATE;
    }
    component->state = PXA_COMPONENT_STOPPING;
    component->callback = PXA_GUEST_CALLBACK_STOP;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_component_finish_stop(pxa_runtime_t *runtime,
                                       pxa_component_t component_ref) {
    uint32_t index;
    pxa_component_slot_t *component =
        pxa_runtime_find_component(runtime, component_ref, &index);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state != PXA_COMPONENT_STOPPING ||
        component->callback != PXA_GUEST_CALLBACK_STOP) {
        return PXA_STATUS_BAD_STATE;
    }
    pxa_runtime_cleanup_component(runtime, index);
    return PXA_STATUS_OK;
}

pxa_status_t pxa_component_abort(pxa_runtime_t *runtime,
                                 pxa_component_t component_ref,
                                 pxa_stop_reason_t reason) {
    uint32_t index;
    pxa_component_slot_t *component =
        pxa_runtime_find_component(runtime, component_ref, &index);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state == PXA_COMPONENT_STOPPED) return PXA_STATUS_OK;
    component->stop_reason = reason;
    pxa_runtime_cleanup_component(runtime, index);
    return PXA_STATUS_OK;
}

pxa_status_t pxa_component_validate_import(const pxa_runtime_t *runtime,
                                           pxa_component_t component_ref) {
    const pxa_component_slot_t *component =
        pxa_runtime_find_component_const(runtime, component_ref, NULL);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if ((component->state == PXA_COMPONENT_STARTING &&
         component->callback == PXA_GUEST_CALLBACK_START) ||
        ((component->state == PXA_COMPONENT_RUNNING ||
          component->state == PXA_COMPONENT_STOP_REQUESTED) &&
         component->callback == PXA_GUEST_CALLBACK_EVENT)) {
        return PXA_STATUS_OK;
    }
    return PXA_STATUS_BAD_STATE;
}

pxa_status_t pxa_component_snapshot(const pxa_runtime_t *runtime,
                                    pxa_component_t component_ref,
                                    pxa_component_snapshot_t *output) {
    const pxa_component_slot_t *component;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    component = pxa_runtime_find_component_const(runtime, component_ref, NULL);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    output->state = component->state;
    output->callback = component->callback;
    output->stop_reason = component->stop_reason;
    output->pending_requests = pxa_request_owner_count(&component->requests);
    output->open_handles = pxa_resource_owner_count(&component->resources);
    output->queued_events = pxa_event_mailbox_count(&component->mailbox);
    return PXA_STATUS_OK;
}

static void close_authority_resources(pxa_runtime_t *runtime,
                                      pxa_component_slot_t *component,
                                      uint32_t component_index,
                                      pxa_authority_t authority) {
    pxa_resource_table_detach_authority(
        &runtime->resources, &component->resources, component_index,
        authority);
    pxa_request_table_revoke_authority(
        &runtime->requests, &component->requests, component_index,
        &runtime->event_pool, &component->mailbox,
        runtime->limits.mailbox_capacity, authority);
    pxa_resource_table_finish_authority(&runtime->resources, component_index,
                                        authority);
}

pxa_status_t pxa_authority_revoke(pxa_runtime_t *runtime,
                                  pxa_component_t component_ref,
                                  pxa_authority_t authority) {
    uint32_t component_index;
    pxa_component_slot_t *component;
    uint64_t *authorities;
    if (authority == 0) return PXA_STATUS_INVALID_ARGUMENT;
    component =
        pxa_runtime_find_component(runtime, component_ref, &component_index);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state != PXA_COMPONENT_STARTING &&
        component->state != PXA_COMPONENT_RUNNING &&
        component->state != PXA_COMPONENT_STOP_REQUESTED) {
        return PXA_STATUS_BAD_STATE;
    }
    if (pxa_runtime_authority_is_revoked(runtime, component_index, authority)) {
        return PXA_STATUS_OK;
    }
    if (component->revoked_count >=
        runtime->limits.max_revoked_authorities_per_component) {
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    authorities = runtime->revoked_authorities +
        (size_t)component_index *
            runtime->limits.max_revoked_authorities_per_component;
    authorities[component->revoked_count++] = authority;
    close_authority_resources(runtime, component, component_index, authority);
    return PXA_STATUS_OK;
}

pxa_status_t pxa_runtime_release_authority(pxa_runtime_t *runtime,
                                           pxa_component_t component_ref,
                                           pxa_authority_t authority) {
    uint32_t component_index;
    pxa_component_slot_t *component;
    if (authority == 0) return PXA_STATUS_INVALID_ARGUMENT;
    component =
        pxa_runtime_find_component(runtime, component_ref, &component_index);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state != PXA_COMPONENT_STARTING &&
        component->state != PXA_COMPONENT_RUNNING &&
        component->state != PXA_COMPONENT_STOP_REQUESTED) {
        return PXA_STATUS_BAD_STATE;
    }
    close_authority_resources(runtime, component, component_index, authority);
    return PXA_STATUS_OK;
}
