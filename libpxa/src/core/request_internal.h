#ifndef PXA_CORE_REQUEST_TABLE_H
#define PXA_CORE_REQUEST_TABLE_H

#include <stddef.h>
#include <stdint.h>

#include "core/event_internal.h"

typedef struct {
    pxa_authority_t authority;
    uint32_t request_id;
    uint32_t next_free;
    uint32_t owner_next;
    uint32_t hash_next;
    uint32_t completion_next;
    uint32_t completion_event;
    uint16_t owner_index;
    uint16_t service;
    uint16_t opcode;
    uint8_t occupied;
    uint8_t committed;
    uint8_t cancelling;
    uint8_t completion_queued;
} pxa_request_slot_t;

typedef struct {
    uint32_t request_head;
    uint32_t completion_head;
    uint32_t completion_tail;
    uint16_t count;
} pxa_request_owner_t;

typedef struct {
    pxa_request_slot_t *slots;
    uint32_t *buckets;
    uint32_t free_head;
    uint32_t buckets_per_owner;
    uint16_t capacity;
    uint16_t owner_capacity;
    uint16_t count;
    uint16_t peak;
} pxa_request_table_t;

uint32_t pxa_request_table_bucket_count(uint16_t per_owner_capacity);
void pxa_request_table_init(pxa_request_table_t *table,
                            pxa_request_slot_t *slots, uint16_t capacity,
                            uint32_t *buckets, uint32_t buckets_per_owner,
                            uint16_t owner_capacity);
void pxa_request_owner_init(pxa_request_owner_t *owner);
uint16_t pxa_request_owner_count(const pxa_request_owner_t *owner);

pxa_status_t pxa_request_table_begin(
    pxa_request_table_t *table, pxa_request_owner_t *owner,
    uint32_t owner_index, uint16_t per_owner_capacity, uint32_t request_id,
    uint16_t service, uint16_t opcode, pxa_authority_t authority);
pxa_status_t pxa_request_table_commit(pxa_request_table_t *table,
                                      uint32_t owner_index,
                                      uint32_t request_id);
pxa_status_t pxa_request_table_complete(
    pxa_request_table_t *table, pxa_request_owner_t *owner,
    uint32_t owner_index, pxa_event_pool_t *event_pool,
    pxa_event_mailbox_t *mailbox, uint16_t mailbox_capacity,
    uint32_t request_id, pxa_status_t result, const void *payload,
    size_t payload_size);
pxa_status_t pxa_request_table_cancel(
    pxa_request_table_t *table, pxa_request_owner_t *owner,
    uint32_t owner_index, pxa_event_pool_t *event_pool,
    pxa_event_mailbox_t *mailbox, uint16_t mailbox_capacity,
    uint32_t request_id);
int pxa_request_table_is_active(const pxa_request_table_t *table,
                                uint32_t owner_index, uint32_t request_id);
void pxa_request_table_revoke_authority(
    pxa_request_table_t *table, pxa_request_owner_t *owner,
    uint32_t owner_index, pxa_event_pool_t *event_pool,
    pxa_event_mailbox_t *mailbox, uint16_t mailbox_capacity,
    pxa_authority_t authority);
void pxa_request_table_flush(pxa_request_table_t *table,
                             pxa_request_owner_t *owner,
                             uint32_t owner_index,
                             pxa_event_pool_t *event_pool,
                             pxa_event_mailbox_t *mailbox,
                             uint16_t mailbox_capacity);
void pxa_request_table_clear_owner(pxa_request_table_t *table,
                                   pxa_request_owner_t *owner,
                                   uint32_t owner_index,
                                   pxa_event_pool_t *event_pool);

#endif
