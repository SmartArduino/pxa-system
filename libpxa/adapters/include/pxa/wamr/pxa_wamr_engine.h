#ifndef PXA_WAMR_ENGINE_H
#define PXA_WAMR_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/activation.h"
#include "pxa/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Layer 2 reference adapter: WAMR implementation of the Component engine
 * (`pxa_component_engine_t`). Mirrors the legacy C++ WAMR host semantics:
 *  - loads one Artifact (portable Wasm or AOT) per Component from the package
 *    root through the `read_artifact` callback;
 *  - binds the two `pxa.core.v0` natives `pxa_control` and `pxa_io`, which
 *    dispatch through pxa_runtime_control / pxa_runtime_io;
 *  - calls the guest exports pxa_app_start / pxa_app_on_event / pxa_app_stop;
 *  - optional guest-call deadline: poll pxa_wamr_engine_poll_deadlines from a
 *    host timer to abort overdue calls with wasm_runtime_terminate. Hosts that
 *    need an operator decision can take an expired deadline, then extend or
 *    terminate that active call explicitly.
 *
 * The adapter is single-owner-thread. The component table lives in the
 * caller-supplied workspace. WAMR uses a fixed pool there unless the Host
 * provides the runtime allocator callbacks, in which case all WAMR allocations
 * are made and released on demand. By default each Component also gets a
 * distinct maximum-sized module buffer in the workspace. Hosts may instead
 * provide the paired Artifact allocator callbacks. The adapter releases each
 * temporary Artifact after loading when WAMR reports that its underlying
 * buffer is freeable; XIP and unsupported interpreter modes retain it until
 * WAMR unloads the module. Event delivery copies through a transient
 * module_malloc buffer per event.
 */

#define PXA_WAMR_ENGINE_MAX_CONFIG_BYTES ((size_t)256)

typedef void (*pxa_wamr_synchronize_fn)(void *context);
typedef void *(*pxa_wamr_artifact_allocate_fn)(void *context, size_t size);
typedef void (*pxa_wamr_artifact_release_fn)(void *context, void *memory);
typedef void *(*pxa_wamr_runtime_allocate_fn)(void *context, size_t size);
typedef void *(*pxa_wamr_runtime_reallocate_fn)(void *context, void *memory,
                                                size_t size);
typedef void (*pxa_wamr_runtime_release_fn)(void *context, void *memory);

typedef struct {
    uint32_t struct_size;
    void *host_context;
    /* Required: read one Artifact file into `output` (bounded). A call with
     * output == NULL and capacity == 0 queries its size without reading it. */
    pxa_status_t (*read_artifact)(void *host_context, pxa_bytes_t path,
                                  uint8_t *output, size_t capacity,
                                  size_t *size);
    /* Required when call_timeout_us != 0: monotonic microseconds. */
    uint64_t (*now_us)(void *host_context);
    /* Optional: called before pxa_app_start, with the runtime component
     * handle resolved. Hosts bind per-component services (e.g. Window) here,
     * mirroring the legacy C++ Start() semantics. */
    pxa_status_t (*prepare_start)(void *host_context, pxa_component_t component,
                                  uint64_t instance_id, uint8_t kind);
    uint64_t call_timeout_us; /* 0 disables deadline tracking */
    uint32_t guest_stack_size; /* default 16384 when 0 */
    /* Heap inserted by WAMR for Host-side Guest-memory allocations. This is
     * separate from a libc heap implemented by the Wasm module. */
    uint32_t host_managed_heap_size; /* default 16384 when 0 */
    uint16_t max_components;
    /* Enables the capability-checked WASI Preview 1 reactor environment. */
    uint8_t wasi_enabled;
    /* Fixed WAMR pool size, default 1 MiB when zero. Ignored when the runtime
     * allocator callbacks below are configured. */
    size_t pool_bytes;
    /* Largest Artifact when module buffers live in the engine workspace.
     * With an artifact allocator, zero disables the preflight size limit and
     * lets the allocator accept or reject each exact-sized Artifact. */
    size_t max_module_bytes;
    /* Optional pair used only for active-call/deadline state. Configure both
     * when watchdog APIs can run on a thread other than the owner thread. */
    void *synchronization_context;
    pxa_wamr_synchronize_fn enter_critical;
    pxa_wamr_synchronize_fn leave_critical;
    /* Optional pair. When absent, max_components independent module buffers
     * are included in the engine workspace. When present, the adapter queries
     * the Artifact size and makes one exact-sized allocation per load. WAMR's
     * ownership result determines whether it is released after loading or
     * retained until wasm_runtime_unload() completes. */
    void *artifact_allocator_context;
    pxa_wamr_artifact_allocate_fn allocate_artifact;
    pxa_wamr_artifact_release_fn release_artifact;
    /* Optional all-or-none callbacks for on-demand WAMR allocations. The
     * reallocator must preserve the old allocation when it returns NULL. */
    void *runtime_allocator_context;
    pxa_wamr_runtime_allocate_fn allocate_runtime;
    pxa_wamr_runtime_reallocate_fn reallocate_runtime;
    pxa_wamr_runtime_release_fn release_runtime;
} pxa_wamr_engine_config_t;

typedef struct pxa_wamr_engine pxa_wamr_engine_t;

/* Metadata and return value for one guest event callback. Payload bytes are
 * intentionally not exposed because the adapter releases its temporary
 * guest buffer before returning. */
typedef struct {
    uint16_t service;
    uint16_t opcode;
    uint32_t request_id;
    size_t payload_size;
    int32_t guest_result;
    /* Set after the event has been removed from the mailbox. */
    uint8_t event_consumed;
} pxa_wamr_event_result_t;

/* WAMR allocation occupancy and high-water mark since engine initialization.
 * total_bytes is zero for an on-demand allocator with no adapter-level cap. */
typedef struct {
    uint32_t total_bytes;
    uint32_t current_bytes;
    uint32_t peak_bytes;
} pxa_wamr_memory_snapshot_t;

size_t pxa_wamr_engine_workspace_size(const pxa_wamr_engine_config_t *config);
pxa_status_t pxa_wamr_engine_init(void *workspace, size_t workspace_size,
                                  const pxa_wamr_engine_config_t *config,
                                  pxa_wamr_engine_t **output,
                                  pxa_component_engine_t *engine_output);
void pxa_wamr_engine_deinit(pxa_wamr_engine_t *engine);
pxa_status_t pxa_wamr_engine_memory_snapshot(
    const pxa_wamr_engine_t *engine, pxa_wamr_memory_snapshot_t *output);

/* Bind the runtime the pxa.core.v0 natives dispatch into. */
void pxa_wamr_engine_set_runtime(pxa_wamr_engine_t *engine,
                                 pxa_runtime_t *runtime);

/* Optional start configuration (e.g. job schedule) passed to
 * pxa_app_start. Keyed by instance_id; call before activation. */
pxa_status_t pxa_wamr_engine_set_config(pxa_wamr_engine_t *engine,
                                        uint64_t instance_id,
                                        pxa_bytes_t config);

/* Deliver one pending event to the guest. Returns PXA_STATUS_WOULD_BLOCK when
 * the mailbox is empty. Negative results mean the guest or event contract
 * failed and the Host should fault the Component. */
pxa_status_t pxa_wamr_engine_deliver_event(pxa_wamr_engine_t *engine,
                                           pxa_runtime_t *runtime,
                                           pxa_component_t component);

/* As above, also returns the event metadata, consumption state, and Guest
 * callback result. RESOURCE_LIMIT with event_consumed == 0 leaves the event
 * queued and the Component intact, allowing the Host to retry after memory is
 * available. This also lets Hosts resolve events whose result has Host
 * semantics, such as PXA_WINDOW_BACK_REQUESTED. */
pxa_status_t pxa_wamr_engine_deliver_event_result(
    pxa_wamr_engine_t *engine, pxa_runtime_t *runtime,
    pxa_component_t component, pxa_wamr_event_result_t *output);

/* Abort guest calls that exceeded the deadline. Call from a host timer.
 * Cross-thread use requires the synchronization callbacks in the config.
 * Returns 1 when a call was terminated. */
int pxa_wamr_engine_poll_deadlines(pxa_wamr_engine_t *engine,
                                   uint64_t now_us);

/* Take one expired deadline without terminating the guest call. The deadline
 * is disarmed so this returns 1 only once until the host extends it. */
int pxa_wamr_engine_take_expired_deadline(pxa_wamr_engine_t *engine,
                                          uint64_t now_us);

/* Extend an active call from now by its configured deadline. */
int pxa_wamr_engine_extend_active_deadline(pxa_wamr_engine_t *engine,
                                           uint64_t now_us);

/* Terminate the active guest call. Returns 1 when a call was terminated. */
int pxa_wamr_engine_terminate_active_call(pxa_wamr_engine_t *engine);

/* 1 while a guest call is executing. */
int pxa_wamr_engine_busy(const pxa_wamr_engine_t *engine);

#ifdef __cplusplus
}
#endif

#endif
