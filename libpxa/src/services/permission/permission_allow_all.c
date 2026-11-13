#include "pxa/permission.h"
#include "common/checked_math.h"
#include "core/runtime_internal.h"

#include <string.h>

/* No declarations, decisions, persistence or prompts. This pool only preserves
 * the ABI rule that closing a permission handle releases its child resources. */
typedef struct {
    struct pxa_permission_service *service;
    pxa_component_t component;
    pxa_authority_t authority;
    uint16_t next_free;
    uint8_t active;
} issued_t;

struct pxa_permission_service {
    pxa_runtime_t *runtime;
    issued_t *issued;
    pxa_authority_t next_authority;
    uint16_t count, free_head;
    uint8_t registered;
};

static int valid(const pxa_permission_service_t *service) {
    return service != NULL && service->runtime != NULL;
}

size_t pxa_permission_service_workspace_size(const pxa_permission_config_t *config) {
    if (!config || config->struct_size < sizeof(*config)) return 0;
    size_t size = PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    return pxa_internal_add_array_size(&size, 1, sizeof(pxa_permission_service_t), PXA_INTERNAL_WORKSPACE_ALIGNMENT) &&
        pxa_internal_add_array_size(&size, config->max_authorities ? config->max_authorities : 64u,
            sizeof(issued_t), PXA_INTERNAL_WORKSPACE_ALIGNMENT) ? size : 0;
}

pxa_status_t pxa_permission_service_init(void *workspace, size_t size,
    pxa_runtime_t *runtime, const pxa_permission_config_t *config,
    pxa_permission_service_t **output) {
    if (!output) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    size_t needed = pxa_permission_service_workspace_size(config);
    if (!workspace || !runtime || !needed || size < needed ||
        (uintptr_t)workspace > UINTPTR_MAX - size) return PXA_STATUS_INVALID_ARGUMENT;
    uint8_t *cursor = workspace;
    pxa_permission_service_t *service = pxa_internal_layout_take(&cursor,
        cursor + size, 1, sizeof(*service), PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    if (!service) return PXA_STATUS_INVALID_ARGUMENT;
    memset(service, 0, sizeof(*service));
    service->runtime = runtime;
    service->count = config->max_authorities ? config->max_authorities : 64u;
    service->next_authority = 1;
    service->issued = pxa_internal_layout_take(&cursor, (uint8_t *)workspace + size,
        service->count, sizeof(issued_t), PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    if (!service->issued) return PXA_STATUS_INVALID_ARGUMENT;
    memset(service->issued, 0, service->count * sizeof(issued_t));
    for (uint16_t i = 0; i < service->count; ++i)
        service->issued[i].next_free = i + 1u < service->count ? (uint16_t)(i + 1u) : UINT16_MAX;
    *output = service;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_permission_policy_load(pxa_permission_service_t *service) {
    return valid(service) ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
}

int pxa_permission_can_activate(const pxa_permission_service_t *service) { return valid(service); }

pxa_permission_decision_t pxa_permission_get(const pxa_permission_service_t *service,
    pxa_bytes_t name, pxa_bytes_t scope) {
    (void)name; (void)scope;
    return valid(service) ? PXA_PERMISSION_ALLOW : PXA_PERMISSION_DENY;
}

pxa_status_t pxa_permission_set(pxa_permission_service_t *service,
    pxa_bytes_t name, pxa_bytes_t scope, pxa_permission_decision_t decision) {
    (void)name; (void)scope; (void)decision;
    return valid(service) ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
}

pxa_status_t pxa_permission_prompt_complete(pxa_permission_service_t *service,
    pxa_component_t component, pxa_request_token_t token, pxa_permission_decision_t decision) {
    (void)service; (void)component; (void)token; (void)decision;
    return PXA_STATUS_NOT_FOUND;
}

pxa_status_t pxa_permission_revoke(pxa_permission_service_t *service,
    pxa_bytes_t name, pxa_bytes_t scope, pxa_component_t *affected,
    size_t capacity, size_t *count) {
    (void)name; (void)scope; (void)affected; (void)capacity;
    if (!valid(service) || !count) return PXA_STATUS_INVALID_ARGUMENT;
    *count = 0;
    return PXA_STATUS_OK;
}

static pxa_status_t resolve(const pxa_permission_service_t *service,
    pxa_component_t component, const pxa_resource_t *resource, pxa_authority_t *authority) {
    issued_t *issued = resource->context;
    if (!issued || !issued->active || issued->service != service || issued->component != component)
        return PXA_STATUS_DENIED;
    *authority = issued->authority;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_permission_resolve64(const pxa_permission_service_t *service,
    pxa_component_t component, pxa_handle64_t handle, pxa_bytes_t name,
    pxa_bytes_t scope, pxa_authority_t *authority) {
    (void)name; (void)scope;
    if (!valid(service) || !authority) return PXA_STATUS_INVALID_ARGUMENT;
    *authority = 0;
    pxa_resource_t resource;
    pxa_status_t status = pxa_handle_get64(service->runtime, component, handle,
        PXA_RESOURCE_PERMISSION, &resource);
    return status == PXA_STATUS_OK ? resolve(service, component, &resource, authority) : status;
}

pxa_status_t pxa_permission_resolve(const pxa_permission_service_t *service,
    pxa_component_t component, pxa_handle_t handle, pxa_bytes_t name,
    pxa_bytes_t scope, pxa_authority_t *authority) {
    (void)name; (void)scope;
    if (!valid(service) || !authority) return PXA_STATUS_INVALID_ARGUMENT;
    *authority = 0;
    pxa_resource_t resource;
    pxa_status_t status = pxa_handle_get(service->runtime, component, handle,
        PXA_RESOURCE_PERMISSION, &resource);
    return status == PXA_STATUS_OK ? resolve(service, component, &resource, authority) : status;
}

static void release_issued(issued_t *issued) {
    pxa_permission_service_t *service = issued->service;
    uint16_t index = (uint16_t)(issued - service->issued);
    memset(issued, 0, sizeof(*issued));
    issued->next_free = service->free_head;
    service->free_head = index;
}

static void closed(void *context) {
    issued_t *issued = context;
    if (!issued || !issued->active) return;
    (void)pxa_runtime_release_authority(issued->service->runtime, issued->component, issued->authority);
    if (issued->active) release_issued(issued);
}

static pxa_status_t acquire(pxa_permission_service_t *service,
    pxa_component_t component, uint16_t wire, pxa_handle64_t *handle) {
    if (service->free_head == UINT16_MAX || service->next_authority == UINT64_MAX)
        return PXA_STATUS_RESOURCE_LIMIT;
    issued_t *issued = &service->issued[service->free_head];
    service->free_head = issued->next_free;
    issued->service = service; issued->component = component;
    issued->authority = service->next_authority++; issued->active = 1;
    pxa_resource_t resource = {0}; resource.context = issued; resource.close = closed;
    pxa_status_t status;
    if (wire == 1) status = pxa_handle_open64(service->runtime, component, PXA_RESOURCE_PERMISSION,
        issued->authority, &resource, handle);
    else {
        pxa_handle_t legacy = PXA_HANDLE_INVALID;
        status = pxa_handle_open(service->runtime, component, PXA_RESOURCE_PERMISSION,
            issued->authority, &resource, &legacy);
        *handle = legacy;
    }
    if (status != PXA_STATUS_OK) release_issued(issued);
    return status;
}

static pxa_status_t valid_request(pxa_bytes_t payload) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    uint8_t has_name = 0, has_scope = 0;
    pxa_record_iterator_init(&iterator, payload);
    for (;;) {
        pxa_status_t status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) return status;
        if (record.raw_tag < previous) return PXA_STATUS_INVALID_ARGUMENT;
        previous = record.raw_tag;
        if (record.optional) return PXA_STATUS_UNSUPPORTED;
        if (record.tag == 1 && !has_name && record.payload.size && record.payload.size <= 96)
            has_name = 1;
        else if (record.tag == 2 && !has_scope && record.payload.size <= 1024)
            has_scope = 1;
        else return PXA_STATUS_INVALID_ARGUMENT;
    }
    return has_name ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
}

static pxa_status_t control(void *context, pxa_runtime_t *runtime,
    pxa_component_t component, const pxa_message_view_t *message) {
    pxa_permission_service_t *service = context;
    if (!message->request_token) return PXA_STATUS_INVALID_ARGUMENT;
    if (message->opcode != PXA_PERMISSION_CHECK && message->opcode != PXA_PERMISSION_ACQUIRE)
        return PXA_STATUS_UNSUPPORTED;
    uint16_t wire = 0;
    pxa_status_t status = pxa_component_wire_profile(runtime, component, &wire);
    if (status != PXA_STATUS_OK) return status;
    const size_t bytes = message->opcode == PXA_PERMISSION_CHECK ? 1 : wire == 1 ? 8 : 4;
    status = pxa_request_begin_reserved(runtime, component, message->request_token,
        PXA_PERMISSION_SERVICE_ID, message->opcode, 0, bytes);
    if (status != PXA_STATUS_OK) return status;
    status = valid_request(message->payload);
    pxa_handle64_t handle = PXA_HANDLE64_INVALID;
    uint8_t result[8] = {PXA_PERMISSION_ALLOW};
    if (status == PXA_STATUS_OK && message->opcode == PXA_PERMISSION_ACQUIRE) {
        status = acquire(service, component, wire, &handle);
        if (wire == 1) pxa_write_u64(result, handle);
        else pxa_write_u32(result, (uint32_t)handle);
    }
    pxa_status_t complete = pxa_request_complete(runtime, component,
        message->request_token, status, status == PXA_STATUS_OK ? result : NULL,
        status == PXA_STATUS_OK ? bytes : 0);
    if (complete != PXA_STATUS_OK) {
        if (handle != PXA_HANDLE64_INVALID) {
            if (wire == 1) (void)pxa_handle_close64(runtime, component, handle);
            else (void)pxa_handle_close(runtime, component, (pxa_handle_t)handle);
        }
        (void)pxa_request_cancel(runtime, component, message->request_token);
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_permission_service_register(pxa_permission_service_t *service) {
    if (!valid(service) || service->registered) return PXA_STATUS_BAD_STATE;
    pxa_service_ops_t operations = {0};
    operations.struct_size = sizeof(operations);
    operations.service_id = PXA_PERMISSION_SERVICE_ID;
    operations.major = PXA_PERMISSION_SERVICE_MAJOR;
    operations.minor = PXA_PERMISSION_SERVICE_MINOR;
    operations.context = service;
    operations.control = control;
    pxa_status_t status = pxa_service_register(service->runtime, &operations);
    if (status == PXA_STATUS_OK) service->registered = 1;
    return status;
}
