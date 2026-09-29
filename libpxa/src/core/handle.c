#include "core/handle_internal.h"
#include "core/runtime_internal.h"

#include "core/slot_token.h"

#include <limits.h>
#include <string.h>

#define PXA_RESOURCE_INDEX_NONE UINT32_MAX

static pxa_resource_slot_t *find_resource_at(pxa_resource_table_t *table,
                                             uint32_t owner_index,
                                             uint32_t index,
                                             uint32_t generation,
                                             uint32_t *index_out,
                                             pxa_status_t *status_out) {
    pxa_resource_slot_t *slot;
    slot = &table->slots[index];
    if (!slot->occupied || slot->generation != generation ||
        slot->owner_index != owner_index) {
        if (status_out != NULL) *status_out = PXA_STATUS_NOT_FOUND;
        return NULL;
    }
    if (index_out != NULL) *index_out = index;
    if (status_out != NULL) *status_out = PXA_STATUS_OK;
    return slot;
}

static pxa_resource_slot_t *find_resource(pxa_resource_table_t *table,
                                          uint32_t owner_index,
                                          pxa_handle_t handle,
                                          uint32_t *index_out,
                                          pxa_status_t *status_out) {
    uint32_t index;
    uint16_t generation;
    if (table == NULL || table->slots == NULL ||
        !pxa_internal_slot_token_decode(handle, table->capacity, &index,
                                        &generation)) {
        if (status_out != NULL) *status_out = PXA_STATUS_INVALID_ARGUMENT;
        return NULL;
    }
    return find_resource_at(table, owner_index, index, generation, index_out,
                            status_out);
}

static pxa_resource_slot_t *find_resource64(pxa_resource_table_t *table,
                                            uint32_t owner_index,
                                            pxa_handle64_t handle,
                                            uint32_t *index_out,
                                            pxa_status_t *status_out) {
    uint32_t index;
    uint32_t generation;
    if (table == NULL || table->slots == NULL ||
        !pxa_internal_handle64_token_decode(
            handle, table->capacity, &index, &generation)) {
        if (status_out != NULL) *status_out = PXA_STATUS_INVALID_ARGUMENT;
        return NULL;
    }
    return find_resource_at(table, owner_index, index, generation, index_out,
                            status_out);
}

static void detach_resource(pxa_resource_table_t *table,
                            pxa_resource_owner_t *owner, uint32_t index) {
    pxa_resource_slot_t *slot = &table->slots[index];
    if (!slot->occupied) return;
    slot->occupied = 0;
    slot->closing = 1;
    if (owner != NULL && owner->count != 0) owner->count--;
    if (table->count != 0) table->count--;
    if (slot->generation == UINT32_MAX) {
        slot->retired = 1;
    } else {
        slot->generation++;
        slot->next_free = PXA_RESOURCE_INDEX_NONE;
        if (table->free_tail == PXA_RESOURCE_INDEX_NONE) {
            table->free_head = index;
        } else {
            table->slots[table->free_tail].next_free = index;
        }
        table->free_tail = index;
    }
}

static void finish_resource_close(pxa_resource_slot_t *slot) {
    pxa_resource_t resource;
    if (slot == NULL || !slot->closing) return;
    resource = slot->resource;
    memset(&slot->resource, 0, sizeof(slot->resource));
    slot->authority = 0;
    slot->owner_index = 0;
    slot->type = PXA_RESOURCE_UNKNOWN;
    slot->closing = 0;
    if (resource.close != NULL) resource.close(resource.context);
}

void pxa_resource_table_init(pxa_resource_table_t *table,
                             pxa_resource_slot_t *slots,
                             uint16_t capacity) {
    uint32_t index;
    if (table == NULL) return;
    memset(table, 0, sizeof(*table));
    table->slots = slots;
    table->capacity = capacity;
    if (slots != NULL) {
        memset(slots, 0, (size_t)capacity * sizeof(*slots));
        for (index = 0; index < capacity; ++index) {
            slots[index].generation = 1;
            slots[index].next_free = index + 1u < capacity
                                         ? index + 1u
                                         : PXA_RESOURCE_INDEX_NONE;
        }
    }
    table->free_head = capacity == 0 ? PXA_RESOURCE_INDEX_NONE : 0;
    table->free_tail = capacity == 0 ? PXA_RESOURCE_INDEX_NONE
                                     : (uint32_t)capacity - 1u;
}

void pxa_resource_owner_init(pxa_resource_owner_t *owner) {
    if (owner != NULL) owner->count = 0;
}

uint16_t pxa_resource_owner_count(const pxa_resource_owner_t *owner) {
    return owner == NULL ? 0 : owner->count;
}

static pxa_status_t open_resource_slot(
    pxa_resource_table_t *table, pxa_resource_owner_t *owner,
    uint32_t owner_index, pxa_resource_type_t type,
    pxa_authority_t authority, const pxa_resource_t *resource,
    int legacy, uint32_t *index_out) {
    uint32_t index = table->free_head;
    uint32_t previous = PXA_RESOURCE_INDEX_NONE;
    pxa_resource_slot_t *slot;
    while (index != PXA_RESOURCE_INDEX_NONE && legacy &&
           table->slots[index].generation > UINT16_MAX) {
        previous = index;
        index = table->slots[index].next_free;
    }
    if (index == PXA_RESOURCE_INDEX_NONE) return PXA_STATUS_RESOURCE_LIMIT;
    slot = &table->slots[index];
    if (previous == PXA_RESOURCE_INDEX_NONE)
        table->free_head = slot->next_free;
    else
        table->slots[previous].next_free = slot->next_free;
    if (table->free_tail == index)
        table->free_tail = previous;
    slot->next_free = PXA_RESOURCE_INDEX_NONE;
    slot->occupied = 1;
    slot->owner_index = (uint16_t)owner_index;
    slot->type = type;
    slot->authority = authority;
    slot->resource = *resource;
    owner->count++;
    ++table->count;
    if (table->count > table->peak) table->peak = table->count;
    *index_out = index;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_resource_table_open(
    pxa_resource_table_t *table, pxa_resource_owner_t *owner,
    uint32_t owner_index, pxa_resource_type_t type,
    pxa_authority_t authority, const pxa_resource_t *resource,
    pxa_handle_t *output) {
    uint32_t index;
    pxa_resource_slot_t *slot;
    if (table == NULL || owner == NULL || table->slots == NULL ||
        type == PXA_RESOURCE_UNKNOWN || resource == NULL || output == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *output = PXA_HANDLE_INVALID;
    {
        pxa_status_t status = open_resource_slot(
            table, owner, owner_index, type, authority, resource, 1, &index);
        if (status != PXA_STATUS_OK) return status;
    }
    slot = &table->slots[index];
    *output = pxa_internal_slot_token_encode(index,
                                               (uint16_t)slot->generation);
    return PXA_STATUS_OK;
}

pxa_status_t pxa_resource_table_open64(
    pxa_resource_table_t *table, pxa_resource_owner_t *owner,
    uint32_t owner_index, pxa_resource_type_t type,
    pxa_authority_t authority, const pxa_resource_t *resource,
    pxa_handle64_t *output) {
    uint32_t index;
    pxa_status_t status;
    if (table == NULL || owner == NULL || table->slots == NULL ||
        type == PXA_RESOURCE_UNKNOWN || resource == NULL || output == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    *output = PXA_HANDLE64_INVALID;
    status = open_resource_slot(table, owner, owner_index, type, authority,
                                resource, 0, &index);
    if (status != PXA_STATUS_OK) return status;
    *output = pxa_internal_handle64_token_encode(
        index, table->slots[index].generation);
    return PXA_STATUS_OK;
}

pxa_status_t pxa_resource_table_get(const pxa_resource_table_t *table,
                                    uint32_t owner_index,
                                    pxa_handle_t handle,
                                    pxa_resource_type_t expected_type,
                                    pxa_resource_t *output) {
    pxa_status_t status;
    const pxa_resource_slot_t *slot;
    if (output == NULL || expected_type == PXA_RESOURCE_UNKNOWN) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    memset(output, 0, sizeof(*output));
    slot = find_resource((pxa_resource_table_t *)table, owner_index, handle,
                         NULL, &status);
    if (slot == NULL) return status;
    if (slot->type != expected_type) return PXA_STATUS_NOT_FOUND;
    *output = slot->resource;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_resource_table_get64(const pxa_resource_table_t *table,
                                      uint32_t owner_index,
                                      pxa_handle64_t handle,
                                      pxa_resource_type_t expected_type,
                                      pxa_resource_t *output) {
    pxa_status_t status;
    const pxa_resource_slot_t *slot;
    if (output == NULL || expected_type == PXA_RESOURCE_UNKNOWN)
        return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    slot = find_resource64((pxa_resource_table_t *)table, owner_index,
                           handle, NULL, &status);
    if (slot == NULL) return status;
    if (slot->type != expected_type) return PXA_STATUS_NOT_FOUND;
    *output = slot->resource;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_resource_table_widen(const pxa_resource_table_t *table,
                                      uint32_t owner_index,
                                      pxa_handle_t legacy_handle,
                                      pxa_handle64_t *output) {
    uint32_t index;
    pxa_status_t status;
    const pxa_resource_slot_t *slot;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = PXA_HANDLE64_INVALID;
    slot = find_resource((pxa_resource_table_t *)table, owner_index,
                         legacy_handle, &index, &status);
    if (slot == NULL) return status;
    *output = pxa_internal_handle64_token_encode(index, slot->generation);
    return PXA_STATUS_OK;
}

static int32_t io_resource_slot(pxa_resource_slot_t *slot,
                                uint32_t operation, uint8_t *data,
                                size_t size) {
    const pxa_resource_ops_t *operations;
    if (slot->resource.operations == NULL) return PXA_STATUS_NOT_FOUND;
    operations = (const pxa_resource_ops_t *)slot->resource.operations;
    if (operations->struct_size < sizeof(*operations) ||
        operations->io == NULL)
        return PXA_STATUS_UNSUPPORTED;
    return operations->io(slot->resource.context, operation, data, size);
}

int32_t pxa_resource_table_io(pxa_resource_table_t *table,
                              uint32_t owner_index, pxa_handle_t handle,
                              uint32_t operation, uint8_t *data, size_t size) {
    pxa_status_t status;
    pxa_resource_slot_t *slot =
        find_resource(table, owner_index, handle, NULL, &status);
    if (slot == NULL) return status;
    return io_resource_slot(slot, operation, data, size);
}

int32_t pxa_resource_table_io64(pxa_resource_table_t *table,
                                uint32_t owner_index, pxa_handle64_t handle,
                                uint32_t operation, uint8_t *data,
                                size_t size) {
    pxa_status_t status;
    pxa_resource_slot_t *slot =
        find_resource64(table, owner_index, handle, NULL, &status);
    if (slot == NULL) return status;
    return io_resource_slot(slot, operation, data, size);
}

pxa_status_t pxa_resource_table_close(pxa_resource_table_t *table,
                                      pxa_resource_owner_t *owner,
                                      uint32_t owner_index,
                                      pxa_handle_t handle) {
    uint32_t index;
    pxa_status_t status;
    pxa_resource_slot_t *slot =
        find_resource(table, owner_index, handle, &index, &status);
    if (slot == NULL) return status;
    detach_resource(table, owner, index);
    finish_resource_close(slot);
    return PXA_STATUS_OK;
}

pxa_status_t pxa_resource_table_close64(pxa_resource_table_t *table,
                                        pxa_resource_owner_t *owner,
                                        uint32_t owner_index,
                                        pxa_handle64_t handle) {
    uint32_t index;
    pxa_status_t status;
    pxa_resource_slot_t *slot =
        find_resource64(table, owner_index, handle, &index, &status);
    if (slot == NULL) return status;
    detach_resource(table, owner, index);
    finish_resource_close(slot);
    return PXA_STATUS_OK;
}

void pxa_resource_table_detach_owner(pxa_resource_table_t *table,
                                     pxa_resource_owner_t *owner,
                                     uint32_t owner_index) {
    uint32_t index;
    if (table == NULL || owner == NULL) return;
    for (index = 0; index < table->capacity; ++index) {
        pxa_resource_slot_t *slot = &table->slots[index];
        if (slot->occupied && slot->owner_index == owner_index) {
            detach_resource(table, owner, index);
        }
    }
}

void pxa_resource_table_finish_owner(pxa_resource_table_t *table,
                                     uint32_t owner_index) {
    uint32_t index;
    if (table == NULL) return;
    for (index = 0; index < table->capacity; ++index) {
        pxa_resource_slot_t *slot = &table->slots[index];
        if (slot->closing && slot->owner_index == owner_index) {
            finish_resource_close(slot);
        }
    }
}

void pxa_resource_table_detach_authority(
    pxa_resource_table_t *table, pxa_resource_owner_t *owner,
    uint32_t owner_index, pxa_authority_t authority) {
    uint32_t index;
    if (table == NULL || owner == NULL) return;
    for (index = 0; index < table->capacity; ++index) {
        pxa_resource_slot_t *slot = &table->slots[index];
        if (slot->occupied && slot->owner_index == owner_index &&
            slot->authority == authority) {
            detach_resource(table, owner, index);
        }
    }
}

void pxa_resource_table_finish_authority(pxa_resource_table_t *table,
                                         uint32_t owner_index,
                                         pxa_authority_t authority) {
    uint32_t index;
    if (table == NULL) return;
    for (index = 0; index < table->capacity; ++index) {
        pxa_resource_slot_t *slot = &table->slots[index];
        if (slot->closing && slot->owner_index == owner_index &&
            slot->authority == authority) {
            finish_resource_close(slot);
        }
    }
}

int32_t pxa_runtime_io(pxa_runtime_t *runtime, pxa_component_t component_ref,
                       pxa_handle_t handle, uint32_t operation,
                       uint8_t *data, size_t size) {
    uint32_t component_index;
    pxa_status_t status = pxa_component_validate_import(runtime, component_ref);
    if (status != PXA_STATUS_OK) return status;
    if ((data == NULL && size != 0) || size > INT32_MAX) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (pxa_runtime_find_component(runtime, component_ref, &component_index) ==
        NULL) {
        return PXA_STATUS_NOT_FOUND;
    }
    return pxa_resource_table_io(&runtime->resources, component_index, handle,
                                 operation, data, size);
}

int32_t pxa_runtime_io64(pxa_runtime_t *runtime,
                         pxa_component_t component_ref,
                         pxa_handle64_t handle, uint32_t operation,
                         uint8_t *data, size_t size) {
    uint32_t component_index;
    pxa_status_t status = pxa_component_validate_import(runtime, component_ref);
    if (status != PXA_STATUS_OK) return status;
    if ((data == NULL && size != 0) || size > INT32_MAX)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (pxa_runtime_find_component(runtime, component_ref, &component_index) ==
        NULL)
        return PXA_STATUS_NOT_FOUND;
    return pxa_resource_table_io64(&runtime->resources, component_index,
                                   handle, operation, data, size);
}

pxa_status_t pxa_handle_open(pxa_runtime_t *runtime,
                             pxa_component_t component_ref,
                             pxa_resource_type_t type,
                             pxa_authority_t authority,
                             const pxa_resource_t *resource,
                             pxa_handle_t *output) {
    uint32_t component_index;
    pxa_component_slot_t *component;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = PXA_HANDLE_INVALID;
    if (type == PXA_RESOURCE_UNKNOWN || resource == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    component = pxa_runtime_find_component(runtime, component_ref,
                                           &component_index);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state != PXA_COMPONENT_STARTING &&
        component->state != PXA_COMPONENT_RUNNING) {
        return PXA_STATUS_BAD_STATE;
    }
    if (authority != 0 && pxa_runtime_authority_is_revoked(
                              runtime, component_index, authority)) {
        return PXA_STATUS_DENIED;
    }
    return pxa_resource_table_open(
        &runtime->resources, &component->resources, component_index, type,
        authority, resource, output);
}

pxa_status_t pxa_handle_open64(pxa_runtime_t *runtime,
                               pxa_component_t component_ref,
                               pxa_resource_type_t type,
                               pxa_authority_t authority,
                               const pxa_resource_t *resource,
                               pxa_handle64_t *output) {
    uint32_t component_index;
    pxa_component_slot_t *component;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = PXA_HANDLE64_INVALID;
    if (type == PXA_RESOURCE_UNKNOWN || resource == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    component = pxa_runtime_find_component(runtime, component_ref,
                                           &component_index);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state != PXA_COMPONENT_STARTING &&
        component->state != PXA_COMPONENT_RUNNING)
        return PXA_STATUS_BAD_STATE;
    if (authority != 0 && pxa_runtime_authority_is_revoked(
                              runtime, component_index, authority))
        return PXA_STATUS_DENIED;
    return pxa_resource_table_open64(
        &runtime->resources, &component->resources, component_index, type,
        authority, resource, output);
}

pxa_status_t pxa_handle_get(const pxa_runtime_t *runtime,
                            pxa_component_t component_ref,
                            pxa_handle_t handle,
                            pxa_resource_type_t expected_type,
                            pxa_resource_t *output) {
    uint32_t component_index;
    const pxa_component_slot_t *component;
    if (output == NULL || expected_type == PXA_RESOURCE_UNKNOWN) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    memset(output, 0, sizeof(*output));
    component = pxa_runtime_find_component_const(runtime, component_ref,
                                                 &component_index);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state != PXA_COMPONENT_STARTING &&
        component->state != PXA_COMPONENT_RUNNING &&
        component->state != PXA_COMPONENT_STOP_REQUESTED) {
        return PXA_STATUS_BAD_STATE;
    }
    return pxa_resource_table_get(&runtime->resources, component_index,
                                  handle, expected_type, output);
}

pxa_status_t pxa_handle_get64(const pxa_runtime_t *runtime,
                              pxa_component_t component_ref,
                              pxa_handle64_t handle,
                              pxa_resource_type_t expected_type,
                              pxa_resource_t *output) {
    uint32_t component_index;
    const pxa_component_slot_t *component;
    if (output == NULL || expected_type == PXA_RESOURCE_UNKNOWN)
        return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    component = pxa_runtime_find_component_const(runtime, component_ref,
                                                 &component_index);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state != PXA_COMPONENT_STARTING &&
        component->state != PXA_COMPONENT_RUNNING &&
        component->state != PXA_COMPONENT_STOP_REQUESTED)
        return PXA_STATUS_BAD_STATE;
    return pxa_resource_table_get64(&runtime->resources, component_index,
                                    handle, expected_type, output);
}

pxa_status_t pxa_handle_close(pxa_runtime_t *runtime,
                              pxa_component_t component_ref,
                              pxa_handle_t handle) {
    uint32_t component_index;
    pxa_component_slot_t *component = pxa_runtime_find_component(
        runtime, component_ref, &component_index);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state != PXA_COMPONENT_STARTING &&
        component->state != PXA_COMPONENT_RUNNING &&
        component->state != PXA_COMPONENT_STOP_REQUESTED) {
        return PXA_STATUS_BAD_STATE;
    }
    return pxa_resource_table_close(&runtime->resources,
                                    &component->resources, component_index,
                                    handle);
}

pxa_status_t pxa_handle_close64(pxa_runtime_t *runtime,
                                pxa_component_t component_ref,
                                pxa_handle64_t handle) {
    uint32_t component_index;
    pxa_component_slot_t *component = pxa_runtime_find_component(
        runtime, component_ref, &component_index);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state != PXA_COMPONENT_STARTING &&
        component->state != PXA_COMPONENT_RUNNING &&
        component->state != PXA_COMPONENT_STOP_REQUESTED)
        return PXA_STATUS_BAD_STATE;
    return pxa_resource_table_close64(&runtime->resources,
                                      &component->resources, component_index,
                                      handle);
}

pxa_status_t pxa_handle_widen(const pxa_runtime_t *runtime,
                              pxa_component_t component_ref,
                              pxa_handle_t legacy_handle,
                              pxa_handle64_t *output) {
    uint32_t component_index;
    const pxa_component_slot_t *component;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = PXA_HANDLE64_INVALID;
    component = pxa_runtime_find_component_const(runtime, component_ref,
                                                 &component_index);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state != PXA_COMPONENT_STARTING &&
        component->state != PXA_COMPONENT_RUNNING &&
        component->state != PXA_COMPONENT_STOP_REQUESTED)
        return PXA_STATUS_BAD_STATE;
    return pxa_resource_table_widen(&runtime->resources, component_index,
                                    legacy_handle, output);
}
