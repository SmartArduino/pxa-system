#include "services/permission/permission_internal.h"

#include "common/checked_math.h"
#include "common/status_internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

static int config_valid(const pxa_permission_config_t *config,
                        size_t *blob_size) {
    size_t total;
    uint16_t index;
    uint16_t other;
    if (config == NULL || config->struct_size < sizeof(*config) ||
        !pxa_permission_bytes_valid_internal(config->app_identity) ||
        config->app_identity.size == 0 ||
        (config->declaration_count != 0 && config->declarations == NULL) ||
        config->max_authorities == 0 ||
        (config->max_pending_prompts != 0 && config->prompt == NULL) ||
        (config->max_pending_prompts == 0 && config->prompt != NULL) ||
        config->store.struct_size < sizeof(config->store) ||
        config->store.load == NULL || config->store.save == NULL) {
        return 0;
    }
    total = config->app_identity.size;
    for (index = 0; index < config->declaration_count; ++index) {
        const pxa_permission_declaration_t *item = &config->declarations[index];
        if (!pxa_permission_bytes_valid_internal(item->name) ||
            item->name.size == 0 ||
            item->name.size > PXA_PERMISSION_MAX_NAME ||
            !pxa_permission_bytes_valid_internal(item->scope) ||
            item->scope.size > PXA_PERMISSION_MAX_SCOPE || item->required > 1 ||
            item->name.size > SIZE_MAX - total ||
            item->scope.size > SIZE_MAX - total - item->name.size) {
            return 0;
        }
        for (other = 0; other < index; ++other) {
            if (pxa_permission_bytes_equal_internal(
                    item->name, config->declarations[other].name) &&
                pxa_permission_bytes_equal_internal(
                    item->scope, config->declarations[other].scope)) {
                return 0;
            }
        }
        total += item->name.size + item->scope.size;
    }
    if (blob_size != NULL) *blob_size = total;
    return 1;
}

size_t pxa_permission_service_workspace_size(
    const pxa_permission_config_t *config) {
    size_t blob_size;
    size_t total = PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    if (!config_valid(config, &blob_size) ||
        !pxa_internal_add_array_size(
            &total, 1, sizeof(pxa_permission_service_t),
            PXA_INTERNAL_WORKSPACE_ALIGNMENT) ||
        !pxa_internal_add_array_size(
            &total, config->declaration_count,
            sizeof(pxa_permission_entry_t),
            PXA_INTERNAL_WORKSPACE_ALIGNMENT) ||
        !pxa_internal_add_array_size(
            &total, config->max_authorities,
            sizeof(pxa_permission_authority_t),
            PXA_INTERNAL_WORKSPACE_ALIGNMENT) ||
        !pxa_internal_add_array_size(
            &total, config->max_pending_prompts,
            sizeof(pxa_permission_pending_prompt_t),
            PXA_INTERNAL_WORKSPACE_ALIGNMENT) ||
        blob_size > SIZE_MAX - total) {
        return 0;
    }
    return total + blob_size;
}

static void initialize_slots(pxa_permission_service_t *service) {
    uint16_t index;
    memset(service->authorities, 0,
           (size_t)service->max_authorities * sizeof(*service->authorities));
    for (index = 0; index < service->max_authorities; ++index) {
        service->authorities[index].slot.next_free =
            index + 1u < service->max_authorities
                ? (uint16_t)(index + 1u)
                : PXA_PERMISSION_SLOT_NONE;
    }
    service->free_authority_head =
        service->max_authorities == 0 ? PXA_PERMISSION_SLOT_NONE : 0;

    memset(service->pending_prompts, 0,
           (size_t)service->max_pending_prompts *
               sizeof(*service->pending_prompts));
    for (index = 0; index < service->max_pending_prompts; ++index) {
        service->pending_prompts[index].slot.next_free =
            index + 1u < service->max_pending_prompts
                ? (uint16_t)(index + 1u)
                : PXA_PERMISSION_SLOT_NONE;
    }
    service->free_prompt_head =
        service->max_pending_prompts == 0 ? PXA_PERMISSION_SLOT_NONE : 0;
}

pxa_status_t pxa_permission_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_permission_config_t *config,
    pxa_permission_service_t **output) {
    uint8_t *cursor;
    const uint8_t *end;
    size_t required;
    size_t blob_size;
    pxa_permission_service_t *service;
    uint8_t *blob;
    uint16_t index;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    required = pxa_permission_service_workspace_size(config);
    if (workspace == NULL || runtime == NULL || required == 0 ||
        workspace_size < required ||
        (uintptr_t)workspace > UINTPTR_MAX - workspace_size ||
        !config_valid(config, &blob_size)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    cursor = (uint8_t *)workspace;
    end = cursor + workspace_size;
    service = (pxa_permission_service_t *)pxa_internal_layout_take(
        &cursor, end, 1, sizeof(*service),
        PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    if (service == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    memset(service, 0, sizeof(*service));
    service->declarations =
        (pxa_permission_entry_t *)pxa_internal_layout_take(
            &cursor, end, config->declaration_count,
            sizeof(*service->declarations), PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    service->authorities =
        (pxa_permission_authority_t *)pxa_internal_layout_take(
            &cursor, end, config->max_authorities,
            sizeof(*service->authorities), PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    service->pending_prompts =
        (pxa_permission_pending_prompt_t *)pxa_internal_layout_take(
            &cursor, end, config->max_pending_prompts,
            sizeof(*service->pending_prompts),
            PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    if (service->declarations == NULL || service->authorities == NULL ||
        service->pending_prompts == NULL || blob_size > (size_t)(end - cursor)) {
        memset(service, 0, sizeof(*service));
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    blob = cursor;
    memset(service->declarations, 0,
           (size_t)config->declaration_count * sizeof(*service->declarations));
    service->identity.data = blob;
    service->identity.size = config->app_identity.size;
    memcpy(blob, config->app_identity.data, config->app_identity.size);
    blob += config->app_identity.size;
    for (index = 0; index < config->declaration_count; ++index) {
        pxa_permission_entry_t *target = &service->declarations[index];
        const pxa_permission_declaration_t *source =
            &config->declarations[index];
        target->name.data = blob;
        target->name.size = source->name.size;
        memcpy(blob, source->name.data, source->name.size);
        blob += source->name.size;
        target->scope.data = blob;
        target->scope.size = source->scope.size;
        if (source->scope.size != 0) {
            memcpy(blob, source->scope.data, source->scope.size);
            blob += source->scope.size;
        }
        target->required = source->required;
        target->decision = PXA_PERMISSION_DENY;
    }
    service->runtime = runtime;
    service->store = config->store;
    service->declaration_count = config->declaration_count;
    service->max_authorities = config->max_authorities;
    service->max_pending_prompts = config->max_pending_prompts;
    service->next_authority = 1;
    service->prompt_context = config->prompt_context;
    service->prompt = config->prompt;
    initialize_slots(service);
    service->magic = PXA_PERMISSION_MAGIC;
    *output = service;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_permission_policy_load(pxa_permission_service_t *service) {
    uint16_t index;
    if (!pxa_permission_service_valid_internal(service))
        return PXA_STATUS_INVALID_ARGUMENT;
    if (service->registered) return PXA_STATUS_BAD_STATE;
    service->initialized = 0;
    initialize_slots(service);
    service->next_authority = 1;
    for (index = 0; index < service->declaration_count; ++index) {
        pxa_permission_decision_t decision = PXA_PERMISSION_DENY;
        pxa_status_t status = pxa_status_normalize(service->store.load(
            service->store.context, service->identity,
            service->declarations[index].name,
            service->declarations[index].scope, &decision));
        if (status == PXA_STATUS_NOT_FOUND)
            decision = PXA_PERMISSION_DENY;
        else if (status != PXA_STATUS_OK)
            return status;
        if (decision != PXA_PERMISSION_DENY &&
            decision != PXA_PERMISSION_ALLOW) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        service->declarations[index].decision = decision;
    }
    service->initialized = 1;
    return PXA_STATUS_OK;
}

int pxa_permission_can_activate(const pxa_permission_service_t *service) {
    uint16_t index;
    if (!pxa_permission_service_valid_internal(service) ||
        !service->initialized) {
        return 0;
    }
    for (index = 0; index < service->declaration_count; ++index) {
        if (service->declarations[index].required &&
            service->declarations[index].decision != PXA_PERMISSION_ALLOW) {
            return 0;
        }
    }
    return 1;
}

pxa_permission_decision_t pxa_permission_get(
    const pxa_permission_service_t *service, pxa_bytes_t name,
    pxa_bytes_t scope) {
    uint16_t index;
    if (!pxa_permission_service_valid_internal(service) ||
        !service->initialized ||
        !pxa_permission_find_declaration_internal(service, name, scope,
                                                  &index)) {
        return PXA_PERMISSION_DENY;
    }
    return service->declarations[index].decision;
}

pxa_status_t pxa_permission_set(pxa_permission_service_t *service,
                                pxa_bytes_t name, pxa_bytes_t scope,
                                pxa_permission_decision_t decision) {
    uint16_t index;
    pxa_status_t status;
    if (!pxa_permission_service_valid_internal(service) ||
        !service->initialized ||
        (decision != PXA_PERMISSION_DENY &&
         decision != PXA_PERMISSION_ALLOW) ||
        !pxa_permission_find_declaration_internal(service, name, scope,
                                                  &index)) {
        return PXA_STATUS_DENIED;
    }
    if (service->declarations[index].decision == decision)
        return PXA_STATUS_OK;
    status = pxa_status_normalize(service->store.save(
        service->store.context, service->identity,
        service->declarations[index].name,
        service->declarations[index].scope, decision));
    if (status == PXA_STATUS_OK)
        service->declarations[index].decision = decision;
    return status;
}
