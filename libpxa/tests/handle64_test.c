#include <assert.h>
#include <stdint.h>

#include "core/handle_internal.h"

int main(void) {
    pxa_resource_slot_t slot;
    pxa_resource_table_t table;
    pxa_resource_owner_t owner;
    pxa_resource_t resource = {0};
    pxa_resource_t found;
    pxa_handle_t legacy = PXA_HANDLE_INVALID;
    pxa_handle64_t wide = PXA_HANDLE64_INVALID;
    pxa_handle64_t stale;

    pxa_resource_table_init(&table, &slot, 1);
    pxa_resource_owner_init(&owner);
    slot.generation = UINT16_MAX;
    assert(pxa_resource_table_open(&table, &owner, 0, PXA_RESOURCE_FILE, 0,
                                   &resource, &legacy) == PXA_STATUS_OK);
    assert((legacy >> 16) == UINT16_MAX);
    assert(pxa_resource_table_close(&table, &owner, 0, legacy) ==
           PXA_STATUS_OK);
    assert(slot.generation == UINT16_MAX + UINT32_C(1));
    assert(pxa_resource_table_open(&table, &owner, 0, PXA_RESOURCE_FILE, 0,
                                   &resource, &legacy) ==
           PXA_STATUS_RESOURCE_LIMIT);
    assert(legacy == PXA_HANDLE_INVALID);
    assert(pxa_resource_table_open64(&table, &owner, 0, PXA_RESOURCE_FILE, 0,
                                     &resource, &wide) == PXA_STATUS_OK);
    assert((uint32_t)(wide >> 32) == UINT16_MAX + UINT32_C(1));
    stale = wide;
    assert(pxa_resource_table_close64(&table, &owner, 0, wide) ==
           PXA_STATUS_OK);
    assert(pxa_resource_table_get64(&table, 0, stale, PXA_RESOURCE_FILE,
                                    &found) == PXA_STATUS_NOT_FOUND);

    slot.generation = UINT32_MAX - UINT32_C(1);
    assert(pxa_resource_table_open64(&table, &owner, 0, PXA_RESOURCE_FILE, 0,
                                     &resource, &wide) == PXA_STATUS_OK);
    assert((uint32_t)(wide >> 32) == UINT32_MAX - UINT32_C(1));
    assert(pxa_resource_table_close64(&table, &owner, 0, wide) ==
           PXA_STATUS_OK);
    assert(slot.generation == UINT32_MAX && !slot.retired);
    assert(pxa_resource_table_open64(&table, &owner, 0, PXA_RESOURCE_FILE, 0,
                                     &resource, &wide) == PXA_STATUS_OK);
    assert((uint32_t)(wide >> 32) == UINT32_MAX);
    assert(pxa_resource_table_close64(&table, &owner, 0, wide) ==
           PXA_STATUS_OK);
    assert(slot.retired && owner.count == 0 && table.count == 0);
    assert(pxa_resource_table_open64(&table, &owner, 0, PXA_RESOURCE_FILE, 0,
                                     &resource, &wide) ==
           PXA_STATUS_RESOURCE_LIMIT);
    assert(wide == PXA_HANDLE64_INVALID);

    {
        pxa_resource_slot_t mixed_slots[2];
        pxa_resource_table_init(&table, mixed_slots, 2);
        pxa_resource_owner_init(&owner);
        mixed_slots[0].generation = UINT16_MAX + UINT32_C(1);
        assert(pxa_resource_table_open(&table, &owner, 0,
                                       PXA_RESOURCE_FILE, 0, &resource,
                                       &legacy) == PXA_STATUS_OK);
        assert((legacy & UINT32_C(0xffff)) == 2u);
        assert(pxa_resource_table_close(&table, &owner, 0, legacy) ==
               PXA_STATUS_OK);
        assert(pxa_resource_table_open64(&table, &owner, 0,
                                         PXA_RESOURCE_FILE, 0, &resource,
                                         &wide) == PXA_STATUS_OK);
        assert((uint32_t)wide == 1u);
        assert(pxa_resource_table_close64(&table, &owner, 0, wide) ==
               PXA_STATUS_OK);
    }
    return 0;
}
