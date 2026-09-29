#ifndef PXA_POSIX_STORAGE_GATE_H
#define PXA_POSIX_STORAGE_GATE_H
#include "pxa/resource_budget.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PXA_STORAGE_RESOURCE 0u
#define PXA_STORAGE_MUSIC 1u
typedef struct pxa_posix_storage_gate pxa_posix_storage_gate_t;
typedef struct { uint64_t bytes_per_second; uint32_t latency_us; } pxa_posix_storage_gate_config_t;
typedef struct {
    uint64_t reads, bytes, wait_us, max_wait_us, service_us, max_service_us, errors;
    uint32_t max_read_bytes;
} pxa_posix_storage_lane_t;
typedef struct {
    pxa_posix_storage_lane_t lanes[2];
    uint64_t elapsed_us, stalls, stall_requested_us;
    uint32_t waiting[2], active;
} pxa_posix_storage_stats_t;
/* One bounded read in flight across both workers. A waiting music read goes
 * next, before resource reads. No preemption of an already executing syscall.
 * Zero rate/latency disables injection, not serialization. Allocator is required.
 * Destroy only after both workers join. No use on render/audio callback threads. */
pxa_status_t pxa_posix_storage_gate_create(const pxa_posix_storage_gate_config_t *,
    const pxa_memory_allocator_t *, pxa_posix_storage_gate_t **);
void pxa_posix_storage_gate_destroy(pxa_posix_storage_gate_t *);
pxa_status_t pxa_posix_storage_gate_read(pxa_posix_storage_gate_t *, unsigned lane,
    int fd, uint8_t *, size_t capacity, size_t *bytes, void *context, int (*cancelled)(void *));
/* Deterministic outage of the shared device, including an in-flight injected
 * transfer. Zero releases it. Real OS read itself cannot be interrupted here. */
void pxa_posix_storage_gate_stall(pxa_posix_storage_gate_t *, uint32_t us);
void pxa_posix_storage_gate_stats(pxa_posix_storage_gate_t *, pxa_posix_storage_stats_t *);
#ifdef __cplusplus
}
#endif
#endif
