#ifndef PXA_ACTIVATION_H
#define PXA_ACTIVATION_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/package.h"
#include "pxa/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const pxa_package_component_t *component;
    const pxa_package_artifact_t *artifact;
} pxa_activation_entry_t;

typedef struct pxa_activation_plan {
    pxa_bytes_t package_root;
    const pxa_package_manifest_t *manifest;
    const pxa_activation_entry_t *entries;
    uint16_t entry_count;
} pxa_activation_plan_t;

size_t pxa_activation_plan_workspace_size(uint16_t max_components);
pxa_status_t pxa_activation_plan_prepare(
    void *workspace, size_t workspace_size,
    const pxa_package_manifest_t *manifest,
    const pxa_package_activation_profile_t *capabilities,
    const pxa_package_host_profile_t *host, pxa_bytes_t package_root,
    pxa_activation_plan_t **output);

typedef pxa_status_t (*pxa_engine_instantiate_fn)(
    void *context, pxa_bytes_t package_root,
    const pxa_activation_entry_t *entry, pxa_component_t component,
    uint64_t instance_id);
typedef pxa_status_t (*pxa_engine_start_fn)(
    void *context, pxa_component_t component);
/* stop and destroy must accept resources left by a failed start. */
typedef void (*pxa_engine_stop_fn)(
    void *context, pxa_component_t component, pxa_stop_reason_t reason);
/* destroy is also called after an instantiate attempt returns an error. */
typedef void (*pxa_engine_destroy_fn)(
    void *context, pxa_component_t component);

typedef struct {
    uint32_t struct_size;
    void *context;
    pxa_engine_instantiate_fn instantiate;
    pxa_engine_start_fn start;
    pxa_engine_stop_fn stop;
    pxa_engine_destroy_fn destroy;
} pxa_component_engine_t;

typedef struct pxa_activation_coordinator pxa_activation_coordinator_t;

size_t pxa_activation_coordinator_workspace_size(
    const pxa_activation_plan_t *plan);
pxa_status_t pxa_activation_coordinator_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_activation_plan_t *plan,
    const pxa_component_engine_t *engine,
    pxa_activation_coordinator_t **output);
pxa_status_t pxa_activation_activate(
    pxa_activation_coordinator_t *coordinator, pxa_bytes_t component_id,
    uint64_t instance_id, pxa_component_t *component);
pxa_status_t pxa_activation_deactivate(
    pxa_activation_coordinator_t *coordinator, pxa_bytes_t component_id,
    pxa_stop_reason_t reason);
void pxa_activation_deactivate_all(
    pxa_activation_coordinator_t *coordinator, pxa_stop_reason_t reason);
pxa_status_t pxa_activation_find(
    const pxa_activation_coordinator_t *coordinator,
    pxa_bytes_t component_id, uint64_t *instance_id,
    pxa_component_t *component);

#ifdef __cplusplus
}
#endif

#endif
