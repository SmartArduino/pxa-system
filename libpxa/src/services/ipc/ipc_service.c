#include "pxa/ipc.h"
#include "common/checked_math.h"
#include "common/status_internal.h"
#include "services/ipc/ipc_internal.h"

#include <stdint.h>
#include <string.h>

#define PXA_IPC_MAGIC UINT32_C(0x50584950)
#define PXA_IPC_SLOT_NONE UINT16_MAX

typedef struct {
    pxa_component_t provider;
    uint8_t name[64];
    uint16_t next;
    uint8_t name_size;
    uint8_t lazy;
} pxa_ipc_endpoint_t;

typedef struct {
    uint32_t call_id;
    uint32_t caller_request_id;
    pxa_component_t caller;
    pxa_component_t provider;
    pxa_status_t pending_status;
    uint8_t *request_payload;
    uint8_t endpoint[PXA_IPC_MAX_ENDPOINT_BYTES];
    uint16_t next;
    uint16_t request_payload_size;
    uint8_t endpoint_size;
    uint8_t pending_notification;
    uint8_t waiting_provider;
} pxa_ipc_call_t;

struct pxa_ipc_broker {
    uint32_t magic;
    pxa_runtime_t *runtime;
    pxa_ipc_endpoint_t *endpoints;
    pxa_ipc_call_t *calls;
    uint16_t max_endpoints;
    uint16_t max_pending_calls;
    uint16_t endpoint_free_head;
    uint16_t endpoint_active_head;
    uint16_t call_free_head;
    uint16_t call_active_head;
    uint32_t next_call_id;
    void *allocator_context;
    pxa_ipc_allocate_fn allocate;
    pxa_ipc_release_fn release;
    void *resolver_context;
    pxa_ipc_endpoint_resolver_fn resolver;
    uint8_t registered;
    uint8_t resolving;
};

static int broker_valid(const pxa_ipc_broker_t *broker) {
    return broker != NULL && broker->magic == PXA_IPC_MAGIC;
}

void pxa_ipc_limits_init(pxa_ipc_limits_t *limits) {
    if (limits == NULL) return;
    memset(limits, 0, sizeof(*limits));
    limits->struct_size = sizeof(*limits);
    limits->max_endpoints = 16;
    limits->max_pending_calls = 16;
}

static int limits_valid(const pxa_ipc_limits_t *limits) {
    return limits != NULL && limits->struct_size >= sizeof(*limits) &&
           limits->max_endpoints != 0 && limits->max_pending_calls != 0;
}

size_t pxa_ipc_broker_workspace_size(const pxa_ipc_limits_t *limits) {
    size_t size;
    if (!limits_valid(limits)) return 0;
    size = PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    size = (size_t)pxa_internal_align_pointer(size, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    size += sizeof(pxa_ipc_broker_t);
    size = (size_t)pxa_internal_align_pointer(size, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    size += (size_t)limits->max_endpoints * sizeof(pxa_ipc_endpoint_t);
    size = (size_t)pxa_internal_align_pointer(size, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    size += (size_t)limits->max_pending_calls * sizeof(pxa_ipc_call_t);
    return size;
}

pxa_status_t pxa_ipc_broker_init(void *workspace, size_t workspace_size,
                                 pxa_runtime_t *runtime,
                                 const pxa_ipc_limits_t *limits,
                                 pxa_ipc_broker_t **output) {
    uintptr_t cursor;
    uintptr_t end;
    pxa_ipc_broker_t *broker;
    size_t required;
    uint16_t index;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    required = pxa_ipc_broker_workspace_size(limits);
    if (workspace == NULL || runtime == NULL || required == 0 ||
        workspace_size < required ||
        (uintptr_t)workspace > UINTPTR_MAX - workspace_size) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    end = (uintptr_t)workspace + workspace_size;
    cursor = pxa_internal_align_pointer((uintptr_t)workspace, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    broker = (pxa_ipc_broker_t *)cursor;
    cursor = pxa_internal_align_pointer(cursor + sizeof(*broker), PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    if (cursor > end ||
        (size_t)limits->max_endpoints * sizeof(*broker->endpoints) >
            end - cursor) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    memset(broker, 0, sizeof(*broker));
    broker->endpoints = (pxa_ipc_endpoint_t *)cursor;
    cursor += (size_t)limits->max_endpoints * sizeof(*broker->endpoints);
    cursor = pxa_internal_align_pointer(cursor, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    if (cursor > end ||
        (size_t)limits->max_pending_calls * sizeof(*broker->calls) >
            end - cursor) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    broker->calls = (pxa_ipc_call_t *)cursor;
    memset(broker->endpoints, 0,
           (size_t)limits->max_endpoints * sizeof(*broker->endpoints));
    memset(broker->calls, 0,
           (size_t)limits->max_pending_calls * sizeof(*broker->calls));
    broker->runtime = runtime;
    broker->max_endpoints = limits->max_endpoints;
    broker->max_pending_calls = limits->max_pending_calls;
    broker->endpoint_free_head = 0;
    broker->endpoint_active_head = PXA_IPC_SLOT_NONE;
    for (index = 0; index < broker->max_endpoints; ++index) {
        broker->endpoints[index].next =
            index + 1u < broker->max_endpoints
                ? (uint16_t)(index + 1u)
                : PXA_IPC_SLOT_NONE;
    }
    broker->call_free_head = 0;
    broker->call_active_head = PXA_IPC_SLOT_NONE;
    for (index = 0; index < broker->max_pending_calls; ++index) {
        broker->calls[index].next =
            index + 1u < broker->max_pending_calls
                ? (uint16_t)(index + 1u)
                : PXA_IPC_SLOT_NONE;
    }
    broker->next_call_id = 1;
    broker->magic = PXA_IPC_MAGIC;
    *output = broker;
    return PXA_STATUS_OK;
}

static int endpoint_equal(const pxa_ipc_endpoint_t *entry,
                          pxa_bytes_t name) {
    return entry->name_size == name.size &&
           memcmp(entry->name, name.data, name.size) == 0;
}

static uint16_t find_endpoint_index(
    const pxa_ipc_broker_t *broker, pxa_bytes_t name, uint16_t *previous) {
    uint16_t prior = PXA_IPC_SLOT_NONE;
    uint16_t index = broker->endpoint_active_head;
    while (index != PXA_IPC_SLOT_NONE) {
        const pxa_ipc_endpoint_t *entry = &broker->endpoints[index];
        if (endpoint_equal(entry, name)) {
            if (previous != NULL) *previous = prior;
            return index;
        }
        prior = index;
        index = entry->next;
    }
    return PXA_IPC_SLOT_NONE;
}

static pxa_ipc_endpoint_t *find_endpoint(pxa_ipc_broker_t *broker,
                                         pxa_bytes_t name) {
    uint16_t index = find_endpoint_index(broker, name, NULL);
    return index == PXA_IPC_SLOT_NONE ? NULL : &broker->endpoints[index];
}

static void release_endpoint(pxa_ipc_broker_t *broker, uint16_t index,
                             uint16_t previous) {
    pxa_ipc_endpoint_t *entry = &broker->endpoints[index];
    if (previous == PXA_IPC_SLOT_NONE) {
        broker->endpoint_active_head = entry->next;
    } else {
        broker->endpoints[previous].next = entry->next;
    }
    memset(entry, 0, sizeof(*entry));
    entry->next = broker->endpoint_free_head;
    broker->endpoint_free_head = index;
}

static pxa_status_t add_endpoint(pxa_ipc_broker_t *broker,
                                 pxa_bytes_t endpoint,
                                 pxa_component_t provider, uint8_t lazy) {
    uint16_t index;
    pxa_ipc_endpoint_t *entry;
    if (find_endpoint(broker, endpoint) != NULL) return PXA_STATUS_BUSY;
    index = broker->endpoint_free_head;
    if (index == PXA_IPC_SLOT_NONE) return PXA_STATUS_RESOURCE_LIMIT;
    entry = &broker->endpoints[index];
    broker->endpoint_free_head = entry->next;
    memset(entry, 0, sizeof(*entry));
    entry->provider = provider;
    entry->name_size = (uint8_t)endpoint.size;
    entry->lazy = lazy;
    memcpy(entry->name, endpoint.data, endpoint.size);
    entry->next = broker->endpoint_active_head;
    broker->endpoint_active_head = index;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_ipc_broker_set_endpoint_resolver(
    pxa_ipc_broker_t *broker, void *context,
    pxa_ipc_endpoint_resolver_fn resolver) {
    if (!broker_valid(broker) || (resolver == NULL && context != NULL)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (broker->resolving) return PXA_STATUS_BUSY;
    broker->resolver_context = context;
    broker->resolver = resolver;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_ipc_broker_set_allocator(pxa_ipc_broker_t *broker,
                                          void *context,
                                          pxa_ipc_allocate_fn allocate,
                                          pxa_ipc_release_fn release) {
    uint16_t index;
    if (!broker_valid(broker) || (allocate == NULL) != (release == NULL) ||
        (allocate == NULL && context != NULL)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    for (index = broker->call_active_head; index != PXA_IPC_SLOT_NONE;
         index = broker->calls[index].next) {
        if (broker->calls[index].request_payload != NULL) {
            return PXA_STATUS_BUSY;
        }
    }
    broker->allocator_context = context;
    broker->allocate = allocate;
    broker->release = release;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_ipc_endpoint_declare(pxa_ipc_broker_t *broker,
                                      pxa_bytes_t endpoint) {
    if (!broker_valid(broker) || !pxa_ipc_endpoint_is_valid(endpoint)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    return add_endpoint(broker, endpoint, PXA_COMPONENT_INVALID, 1);
}

pxa_status_t pxa_ipc_endpoint_register(pxa_ipc_broker_t *broker,
                                       pxa_bytes_t endpoint,
                                       pxa_component_t provider) {
    pxa_component_snapshot_t snapshot;
    if (!broker_valid(broker) || !pxa_ipc_endpoint_is_valid(endpoint) ||
        provider == PXA_COMPONENT_INVALID) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (pxa_component_snapshot(broker->runtime, provider, &snapshot) !=
        PXA_STATUS_OK) {
        return PXA_STATUS_NOT_FOUND;
    }
    if (snapshot.state != PXA_COMPONENT_RUNNING) return PXA_STATUS_BAD_STATE;
    return add_endpoint(broker, endpoint, provider, 0);
}

static pxa_status_t resolve_endpoint_provider(
    pxa_ipc_broker_t *broker, pxa_bytes_t endpoint_name,
    pxa_ipc_endpoint_t **endpoint_output) {
    pxa_component_snapshot_t snapshot;
    pxa_component_t provider = PXA_COMPONENT_INVALID;
    pxa_ipc_endpoint_t *endpoint = find_endpoint(broker, endpoint_name);
    pxa_status_t status;
    if (endpoint_output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *endpoint_output = NULL;
    if (endpoint == NULL) return PXA_STATUS_NOT_FOUND;
    if (endpoint->provider == PXA_COMPONENT_INVALID) {
        if (!endpoint->lazy || broker->resolver == NULL) {
            return PXA_STATUS_UNAVAILABLE;
        }
        if (broker->resolving) return PXA_STATUS_WOULD_BLOCK;
        broker->resolving = 1;
        status = pxa_status_normalize(broker->resolver(
            broker->resolver_context, endpoint_name, &provider));
        broker->resolving = 0;
        if (status != PXA_STATUS_OK) return status;
        if (provider == PXA_COMPONENT_INVALID) return PXA_STATUS_NOT_FOUND;
        if (pxa_component_snapshot(broker->runtime, provider, &snapshot) !=
            PXA_STATUS_OK) {
            return PXA_STATUS_NOT_FOUND;
        }
        if (snapshot.state != PXA_COMPONENT_RUNNING) {
            return PXA_STATUS_BAD_STATE;
        }
        /* The resolver may run arbitrary Host logic, so look the declaration
         * up again before publishing the provider binding. */
        endpoint = find_endpoint(broker, endpoint_name);
        if (endpoint == NULL || !endpoint->lazy) return PXA_STATUS_NOT_FOUND;
        endpoint->provider = provider;
    }
    *endpoint_output = endpoint;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_ipc_endpoint_unregister(pxa_ipc_broker_t *broker,
                                         pxa_bytes_t endpoint,
                                         pxa_component_t provider) {
    pxa_ipc_endpoint_t *entry;
    uint16_t index;
    uint16_t previous;
    if (!broker_valid(broker) || !pxa_ipc_endpoint_is_valid(endpoint)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    index = find_endpoint_index(broker, endpoint, &previous);
    entry = index == PXA_IPC_SLOT_NONE ? NULL : &broker->endpoints[index];
    if (entry == NULL || entry->provider != provider) return PXA_STATUS_NOT_FOUND;
    release_endpoint(broker, index, previous);
    return PXA_STATUS_OK;
}

static pxa_ipc_call_t *allocate_call(pxa_ipc_broker_t *broker) {
    uint16_t index = broker->call_free_head;
    pxa_ipc_call_t *call;
    if (index == PXA_IPC_SLOT_NONE) return NULL;
    call = &broker->calls[index];
    broker->call_free_head = call->next;
    memset(call, 0, sizeof(*call));
    call->next = broker->call_active_head;
    broker->call_active_head = index;
    return call;
}

static uint16_t find_call_index(
    const pxa_ipc_broker_t *broker, uint32_t call_id, uint16_t *previous) {
    uint16_t prior = PXA_IPC_SLOT_NONE;
    uint16_t index = broker->call_active_head;
    while (index != PXA_IPC_SLOT_NONE) {
        const pxa_ipc_call_t *call = &broker->calls[index];
        if (call->call_id == call_id) {
            if (previous != NULL) *previous = prior;
            return index;
        }
        prior = index;
        index = call->next;
    }
    return PXA_IPC_SLOT_NONE;
}

static void release_call(pxa_ipc_broker_t *broker, uint16_t index,
                         uint16_t previous) {
    pxa_ipc_call_t *call = &broker->calls[index];
    if (previous == PXA_IPC_SLOT_NONE) {
        broker->call_active_head = call->next;
    } else {
        broker->calls[previous].next = call->next;
    }
    if (call->request_payload != NULL && broker->release != NULL) {
        broker->release(broker->allocator_context, call->request_payload);
    }
    memset(call, 0, sizeof(*call));
    call->next = broker->call_free_head;
    broker->call_free_head = index;
}

static uint32_t allocate_call_id(pxa_ipc_broker_t *broker) {
    uint16_t attempt;
    for (attempt = 0; attempt <= broker->max_pending_calls; ++attempt) {
        uint32_t candidate = broker->next_call_id++;
        if (candidate != 0 &&
            find_call_index(broker, candidate, NULL) == PXA_IPC_SLOT_NONE) {
            return candidate;
        }
    }
    return 0;
}

static pxa_status_t post_reply(pxa_ipc_broker_t *broker,
                               const pxa_ipc_call_t *call,
                               pxa_status_t status, pxa_bytes_t payload) {
    uint8_t status_bytes[4];
    uint8_t record_header[PXA_RECORD_HEADER_SIZE];
    pxa_bytes_t parts[3];
    size_t part_count = 1;
    pxa_write_u32(status_bytes, (uint32_t)status);
    parts[0] = (pxa_bytes_t){status_bytes, sizeof(status_bytes)};
    if (status == PXA_STATUS_OK && payload.size != 0) {
        pxa_write_u16(record_header, 3);
        pxa_write_u16(record_header + 2, (uint16_t)payload.size);
        parts[1] =
            (pxa_bytes_t){record_header, sizeof(record_header)};
        parts[2] = payload;
        part_count = 3;
    }
    return pxa_event_post_messagev(
        broker->runtime, call->caller, PXA_IPC_SERVICE_ID,
        PXA_IPC_REPLY_EVENT, call->call_id, parts, part_count, 1, 0);
}

static pxa_status_t queue_waiting_call(pxa_ipc_broker_t *broker,
                                       pxa_ipc_call_t *call,
                                       pxa_component_t caller,
                                       uint32_t caller_request_id,
                                       const pxa_ipc_call_request_t *request) {
    if (request->payload.size != 0) {
        if (broker->allocate == NULL) return PXA_STATUS_UNAVAILABLE;
        call->request_payload = (uint8_t *)broker->allocate(
            broker->allocator_context, request->payload.size);
        if (call->request_payload == NULL) return PXA_STATUS_RESOURCE_LIMIT;
        memcpy(call->request_payload, request->payload.data,
               request->payload.size);
    }
    memcpy(call->endpoint, request->endpoint.data, request->endpoint.size);
    call->endpoint_size = (uint8_t)request->endpoint.size;
    call->request_payload_size = (uint16_t)request->payload.size;
    call->caller = caller;
    call->caller_request_id = caller_request_id;
    call->provider = PXA_COMPONENT_INVALID;
    call->waiting_provider = 1;
    return PXA_STATUS_OK;
}

static pxa_status_t post_waiting_request(pxa_ipc_broker_t *broker,
                                         pxa_ipc_call_t *call,
                                         pxa_component_t provider,
                                         uint32_t call_id) {
    uint8_t headers[2u * PXA_RECORD_HEADER_SIZE];
    pxa_bytes_t parts[4];
    size_t part_count = 2;
    pxa_write_u16(headers, 1);
    pxa_write_u16(headers + 2, call->endpoint_size);
    parts[0] = (pxa_bytes_t){headers, PXA_RECORD_HEADER_SIZE};
    parts[1] = (pxa_bytes_t){call->endpoint, call->endpoint_size};
    if (call->request_payload_size != 0) {
        pxa_write_u16(headers + PXA_RECORD_HEADER_SIZE, 2);
        pxa_write_u16(headers + PXA_RECORD_HEADER_SIZE + 2,
                      call->request_payload_size);
        parts[2] = (pxa_bytes_t){headers + PXA_RECORD_HEADER_SIZE,
                                 PXA_RECORD_HEADER_SIZE};
        parts[3] = (pxa_bytes_t){call->request_payload,
                                 call->request_payload_size};
        part_count = 4;
    }
    return pxa_event_post_messagev(
        broker->runtime, provider, PXA_IPC_SERVICE_ID, PXA_IPC_REQUEST_EVENT,
        call_id, parts, part_count, 1, 0);
}

static pxa_status_t complete_waiting_request(pxa_ipc_broker_t *broker,
                                             pxa_ipc_call_t *call,
                                             pxa_status_t result,
                                             uint32_t call_id) {
    uint8_t result_data[4];
    pxa_status_t status;
    pxa_write_u32(result_data, call_id);
    status = pxa_request_complete(
        broker->runtime, call->caller, call->caller_request_id, result,
        result == PXA_STATUS_OK ? result_data : NULL,
        result == PXA_STATUS_OK ? sizeof(result_data) : 0);
    if (status != PXA_STATUS_OK) {
        (void)pxa_request_cancel(broker->runtime, call->caller,
                                 call->caller_request_id);
    }
    return status;
}

static pxa_status_t ipc_control(void *context, pxa_runtime_t *runtime,
                                pxa_component_t component,
                                const pxa_message_view_t *message) {
    pxa_ipc_broker_t *broker = (pxa_ipc_broker_t *)context;
    pxa_status_t result;
    pxa_status_t complete;
    uint8_t result_data[4] = {0};
    size_t result_size = 0;
    int defer_completion = 0;
    (void)runtime;
    if (!broker_valid(broker) || message->request_id == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (message->opcode != PXA_IPC_CALL && message->opcode != PXA_IPC_REPLY) {
        return PXA_STATUS_UNSUPPORTED;
    }
    result = pxa_request_begin(broker->runtime, component, message->request_id,
                               PXA_IPC_SERVICE_ID, message->opcode, 0);
    if (result != PXA_STATUS_OK) return result;
    if (message->opcode == PXA_IPC_CALL) {
        pxa_ipc_call_request_t request;
        pxa_ipc_endpoint_t *endpoint = NULL;
        pxa_ipc_call_t *call = NULL;
        uint32_t call_id = 0;
        result = pxa_ipc_parse_call(message->payload, &request);
        if (result == PXA_STATUS_OK) {
            endpoint = find_endpoint(broker, request.endpoint);
            if (endpoint == NULL) result = PXA_STATUS_NOT_FOUND;
        }
        if (result == PXA_STATUS_OK) {
            call = allocate_call(broker);
            if (call == NULL) result = PXA_STATUS_RESOURCE_LIMIT;
        }
        if (result == PXA_STATUS_OK &&
            endpoint->provider == PXA_COMPONENT_INVALID) {
            if (!endpoint->lazy || broker->resolver == NULL) {
                result = PXA_STATUS_UNAVAILABLE;
            } else {
                result = queue_waiting_call(broker, call, component,
                                            message->request_id, &request);
                defer_completion = result == PXA_STATUS_OK;
            }
        }
        if (result == PXA_STATUS_OK && !defer_completion) {
            call_id = allocate_call_id(broker);
            if (call_id == 0) result = PXA_STATUS_RESOURCE_LIMIT;
        }
        if (result == PXA_STATUS_OK && !defer_completion) {
            result = pxa_event_post_message(
                broker->runtime, endpoint->provider, PXA_IPC_SERVICE_ID,
                PXA_IPC_REQUEST_EVENT, call_id, message->payload, 1, 0);
        }
        if (result == PXA_STATUS_OK && !defer_completion) {
            call->call_id = call_id;
            call->caller = component;
            call->provider = endpoint->provider;
            pxa_write_u32(result_data, call_id);
            result_size = 4;
        } else if (result != PXA_STATUS_OK && call != NULL) {
            release_call(broker, (uint16_t)(call - broker->calls),
                         PXA_IPC_SLOT_NONE);
        }
    } else {
        pxa_ipc_reply_request_t request;
        pxa_ipc_call_t *call = NULL;
        uint16_t call_index = PXA_IPC_SLOT_NONE;
        uint16_t previous = PXA_IPC_SLOT_NONE;
        result = pxa_ipc_parse_reply(message->payload, &request);
        if (result == PXA_STATUS_OK) {
            call_index = find_call_index(broker, request.call_id, &previous);
            call = call_index == PXA_IPC_SLOT_NONE
                       ? NULL
                       : &broker->calls[call_index];
            if (call == NULL) result = PXA_STATUS_NOT_FOUND;
        }
        if (result == PXA_STATUS_OK && call->provider != component) {
            result = PXA_STATUS_DENIED;
        }
        if (result == PXA_STATUS_OK) {
            result = post_reply(broker, call, request.status, request.payload);
            if (result == PXA_STATUS_OK) {
                release_call(broker, call_index, previous);
            }
        }
    }
    if (defer_completion) return PXA_STATUS_OK;
    complete = pxa_request_complete(broker->runtime, component,
                                    message->request_id, result,
                                    result == PXA_STATUS_OK ? result_data : NULL,
                                    result == PXA_STATUS_OK ? result_size : 0);
    if (complete != PXA_STATUS_OK) {
        (void)pxa_request_cancel(broker->runtime, component,
                                 message->request_id);
    }
    return PXA_STATUS_OK;
}

static pxa_status_t flush_pending_notifications(pxa_ipc_broker_t *broker) {
    uint16_t previous = PXA_IPC_SLOT_NONE;
    uint16_t index = broker->call_active_head;
    pxa_status_t final_status = PXA_STATUS_OK;
    while (index != PXA_IPC_SLOT_NONE) {
        pxa_ipc_call_t *call = &broker->calls[index];
        uint16_t next = call->next;
        if (call->pending_notification) {
            pxa_status_t status = post_reply(
                broker, call, call->pending_status, (pxa_bytes_t){NULL, 0});
            if (status == PXA_STATUS_OK || status == PXA_STATUS_BAD_STATE ||
                status == PXA_STATUS_NOT_FOUND) {
                release_call(broker, index, previous);
            } else {
                final_status = status;
                previous = index;
            }
        } else {
            previous = index;
        }
        index = next;
    }
    return final_status;
}

static void ipc_component_stopped(void *context, pxa_runtime_t *runtime,
                                  pxa_component_t component) {
    pxa_ipc_broker_t *broker = (pxa_ipc_broker_t *)context;
    uint16_t previous = PXA_IPC_SLOT_NONE;
    uint16_t index = broker->endpoint_active_head;
    (void)runtime;
    while (index != PXA_IPC_SLOT_NONE) {
        pxa_ipc_endpoint_t *entry = &broker->endpoints[index];
        uint16_t next = entry->next;
        if (entry->provider == component) {
            if (entry->lazy) {
                entry->provider = PXA_COMPONENT_INVALID;
                previous = index;
            } else {
                release_endpoint(broker, index, previous);
            }
        } else {
            previous = index;
        }
        index = next;
    }
    previous = PXA_IPC_SLOT_NONE;
    index = broker->call_active_head;
    while (index != PXA_IPC_SLOT_NONE) {
        pxa_ipc_call_t *call = &broker->calls[index];
        uint16_t next = call->next;
        if (call->caller == component) {
            release_call(broker, index, previous);
        } else if (call->provider == component) {
            call->provider = PXA_COMPONENT_INVALID;
            call->pending_status = PXA_STATUS_CANCELLED;
            call->pending_notification = 1;
            previous = index;
        } else {
            previous = index;
        }
        index = next;
    }
    /* Teardown may stop several Components in sequence. Only flush existing
     * cancellation notices here; resolving another endpoint would activate a
     * new provider while the Package is shutting down. */
    (void)flush_pending_notifications(broker);
}

pxa_status_t pxa_ipc_broker_register(pxa_ipc_broker_t *broker) {
    pxa_service_ops_t operations;
    pxa_status_t status;
    if (!broker_valid(broker)) return PXA_STATUS_INVALID_ARGUMENT;
    if (broker->registered) return PXA_STATUS_BAD_STATE;
    memset(&operations, 0, sizeof(operations));
    operations.struct_size = sizeof(operations);
    operations.service_id = PXA_IPC_SERVICE_ID;
    operations.major = PXA_IPC_SERVICE_MAJOR;
    operations.minor = PXA_IPC_SERVICE_MINOR;
    operations.context = broker;
    operations.control = ipc_control;
    operations.component_stopped = ipc_component_stopped;
    status = pxa_service_register(broker->runtime, &operations);
    if (status == PXA_STATUS_OK) broker->registered = 1;
    return status;
}

pxa_status_t pxa_ipc_flush(pxa_ipc_broker_t *broker) {
    uint16_t previous = PXA_IPC_SLOT_NONE;
    uint16_t index;
    pxa_status_t final_status = PXA_STATUS_OK;
    if (!broker_valid(broker)) return PXA_STATUS_INVALID_ARGUMENT;
    index = broker->call_active_head;
    while (index != PXA_IPC_SLOT_NONE) {
        pxa_ipc_call_t *call = &broker->calls[index];
        uint16_t next = call->next;
        if (call->waiting_provider) {
            pxa_bytes_t endpoint_name = {call->endpoint,
                                         call->endpoint_size};
            pxa_ipc_endpoint_t *endpoint = NULL;
            pxa_status_t status = resolve_endpoint_provider(
                broker, endpoint_name, &endpoint);
            if (status == PXA_STATUS_WOULD_BLOCK) {
                final_status = status;
                previous = index;
            } else if (status != PXA_STATUS_OK) {
                pxa_status_t complete = complete_waiting_request(
                    broker, call, status, 0);
                release_call(broker, index, previous);
                if (complete != PXA_STATUS_OK) final_status = complete;
            } else {
                uint32_t call_id = allocate_call_id(broker);
                if (call_id == 0) {
                    final_status = PXA_STATUS_RESOURCE_LIMIT;
                    previous = index;
                } else {
                    status = post_waiting_request(broker, call,
                                                  endpoint->provider,
                                                  call_id);
                    if (status != PXA_STATUS_OK) {
                        final_status = status;
                        previous = index;
                    } else {
                        pxa_status_t complete;
                        call->call_id = call_id;
                        call->provider = endpoint->provider;
                        call->waiting_provider = 0;
                        complete = complete_waiting_request(
                            broker, call, PXA_STATUS_OK, call_id);
                        call->caller_request_id = 0;
                        if (call->request_payload != NULL) {
                            broker->release(broker->allocator_context,
                                            call->request_payload);
                            call->request_payload = NULL;
                        }
                        call->request_payload_size = 0;
                        if (complete != PXA_STATUS_OK) {
                            release_call(broker, index, previous);
                            final_status = complete;
                        } else {
                            previous = index;
                        }
                    }
                }
            }
        } else {
            previous = index;
        }
        index = next;
    }
    {
        pxa_status_t status = flush_pending_notifications(broker);
        if (status != PXA_STATUS_OK) final_status = status;
    }
    return final_status;
}
