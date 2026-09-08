#ifndef PXA_CORE_RESOURCE_TABLE_H
#define PXA_CORE_RESOURCE_TABLE_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/runtime.h"

typedef struct {
    pxa_resource_t resource;
    pxa_authority_t authority;
    uint32_t next_free;
    uint16_t generation;
    uint16_t owner_index;
    pxa_resource_type_t type;
    uint8_t occupied;
    uint8_t retired;
    uint8_t closing;
} pxa_resource_slot_t;

typedef struct {
    uint16_t count;
} pxa_resource_owner_t;

typedef struct {
    pxa_resource_slot_t *slots;
    uint32_t free_head;
    uint16_t capacity;
    uint16_t count;
    uint16_t peak;
} pxa_resource_table_t;

void pxa_resource_table_init(pxa_resource_table_t *table,
                             pxa_resource_slot_t *slots,
                             uint16_t capacity);
void pxa_resource_owner_init(pxa_resource_owner_t *owner);
uint16_t pxa_resource_owner_count(const pxa_resource_owner_t *owner);

pxa_status_t pxa_resource_table_open(
    pxa_resource_table_t *table, pxa_resource_owner_t *owner,
    uint32_t owner_index, pxa_resource_type_t type,
    pxa_authority_t authority, const pxa_resource_t *resource,
    pxa_handle_t *output);
pxa_status_t pxa_resource_table_get(const pxa_resource_table_t *table,
                                    uint32_t owner_index,
                                    pxa_handle_t handle,
                                    pxa_resource_type_t expected_type,
                                    pxa_resource_t *output);
int32_t pxa_resource_table_io(pxa_resource_table_t *table,
                              uint32_t owner_index, pxa_handle_t handle,
                              uint32_t operation, uint8_t *data, size_t size);
pxa_status_t pxa_resource_table_close(pxa_resource_table_t *table,
                                      pxa_resource_owner_t *owner,
                                      uint32_t owner_index,
                                      pxa_handle_t handle);

void pxa_resource_table_detach_owner(pxa_resource_table_t *table,
                                     pxa_resource_owner_t *owner,
                                     uint32_t owner_index);
void pxa_resource_table_finish_owner(pxa_resource_table_t *table,
                                     uint32_t owner_index);
void pxa_resource_table_detach_authority(
    pxa_resource_table_t *table, pxa_resource_owner_t *owner,
    uint32_t owner_index, pxa_authority_t authority);
void pxa_resource_table_finish_authority(pxa_resource_table_t *table,
                                         uint32_t owner_index,
                                         pxa_authority_t authority);

#endif
