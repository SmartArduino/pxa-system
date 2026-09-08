#ifndef PXA_CORE_EVENT_POOL_H
#define PXA_CORE_EVENT_POOL_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/runtime.h"

typedef struct {
    uint64_t coalesce_key;
    uint32_t next_free;
    uint32_t next_queue;
    uint32_t first_block;
    uint32_t size;
    uint16_t generation;
    uint16_t owner_index;
    uint8_t occupied;
    uint8_t reliable;
} pxa_event_slot_t;

typedef struct {
    uint32_t head;
    uint32_t tail;
    uint16_t count;
} pxa_event_mailbox_t;

typedef struct {
    pxa_event_slot_t *slots;
    uint32_t *block_next;
    uint8_t *block_data;
    uint32_t event_free_head;
    uint32_t block_free_head;
    uint32_t block_free_count;
    uint16_t event_capacity;
    uint16_t block_size;
    uint16_t block_count;
    uint16_t event_count;
    uint16_t event_peak;
    uint16_t block_peak;
} pxa_event_pool_t;

typedef struct {
    pxa_event_pool_t *pool;
    uint32_t block;
    size_t block_offset;
    size_t written;
} pxa_event_writer_t;

void pxa_event_pool_init(pxa_event_pool_t *pool, pxa_event_slot_t *slots,
                         uint16_t event_capacity, uint32_t *block_next,
                         uint8_t *block_data, uint16_t block_size,
                         uint16_t block_count);
void pxa_event_mailbox_init(pxa_event_mailbox_t *mailbox);
uint16_t pxa_event_mailbox_count(const pxa_event_mailbox_t *mailbox);

pxa_status_t pxa_event_pool_allocate(pxa_event_pool_t *pool,
                                     uint32_t owner_index, size_t size,
                                     uint8_t reliable, uint64_t coalesce_key,
                                     uint32_t *output);
void pxa_event_pool_release(pxa_event_pool_t *pool, uint32_t event_index);
void pxa_event_writer_init(pxa_event_writer_t *writer,
                           pxa_event_pool_t *pool, uint32_t event_index);
pxa_status_t pxa_event_writer_write(pxa_event_writer_t *writer,
                                    const void *data, size_t size);

void pxa_event_mailbox_append(pxa_event_pool_t *pool,
                              pxa_event_mailbox_t *mailbox,
                              uint32_t event_index);
void pxa_event_mailbox_clear(pxa_event_pool_t *pool,
                             pxa_event_mailbox_t *mailbox);
pxa_status_t pxa_event_mailbox_post(
    pxa_event_pool_t *pool, pxa_event_mailbox_t *mailbox,
    uint32_t owner_index, uint16_t mailbox_capacity,
    uint16_t reliable_reserve, const void *message, size_t message_size,
    uint8_t reliable, uint64_t coalesce_key);
pxa_status_t pxa_event_mailbox_peek(const pxa_event_pool_t *pool,
                                    const pxa_event_mailbox_t *mailbox,
                                    pxa_event_view_t *output);
pxa_status_t pxa_event_mailbox_pop(pxa_event_pool_t *pool,
                                   pxa_event_mailbox_t *mailbox,
                                   void *output, size_t capacity,
                                   size_t *event_size);
pxa_status_t pxa_event_mailbox_consume(pxa_event_pool_t *pool,
                                       pxa_event_mailbox_t *mailbox,
                                       uint32_t owner_index,
                                       pxa_event_token_t token);
pxa_status_t pxa_event_pool_read(const pxa_event_pool_t *pool,
                                 pxa_event_token_t token, size_t offset,
                                 void *output, size_t capacity,
                                 size_t *read_size);

#endif
