#ifndef PXA_SLOT_TRANSACTION_H
#define PXA_SLOT_TRANSACTION_H

#include <stdint.h>

#include "pxa/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t pxa_package_slot_t;
#define PXA_PACKAGE_SLOT_CURRENT ((pxa_package_slot_t)1)
#define PXA_PACKAGE_SLOT_PREVIOUS ((pxa_package_slot_t)2)
#define PXA_PACKAGE_SLOT_INCOMING ((pxa_package_slot_t)3)

typedef uint8_t pxa_slot_state_t;
#define PXA_SLOT_ABSENT ((pxa_slot_state_t)0)
#define PXA_SLOT_VERIFIED ((pxa_slot_state_t)1)
#define PXA_SLOT_CORRUPT ((pxa_slot_state_t)2)

typedef uint8_t pxa_transaction_checkpoint_t;
#define PXA_CHECKPOINT_INCOMING_VERIFIED ((pxa_transaction_checkpoint_t)1)
#define PXA_CHECKPOINT_CURRENT_MOVED ((pxa_transaction_checkpoint_t)2)

typedef pxa_status_t (*pxa_slot_inspect_fn)(
    void *context, pxa_package_slot_t slot, pxa_slot_state_t *state);
typedef pxa_status_t (*pxa_slot_remove_fn)(
    void *context, pxa_package_slot_t slot);
typedef pxa_status_t (*pxa_slot_rename_fn)(
    void *context, pxa_package_slot_t from, pxa_package_slot_t to);
typedef pxa_status_t (*pxa_slot_barrier_fn)(void *context);
typedef pxa_status_t (*pxa_slot_checkpoint_fn)(
    void *context, pxa_transaction_checkpoint_t checkpoint);

typedef struct {
    uint32_t struct_size;
    void *context;
    pxa_slot_inspect_fn inspect;
    pxa_slot_remove_fn remove;
    pxa_slot_rename_fn rename;
    pxa_slot_barrier_fn barrier;
} pxa_slot_storage_t;

typedef struct {
    void *context;
    pxa_slot_checkpoint_fn checkpoint;
} pxa_slot_faults_t;

pxa_status_t pxa_slots_recover(const pxa_slot_storage_t *storage);
pxa_status_t pxa_slots_commit_incoming(
    const pxa_slot_storage_t *storage, const pxa_slot_faults_t *faults);

#ifdef __cplusplus
}
#endif

#endif
