#ifndef PXA_IPC_H
#define PXA_IPC_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/service.h"

#ifdef __cplusplus
extern "C" {
#endif
#define PXA_IPC_SERVICE_ID UINT16_C(7)
#define PXA_IPC_SERVICE_MAJOR UINT16_C(0)
#define PXA_IPC_SERVICE_MINOR UINT16_C(1)
#define PXA_IPC_SERVICE_PATCH UINT16_C(0)
#define PXA_IPC_CALL UINT16_C(1)
#define PXA_IPC_REPLY UINT16_C(2)
#define PXA_IPC_REQUEST_EVENT UINT16_C(0x8001)
#define PXA_IPC_REPLY_EVENT UINT16_C(0x8002)
#define PXA_IPC_MAX_ENDPOINT_BYTES ((size_t)64)
#define PXA_IPC_MAX_PAYLOAD_BYTES ((size_t)1024)

typedef struct {
    uint32_t struct_size;
    uint16_t max_endpoints;
    uint16_t max_pending_calls;
} pxa_ipc_limits_t;

typedef struct pxa_ipc_broker pxa_ipc_broker_t;
typedef void *(*pxa_ipc_allocate_fn)(void *context, size_t size);
typedef void (*pxa_ipc_release_fn)(void *context, void *memory);
typedef pxa_status_t (*pxa_ipc_endpoint_resolver_fn)(
    void *context, pxa_bytes_t endpoint, pxa_component_t *provider);

void pxa_ipc_limits_init(pxa_ipc_limits_t *limits);
size_t pxa_ipc_broker_workspace_size(const pxa_ipc_limits_t *limits);
pxa_status_t pxa_ipc_broker_init(void *workspace, size_t workspace_size,
                                 pxa_runtime_t *runtime,
                                 const pxa_ipc_limits_t *limits,
                                 pxa_ipc_broker_t **output);
pxa_status_t pxa_ipc_broker_register(pxa_ipc_broker_t *broker);
pxa_status_t pxa_ipc_broker_set_allocator(pxa_ipc_broker_t *broker,
                                          void *context,
                                          pxa_ipc_allocate_fn allocate,
                                          pxa_ipc_release_fn release);
pxa_status_t pxa_ipc_broker_set_endpoint_resolver(
    pxa_ipc_broker_t *broker, void *context,
    pxa_ipc_endpoint_resolver_fn resolver);

int pxa_ipc_endpoint_is_valid(pxa_bytes_t endpoint);
/* Reserves an endpoint name without requiring its provider to be running.
 * pxa_ipc_flush() resolves queued requests outside the caller's Guest stack. */
pxa_status_t pxa_ipc_endpoint_declare(pxa_ipc_broker_t *broker,
                                      pxa_bytes_t endpoint);
pxa_status_t pxa_ipc_endpoint_register(pxa_ipc_broker_t *broker,
                                       pxa_bytes_t endpoint,
                                       pxa_component_t provider);
pxa_status_t pxa_ipc_endpoint_unregister(pxa_ipc_broker_t *broker,
                                         pxa_bytes_t endpoint,
                                         pxa_component_t provider);
pxa_status_t pxa_ipc_flush(pxa_ipc_broker_t *broker);

#ifdef __cplusplus
}
#endif

#endif
