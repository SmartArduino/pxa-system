#include "services/permission/permission_internal.h"

#include "common/status_internal.h"
#include "core/runtime_internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

static pxa_permission_authority_t *allocate_authority(
    pxa_permission_service_t *service) {
    pxa_permission_authority_t *issued;
    uint16_t index = service->free_authority_head;
    if (index == PXA_PERMISSION_SLOT_NONE) return NULL;
    issued = &service->authorities[index];
    service->free_authority_head = issued->slot.next_free;
    memset(issued, 0, sizeof(*issued));
    issued->active = 1;
    return issued;
}

static void release_authority(pxa_permission_authority_t *issued) {
    pxa_permission_service_t *service;
    uint16_t index;
    if (issued == NULL || !issued->active || issued->service == NULL) return;
    service = issued->service;
    index = (uint16_t)(issued - service->authorities);
    memset(issued, 0, sizeof(*issued));
    issued->slot.next_free = service->free_authority_head;
    service->free_authority_head = index;
}

static void permission_authority_closed(void *context) {
    pxa_permission_authority_t *issued =
        (pxa_permission_authority_t *)context;
    pxa_permission_service_t *service;
    pxa_component_t component;
    pxa_authority_t authority;
    if (issued == NULL || !issued->active || issued->service == NULL) return;
    service = issued->service;
    component = issued->component;
    authority = issued->authority;
    (void)pxa_runtime_release_authority(service->runtime, component,
                                        authority);
    if (issued->active) release_authority(issued);
}

static pxa_permission_pending_prompt_t *allocate_pending_prompt(
    pxa_permission_service_t *service) {
    pxa_permission_pending_prompt_t *pending;
    uint16_t index = service->free_prompt_head;
    if (index == PXA_PERMISSION_SLOT_NONE) return NULL;
    pending = &service->pending_prompts[index];
    service->free_prompt_head = pending->slot.next_free;
    memset(pending, 0, sizeof(*pending));
    pending->active = 1;
    return pending;
}

static void release_pending_prompt(pxa_permission_service_t *service,
                                   pxa_permission_pending_prompt_t *pending) {
    uint16_t index;
    if (pending == NULL || !pending->active) return;
    index = (uint16_t)(pending - service->pending_prompts);
    memset(pending, 0, sizeof(*pending));
    pending->slot.next_free = service->free_prompt_head;
    service->free_prompt_head = index;
}

static pxa_permission_pending_prompt_t *find_pending_prompt(
    pxa_permission_service_t *service, pxa_component_t component,
    uint32_t request_id) {
    uint16_t index;
    if (request_id == 0) return NULL;
    for (index = 0; index < service->max_pending_prompts; ++index) {
        pxa_permission_pending_prompt_t *pending =
            &service->pending_prompts[index];
        if (pending->active && pending->component == component &&
            pending->request_id == request_id) {
            return pending;
        }
    }
    return NULL;
}

static pxa_status_t authorize(pxa_permission_service_t *service,
                              pxa_component_t component,
                              uint16_t declaration_index,
                              pxa_handle_t *handle_out) {
    pxa_permission_authority_t *issued;
    pxa_component_snapshot_t snapshot;
    pxa_resource_t resource;
    pxa_status_t status;
    if (service->declarations[declaration_index].decision !=
        PXA_PERMISSION_ALLOW) {
        return PXA_STATUS_DENIED;
    }
    if (service->next_authority == 0 ||
        service->next_authority == UINT64_MAX) {
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    if (pxa_component_snapshot(service->runtime, component, &snapshot) !=
        PXA_STATUS_OK) {
        return PXA_STATUS_NOT_FOUND;
    }
    if (snapshot.state != PXA_COMPONENT_STARTING &&
        snapshot.state != PXA_COMPONENT_RUNNING) {
        return PXA_STATUS_BAD_STATE;
    }
    issued = allocate_authority(service);
    if (issued == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    issued->service = service;
    issued->component = component;
    issued->authority = service->next_authority++;
    issued->slot.declaration_index = declaration_index;
    memset(&resource, 0, sizeof(resource));
    resource.context = issued;
    resource.close = permission_authority_closed;
    status = pxa_handle_open(service->runtime, component,
                             PXA_RESOURCE_PERMISSION, issued->authority,
                             &resource, handle_out);
    if (status != PXA_STATUS_OK) {
        release_authority(issued);
        return status;
    }
    issued->permission_handle = *handle_out;
    return PXA_STATUS_OK;
}

static void discard_authority(pxa_permission_service_t *service,
                              pxa_component_t component,
                              pxa_handle_t handle) {
    if (handle == PXA_HANDLE_INVALID) return;
    (void)pxa_handle_close(service->runtime, component, handle);
}

static pxa_status_t complete_permission_request(
    pxa_permission_service_t *service, pxa_component_t component,
    uint32_t request_id, uint16_t opcode, pxa_status_t result,
    pxa_handle_t handle) {
    uint8_t result_data[4];
    size_t result_size = 0;
    pxa_status_t complete;
    if (result == PXA_STATUS_OK && opcode == PXA_PERMISSION_ACQUIRE) {
        pxa_write_u32(result_data, handle);
        result_size = sizeof(result_data);
    }
    complete = pxa_request_complete(service->runtime, component, request_id,
                                    result,
                                    result == PXA_STATUS_OK ? result_data : NULL,
                                    result == PXA_STATUS_OK ? result_size : 0);
    if (complete != PXA_STATUS_OK) {
        discard_authority(service, component, handle);
        (void)pxa_request_cancel(service->runtime, component, request_id);
    }
    return complete;
}

static pxa_status_t request_runtime_prompt(pxa_permission_service_t *service,
                                           pxa_component_t component,
                                           uint32_t request_id,
                                           uint16_t declaration_index) {
    pxa_permission_pending_prompt_t *pending;
    pxa_status_t status;
    if (service->prompt == NULL || service->max_pending_prompts == 0) {
        return PXA_STATUS_DENIED;
    }
    pending = allocate_pending_prompt(service);
    if (pending == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    pending->component = component;
    pending->request_id = request_id;
    pending->slot.declaration_index = declaration_index;
    status = pxa_status_normalize(service->prompt(
        service->prompt_context, component, request_id, service->identity,
        service->declarations[declaration_index].name,
        service->declarations[declaration_index].scope));
    if (status != PXA_STATUS_OK) release_pending_prompt(service, pending);
    return status;
}

pxa_status_t pxa_permission_resolve(
    const pxa_permission_service_t *service, pxa_component_t component,
    pxa_handle_t permission_handle, pxa_bytes_t expected_name,
    pxa_bytes_t expected_scope, pxa_authority_t *authority) {
    pxa_resource_t resource;
    pxa_permission_authority_t *issued;
    const pxa_permission_entry_t *declaration;
    pxa_status_t status;
    if (authority == NULL ||
        !pxa_permission_service_valid_internal(service)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *authority = 0;
    status = pxa_handle_get(service->runtime, component, permission_handle,
                            PXA_RESOURCE_PERMISSION, &resource);
    if (status != PXA_STATUS_OK) return status;
    issued = (pxa_permission_authority_t *)resource.context;
    if (issued == NULL || !issued->active || issued->service != service ||
        issued->component != component ||
        issued->permission_handle != permission_handle ||
        issued->slot.declaration_index >= service->declaration_count) {
        return PXA_STATUS_DENIED;
    }
    declaration = &service->declarations[issued->slot.declaration_index];
    if (!pxa_permission_bytes_equal_internal(declaration->name,
                                             expected_name) ||
        !pxa_permission_bytes_equal_internal(declaration->scope,
                                             expected_scope)) {
        return PXA_STATUS_DENIED;
    }
    *authority = issued->authority;
    return PXA_STATUS_OK;
}

static pxa_status_t parse_request(pxa_bytes_t payload, pxa_bytes_t *name,
                                  pxa_bytes_t *scope) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    uint8_t has_name = 0;
    uint8_t has_scope = 0;
    pxa_status_t status;
    name->data = NULL;
    name->size = 0;
    scope->data = NULL;
    scope->size = 0;
    pxa_record_iterator_init(&iterator, payload);
    for (;;) {
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) return status;
        if (record.raw_tag < previous) return PXA_STATUS_INVALID_ARGUMENT;
        previous = record.raw_tag;
        if (record.optional) return PXA_STATUS_UNSUPPORTED;
        if (record.tag == 1 && !has_name && record.payload.size != 0 &&
            record.payload.size <= PXA_PERMISSION_MAX_NAME) {
            *name = record.payload;
            has_name = 1;
        } else if (record.tag == 2 && !has_scope &&
                   record.payload.size <= PXA_PERMISSION_MAX_SCOPE) {
            *scope = record.payload;
            has_scope = 1;
        } else {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
    }
    return has_name ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
}

static pxa_status_t permission_control(void *context, pxa_runtime_t *runtime,
                                       pxa_component_t component,
                                       const pxa_message_view_t *message) {
    pxa_permission_service_t *service = (pxa_permission_service_t *)context;
    pxa_bytes_t name;
    pxa_bytes_t scope;
    pxa_status_t result;
    uint16_t declaration_index = 0;
    uint8_t result_data[4];
    size_t result_size = 0;
    pxa_handle_t handle = PXA_HANDLE_INVALID;
    (void)runtime;
    if (message->request_id == 0) return PXA_STATUS_INVALID_ARGUMENT;
    if (message->opcode != PXA_PERMISSION_CHECK &&
        message->opcode != PXA_PERMISSION_ACQUIRE) {
        return PXA_STATUS_UNSUPPORTED;
    }
    result = pxa_request_begin(service->runtime, component,
                               message->request_id,
                               PXA_PERMISSION_SERVICE_ID,
                               message->opcode, 0);
    if (result != PXA_STATUS_OK) return result;
    result = parse_request(message->payload, &name, &scope);
    if (result == PXA_STATUS_OK &&
        !pxa_permission_find_declaration_internal(
            service, name, scope, &declaration_index)) {
        result = message->opcode == PXA_PERMISSION_CHECK
                     ? PXA_STATUS_OK
                     : PXA_STATUS_DENIED;
    }
    if (result == PXA_STATUS_OK && message->opcode == PXA_PERMISSION_CHECK) {
        result_data[0] = pxa_permission_get(service, name, scope);
        result_size = 1;
    } else if (result == PXA_STATUS_OK) {
        const pxa_permission_entry_t *declaration =
            &service->declarations[declaration_index];
        if (declaration->decision == PXA_PERMISSION_DENY &&
            !declaration->required && service->prompt != NULL) {
            result = request_runtime_prompt(service, component,
                                            message->request_id,
                                            declaration_index);
            if (result == PXA_STATUS_OK) return PXA_STATUS_OK;
        }
        if (result == PXA_STATUS_OK) {
            result = authorize(service, component, declaration_index, &handle);
        }
        if (result == PXA_STATUS_OK) {
            pxa_write_u32(result_data, handle);
            result_size = 4;
        }
    }
    {
        pxa_status_t complete = pxa_request_complete(
            service->runtime, component, message->request_id, result,
            result == PXA_STATUS_OK ? result_data : NULL,
            result == PXA_STATUS_OK ? result_size : 0);
        if (complete != PXA_STATUS_OK) {
            discard_authority(service, component, handle);
            (void)pxa_request_cancel(service->runtime, component,
                                     message->request_id);
        }
    }
    return PXA_STATUS_OK;
}

static void permission_component_stopped(void *context,
                                         pxa_runtime_t *runtime,
                                         pxa_component_t component) {
    pxa_permission_service_t *service = (pxa_permission_service_t *)context;
    uint16_t index;
    (void)runtime;
    for (index = 0; index < service->max_authorities; ++index) {
        if (service->authorities[index].active &&
            service->authorities[index].component == component) {
            release_authority(&service->authorities[index]);
        }
    }
    for (index = 0; index < service->max_pending_prompts; ++index) {
        if (service->pending_prompts[index].active &&
            service->pending_prompts[index].component == component) {
            release_pending_prompt(service, &service->pending_prompts[index]);
        }
    }
}

pxa_status_t pxa_permission_service_register(
    pxa_permission_service_t *service) {
    pxa_service_ops_t operations;
    pxa_status_t status;
    if (!pxa_permission_service_valid_internal(service) ||
        !service->initialized) {
        return PXA_STATUS_BAD_STATE;
    }
    if (service->registered) return PXA_STATUS_BAD_STATE;
    memset(&operations, 0, sizeof(operations));
    operations.struct_size = sizeof(operations);
    operations.service_id = PXA_PERMISSION_SERVICE_ID;
    operations.major = PXA_PERMISSION_SERVICE_MAJOR;
    operations.minor = PXA_PERMISSION_SERVICE_MINOR;
    operations.context = service;
    operations.control = permission_control;
    operations.component_stopped = permission_component_stopped;
    status = pxa_service_register(service->runtime, &operations);
    if (status == PXA_STATUS_OK) service->registered = 1;
    return status;
}

pxa_status_t pxa_permission_prompt_complete(
    pxa_permission_service_t *service, pxa_component_t component,
    uint32_t request_id, pxa_permission_decision_t decision) {
    pxa_permission_pending_prompt_t *pending;
    pxa_status_t result;
    pxa_status_t complete;
    pxa_handle_t handle = PXA_HANDLE_INVALID;
    if (!pxa_permission_service_valid_internal(service) ||
        !service->initialized ||
        (decision != PXA_PERMISSION_DENY && decision != PXA_PERMISSION_ALLOW)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    pending = find_pending_prompt(service, component, request_id);
    if (pending == NULL) return PXA_STATUS_NOT_FOUND;
    if (decision == PXA_PERMISSION_ALLOW) {
        result = pxa_permission_set(
            service,
            service->declarations[pending->slot.declaration_index].name,
            service->declarations[pending->slot.declaration_index].scope,
            decision);
        if (result != PXA_STATUS_OK) return result;
        {
            uint16_t declaration_index = pending->slot.declaration_index;
            release_pending_prompt(service, pending);
            result = authorize(service, component, declaration_index, &handle);
        }
    } else {
        release_pending_prompt(service, pending);
        result = PXA_STATUS_DENIED;
    }
    complete = complete_permission_request(service, component, request_id,
                                           PXA_PERMISSION_ACQUIRE, result,
                                           handle);
    return complete;
}

static void append_affected(pxa_component_t component,
                            pxa_component_t *affected, size_t capacity,
                            size_t *count) {
    size_t index;
    for (index = 0; index < *count && index < capacity; ++index) {
        if (affected[index] == component) return;
    }
    if (*count < capacity && affected != NULL) affected[*count] = component;
    (*count)++;
}

pxa_status_t pxa_permission_revoke(
    pxa_permission_service_t *service, pxa_bytes_t name, pxa_bytes_t scope,
    pxa_component_t *affected, size_t capacity, size_t *count) {
    uint16_t declaration_index;
    uint16_t index;
    pxa_status_t result;
    uint8_t record_headers[2u * PXA_RECORD_HEADER_SIZE];
    pxa_bytes_t payload_parts[4];
    if (!pxa_permission_service_valid_internal(service) || count == NULL ||
        (affected == NULL && capacity != 0) ||
        !pxa_permission_find_declaration_internal(
            service, name, scope, &declaration_index)) {
        return PXA_STATUS_DENIED;
    }
    *count = 0;
    result = pxa_permission_set(service, name, scope, PXA_PERMISSION_DENY);
    if (result != PXA_STATUS_OK) return result;
    for (index = 0; index < service->max_pending_prompts; ++index) {
        pxa_permission_pending_prompt_t *pending =
            &service->pending_prompts[index];
        pxa_component_t pending_component;
        uint32_t pending_request_id;
        if (!pending->active ||
            pending->slot.declaration_index != declaration_index) {
            continue;
        }
        pending_component = pending->component;
        pending_request_id = pending->request_id;
        release_pending_prompt(service, pending);
        result = complete_permission_request(
            service, pending_component, pending_request_id,
            PXA_PERMISSION_ACQUIRE, PXA_STATUS_DENIED,
            PXA_HANDLE_INVALID);
        if (result != PXA_STATUS_OK && result != PXA_STATUS_NOT_FOUND &&
            result != PXA_STATUS_BAD_STATE) {
            return result;
        }
        if (result == PXA_STATUS_OK) {
            append_affected(pending_component, affected, capacity, count);
        }
    }
    for (index = 0; index < service->max_authorities; ++index) {
        pxa_permission_authority_t *issued = &service->authorities[index];
        if (!issued->active ||
            issued->slot.declaration_index != declaration_index) continue;
        append_affected(issued->component, affected, capacity, count);
        result = pxa_authority_revoke(service->runtime, issued->component,
                                      issued->authority);
        if (result != PXA_STATUS_OK && result != PXA_STATUS_BAD_STATE &&
            result != PXA_STATUS_NOT_FOUND) {
            return result;
        }
        if (issued->active) release_authority(issued);
    }
    pxa_write_u16(record_headers, 1);
    pxa_write_u16(record_headers + 2, (uint16_t)name.size);
    pxa_write_u16(record_headers + 4, 2);
    pxa_write_u16(record_headers + 6, (uint16_t)scope.size);
    payload_parts[0] =
        (pxa_bytes_t){record_headers, PXA_RECORD_HEADER_SIZE};
    payload_parts[1] = name;
    payload_parts[2] =
        (pxa_bytes_t){record_headers + PXA_RECORD_HEADER_SIZE,
                      PXA_RECORD_HEADER_SIZE};
    payload_parts[3] = scope;
    for (index = 0; index < *count && index < capacity; ++index) {
        (void)pxa_event_post_messagev(
            service->runtime, affected[index], PXA_PERMISSION_SERVICE_ID,
            PXA_PERMISSION_REVOKED, 0, payload_parts, 4, 1, 0);
    }
    return *count > capacity ? PXA_STATUS_RESOURCE_LIMIT : PXA_STATUS_OK;
}
