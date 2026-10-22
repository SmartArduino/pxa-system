#ifndef PXA_INSTALL_PLAN_H
#define PXA_INSTALL_PLAN_H

#include "pxa/volume.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Host control plane. No paths, device pointers, allocation or flash writes.
 * File footprints include allocation-unit rounding, initial private state and
 * local installation records. Temporary bytes describe the actual peak of
 * the selected transfer strategy, rather than always duplicating the pack. */
typedef struct {
    uint64_t app_bytes;
    uint64_t app_temporary_bytes;
    uint64_t media_bytes;
    uint64_t media_temporary_bytes;
    uint64_t volume_headroom_bytes;
} pxa_install_requirements_t;

typedef struct {
    pxa_volume_id_t id;
    uint64_t mount_epoch;
    uint64_t bytes;
} pxa_install_reservation_t;

/* Noncopyable owner state: retain the same object until release. Consumers
 * may expose immutable volume IDs/size estimates to UI, never the lease. */
typedef struct {
    pxa_install_reservation_t volumes[2];
    uint64_t app_mount_epoch;
    uint64_t media_mount_epoch;
    uint8_t volume_count;
    uint8_t reserved;
} pxa_install_plan_t;

pxa_status_t pxa_install_file_allocation(const pxa_volume_info_t *,
    uint64_t logical_bytes, uint64_t *allocated_bytes);
/* Zero media epoch means follow the application. Output is cleared on failure.
 * Each distinct destination is charged once for filesystem headroom. */
pxa_status_t pxa_install_plan_evaluate(const pxa_volume_registry_t *,
    const pxa_install_requirements_t *, uint64_t app_epoch, uint64_t media_epoch,
    pxa_install_plan_t *output);
/* Preferred mount when suitable; otherwise a fitting nonremovable volume,
 * then a fitting removable volume. Resources follow the application. */
pxa_status_t pxa_install_plan_select(const pxa_volume_registry_t *,
    const pxa_install_requirements_t *, uint64_t preferred_epoch,
    pxa_install_plan_t *output);
/* Owner-serialized all-or-nothing admission, including rollback when a second
 * destination became full. No filesystem write may start before success. */
pxa_status_t pxa_install_plan_reserve(pxa_volume_registry_t *, pxa_install_plan_t *);
pxa_status_t pxa_install_plan_validate(const pxa_volume_registry_t *,
    const pxa_install_plan_t *);
/* Releases every surviving destination even after one was unplugged. Repeated
 * release on the same object is harmless. UNAVAILABLE reports a removed mount. */
pxa_status_t pxa_install_plan_release(pxa_volume_registry_t *, pxa_install_plan_t *);

#ifdef __cplusplus
}
#endif
#endif
