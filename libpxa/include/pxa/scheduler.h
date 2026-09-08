#ifndef PXA_SCHEDULER_H
#define PXA_SCHEDULER_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/service.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_WORK_SERVICE_ID UINT16_C(13)
#define PXA_WORK_SERVICE_MAJOR UINT16_C(0)
#define PXA_WORK_SERVICE_MINOR UINT16_C(1)
#define PXA_WORK_SERVICE_PATCH UINT16_C(0)
#define PXA_WORK_ENQUEUE UINT16_C(1)
#define PXA_WORK_CANCEL UINT16_C(2)
#define PXA_WORK_COMPLETE UINT16_C(3)
#define PXA_WORK_STOP_REQUESTED UINT16_C(0x8001)

#define PXA_SCHEDULER_MAX_COMPONENT_ID_BYTES ((size_t)64)
#define PXA_SCHEDULER_DEFAULT_MAX_ENTRIES UINT16_C(8)
#define PXA_SCHEDULER_DEFAULT_MIN_DELAY_MS UINT32_C(1000)
#define PXA_SCHEDULER_DEFAULT_MAX_DELAY_MS UINT32_C(604800000)
#define PXA_SCHEDULER_DEFAULT_MAX_EXECUTION_MS UINT32_C(60000)
#define PXA_WORK_DEFAULT_EXECUTION_MS UINT32_C(10000)
#define PXA_WORK_MAX_INPUT_BYTES ((size_t)24)
#define PXA_WORK_MAX_ATTEMPTS UINT8_C(5)

typedef uint8_t pxa_work_result_t;
#define PXA_WORK_RESULT_SUCCESS ((pxa_work_result_t)1)
#define PXA_WORK_RESULT_RETRY ((pxa_work_result_t)2)
#define PXA_WORK_RESULT_FAILURE ((pxa_work_result_t)3)

typedef struct {
    uint32_t id;
    uint64_t due_at_ms;
    uint32_t max_execution_ms;
    uint8_t component_id[PXA_SCHEDULER_MAX_COMPONENT_ID_BYTES];
    uint8_t component_id_size;
    uint32_t retry_delay_ms;
    uint8_t attempt;
    uint8_t max_attempts;
    uint8_t input_size;
    uint8_t input[PXA_WORK_MAX_INPUT_BYTES];
} pxa_scheduler_entry_t;

typedef uint64_t (*pxa_scheduler_clock_fn)(void *context);
typedef pxa_status_t (*pxa_scheduler_load_fn)(
    void *context, pxa_scheduler_entry_t *entries, size_t capacity,
    size_t *count);
typedef pxa_status_t (*pxa_scheduler_save_fn)(
    void *context, const pxa_scheduler_entry_t *entries, size_t count);
typedef pxa_status_t (*pxa_work_complete_fn)(
    void *context, pxa_component_t component, uint32_t work_id,
    pxa_work_result_t result);
typedef pxa_status_t (*pxa_work_cancel_fn)(void *context, uint32_t work_id);

typedef struct {
    uint32_t struct_size;
    void *context;
    pxa_scheduler_load_fn load;
    pxa_scheduler_save_fn save;
} pxa_scheduler_store_t;

typedef struct {
    uint32_t struct_size;
    /* May be NULL when job_component_count is zero. */
    const pxa_bytes_t *job_components;
    uint16_t job_component_count;
    uint16_t max_entries;
    uint32_t min_delay_ms;
    uint32_t max_delay_ms;
    uint32_t default_execution_ms;
    uint32_t max_execution_ms;
    void *clock_context;
    pxa_scheduler_clock_fn clock;
    pxa_scheduler_store_t store;
    void *work_context;
    pxa_work_complete_fn complete_work;
    pxa_work_cancel_fn cancel_work;
} pxa_scheduler_config_t;

typedef struct pxa_scheduler_service pxa_scheduler_service_t;

void pxa_scheduler_config_init(pxa_scheduler_config_t *config);
size_t pxa_scheduler_service_workspace_size(
    const pxa_scheduler_config_t *config);
pxa_status_t pxa_scheduler_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_scheduler_config_t *config, pxa_scheduler_service_t **output);
pxa_status_t pxa_scheduler_load(pxa_scheduler_service_t *service);
/* Replace the resident Package's declared Job Components and atomically
 * reload entries from the configured store. The Host must call this only
 * while no Component can dispatch Scheduler requests. The new declaration
 * count may not exceed the count supplied at service initialization. */
pxa_status_t pxa_scheduler_rebind(
    pxa_scheduler_service_t *service, const pxa_bytes_t *job_components,
    uint16_t job_component_count);
pxa_status_t pxa_scheduler_service_register(
    pxa_scheduler_service_t *service);
int pxa_scheduler_has_pending(const pxa_scheduler_service_t *service);
pxa_status_t pxa_scheduler_take_due(
    pxa_scheduler_service_t *service, pxa_scheduler_entry_t *output,
    size_t capacity, size_t *count);
pxa_status_t pxa_scheduler_retry(
    pxa_scheduler_service_t *service, const pxa_scheduler_entry_t *entry);
/* Requeue work without consuming an attempt. Hosts use this when a due worker
 * cannot be activated because the execution slot is temporarily busy. */
pxa_status_t pxa_scheduler_defer(
    pxa_scheduler_service_t *service, const pxa_scheduler_entry_t *entry,
    uint32_t delay_ms);
pxa_status_t pxa_scheduler_post_work_stop(
    pxa_scheduler_service_t *service, pxa_component_t component,
    uint32_t work_id, uint64_t deadline_ms);
pxa_status_t pxa_scheduler_encode_start_config(
    const pxa_scheduler_entry_t *entry, uint64_t deadline_ms,
    uint8_t *output, size_t capacity, size_t *output_size);

#ifdef __cplusplus
}
#endif

#endif
