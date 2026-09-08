#ifndef PXA_PERMISSION_H
#define PXA_PERMISSION_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/service.h"

#ifdef __cplusplus
extern "C" {
#endif
#define PXA_PERMISSION_SERVICE_ID UINT16_C(11)
#define PXA_PERMISSION_SERVICE_MAJOR UINT16_C(0)
#define PXA_PERMISSION_SERVICE_MINOR UINT16_C(1)
#define PXA_PERMISSION_SERVICE_PATCH UINT16_C(0)

#define PXA_PERMISSION_CHECK UINT16_C(1)
#define PXA_PERMISSION_ACQUIRE UINT16_C(2)
#define PXA_PERMISSION_REVOKED UINT16_C(0x8001)

typedef uint8_t pxa_permission_decision_t;
#define PXA_PERMISSION_DENY ((pxa_permission_decision_t)0)
#define PXA_PERMISSION_ALLOW ((pxa_permission_decision_t)1)

typedef struct {
    pxa_bytes_t name;
    pxa_bytes_t scope;
    uint8_t required;
} pxa_permission_declaration_t;

typedef pxa_status_t (*pxa_permission_store_load_fn)(
    void *context, pxa_bytes_t app_identity, pxa_bytes_t name,
    pxa_bytes_t scope, pxa_permission_decision_t *decision);
typedef pxa_status_t (*pxa_permission_store_save_fn)(
    void *context, pxa_bytes_t app_identity, pxa_bytes_t name,
    pxa_bytes_t scope, pxa_permission_decision_t decision);

/* Called for an optional, declared permission that has not been granted.
 * Return PXA_STATUS_OK after the host has queued a user decision. The host
 * must later call pxa_permission_prompt_complete() from the runtime owner
 * thread. */
typedef pxa_status_t (*pxa_permission_prompt_fn)(
    void *context, pxa_component_t component, uint32_t request_id,
    pxa_bytes_t app_identity, pxa_bytes_t name, pxa_bytes_t scope);

typedef struct {
    uint32_t struct_size;
    void *context;
    pxa_permission_store_load_fn load;
    pxa_permission_store_save_fn save;
} pxa_permission_store_t;

typedef struct {
    uint32_t struct_size;
    pxa_bytes_t app_identity;
    const pxa_permission_declaration_t *declarations;
    uint16_t declaration_count;
    uint16_t max_authorities;
    uint16_t max_pending_prompts;
    uint16_t reserved;
    void *prompt_context;
    pxa_permission_prompt_fn prompt;
    pxa_permission_store_t store;
} pxa_permission_config_t;

typedef struct pxa_permission_service pxa_permission_service_t;

size_t pxa_permission_service_workspace_size(
    const pxa_permission_config_t *config);
pxa_status_t pxa_permission_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_permission_config_t *config,
    pxa_permission_service_t **output);
pxa_status_t pxa_permission_service_register(
    pxa_permission_service_t *service);
pxa_status_t pxa_permission_policy_load(pxa_permission_service_t *service);

int pxa_permission_can_activate(const pxa_permission_service_t *service);
pxa_permission_decision_t pxa_permission_get(
    const pxa_permission_service_t *service, pxa_bytes_t name,
    pxa_bytes_t scope);
pxa_status_t pxa_permission_set(pxa_permission_service_t *service,
                                pxa_bytes_t name, pxa_bytes_t scope,
                                pxa_permission_decision_t decision);
/* Completes a previously accepted runtime permission prompt. A deny decision
 * completes the guest request with PXA_STATUS_DENIED. If persisting an allow
 * decision fails, the prompt remains pending and the Host may retry this call.
 * PXA_STATUS_OK means the guest completion was queued. */
pxa_status_t pxa_permission_prompt_complete(
    pxa_permission_service_t *service, pxa_component_t component,
    uint32_t request_id, pxa_permission_decision_t decision);
pxa_status_t pxa_permission_revoke(
    pxa_permission_service_t *service, pxa_bytes_t name, pxa_bytes_t scope,
    pxa_component_t *affected, size_t capacity, size_t *count);

/* Closing permission_handle revokes resources opened with its authority. */
pxa_status_t pxa_permission_resolve(
    const pxa_permission_service_t *service, pxa_component_t component,
    pxa_handle_t permission_handle, pxa_bytes_t expected_name,
    pxa_bytes_t expected_scope, pxa_authority_t *authority);

#ifdef __cplusplus
}
#endif

#endif
