#ifndef PXA_RUNTIME_H
#define PXA_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/status.h"
#include "pxa/wire.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pxa_runtime pxa_runtime_t;
typedef uint32_t pxa_component_t;
typedef uint32_t pxa_handle_t;
typedef uint32_t pxa_event_token_t;
typedef uint64_t pxa_authority_t;

#define PXA_COMPONENT_INVALID UINT32_C(0)
#define PXA_HANDLE_INVALID UINT32_C(0)
#define PXA_EVENT_TOKEN_INVALID UINT32_C(0)

typedef uint8_t pxa_component_state_t;
#define PXA_COMPONENT_CREATED ((pxa_component_state_t)0)
#define PXA_COMPONENT_STARTING ((pxa_component_state_t)1)
#define PXA_COMPONENT_RUNNING ((pxa_component_state_t)2)
#define PXA_COMPONENT_STOP_REQUESTED ((pxa_component_state_t)3)
#define PXA_COMPONENT_STOPPING ((pxa_component_state_t)4)
#define PXA_COMPONENT_STOPPED ((pxa_component_state_t)5)

typedef uint8_t pxa_guest_callback_t;
#define PXA_GUEST_CALLBACK_NONE ((pxa_guest_callback_t)0)
#define PXA_GUEST_CALLBACK_START ((pxa_guest_callback_t)1)
#define PXA_GUEST_CALLBACK_EVENT ((pxa_guest_callback_t)2)
#define PXA_GUEST_CALLBACK_STOP ((pxa_guest_callback_t)3)

typedef uint32_t pxa_stop_reason_t;
#define PXA_STOP_NORMAL ((pxa_stop_reason_t)0)
#define PXA_STOP_REPLACED ((pxa_stop_reason_t)1)
#define PXA_STOP_POLICY ((pxa_stop_reason_t)2)
#define PXA_STOP_RESOURCE_PRESSURE ((pxa_stop_reason_t)3)
#define PXA_STOP_PERMISSION_REVOKED ((pxa_stop_reason_t)4)
#define PXA_STOP_FAULT ((pxa_stop_reason_t)5)
#define PXA_STOP_SHUTDOWN ((pxa_stop_reason_t)6)

typedef uint16_t pxa_resource_type_t;
#define PXA_RESOURCE_UNKNOWN ((pxa_resource_type_t)0)
#define PXA_RESOURCE_LEASE ((pxa_resource_type_t)1)
#define PXA_RESOURCE_STREAM ((pxa_resource_type_t)2)
#define PXA_RESOURCE_FILE ((pxa_resource_type_t)3)
#define PXA_RESOURCE_DIRECTORY ((pxa_resource_type_t)4)
#define PXA_RESOURCE_SOCKET ((pxa_resource_type_t)5)
#define PXA_RESOURCE_SENSOR ((pxa_resource_type_t)6)
#define PXA_RESOURCE_TIMER ((pxa_resource_type_t)7)
#define PXA_RESOURCE_IPC_CONNECTION ((pxa_resource_type_t)8)
#define PXA_RESOURCE_AUDIO_GRAPH ((pxa_resource_type_t)9)
#define PXA_RESOURCE_AUDIO_STREAM ((pxa_resource_type_t)10)
#define PXA_RESOURCE_PERMISSION ((pxa_resource_type_t)11)
#define PXA_RESOURCE_SURFACE ((pxa_resource_type_t)12)
#define PXA_RESOURCE_GAME_RENDER_CONTEXT ((pxa_resource_type_t)13)

/* Operations shared by byte-oriented resource streams. */
#define PXA_IO_READ UINT32_C(1)
#define PXA_IO_WRITE UINT32_C(2)

typedef struct {
    uint32_t struct_size;
    uint16_t max_components;
    uint16_t max_requests;
    uint16_t max_requests_per_component;
    uint16_t max_handles;
    uint16_t max_events;
    uint16_t mailbox_capacity;
    uint16_t reliable_event_reserve;
    uint16_t max_revoked_authorities_per_component;
    uint16_t max_services;
    uint16_t event_block_size;
    uint16_t event_block_count;
} pxa_runtime_limits_t;

typedef void (*pxa_resource_close_fn)(void *context);
typedef int32_t (*pxa_resource_io_fn)(void *context, uint32_t operation,
                                      uint8_t *data, size_t size);

typedef struct {
    uint32_t struct_size;
    pxa_resource_io_fn io;
} pxa_resource_ops_t;

typedef struct {
    void *context;
    const void *operations;
    pxa_resource_close_fn close;
} pxa_resource_t;

typedef struct {
    pxa_component_state_t state;
    pxa_guest_callback_t callback;
    pxa_stop_reason_t stop_reason;
    uint16_t pending_requests;
    uint16_t open_handles;
    uint16_t queued_events;
} pxa_component_snapshot_t;

typedef struct {
    pxa_event_token_t token;
    size_t size;
    uint64_t coalesce_key;
    uint8_t reliable;
} pxa_event_view_t;

typedef struct {
    uint16_t current_components;
    uint16_t peak_components;
    uint16_t current_requests;
    uint16_t peak_requests;
    uint16_t current_handles;
    uint16_t peak_handles;
    uint16_t current_events;
    uint16_t peak_events;
    uint16_t current_event_blocks;
    uint16_t peak_event_blocks;
    uint16_t registered_services;
} pxa_runtime_usage_t;

void pxa_runtime_limits_init(pxa_runtime_limits_t *limits);
size_t pxa_runtime_workspace_size(const pxa_runtime_limits_t *limits);
pxa_status_t pxa_runtime_init(void *workspace, size_t workspace_size,
                              const pxa_runtime_limits_t *limits,
                              pxa_runtime_t **output);
void pxa_runtime_deinit(pxa_runtime_t *runtime);
pxa_status_t pxa_runtime_usage_snapshot(const pxa_runtime_t *runtime,
                                        pxa_runtime_usage_t *output);

pxa_status_t pxa_component_create(pxa_runtime_t *runtime, uint64_t instance_id,
                                  pxa_component_t *output);
pxa_status_t pxa_component_remove(pxa_runtime_t *runtime,
                                  pxa_component_t component);
pxa_status_t pxa_component_begin_start(pxa_runtime_t *runtime,
                                       pxa_component_t component);
pxa_status_t pxa_component_finish_start(pxa_runtime_t *runtime,
                                        pxa_component_t component,
                                        pxa_status_t result);
pxa_status_t pxa_component_begin_event(pxa_runtime_t *runtime,
                                       pxa_component_t component);
pxa_status_t pxa_component_finish_event(pxa_runtime_t *runtime,
                                        pxa_component_t component,
                                        int32_t result);
pxa_status_t pxa_component_request_stop(pxa_runtime_t *runtime,
                                        pxa_component_t component,
                                        pxa_stop_reason_t reason);
pxa_status_t pxa_component_begin_stop(pxa_runtime_t *runtime,
                                      pxa_component_t component);
pxa_status_t pxa_component_finish_stop(pxa_runtime_t *runtime,
                                       pxa_component_t component);
pxa_status_t pxa_component_abort(pxa_runtime_t *runtime,
                                 pxa_component_t component,
                                 pxa_stop_reason_t reason);
pxa_status_t pxa_component_validate_import(const pxa_runtime_t *runtime,
                                           pxa_component_t component);
pxa_status_t pxa_component_snapshot(const pxa_runtime_t *runtime,
                                    pxa_component_t component,
                                    pxa_component_snapshot_t *output);

pxa_status_t pxa_runtime_control(pxa_runtime_t *runtime,
                                 pxa_component_t component,
                                 const void *message, size_t message_size);
int32_t pxa_runtime_io(pxa_runtime_t *runtime, pxa_component_t component,
                       pxa_handle_t handle, uint32_t operation,
                       uint8_t *data, size_t size);

pxa_status_t pxa_request_begin(pxa_runtime_t *runtime,
                               pxa_component_t component, uint32_t request_id,
                               uint16_t service, uint16_t opcode,
                               pxa_authority_t authority);
pxa_status_t pxa_request_commit(pxa_runtime_t *runtime,
                                pxa_component_t component,
                                uint32_t request_id);
pxa_status_t pxa_request_complete(pxa_runtime_t *runtime,
                                  pxa_component_t component,
                                  uint32_t request_id, pxa_status_t result,
                                  const void *payload, size_t payload_size);
pxa_status_t pxa_request_cancel(pxa_runtime_t *runtime,
                                pxa_component_t component,
                                uint32_t request_id);
int pxa_request_is_active(const pxa_runtime_t *runtime,
                          pxa_component_t component, uint32_t request_id);

pxa_status_t pxa_handle_open(pxa_runtime_t *runtime,
                             pxa_component_t component,
                             pxa_resource_type_t type,
                             pxa_authority_t authority,
                             const pxa_resource_t *resource,
                             pxa_handle_t *output);
pxa_status_t pxa_handle_get(const pxa_runtime_t *runtime,
                            pxa_component_t component, pxa_handle_t handle,
                            pxa_resource_type_t expected_type,
                            pxa_resource_t *output);
pxa_status_t pxa_handle_close(pxa_runtime_t *runtime,
                              pxa_component_t component,
                              pxa_handle_t handle);
pxa_status_t pxa_authority_revoke(pxa_runtime_t *runtime,
                                  pxa_component_t component,
                                  pxa_authority_t authority);

pxa_status_t pxa_event_post(pxa_runtime_t *runtime,
                            pxa_component_t component, const void *message,
                            size_t message_size, uint8_t reliable,
                            uint64_t coalesce_key);
/* Encode and post an event without requiring a contiguous envelope/payload
 * staging buffer. The payload is copied into the Runtime event pool. */
pxa_status_t pxa_event_post_message(
    pxa_runtime_t *runtime, pxa_component_t component, uint16_t service,
    uint16_t opcode, uint32_t request_id, pxa_bytes_t payload,
    uint8_t reliable, uint64_t coalesce_key);
pxa_status_t pxa_event_post_messagev(
    pxa_runtime_t *runtime, pxa_component_t component, uint16_t service,
    uint16_t opcode, uint32_t request_id, const pxa_bytes_t *payload_parts,
    size_t payload_part_count, uint8_t reliable, uint64_t coalesce_key);
pxa_status_t pxa_event_peek(const pxa_runtime_t *runtime,
                            pxa_component_t component,
                            pxa_event_view_t *output);
pxa_status_t pxa_event_read(const pxa_runtime_t *runtime,
                            pxa_event_token_t token, size_t offset,
                            void *output, size_t capacity, size_t *read_size);
pxa_status_t pxa_event_pop(pxa_runtime_t *runtime,
                           pxa_component_t component, void *output,
                           size_t capacity, size_t *event_size);
pxa_status_t pxa_event_consume(pxa_runtime_t *runtime,
                               pxa_component_t component,
                               pxa_event_token_t token);

#ifdef __cplusplus
}
#endif

#endif
