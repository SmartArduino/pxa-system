#include "core/event_internal.h"
#include "core/runtime_internal.h"

#include "core/slot_token.h"

#include <string.h>

#define PXA_EVENT_INDEX_NONE UINT32_MAX

static const pxa_event_slot_t *find_event(const pxa_event_pool_t *pool,
                                          pxa_event_token_t token,
                                          uint32_t *index_out) {
    uint32_t index;
    uint16_t generation;
    const pxa_event_slot_t *event;
    if (pool == NULL || pool->slots == NULL ||
        !pxa_internal_slot_token_decode(token, pool->event_capacity, &index,
                                        &generation)) {
        return NULL;
    }
    event = &pool->slots[index];
    if (!event->occupied || event->generation != generation) return NULL;
    if (index_out != NULL) *index_out = index;
    return event;
}

static void release_blocks(pxa_event_pool_t *pool, uint32_t first) {
    uint32_t block = first;
    while (block != PXA_EVENT_INDEX_NONE) {
        uint32_t next = pool->block_next[block];
        pool->block_next[block] = pool->block_free_head;
        pool->block_free_head = block;
        pool->block_free_count++;
        block = next;
    }
}

static size_t event_copy(const pxa_event_pool_t *pool,
                         const pxa_event_slot_t *event, size_t offset,
                         void *output, size_t capacity) {
    uint32_t block = event->first_block;
    size_t skip = offset;
    size_t remaining = (size_t)event->size - offset;
    size_t copied = 0;
    if (remaining > capacity) remaining = capacity;
    while (skip >= pool->block_size) {
        block = pool->block_next[block];
        skip -= pool->block_size;
    }
    while (copied < remaining) {
        size_t available = pool->block_size - skip;
        size_t part = remaining - copied < available
                          ? remaining - copied
                          : available;
        memcpy((uint8_t *)output + copied,
               pool->block_data + (size_t)block * pool->block_size + skip,
               part);
        copied += part;
        skip = 0;
        block = pool->block_next[block];
    }
    return copied;
}

void pxa_event_pool_init(pxa_event_pool_t *pool, pxa_event_slot_t *slots,
                         uint16_t event_capacity, uint32_t *block_next,
                         uint8_t *block_data, uint16_t block_size,
                         uint16_t block_count) {
    uint32_t index;
    if (pool == NULL) return;
    memset(pool, 0, sizeof(*pool));
    pool->slots = slots;
    pool->block_next = block_next;
    pool->block_data = block_data;
    pool->event_capacity = event_capacity;
    pool->block_size = block_size;
    pool->block_count = block_count;
    if (slots != NULL) {
        memset(slots, 0, (size_t)event_capacity * sizeof(*slots));
        for (index = 0; index < event_capacity; ++index) {
            slots[index].generation = 1;
            slots[index].next_free = index + 1u < event_capacity
                                         ? index + 1u
                                         : PXA_EVENT_INDEX_NONE;
        }
    }
    if (block_next != NULL) {
        for (index = 0; index < block_count; ++index) {
            block_next[index] = index + 1u < block_count
                                    ? index + 1u
                                    : PXA_EVENT_INDEX_NONE;
        }
    }
    pool->event_free_head = event_capacity == 0 ? PXA_EVENT_INDEX_NONE : 0;
    pool->block_free_head = block_count == 0 ? PXA_EVENT_INDEX_NONE : 0;
    pool->block_free_count = block_count;
}

void pxa_event_mailbox_init(pxa_event_mailbox_t *mailbox) {
    if (mailbox == NULL) return;
    mailbox->head = PXA_EVENT_INDEX_NONE;
    mailbox->tail = PXA_EVENT_INDEX_NONE;
    mailbox->count = 0;
}

uint16_t pxa_event_mailbox_count(const pxa_event_mailbox_t *mailbox) {
    return mailbox == NULL ? 0 : mailbox->count;
}

pxa_status_t pxa_event_pool_allocate(pxa_event_pool_t *pool,
                                     uint32_t owner_index, size_t size,
                                     uint8_t reliable, uint64_t coalesce_key,
                                     uint32_t *output) {
    size_t needed;
    uint32_t event_index;
    uint32_t first = PXA_EVENT_INDEX_NONE;
    uint32_t last = PXA_EVENT_INDEX_NONE;
    size_t count;
    if (pool == NULL || pool->slots == NULL || pool->block_next == NULL ||
        pool->block_data == NULL || pool->block_size == 0 || size == 0 ||
        size > PXA_MAX_CONTROL_MESSAGE || output == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    needed = (size + pool->block_size - 1u) / pool->block_size;
    if (pool->event_free_head == PXA_EVENT_INDEX_NONE ||
        needed > pool->block_free_count) {
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    event_index = pool->event_free_head;
    pool->event_free_head = pool->slots[event_index].next_free;
    for (count = 0; count < needed; ++count) {
        uint32_t block = pool->block_free_head;
        pool->block_free_head = pool->block_next[block];
        pool->block_next[block] = PXA_EVENT_INDEX_NONE;
        pool->block_free_count--;
        if (first == PXA_EVENT_INDEX_NONE) first = block;
        if (last != PXA_EVENT_INDEX_NONE) pool->block_next[last] = block;
        last = block;
    }
    pool->slots[event_index].occupied = 1;
    pool->slots[event_index].owner_index = (uint16_t)owner_index;
    pool->slots[event_index].size = (uint32_t)size;
    pool->slots[event_index].first_block = first;
    pool->slots[event_index].next_queue = PXA_EVENT_INDEX_NONE;
    pool->slots[event_index].reliable = reliable != 0;
    pool->slots[event_index].coalesce_key = coalesce_key;
    ++pool->event_count;
    if (pool->event_count > pool->event_peak)
        pool->event_peak = pool->event_count;
    if (pool->block_count - pool->block_free_count > pool->block_peak)
        pool->block_peak =
            (uint16_t)(pool->block_count - pool->block_free_count);
    *output = event_index;
    return PXA_STATUS_OK;
}

void pxa_event_pool_release(pxa_event_pool_t *pool, uint32_t event_index) {
    pxa_event_slot_t *event;
    if (pool == NULL || pool->slots == NULL ||
        event_index >= pool->event_capacity) {
        return;
    }
    event = &pool->slots[event_index];
    if (!event->occupied) return;
    release_blocks(pool, event->first_block);
    event->occupied = 0;
    event->first_block = PXA_EVENT_INDEX_NONE;
    event->next_queue = PXA_EVENT_INDEX_NONE;
    event->size = 0;
    event->coalesce_key = 0;
    event->owner_index = 0;
    event->reliable = 0;
    if (pool->event_count != 0) --pool->event_count;
    if (event->generation == UINT16_MAX) {
        event->next_free = PXA_EVENT_INDEX_NONE;
    } else {
        event->generation++;
        event->next_free = pool->event_free_head;
        pool->event_free_head = event_index;
    }
}

void pxa_event_writer_init(pxa_event_writer_t *writer,
                           pxa_event_pool_t *pool, uint32_t event_index) {
    if (writer == NULL) return;
    writer->pool = pool;
    writer->block = pool->slots[event_index].first_block;
    writer->block_offset = 0;
    writer->written = 0;
}

pxa_status_t pxa_event_writer_write(pxa_event_writer_t *writer,
                                    const void *data, size_t size) {
    const uint8_t *source = (const uint8_t *)data;
    size_t remaining = size;
    if (writer == NULL || writer->pool == NULL ||
        (data == NULL && size != 0)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    while (remaining != 0) {
        size_t available;
        size_t copied;
        if (writer->block == PXA_EVENT_INDEX_NONE) return PXA_STATUS_INTERNAL;
        available = writer->pool->block_size - writer->block_offset;
        copied = remaining < available ? remaining : available;
        memcpy(writer->pool->block_data +
                   (size_t)writer->block * writer->pool->block_size +
                   writer->block_offset,
               source, copied);
        source += copied;
        remaining -= copied;
        writer->written += copied;
        writer->block_offset += copied;
        if (writer->block_offset == writer->pool->block_size) {
            writer->block = writer->pool->block_next[writer->block];
            writer->block_offset = 0;
        }
    }
    return PXA_STATUS_OK;
}

void pxa_event_mailbox_append(pxa_event_pool_t *pool,
                              pxa_event_mailbox_t *mailbox,
                              uint32_t event_index) {
    pool->slots[event_index].next_queue = PXA_EVENT_INDEX_NONE;
    if (mailbox->tail == PXA_EVENT_INDEX_NONE) {
        mailbox->head = event_index;
    } else {
        pool->slots[mailbox->tail].next_queue = event_index;
    }
    mailbox->tail = event_index;
    mailbox->count++;
}

void pxa_event_mailbox_clear(pxa_event_pool_t *pool,
                             pxa_event_mailbox_t *mailbox) {
    uint32_t index;
    if (pool == NULL || mailbox == NULL) return;
    index = mailbox->head;
    while (index != PXA_EVENT_INDEX_NONE) {
        uint32_t next = pool->slots[index].next_queue;
        pxa_event_pool_release(pool, index);
        index = next;
    }
    pxa_event_mailbox_init(mailbox);
}

static pxa_status_t event_mailbox_post_parts(
    pxa_event_pool_t *pool, pxa_event_mailbox_t *mailbox,
    uint32_t owner_index, uint16_t mailbox_capacity,
    uint16_t reliable_reserve, pxa_bytes_t prefix,
    const pxa_bytes_t *parts, size_t part_count, uint8_t reliable,
    uint64_t coalesce_key) {
    uint32_t event_index;
    uint32_t existing = PXA_EVENT_INDEX_NONE;
    uint32_t previous = PXA_EVENT_INDEX_NONE;
    uint16_t transient_limit;
    pxa_event_writer_t writer;
    pxa_status_t status;
    size_t message_size = prefix.size;
    size_t part_index;
    if (pool == NULL || mailbox == NULL || prefix.data == NULL ||
        prefix.size == 0 || prefix.size > PXA_MAX_CONTROL_MESSAGE ||
        (parts == NULL && part_count != 0) ||
        reliable_reserve > mailbox_capacity) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    for (part_index = 0; part_index < part_count; ++part_index) {
        if ((parts[part_index].data == NULL && parts[part_index].size != 0) ||
            parts[part_index].size >
                PXA_MAX_CONTROL_MESSAGE - message_size) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        message_size += parts[part_index].size;
    }
    if (message_size == 0) return PXA_STATUS_INVALID_ARGUMENT;
    if (!reliable) {
        uint32_t scan;
        if (coalesce_key == 0) return PXA_STATUS_INVALID_ARGUMENT;
        scan = mailbox->head;
        while (scan != PXA_EVENT_INDEX_NONE) {
            pxa_event_slot_t *event = &pool->slots[scan];
            if (!event->reliable && event->coalesce_key == coalesce_key) {
                existing = scan;
                break;
            }
            previous = scan;
            scan = event->next_queue;
        }
        transient_limit = mailbox_capacity - reliable_reserve;
        if (existing == PXA_EVENT_INDEX_NONE &&
            mailbox->count >= transient_limit) {
            return PXA_STATUS_BUSY;
        }
    } else if (mailbox->count >= mailbox_capacity) {
        return PXA_STATUS_BUSY;
    }
    status = pxa_event_pool_allocate(pool, owner_index, message_size, reliable,
                                     coalesce_key, &event_index);
    if (status != PXA_STATUS_OK) return status;
    pxa_event_writer_init(&writer, pool, event_index);
    if (pxa_event_writer_write(&writer, prefix.data, prefix.size) !=
        PXA_STATUS_OK) {
        pxa_event_pool_release(pool, event_index);
        return PXA_STATUS_INTERNAL;
    }
    for (part_index = 0; part_index < part_count; ++part_index) {
        if (pxa_event_writer_write(&writer, parts[part_index].data,
                                   parts[part_index].size) != PXA_STATUS_OK) {
            pxa_event_pool_release(pool, event_index);
            return PXA_STATUS_INTERNAL;
        }
    }
    if (writer.written != message_size) {
        pxa_event_pool_release(pool, event_index);
        return PXA_STATUS_INTERNAL;
    }
    if (existing != PXA_EVENT_INDEX_NONE) {
        pxa_event_slot_t *old = &pool->slots[existing];
        pool->slots[event_index].next_queue = old->next_queue;
        if (previous == PXA_EVENT_INDEX_NONE) {
            mailbox->head = event_index;
        } else {
            pool->slots[previous].next_queue = event_index;
        }
        if (mailbox->tail == existing) mailbox->tail = event_index;
        pxa_event_pool_release(pool, existing);
    } else {
        pxa_event_mailbox_append(pool, mailbox, event_index);
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_event_mailbox_post(
    pxa_event_pool_t *pool, pxa_event_mailbox_t *mailbox,
    uint32_t owner_index, uint16_t mailbox_capacity,
    uint16_t reliable_reserve, const void *message, size_t message_size,
    uint8_t reliable, uint64_t coalesce_key) {
    pxa_bytes_t part = {(const uint8_t *)message, message_size};
    return event_mailbox_post_parts(
        pool, mailbox, owner_index, mailbox_capacity, reliable_reserve,
        part, NULL, 0, reliable, coalesce_key);
}

pxa_status_t pxa_event_mailbox_peek(const pxa_event_pool_t *pool,
                                    const pxa_event_mailbox_t *mailbox,
                                    pxa_event_view_t *output) {
    const pxa_event_slot_t *event;
    if (pool == NULL || mailbox == NULL || output == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (mailbox->head == PXA_EVENT_INDEX_NONE) return PXA_STATUS_WOULD_BLOCK;
    event = &pool->slots[mailbox->head];
    output->token = pxa_internal_slot_token_encode(mailbox->head,
                                                   event->generation);
    output->size = event->size;
    output->coalesce_key = event->coalesce_key;
    output->reliable = event->reliable;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_event_pool_read(const pxa_event_pool_t *pool,
                                 pxa_event_token_t token, size_t offset,
                                 void *output, size_t capacity,
                                 size_t *read_size) {
    const pxa_event_slot_t *event;
    if (pool == NULL || read_size == NULL ||
        (output == NULL && capacity != 0)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *read_size = 0;
    event = find_event(pool, token, NULL);
    if (event == NULL) return PXA_STATUS_NOT_FOUND;
    if (offset > event->size) return PXA_STATUS_INVALID_ARGUMENT;
    *read_size = event_copy(pool, event, offset, output, capacity);
    return PXA_STATUS_OK;
}

pxa_status_t pxa_event_mailbox_pop(pxa_event_pool_t *pool,
                                   pxa_event_mailbox_t *mailbox,
                                   void *output, size_t capacity,
                                   size_t *event_size) {
    uint32_t event_index;
    const pxa_event_slot_t *event;
    if (pool == NULL || mailbox == NULL || event_size == NULL ||
        (output == NULL && capacity != 0)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *event_size = 0;
    event_index = mailbox->head;
    if (event_index == PXA_EVENT_INDEX_NONE) return PXA_STATUS_WOULD_BLOCK;
    event = &pool->slots[event_index];
    if (capacity < event->size) {
        *event_size = event->size;
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    *event_size = event_copy(pool, event, 0, output, capacity);
    mailbox->head = event->next_queue;
    if (mailbox->head == PXA_EVENT_INDEX_NONE) {
        mailbox->tail = PXA_EVENT_INDEX_NONE;
    }
    if (mailbox->count != 0) mailbox->count--;
    pxa_event_pool_release(pool, event_index);
    return PXA_STATUS_OK;
}

pxa_status_t pxa_event_mailbox_consume(pxa_event_pool_t *pool,
                                       pxa_event_mailbox_t *mailbox,
                                       uint32_t owner_index,
                                       pxa_event_token_t token) {
    uint32_t event_index;
    const pxa_event_slot_t *event;
    if (pool == NULL || mailbox == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    event = find_event(pool, token, &event_index);
    if (event == NULL || event_index != mailbox->head ||
        event->owner_index != owner_index) {
        return PXA_STATUS_NOT_FOUND;
    }
    mailbox->head = event->next_queue;
    if (mailbox->head == PXA_EVENT_INDEX_NONE) {
        mailbox->tail = PXA_EVENT_INDEX_NONE;
    }
    if (mailbox->count != 0) mailbox->count--;
    pxa_event_pool_release(pool, event_index);
    return PXA_STATUS_OK;
}

pxa_status_t pxa_event_post(pxa_runtime_t *runtime,
                            pxa_component_t component_ref,
                            const void *message, size_t message_size,
                            uint8_t reliable, uint64_t coalesce_key) {
    uint32_t component_index;
    pxa_component_slot_t *component;
    pxa_message_view_t decoded;
    if (pxa_message_decode((const uint8_t *)message, message_size,
                           PXA_MAX_CONTROL_MESSAGE, &decoded) != PXA_STATUS_OK) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    component = pxa_runtime_find_component(runtime, component_ref,
                                           &component_index);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state != PXA_COMPONENT_RUNNING) return PXA_STATUS_BAD_STATE;
    return pxa_event_mailbox_post(
        &runtime->event_pool, &component->mailbox, component_index,
        runtime->limits.mailbox_capacity,
        runtime->limits.reliable_event_reserve, message, message_size,
        reliable, coalesce_key);
}

pxa_status_t pxa_event_post_message(
    pxa_runtime_t *runtime, pxa_component_t component_ref, uint16_t service,
    uint16_t opcode, uint32_t request_id, pxa_bytes_t payload,
    uint8_t reliable, uint64_t coalesce_key) {
    return pxa_event_post_messagev(
        runtime, component_ref, service, opcode, request_id, &payload, 1,
        reliable, coalesce_key);
}

pxa_status_t pxa_event_post_messagev(
    pxa_runtime_t *runtime, pxa_component_t component_ref, uint16_t service,
    uint16_t opcode, uint32_t request_id, const pxa_bytes_t *payload_parts,
    size_t payload_part_count, uint8_t reliable, uint64_t coalesce_key) {
    uint8_t envelope[PXA_ENVELOPE_SIZE];
    uint32_t component_index;
    pxa_component_slot_t *component;
    size_t payload_size = 0;
    size_t index;
    if (payload_parts == NULL && payload_part_count != 0)
        return PXA_STATUS_INVALID_ARGUMENT;
    for (index = 0; index < payload_part_count; ++index) {
        if ((payload_parts[index].data == NULL &&
             payload_parts[index].size != 0) ||
            payload_parts[index].size >
                PXA_MAX_CONTROL_MESSAGE - PXA_ENVELOPE_SIZE - payload_size) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        payload_size += payload_parts[index].size;
    }
    component = pxa_runtime_find_component(runtime, component_ref,
                                           &component_index);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state != PXA_COMPONENT_RUNNING) return PXA_STATUS_BAD_STATE;
    pxa_write_u16(envelope, service);
    pxa_write_u16(envelope + 2, opcode);
    pxa_write_u32(envelope + 4, request_id);
    pxa_write_u32(envelope + 8, (uint32_t)payload_size);
    return event_mailbox_post_parts(
        &runtime->event_pool, &component->mailbox, component_index,
        runtime->limits.mailbox_capacity,
        runtime->limits.reliable_event_reserve,
        (pxa_bytes_t){envelope, sizeof(envelope)}, payload_parts,
        payload_part_count, reliable, coalesce_key);
}

pxa_status_t pxa_event_peek(const pxa_runtime_t *runtime,
                            pxa_component_t component_ref,
                            pxa_event_view_t *output) {
    const pxa_component_slot_t *component;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    component = pxa_runtime_find_component_const(runtime, component_ref, NULL);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state != PXA_COMPONENT_RUNNING) return PXA_STATUS_BAD_STATE;
    return pxa_event_mailbox_peek(&runtime->event_pool, &component->mailbox,
                                  output);
}

pxa_status_t pxa_event_read(const pxa_runtime_t *runtime,
                            pxa_event_token_t token, size_t offset,
                            void *output, size_t capacity, size_t *read_size) {
    if (read_size == NULL || (output == NULL && capacity != 0)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *read_size = 0;
    if (!pxa_runtime_is_valid(runtime)) return PXA_STATUS_NOT_FOUND;
    return pxa_event_pool_read(&runtime->event_pool, token, offset, output,
                               capacity, read_size);
}

pxa_status_t pxa_event_pop(pxa_runtime_t *runtime,
                           pxa_component_t component_ref, void *output,
                           size_t capacity, size_t *event_size) {
    uint32_t component_index;
    pxa_component_slot_t *component;
    pxa_status_t status;
    if (event_size == NULL || (output == NULL && capacity != 0)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *event_size = 0;
    component = pxa_runtime_find_component(runtime, component_ref,
                                           &component_index);
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state != PXA_COMPONENT_RUNNING) return PXA_STATUS_BAD_STATE;
    status = pxa_event_mailbox_pop(&runtime->event_pool, &component->mailbox,
                                   output, capacity, event_size);
    if (status != PXA_STATUS_OK) return status;
    pxa_request_table_flush(
        &runtime->requests, &component->requests, component_index,
        &runtime->event_pool, &component->mailbox,
        runtime->limits.mailbox_capacity);
    return PXA_STATUS_OK;
}

pxa_status_t pxa_event_consume(pxa_runtime_t *runtime,
                               pxa_component_t component_ref,
                               pxa_event_token_t token) {
    uint32_t component_index;
    pxa_component_slot_t *component = pxa_runtime_find_component(
        runtime, component_ref, &component_index);
    pxa_status_t status;
    if (component == NULL) return PXA_STATUS_NOT_FOUND;
    if (component->state != PXA_COMPONENT_RUNNING) return PXA_STATUS_BAD_STATE;
    status = pxa_event_mailbox_consume(&runtime->event_pool,
                                       &component->mailbox, component_index,
                                       token);
    if (status != PXA_STATUS_OK) return status;
    pxa_request_table_flush(
        &runtime->requests, &component->requests, component_index,
        &runtime->event_pool, &component->mailbox,
        runtime->limits.mailbox_capacity);
    return PXA_STATUS_OK;
}
