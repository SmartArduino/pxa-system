#ifndef PXA_GUEST_PENDING_H
#define PXA_GUEST_PENDING_H

#include "pxa_core.h"

/* Caller-owned storage; no heap allocation or hidden token generator. */
typedef struct {
    uint64_t token;
    uint16_t service;
    uint16_t opcode;
    void *user;
} pxa_pending_entry_t;

typedef struct {
    pxa_pending_entry_t *entries;
    uint32_t capacity;
} pxa_pending_table_t;

static inline int pxa_pending_init(pxa_pending_table_t *table,
                                      pxa_pending_entry_t *entries,
                                      uint32_t capacity) {
    if (table == NULL) return 0;
    table->entries = NULL;
    table->capacity = 0;
    if (entries == NULL || capacity == 0) return 0;
#if SIZE_MAX <= UINT32_MAX
    if (capacity > SIZE_MAX / sizeof(*entries)) return 0;
#endif
    table->entries = entries;
    table->capacity = capacity;
    pxa_zero(entries, (size_t)capacity * sizeof(*entries));
    return 1;
}

/* Explicit send: reserve the token before calling Host and roll it back on
 * immediate rejection. The packet must already be built by a typed builder. */
static inline int32_t pxa_send_tracked(
    pxa_pending_table_t *table, const uint8_t *packet,
    uint32_t packet_size, void *user) {
    pxa_event_t request;
    pxa_pending_entry_t *free_entry = NULL;
    int32_t status;
    if (table == NULL || table->entries == NULL || table->capacity == 0 ||
        !pxa_parse_event(packet, packet_size, &request) ||
        request.token == 0)
        return -1;
    for (uint32_t i = 0; i < table->capacity; ++i) {
        pxa_pending_entry_t *entry = &table->entries[i];
        if (entry->token == request.token) return -6;
        if (entry->token == 0 && free_entry == NULL) free_entry = entry;
    }
    if (free_entry == NULL) return -9;
    free_entry->token = request.token;
    free_entry->service = request.service;
    free_entry->opcode = request.opcode;
    free_entry->user = user;
    status = pxa_submit(packet, packet_size);
    if (status != 0) pxa_zero(free_entry, sizeof(*free_entry));
    return status;
}

/* Consume only a completion matching the registered token and operation.
 * The event payload remains a callback-scoped view owned by the caller. */
static inline int pxa_pending_take(pxa_pending_table_t *table,
                                      const pxa_event_t *event,
                                      void **user) {
    if (user != NULL) *user = NULL;
    if (table == NULL || table->entries == NULL || event == NULL ||
        event->token == 0)
        return 0;
    for (uint32_t i = 0; i < table->capacity; ++i) {
        pxa_pending_entry_t *entry = &table->entries[i];
        if (entry->token == event->token) {
            if (entry->service != event->service ||
                entry->opcode != event->opcode)
                return 0;
            if (user != NULL) *user = entry->user;
            pxa_zero(entry, sizeof(*entry));
            return 1;
        }
    }
    return 0;
}

#endif
