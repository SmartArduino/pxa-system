#include "core/request_internal.h"
#include "core/runtime_internal.h"

#include <string.h>

#define PXA_REQUEST_INDEX_NONE UINT32_MAX

uint32_t pxa_request_table_bucket_count(uint16_t per_owner_capacity) {
    uint32_t target = (uint32_t)per_owner_capacity * 2u;
    uint32_t result = 1;
    while (result < target && result <= UINT32_MAX / 2u) result *= 2u;
    return result;
}

static uint32_t request_hash(const pxa_request_table_t *table,
                             uint32_t request_id) {
    return (request_id * UINT32_C(2654435761)) &
           (table->buckets_per_owner - 1u);
}

static uint32_t *owner_buckets(pxa_request_table_t *table,
                               uint32_t owner_index) {
    return table->buckets +
           (size_t)owner_index * table->buckets_per_owner;
}

static const uint32_t *owner_buckets_const(
    const pxa_request_table_t *table, uint32_t owner_index) {
    return table->buckets +
           (size_t)owner_index * table->buckets_per_owner;
}

static uint32_t find_request_index(const pxa_request_table_t *table,
                                   uint32_t owner_index,
                                   uint32_t request_id) {
    uint32_t index;
    if (table == NULL || table->slots == NULL || table->buckets == NULL ||
        table->buckets_per_owner == 0 || owner_index >= table->owner_capacity) {
        return PXA_REQUEST_INDEX_NONE;
    }
    index = owner_buckets_const(table, owner_index)
        [request_hash(table, request_id)];
    while (index != PXA_REQUEST_INDEX_NONE) {
        const pxa_request_slot_t *request = &table->slots[index];
        if (request->occupied && request->owner_index == owner_index &&
            request->request_id == request_id) {
            return index;
        }
        index = request->hash_next;
    }
    return PXA_REQUEST_INDEX_NONE;
}

static pxa_status_t create_completion_event(
    pxa_event_pool_t *event_pool, uint32_t owner_index,
    const pxa_request_slot_t *request, pxa_status_t result,
    const void *payload, size_t payload_size, uint32_t *output) {
    uint8_t header[PXA_ENVELOPE_SIZE];
    uint8_t status_bytes[4];
    size_t event_size;
    pxa_event_writer_t writer;
    pxa_status_t status;
    if (payload_size > PXA_MAX_CONTROL_MESSAGE - PXA_ENVELOPE_SIZE - 4u) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    event_size = PXA_ENVELOPE_SIZE + 4u + payload_size;
    status = pxa_event_pool_allocate(event_pool, owner_index, event_size, 1, 0,
                                     output);
    if (status != PXA_STATUS_OK) return status;
    pxa_write_u16(header, request->service);
    pxa_write_u16(header + 2, request->opcode);
    pxa_write_u32(header + 4, request->request_id);
    pxa_write_u32(header + 8, (uint32_t)(4u + payload_size));
    pxa_write_u32(status_bytes, (uint32_t)result);
    pxa_event_writer_init(&writer, event_pool, *output);
    if (pxa_event_writer_write(&writer, header, sizeof(header)) !=
            PXA_STATUS_OK ||
        pxa_event_writer_write(&writer, status_bytes, sizeof(status_bytes)) !=
            PXA_STATUS_OK ||
        pxa_event_writer_write(&writer, payload, payload_size) !=
            PXA_STATUS_OK ||
        writer.written != event_size) {
        pxa_event_pool_release(event_pool, *output);
        *output = PXA_REQUEST_INDEX_NONE;
        return PXA_STATUS_INTERNAL;
    }
    return PXA_STATUS_OK;
}

static void completion_enqueue(pxa_request_table_t *table,
                               pxa_request_owner_t *owner,
                               uint32_t request_index) {
    pxa_request_slot_t *request = &table->slots[request_index];
    if (request->completion_queued) return;
    request->completion_queued = 1;
    request->completion_next = PXA_REQUEST_INDEX_NONE;
    if (owner->completion_tail == PXA_REQUEST_INDEX_NONE) {
        owner->completion_head = request_index;
    } else {
        table->slots[owner->completion_tail].completion_next = request_index;
    }
    owner->completion_tail = request_index;
}

static void remove_request(pxa_request_table_t *table,
                           pxa_request_owner_t *owner, uint32_t owner_index,
                           uint32_t request_index, int keep_event,
                           pxa_event_pool_t *event_pool) {
    pxa_request_slot_t *request = &table->slots[request_index];
    uint32_t *bucket = &owner_buckets(table, owner_index)
        [request_hash(table, request->request_id)];
    uint32_t *hash_link = bucket;
    uint32_t *owner_link = &owner->request_head;
    while (*hash_link != PXA_REQUEST_INDEX_NONE &&
           *hash_link != request_index) {
        hash_link = &table->slots[*hash_link].hash_next;
    }
    if (*hash_link == request_index) *hash_link = request->hash_next;
    while (*owner_link != PXA_REQUEST_INDEX_NONE &&
           *owner_link != request_index) {
        owner_link = &table->slots[*owner_link].owner_next;
    }
    if (*owner_link == request_index) *owner_link = request->owner_next;
    if (!keep_event && request->completion_event != PXA_REQUEST_INDEX_NONE) {
        pxa_event_pool_release(event_pool, request->completion_event);
    }
    memset(request, 0, sizeof(*request));
    request->completion_event = PXA_REQUEST_INDEX_NONE;
    request->next_free = table->free_head;
    table->free_head = request_index;
    if (owner->count != 0) owner->count--;
    if (table->count != 0) table->count--;
}

void pxa_request_table_init(pxa_request_table_t *table,
                            pxa_request_slot_t *slots, uint16_t capacity,
                            uint32_t *buckets, uint32_t buckets_per_owner,
                            uint16_t owner_capacity) {
    size_t bucket_total = (size_t)buckets_per_owner * owner_capacity;
    size_t bucket_index;
    uint32_t index;
    if (table == NULL) return;
    memset(table, 0, sizeof(*table));
    table->slots = slots;
    table->buckets = buckets;
    table->capacity = capacity;
    table->owner_capacity = owner_capacity;
    table->buckets_per_owner = buckets_per_owner;
    if (slots != NULL) {
        memset(slots, 0, (size_t)capacity * sizeof(*slots));
        for (index = 0; index < capacity; ++index) {
            slots[index].completion_event = PXA_REQUEST_INDEX_NONE;
            slots[index].next_free = index + 1u < capacity
                                         ? index + 1u
                                         : PXA_REQUEST_INDEX_NONE;
        }
    }
    if (buckets != NULL) {
        for (bucket_index = 0; bucket_index < bucket_total; ++bucket_index) {
            buckets[bucket_index] = PXA_REQUEST_INDEX_NONE;
        }
    }
    table->free_head = capacity == 0 ? PXA_REQUEST_INDEX_NONE : 0;
}

void pxa_request_owner_init(pxa_request_owner_t *owner) {
    if (owner == NULL) return;
    owner->request_head = PXA_REQUEST_INDEX_NONE;
    owner->completion_head = PXA_REQUEST_INDEX_NONE;
    owner->completion_tail = PXA_REQUEST_INDEX_NONE;
    owner->count = 0;
}

uint16_t pxa_request_owner_count(const pxa_request_owner_t *owner) {
    return owner == NULL ? 0 : owner->count;
}

pxa_status_t pxa_request_table_begin(
    pxa_request_table_t *table, pxa_request_owner_t *owner,
    uint32_t owner_index, uint16_t per_owner_capacity, uint32_t request_id,
    uint16_t service, uint16_t opcode, pxa_authority_t authority) {
    uint32_t request_index;
    uint32_t hash;
    pxa_request_slot_t *request;
    if (table == NULL || owner == NULL || request_id == 0 || service == 0 ||
        opcode == 0 || owner_index >= table->owner_capacity) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (find_request_index(table, owner_index, request_id) !=
        PXA_REQUEST_INDEX_NONE) {
        return PXA_STATUS_BUSY;
    }
    if (owner->count >= per_owner_capacity ||
        table->free_head == PXA_REQUEST_INDEX_NONE) {
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    request_index = table->free_head;
    request = &table->slots[request_index];
    table->free_head = request->next_free;
    memset(request, 0, sizeof(*request));
    request->occupied = 1;
    request->owner_index = (uint16_t)owner_index;
    request->request_id = request_id;
    request->service = service;
    request->opcode = opcode;
    request->authority = authority;
    request->completion_event = PXA_REQUEST_INDEX_NONE;
    request->owner_next = owner->request_head;
    owner->request_head = request_index;
    hash = request_hash(table, request_id);
    request->hash_next = owner_buckets(table, owner_index)[hash];
    owner_buckets(table, owner_index)[hash] = request_index;
    owner->count++;
    ++table->count;
    if (table->count > table->peak) table->peak = table->count;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_request_table_commit(pxa_request_table_t *table,
                                      uint32_t owner_index,
                                      uint32_t request_id) {
    uint32_t request_index =
        find_request_index(table, owner_index, request_id);
    pxa_request_slot_t *request;
    if (request_index == PXA_REQUEST_INDEX_NONE) return PXA_STATUS_NOT_FOUND;
    request = &table->slots[request_index];
    if (request->cancelling || request->completion_queued ||
        request->completion_event != PXA_REQUEST_INDEX_NONE) {
        return PXA_STATUS_CANCELLED;
    }
    request->committed = 1;
    return PXA_STATUS_OK;
}

void pxa_request_table_flush(pxa_request_table_t *table,
                             pxa_request_owner_t *owner,
                             uint32_t owner_index,
                             pxa_event_pool_t *event_pool,
                             pxa_event_mailbox_t *mailbox,
                             uint16_t mailbox_capacity) {
    while (pxa_event_mailbox_count(mailbox) < mailbox_capacity &&
           owner->completion_head != PXA_REQUEST_INDEX_NONE) {
        uint32_t request_index = owner->completion_head;
        pxa_request_slot_t *request = &table->slots[request_index];
        uint32_t event_index = request->completion_event;
        if (event_index == PXA_REQUEST_INDEX_NONE) {
            if (create_completion_event(event_pool, owner_index, request,
                                        PXA_STATUS_CANCELLED, NULL, 0,
                                        &event_index) != PXA_STATUS_OK) {
                return;
            }
            request->completion_event = event_index;
        }
        owner->completion_head = request->completion_next;
        if (owner->completion_head == PXA_REQUEST_INDEX_NONE) {
            owner->completion_tail = PXA_REQUEST_INDEX_NONE;
        }
        pxa_event_mailbox_append(event_pool, mailbox, event_index);
        request->completion_event = PXA_REQUEST_INDEX_NONE;
        remove_request(table, owner, owner_index, request_index, 1,
                       event_pool);
    }
}

pxa_status_t pxa_request_table_complete(
    pxa_request_table_t *table, pxa_request_owner_t *owner,
    uint32_t owner_index, pxa_event_pool_t *event_pool,
    pxa_event_mailbox_t *mailbox, uint16_t mailbox_capacity,
    uint32_t request_id, pxa_status_t result, const void *payload,
    size_t payload_size) {
    uint32_t request_index =
        find_request_index(table, owner_index, request_id);
    uint32_t event_index;
    pxa_request_slot_t *request;
    pxa_status_t status;
    if (request_index == PXA_REQUEST_INDEX_NONE) return PXA_STATUS_NOT_FOUND;
    request = &table->slots[request_index];
    if (request->completion_queued ||
        request->completion_event != PXA_REQUEST_INDEX_NONE) {
        return PXA_STATUS_NOT_FOUND;
    }
    status = create_completion_event(event_pool, owner_index, request, result,
                                     payload, payload_size, &event_index);
    if (status != PXA_STATUS_OK) return status;
    request->completion_event = event_index;
    completion_enqueue(table, owner, request_index);
    pxa_request_table_flush(table, owner, owner_index, event_pool, mailbox,
                            mailbox_capacity);
    return PXA_STATUS_OK;
}

pxa_status_t pxa_request_table_cancel(
    pxa_request_table_t *table, pxa_request_owner_t *owner,
    uint32_t owner_index, pxa_event_pool_t *event_pool,
    pxa_event_mailbox_t *mailbox, uint16_t mailbox_capacity,
    uint32_t request_id) {
    uint32_t request_index =
        find_request_index(table, owner_index, request_id);
    pxa_request_slot_t *request;
    if (request_index == PXA_REQUEST_INDEX_NONE) return PXA_STATUS_OK;
    request = &table->slots[request_index];
    if (request->committed || request->completion_queued ||
        request->completion_event != PXA_REQUEST_INDEX_NONE) {
        return PXA_STATUS_OK;
    }
    request->cancelling = 1;
    completion_enqueue(table, owner, request_index);
    pxa_request_table_flush(table, owner, owner_index, event_pool, mailbox,
                            mailbox_capacity);
    return PXA_STATUS_OK;
}

int pxa_request_table_is_active(const pxa_request_table_t *table,
                                uint32_t owner_index, uint32_t request_id) {
    uint32_t request_index =
        find_request_index(table, owner_index, request_id);
    const pxa_request_slot_t *request;
    if (request_index == PXA_REQUEST_INDEX_NONE) return 0;
    request = &table->slots[request_index];
    return !request->cancelling && !request->completion_queued &&
           request->completion_event == PXA_REQUEST_INDEX_NONE;
}

void pxa_request_table_revoke_authority(
    pxa_request_table_t *table, pxa_request_owner_t *owner,
    uint32_t owner_index, pxa_event_pool_t *event_pool,
    pxa_event_mailbox_t *mailbox, uint16_t mailbox_capacity,
    pxa_authority_t authority) {
    uint32_t index = owner->request_head;
    while (index != PXA_REQUEST_INDEX_NONE) {
        pxa_request_slot_t *request = &table->slots[index];
        if (request->authority == authority && !request->committed &&
            !request->completion_queued &&
            request->completion_event == PXA_REQUEST_INDEX_NONE) {
            request->cancelling = 1;
            completion_enqueue(table, owner, index);
        }
        index = request->owner_next;
    }
    pxa_request_table_flush(table, owner, owner_index, event_pool, mailbox,
                            mailbox_capacity);
}

void pxa_request_table_clear_owner(pxa_request_table_t *table,
                                   pxa_request_owner_t *owner,
                                   uint32_t owner_index,
                                   pxa_event_pool_t *event_pool) {
    uint32_t index = owner->request_head;
    uint32_t bucket;
    while (index != PXA_REQUEST_INDEX_NONE) {
        uint32_t next = table->slots[index].owner_next;
        if (table->slots[index].completion_event != PXA_REQUEST_INDEX_NONE) {
            pxa_event_pool_release(event_pool,
                                   table->slots[index].completion_event);
        }
        memset(&table->slots[index], 0, sizeof(table->slots[index]));
        table->slots[index].completion_event = PXA_REQUEST_INDEX_NONE;
        table->slots[index].next_free = table->free_head;
        table->free_head = index;
        index = next;
    }
    for (bucket = 0; bucket < table->buckets_per_owner; ++bucket) {
        owner_buckets(table, owner_index)[bucket] = PXA_REQUEST_INDEX_NONE;
    }
    pxa_request_owner_init(owner);
}

pxa_status_t pxa_request_begin(pxa_runtime_t *runtime,
                               pxa_component_t component_ref,
                               uint32_t request_id, uint16_t service,
                               uint16_t opcode, pxa_authority_t authority) {
    uint32_t component_index;
    pxa_component_slot_t *component = pxa_runtime_find_component(
        runtime, component_ref, &component_index);
    if (request_id == 0 || service == 0 || opcode == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state != PXA_COMPONENT_STARTING &&
        component->state != PXA_COMPONENT_RUNNING) {
        return PXA_STATUS_BAD_STATE;
    }
    if (authority != 0 && pxa_runtime_authority_is_revoked(
                              runtime, component_index, authority)) {
        return PXA_STATUS_DENIED;
    }
    return pxa_request_table_begin(
        &runtime->requests, &component->requests, component_index,
        runtime->limits.max_requests_per_component, request_id, service,
        opcode, authority);
}

pxa_status_t pxa_request_commit(pxa_runtime_t *runtime,
                                pxa_component_t component_ref,
                                uint32_t request_id) {
    uint32_t component_index;
    pxa_component_slot_t *component = pxa_runtime_find_component(
        runtime, component_ref, &component_index);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    return pxa_request_table_commit(&runtime->requests, component_index,
                                    request_id);
}

pxa_status_t pxa_request_complete(pxa_runtime_t *runtime,
                                  pxa_component_t component_ref,
                                  uint32_t request_id, pxa_status_t result,
                                  const void *payload, size_t payload_size) {
    uint32_t component_index;
    pxa_component_slot_t *component;
    if (request_id == 0 || !pxa_status_is_known(result) ||
        (payload == NULL && payload_size != 0) ||
        (result != PXA_STATUS_OK && payload_size != 0)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    component = pxa_runtime_find_component(runtime, component_ref,
                                           &component_index);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state != PXA_COMPONENT_STARTING &&
        component->state != PXA_COMPONENT_RUNNING &&
        component->state != PXA_COMPONENT_STOP_REQUESTED) {
        return PXA_STATUS_BAD_STATE;
    }
    return pxa_request_table_complete(
        &runtime->requests, &component->requests, component_index,
        &runtime->event_pool, &component->mailbox,
        runtime->limits.mailbox_capacity, request_id, result, payload,
        payload_size);
}

pxa_status_t pxa_request_cancel(pxa_runtime_t *runtime,
                                pxa_component_t component_ref,
                                uint32_t request_id) {
    uint32_t component_index;
    pxa_component_slot_t *component;
    if (request_id == 0) return PXA_STATUS_INVALID_ARGUMENT;
    component = pxa_runtime_find_component(runtime, component_ref,
                                           &component_index);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state != PXA_COMPONENT_STARTING &&
        component->state != PXA_COMPONENT_RUNNING &&
        component->state != PXA_COMPONENT_STOP_REQUESTED) {
        return PXA_STATUS_BAD_STATE;
    }
    return pxa_request_table_cancel(
        &runtime->requests, &component->requests, component_index,
        &runtime->event_pool, &component->mailbox,
        runtime->limits.mailbox_capacity, request_id);
}

int pxa_request_is_active(const pxa_runtime_t *runtime,
                          pxa_component_t component_ref,
                          uint32_t request_id) {
    uint32_t component_index;
    if (!pxa_runtime_is_valid(runtime) || request_id == 0 ||
        pxa_runtime_find_component_const(runtime, component_ref,
                                         &component_index) == NULL) {
        return 0;
    }
    return pxa_request_table_is_active(&runtime->requests, component_index,
                                       request_id);
}
