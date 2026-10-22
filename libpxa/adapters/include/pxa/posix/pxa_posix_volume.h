#ifndef PXA_POSIX_VOLUME_H
#define PXA_POSIX_VOLUME_H
#include "pxa/volume_identity.h"

#ifdef __cplusplus
extern "C" {
#endif
/* pxa_directory is the Host-opened PXA directory on that filesystem. Identity
 * mirrors live in volume.0.pxv / volume.1.pxv. No device-global registry is
 * required. A missing/corrupt mirror is tolerated when the other is valid;
 * two conflicting identities or two corrupt copies are never reassigned. */
pxa_status_t pxa_posix_volume_identity_load(int pxa_directory, pxa_volume_id_t *);
/* Uses a Host-generated random UUID only when both mirrors are absent.
 * Existing valid identity always wins. It is safe to call this on a full
 * filesystem containing an existing descriptor: no write is attempted. */
pxa_status_t pxa_posix_volume_identity_initialize(int pxa_directory,
    const pxa_volume_id_t *proposed, pxa_volume_id_t *actual);
#ifdef __cplusplus
}
#endif
#endif
