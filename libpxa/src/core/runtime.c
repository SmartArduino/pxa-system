#include "common/checked_math.h"
#include "core/runtime_internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

int pxa_runtime_is_valid(const pxa_runtime_t *runtime) {
    return runtime != NULL && runtime->magic == PXA_RUNTIME_MAGIC;
}

static int limits_valid(const pxa_runtime_limits_t *limits) {
    uint32_t buckets;
    if (limits == NULL || limits->struct_size < sizeof(*limits) ||
        limits->max_components == 0 || limits->max_requests == 0 ||
        limits->max_requests_per_component == 0 || limits->max_handles == 0 ||
        limits->max_events == 0 || limits->mailbox_capacity == 0 ||
        limits->event_block_size < 16 || limits->event_block_count == 0 ||
        limits->max_requests_per_component > limits->max_requests ||
        limits->mailbox_capacity > limits->max_events ||
        limits->reliable_event_reserve > limits->mailbox_capacity ||
        limits->max_revoked_authorities_per_component == 0 ||
        limits->max_services == 0) {
        return 0;
    }
    buckets =
        pxa_request_table_bucket_count(limits->max_requests_per_component);
    return buckets != 0 &&
           (size_t)buckets <= SIZE_MAX / limits->max_components;
}

void pxa_runtime_limits_init(pxa_runtime_limits_t *limits) {
    if (limits == NULL) return;
    memset(limits, 0, sizeof(*limits));
    limits->struct_size = sizeof(*limits);
    limits->max_components = 8;
    limits->max_requests = 64;
    limits->max_requests_per_component = 16;
    limits->max_handles = 64;
    limits->max_events = 64;
    limits->mailbox_capacity = 16;
    limits->reliable_event_reserve = 4;
    limits->max_revoked_authorities_per_component = 16;
    limits->max_services = 24;
    limits->event_block_size = 64;
    limits->event_block_count = 128;
}

size_t pxa_runtime_workspace_size(const pxa_runtime_limits_t *limits) {
    size_t total = PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    size_t bucket_total;
    size_t service_bucket_total;
    size_t authority_total;
    if (!limits_valid(limits)) return 0;
    bucket_total = (size_t)pxa_request_table_bucket_count(
                       limits->max_requests_per_component) *
                   limits->max_components;
    service_bucket_total =
        pxa_service_registry_bucket_count(limits->max_services);
    authority_total = (size_t)limits->max_revoked_authorities_per_component *
                      limits->max_components;
    if (!pxa_internal_add_array_size(
            &total, 1, sizeof(pxa_runtime_t),
            PXA_INTERNAL_WORKSPACE_ALIGNMENT) ||
        !pxa_internal_add_array_size(
            &total, limits->max_components, sizeof(pxa_component_slot_t),
            PXA_INTERNAL_WORKSPACE_ALIGNMENT) ||
        !pxa_internal_add_array_size(
            &total, limits->max_requests, sizeof(pxa_request_slot_t),
            PXA_INTERNAL_WORKSPACE_ALIGNMENT) ||
        !pxa_internal_add_array_size(
            &total, limits->max_handles, sizeof(pxa_resource_slot_t),
            PXA_INTERNAL_WORKSPACE_ALIGNMENT) ||
        !pxa_internal_add_array_size(
            &total, limits->max_events, sizeof(pxa_event_slot_t),
            PXA_INTERNAL_WORKSPACE_ALIGNMENT) ||
        !pxa_internal_add_array_size(
            &total, limits->max_services, sizeof(pxa_service_slot_t),
            PXA_INTERNAL_WORKSPACE_ALIGNMENT) ||
        !pxa_internal_add_array_size(&total, bucket_total, sizeof(uint32_t),
                                     sizeof(uint32_t)) ||
        !pxa_internal_add_array_size(&total, service_bucket_total,
                                     sizeof(uint32_t), sizeof(uint32_t)) ||
        !pxa_internal_add_array_size(&total, authority_total, sizeof(uint64_t),
                                     sizeof(uint64_t)) ||
        !pxa_internal_add_array_size(&total, limits->event_block_count,
                                     sizeof(uint32_t), sizeof(uint32_t)) ||
        !pxa_internal_add_array_size(
            &total, limits->event_block_count, limits->event_block_size,
            PXA_INTERNAL_WORKSPACE_ALIGNMENT)) {
        return 0;
    }
    return total;
}

pxa_status_t pxa_runtime_init(void *workspace, size_t workspace_size,
                              const pxa_runtime_limits_t *limits,
                              pxa_runtime_t **output) {
    uint8_t *cursor;
    const uint8_t *end;
    pxa_runtime_t *runtime;
    size_t required;
    size_t bucket_total;
    size_t service_bucket_total;
    size_t authority_total;
    pxa_service_slot_t *service_slots;
    uint32_t *service_buckets;
    pxa_event_slot_t *event_slots;
    pxa_request_slot_t *request_slots;
    pxa_resource_slot_t *resource_slots;
    uint32_t *request_buckets;
    uint32_t *event_block_next;
    uint8_t *event_block_data;
    uint32_t index;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    required = pxa_runtime_workspace_size(limits);
    if (workspace == NULL || required == 0 || workspace_size < required ||
        (uintptr_t)workspace > UINTPTR_MAX - workspace_size) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    cursor = (uint8_t *)workspace;
    end = cursor + workspace_size;
    runtime = (pxa_runtime_t *)pxa_internal_layout_take(
        &cursor, end, 1, sizeof(*runtime), PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    if (runtime == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    memset(runtime, 0, sizeof(*runtime));
    runtime->limits = *limits;
    bucket_total =
        (size_t)pxa_request_table_bucket_count(
            limits->max_requests_per_component) * limits->max_components;
    service_bucket_total =
        pxa_service_registry_bucket_count(limits->max_services);
    authority_total = (size_t)limits->max_revoked_authorities_per_component *
                      limits->max_components;
    runtime->components = (pxa_component_slot_t *)pxa_internal_layout_take(
        &cursor, end, limits->max_components, sizeof(*runtime->components),
        PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    request_slots = (pxa_request_slot_t *)pxa_internal_layout_take(
        &cursor, end, limits->max_requests, sizeof(*request_slots),
        PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    resource_slots = (pxa_resource_slot_t *)pxa_internal_layout_take(
        &cursor, end, limits->max_handles, sizeof(*resource_slots),
        PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    event_slots = (pxa_event_slot_t *)pxa_internal_layout_take(
        &cursor, end, limits->max_events, sizeof(*event_slots),
        PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    service_slots = (pxa_service_slot_t *)pxa_internal_layout_take(
        &cursor, end, limits->max_services, sizeof(*service_slots),
        PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    request_buckets = (uint32_t *)pxa_internal_layout_take(
        &cursor, end, bucket_total, sizeof(*request_buckets),
        sizeof(uint32_t));
    service_buckets = (uint32_t *)pxa_internal_layout_take(
        &cursor, end, service_bucket_total, sizeof(*service_buckets),
        sizeof(uint32_t));
    runtime->revoked_authorities = (uint64_t *)pxa_internal_layout_take(
        &cursor, end, authority_total, sizeof(*runtime->revoked_authorities),
        sizeof(uint64_t));
    event_block_next = (uint32_t *)pxa_internal_layout_take(
        &cursor, end, limits->event_block_count, sizeof(*event_block_next),
        sizeof(uint32_t));
    event_block_data = (uint8_t *)pxa_internal_layout_take(
        &cursor, end, limits->event_block_count, limits->event_block_size,
        PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    if (runtime->components == NULL || request_slots == NULL ||
        resource_slots == NULL || event_slots == NULL ||
        service_slots == NULL || request_buckets == NULL ||
        service_buckets == NULL ||
        runtime->revoked_authorities == NULL || event_block_next == NULL ||
        event_block_data == NULL) {
        memset(runtime, 0, sizeof(*runtime));
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    memset(runtime->components, 0,
           limits->max_components * sizeof(*runtime->components));
    pxa_request_table_init(
        &runtime->requests, request_slots, limits->max_requests,
        request_buckets,
        pxa_request_table_bucket_count(limits->max_requests_per_component),
        limits->max_components);
    pxa_resource_table_init(&runtime->resources, resource_slots,
                            limits->max_handles);
    pxa_event_pool_init(&runtime->event_pool, event_slots, limits->max_events,
                        event_block_next, event_block_data,
                        limits->event_block_size, limits->event_block_count);
    pxa_service_registry_init(&runtime->services, service_slots,
                              limits->max_services, service_buckets,
                              (uint32_t)service_bucket_total);
    memset(runtime->revoked_authorities, 0,
           authority_total * sizeof(*runtime->revoked_authorities));
    for (index = 0; index < limits->max_components; ++index) {
        runtime->components[index].generation = 1;
        runtime->components[index].next_free =
            index + 1u < limits->max_components
                ? index + 1u
                : PXA_CORE_INDEX_NONE;
    }
    runtime->component_free_head = 0;
    runtime->magic = PXA_RUNTIME_MAGIC;
    *output = runtime;
    return PXA_STATUS_OK;
}

void pxa_runtime_deinit(pxa_runtime_t *runtime) {
    uint32_t index;
    if (!pxa_runtime_is_valid(runtime)) return;
    for (index = 0; index < runtime->limits.max_components; ++index) {
        if (runtime->components[index].occupied &&
            runtime->components[index].state != PXA_COMPONENT_STOPPED) {
            runtime->components[index].stop_reason = PXA_STOP_SHUTDOWN;
            pxa_runtime_cleanup_component(runtime, index);
        }
    }
    runtime->magic = 0;
}

pxa_status_t pxa_runtime_usage_snapshot(const pxa_runtime_t *runtime,
                                        pxa_runtime_usage_t *output) {
    if (!pxa_runtime_is_valid(runtime) || output == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    output->current_components = runtime->component_count;
    output->peak_components = runtime->component_peak;
    output->current_requests = runtime->requests.count;
    output->peak_requests = runtime->requests.peak;
    output->current_handles = runtime->resources.count;
    output->peak_handles = runtime->resources.peak;
    output->current_events = runtime->event_pool.event_count;
    output->peak_events = runtime->event_pool.event_peak;
    output->current_event_blocks =
        (uint16_t)(runtime->event_pool.block_count -
                   runtime->event_pool.block_free_count);
    output->peak_event_blocks = runtime->event_pool.block_peak;
    output->registered_services = runtime->services.count;
    return PXA_STATUS_OK;
}
