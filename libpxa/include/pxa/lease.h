#ifndef PXA_LEASE_H
#define PXA_LEASE_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/service.h"

#ifdef __cplusplus
extern "C" {
#endif
#define PXA_LEASE_ACQUIRE UINT16_C(3)
#define PXA_LEASE_REVOKED UINT16_C(0x8003)

typedef uint64_t (*pxa_lease_clock_fn)(void *context);

typedef struct {
    uint32_t struct_size;
    uint32_t allowed_kinds;
    uint16_t max_leases_per_component;
    uint16_t max_leases;
    uint32_t default_duration_ms;
    uint32_t max_duration_ms;
    void *clock_context;
    pxa_lease_clock_fn clock;
} pxa_lease_limits_t;

typedef struct pxa_lease_service pxa_lease_service_t;

void pxa_lease_limits_init(pxa_lease_limits_t *limits);
size_t pxa_lease_service_workspace_size(const pxa_lease_limits_t *limits);
pxa_status_t pxa_lease_service_init(void *workspace, size_t workspace_size,
                                    pxa_runtime_t *runtime,
                                    const pxa_lease_limits_t *limits,
                                    pxa_lease_service_t **output);
pxa_status_t pxa_lease_service_register(pxa_lease_service_t *service);

int pxa_lease_has_active(const pxa_lease_service_t *service);
pxa_status_t pxa_lease_active_components(
    const pxa_lease_service_t *service, pxa_component_t *output,
    size_t capacity, size_t *count);
pxa_status_t pxa_lease_revoke_expired(
    pxa_lease_service_t *service, pxa_component_t *affected,
    size_t capacity, size_t *count);
pxa_status_t pxa_lease_revoke_all(
    pxa_lease_service_t *service, pxa_status_t reason,
    pxa_component_t *affected, size_t capacity, size_t *count);

#ifdef __cplusplus
}
#endif

#endif
