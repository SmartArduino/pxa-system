#ifndef PXA_INSTALL_RECORD_H
#define PXA_INSTALL_RECORD_H

#include "pxa/volume.h"
#include "pxa/wire.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_INSTALL_ID_BYTES 16u
#define PXA_INSTALL_APP_ID_BYTES 65u
#define PXA_INSTALL_RECORD_V1_BYTES 244u
#define PXA_INSTALL_RECORD_BYTES 252u
#define PXA_INSTALL_RECORD_ENABLED UINT32_C(1)
#define PXA_INSTALL_RECORD_ARCHIVED UINT32_C(2)
#define PXA_INSTALL_RECORD_HAS_MEDIA UINT32_C(4)
#define PXA_INSTALL_RECORD_MANAGED_MEDIA UINT32_C(8)
#define PXA_INSTALL_RECORD_FLAGS UINT32_C(15)

/* Volume-local discovery/commit record, not an authorization credential.
 * Runtime must authenticate the referenced signed package and its identity.
 * CRC detects torn writes; it does not replace signatures or device-bound
 * permission grants. All fields use an explicit little-endian wire encoding. */
typedef struct {
    uint64_t generation;
    uint64_t release_sequence;
    uint32_t flags;
    uint8_t install_id[PXA_INSTALL_ID_BYTES];
    uint8_t publisher_root[32];
    pxa_volume_id_t app_volume;
    pxa_volume_id_t media_volume;
    uint8_t manifest_sha256[32];
    uint8_t media_set_sha256[32];
    char app_id[PXA_INSTALL_APP_ID_BYTES];
    /* Independently committed private-data generation. Zero preserves the
     * legacy flat private root; new homes start at one. Clearing data stages
     * a different generation before changing this reference atomically. */
    uint64_t data_generation;
} pxa_install_record_t;

pxa_status_t pxa_install_record_encode(const pxa_install_record_t *,
    uint8_t output[PXA_INSTALL_RECORD_BYTES]);
pxa_status_t pxa_install_record_decode(pxa_bytes_t,
    const pxa_volume_id_t *expected_volume, pxa_install_record_t *);
/* Selects the latest valid slot. Missing slots have size zero. Corrupt or
 * foreign records never become an empty installation. Inconsistent identities
 * and conflicting records at one generation are rejected. */
pxa_status_t pxa_install_record_select(pxa_bytes_t first, pxa_bytes_t second,
    const pxa_volume_id_t *expected_volume, pxa_install_record_t *, uint8_t *slot);

#ifdef __cplusplus
}
#endif
#endif
