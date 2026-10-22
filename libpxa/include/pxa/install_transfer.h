#ifndef PXA_INSTALL_TRANSFER_H
#define PXA_INSTALL_TRANSFER_H
#include "pxa/install_record.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PXA_INSTALL_TRANSFER_BYTES (76u+PXA_INSTALL_RECORD_BYTES)
#define PXA_INSTALL_TRANSFER_COPYING UINT8_C(1)
#define PXA_INSTALL_TRANSFER_READY UINT8_C(2)
#define PXA_INSTALL_TRANSFER_SURRENDERED UINT8_C(3)
#define PXA_INSTALL_TRANSFER_COMMITTED UINT8_C(4)
#define PXA_INSTALL_TRANSFER_ABORTED UINT8_C(5)
#define PXA_INSTALL_TRANSFER_KIND_MOVE UINT8_C(0)
#define PXA_INSTALL_TRANSFER_KIND_LEGACY_ADOPT UINT8_C(1)

/* Durable application transition, NOT authorization or a distributed lock. Each
 * endpoint must MAC this canonical encoding with its actual role/volume/home
 * binding. A Host may mark SURRENDERED only after durable source retirement;
 * ABORTED requires proving the target never committed/ran the next record.
 * Unknown/offline endpoints cannot be interpreted as an aborted transfer. */
typedef struct {
    uint64_t revision;
    uint8_t phase,kind,transfer_id[16];
    pxa_volume_id_t target_app_volume,target_media_volume;
    pxa_install_record_t source;
} pxa_install_transfer_t;
/* MOVE keeps its canonical V1 wire encoding. LEGACY_ADOPT uses V2 and an
 * explicit kind byte: its synthetic source is generation/data generation 1
 * on the same app volume, and target() preserves that initial generation.
 * It cannot change manual media placement or claim signed-resource ownership.
 * The Host authenticates its bounded source paths/recovery context and retires
 * legacy code before moving private data. No receipt alone grants execution. */
pxa_status_t pxa_install_transfer_encode(const pxa_install_transfer_t *,
    uint8_t output[PXA_INSTALL_TRANSFER_BYTES]);
pxa_status_t pxa_install_transfer_decode(pxa_bytes_t,const pxa_volume_id_t *expected_target,
    pxa_install_transfer_t *);
/* Missing slots have size zero. Reject split identities, conflicting equal
 * revisions, terminal-state changes and phase regressions. MAC verification
 * belongs to the endpoint owner before these CRC-protected bytes are used. */
pxa_status_t pxa_install_transfer_select(pxa_bytes_t first,pxa_bytes_t second,
    const pxa_volume_id_t *expected_target,pxa_install_transfer_t *,uint8_t *slot);
/* Adjacent forward transitions or explicit abort before COMMITTED. Repeating
 * the current phase is idempotent. Output is unchanged on failure. */
pxa_status_t pxa_install_transfer_advance(const pxa_install_transfer_t *,uint8_t phase,
    pxa_install_transfer_t *);
/* Preserve installation/data/policy identity and move generation N -> N+1.
 * Source/receipt authentication, code/resource checks and a durable surrender
 * proof remain mandatory before publishing this projected target record. */
pxa_status_t pxa_install_transfer_target(const pxa_install_transfer_t *,pxa_install_record_t *);
#ifdef __cplusplus
}
#endif
#endif
