#include "pxa/slot_transaction.h"
#include "common/status_internal.h"

#include <stddef.h>

typedef struct {
    pxa_slot_state_t current;
    pxa_slot_state_t previous;
    pxa_slot_state_t incoming;
} pxa_slot_states_t;

static int storage_valid(const pxa_slot_storage_t *storage) {
    return storage != NULL && storage->struct_size >= sizeof(*storage) &&
           storage->inspect != NULL && storage->remove != NULL &&
           storage->rename != NULL && storage->barrier != NULL;
}

static pxa_status_t inspect_all(const pxa_slot_storage_t *storage,
                                pxa_slot_states_t *states) {
    pxa_status_t status = pxa_status_normalize(storage->inspect(
        storage->context, PXA_PACKAGE_SLOT_CURRENT, &states->current));
    if (status == PXA_STATUS_OK) {
        status = pxa_status_normalize(storage->inspect(
            storage->context, PXA_PACKAGE_SLOT_PREVIOUS, &states->previous));
    }
    if (status == PXA_STATUS_OK) {
        status = pxa_status_normalize(storage->inspect(
            storage->context, PXA_PACKAGE_SLOT_INCOMING, &states->incoming));
    }
    if (status != PXA_STATUS_OK) return status;
    if (states->current > PXA_SLOT_CORRUPT ||
        states->previous > PXA_SLOT_CORRUPT ||
        states->incoming > PXA_SLOT_CORRUPT) {
        return PXA_STATUS_INTERNAL;
    }
    return PXA_STATUS_OK;
}

static pxa_status_t barrier(const pxa_slot_storage_t *storage) {
    return pxa_status_normalize(storage->barrier(storage->context));
}

static pxa_status_t remove_and_barrier(const pxa_slot_storage_t *storage,
                                       pxa_package_slot_t slot) {
    pxa_status_t status = pxa_status_normalize(
        storage->remove(storage->context, slot));
    return status == PXA_STATUS_OK ? barrier(storage) : status;
}

static pxa_status_t rename_and_barrier(const pxa_slot_storage_t *storage,
                                       pxa_package_slot_t from,
                                       pxa_package_slot_t to) {
    pxa_status_t status = pxa_status_normalize(
        storage->rename(storage->context, from, to));
    return status == PXA_STATUS_OK ? barrier(storage) : status;
}

static pxa_status_t checkpoint(const pxa_slot_faults_t *faults,
                               pxa_transaction_checkpoint_t value) {
    return faults == NULL || faults->checkpoint == NULL
               ? PXA_STATUS_OK
               : pxa_status_normalize(
                     faults->checkpoint(faults->context, value));
}

pxa_status_t pxa_slots_recover(const pxa_slot_storage_t *storage) {
    pxa_slot_states_t states;
    int current;
    int previous;
    int incoming;
    pxa_status_t status;
    if (!storage_valid(storage)) return PXA_STATUS_INVALID_ARGUMENT;
    status = inspect_all(storage, &states);
    if (status != PXA_STATUS_OK) return status;
    if (states.current == PXA_SLOT_CORRUPT ||
        states.previous == PXA_SLOT_CORRUPT) {
        return PXA_STATUS_DENIED;
    }
    if (states.incoming == PXA_SLOT_CORRUPT) {
        status = remove_and_barrier(storage, PXA_PACKAGE_SLOT_INCOMING);
        if (status != PXA_STATUS_OK) return status;
        states.incoming = PXA_SLOT_ABSENT;
    }
    current = states.current != PXA_SLOT_ABSENT;
    previous = states.previous != PXA_SLOT_ABSENT;
    incoming = states.incoming != PXA_SLOT_ABSENT;
    if (current && previous && incoming) {
        return remove_and_barrier(storage, PXA_PACKAGE_SLOT_INCOMING);
    }
    if (!current && previous && incoming) {
        status = rename_and_barrier(storage, PXA_PACKAGE_SLOT_PREVIOUS,
                                    PXA_PACKAGE_SLOT_CURRENT);
        return status == PXA_STATUS_OK
                   ? remove_and_barrier(storage, PXA_PACKAGE_SLOT_INCOMING)
                   : status;
    }
    if (incoming) {
        return remove_and_barrier(storage, PXA_PACKAGE_SLOT_INCOMING);
    }
    if (!current && previous) {
        return rename_and_barrier(storage, PXA_PACKAGE_SLOT_PREVIOUS,
                                  PXA_PACKAGE_SLOT_CURRENT);
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_slots_commit_incoming(
    const pxa_slot_storage_t *storage, const pxa_slot_faults_t *faults) {
    pxa_slot_states_t states;
    pxa_status_t status;
    if (!storage_valid(storage)) return PXA_STATUS_INVALID_ARGUMENT;
    status = inspect_all(storage, &states);
    if (status != PXA_STATUS_OK) return status;
    if (states.incoming != PXA_SLOT_VERIFIED ||
        states.current == PXA_SLOT_CORRUPT ||
        states.previous == PXA_SLOT_CORRUPT) {
        return PXA_STATUS_DENIED;
    }
    status = checkpoint(faults, PXA_CHECKPOINT_INCOMING_VERIFIED);
    if (status != PXA_STATUS_OK) return status;
    if (states.previous == PXA_SLOT_VERIFIED) {
        status = remove_and_barrier(storage, PXA_PACKAGE_SLOT_PREVIOUS);
        if (status != PXA_STATUS_OK) return status;
    }
    if (states.current == PXA_SLOT_VERIFIED) {
        status = rename_and_barrier(storage, PXA_PACKAGE_SLOT_CURRENT,
                                    PXA_PACKAGE_SLOT_PREVIOUS);
        if (status != PXA_STATUS_OK) return status;
        status = checkpoint(faults, PXA_CHECKPOINT_CURRENT_MOVED);
        if (status != PXA_STATUS_OK) return status;
    }
    return rename_and_barrier(storage, PXA_PACKAGE_SLOT_INCOMING,
                              PXA_PACKAGE_SLOT_CURRENT);
}
