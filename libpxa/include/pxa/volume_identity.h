#ifndef PXA_VOLUME_IDENTITY_H
#define PXA_VOLUME_IDENTITY_H
#include "pxa/volume.h"
#include "pxa/wire.h"

#ifdef __cplusplus
extern "C" {
#endif
#define PXA_VOLUME_IDENTITY_BYTES 32u
/* Filesystem-local UUID, preserved independently of device mount paths. A
 * cloned identity is detected by the volume registry, never silently rebound. */
pxa_status_t pxa_volume_identity_encode(const pxa_volume_id_t *,
    uint8_t output[PXA_VOLUME_IDENTITY_BYTES]);
pxa_status_t pxa_volume_identity_decode(pxa_bytes_t, pxa_volume_id_t *);
#ifdef __cplusplus
}
#endif
#endif
