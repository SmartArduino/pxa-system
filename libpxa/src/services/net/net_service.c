#include "pxa/net.h"
#include "common/checked_math.h"
#include "common/status_internal.h"
#include "services/net/net_internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#define PXA_NET_MAGIC UINT32_C(0x50584e54)
#define PXA_NET_STREAM_MAGIC UINT32_C(0x50584e53)
#define PXA_NET_SLOT_NONE UINT16_MAX
#define PXA_NET_RESULT_OVERHEAD ((size_t)160)

typedef struct pxa_net_pending pxa_net_pending_t;
typedef struct pxa_net_stream pxa_net_stream_t;

struct pxa_net_service {
    uint32_t magic;
    pxa_runtime_t *runtime;
    pxa_permission_service_t *permissions;
    pxa_net_backend_t backend;
    pxa_net_pending_t *pending;
    pxa_net_stream_t *streams;
    uint8_t *result_buffer;
    uint32_t max_response_bytes;
    uint32_t max_response_header_bytes;
    pxa_net_request_limits_t request_limits;
    size_t result_capacity;
    uint16_t max_pending_requests;
    uint16_t max_requests_per_component;
    uint16_t max_response_streams;
    uint16_t pending_count;
    uint16_t pending_free_head;
    uint16_t pending_active_head;
    uint16_t stream_free_head;
    uint8_t registered;
};

struct pxa_net_pending {
    pxa_component_t component;
    pxa_handle_t permission_handle;
    pxa_authority_t authority;
    uint64_t operation;
    uint32_t request_id;
    uint32_t max_response_bytes;
    uint16_t opcode;
    uint16_t next;
    uint16_t previous_active;
};

struct pxa_net_stream {
    uint32_t magic;
    pxa_net_service_t *service;
    void *body_stream;
    uint32_t remaining;
    uint16_t next_free;
    uint8_t active;
};

static int config_valid(const pxa_net_config_t *config) {
    const pxa_net_backend_t *backend;
    if (config == NULL || config->struct_size < sizeof(*config) ||
        config->max_pending_requests == 0 ||
        config->max_requests_per_component == 0 ||
        config->max_requests_per_component > config->max_pending_requests ||
        config->max_response_streams == 0 || config->max_response_bytes == 0 ||
        config->max_headers == 0 || config->max_headers > PXA_NET_MAX_HEADERS ||
        config->max_inline_body_bytes == 0 ||
        config->max_request_header_bytes == 0 ||
        config->max_request_header_bytes > PXA_NET_MAX_HEADER_BLOCK_BYTES ||
        config->max_response_header_bytes == 0 ||
        config->max_response_header_bytes > PXA_NET_MAX_HEADER_BLOCK_BYTES ||
        config->min_timeout_ms == 0 ||
        config->default_timeout_ms < config->min_timeout_ms ||
        config->default_timeout_ms > config->max_timeout_ms ||
        config->min_timeout_ms > config->max_timeout_ms ||
        config->permissions == NULL) {
        return 0;
    }
    backend = &config->backend;
    return backend->struct_size >= sizeof(*backend) &&
           backend->start != NULL && backend->poll != NULL &&
           backend->cancel != NULL && backend->read_body != NULL &&
           backend->close_body != NULL;
}

size_t pxa_net_service_workspace_size(const pxa_net_config_t *config) {
    size_t pending_size;
    size_t streams_size;
    size_t result_size;
    size_t size = PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    if (!config_valid(config) ||
        sizeof(pxa_net_pending_t) >
            SIZE_MAX / (size_t)config->max_pending_requests ||
        sizeof(pxa_net_stream_t) >
            SIZE_MAX / (size_t)config->max_response_streams) {
        return 0;
    }
    pending_size = (size_t)config->max_pending_requests *
                   sizeof(pxa_net_pending_t);
    streams_size = (size_t)config->max_response_streams *
                   sizeof(pxa_net_stream_t);
    result_size = (size_t)config->max_response_header_bytes +
                  PXA_NET_RESULT_OVERHEAD;
    if (sizeof(pxa_net_service_t) > SIZE_MAX - size) return 0;
    size += sizeof(pxa_net_service_t);
    if (pending_size > SIZE_MAX - size) return 0;
    size += pending_size;
    if (streams_size > SIZE_MAX - size) return 0;
    size += streams_size;
    if (result_size > SIZE_MAX - size) return 0;
    return size + result_size;
}

pxa_status_t pxa_net_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_net_config_t *config, pxa_net_service_t **output) {
    size_t required;
    uintptr_t cursor;
    uintptr_t end;
    uint16_t index;
    pxa_net_service_t *service;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    required = pxa_net_service_workspace_size(config);
    if (workspace == NULL || runtime == NULL || required == 0 ||
        workspace_size < required ||
        (uintptr_t)workspace > UINTPTR_MAX - workspace_size) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    end = (uintptr_t)workspace + workspace_size;
    cursor = pxa_internal_align_pointer((uintptr_t)workspace, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    service = (pxa_net_service_t *)cursor;
    memset(service, 0, sizeof(*service));
    cursor += sizeof(*service);
    service->pending = (pxa_net_pending_t *)cursor;
    cursor += (size_t)config->max_pending_requests *
              sizeof(service->pending[0]);
    service->streams = (pxa_net_stream_t *)cursor;
    cursor += (size_t)config->max_response_streams *
              sizeof(service->streams[0]);
    service->result_buffer = (uint8_t *)cursor;
    service->result_capacity =
        (size_t)config->max_response_header_bytes + PXA_NET_RESULT_OVERHEAD;
    cursor += service->result_capacity;
    if (cursor > end) return PXA_STATUS_INVALID_ARGUMENT;
    service->runtime = runtime;
    service->permissions = config->permissions;
    service->backend = config->backend;
    service->max_response_bytes = config->max_response_bytes;
    service->max_response_header_bytes = config->max_response_header_bytes;
    service->request_limits.max_inline_body_bytes =
        config->max_inline_body_bytes;
    service->request_limits.max_request_header_bytes =
        config->max_request_header_bytes;
    service->request_limits.min_timeout_ms = config->min_timeout_ms;
    service->request_limits.default_timeout_ms = config->default_timeout_ms;
    service->request_limits.max_timeout_ms = config->max_timeout_ms;
    service->request_limits.max_headers = config->max_headers;
    service->max_pending_requests = config->max_pending_requests;
    service->max_requests_per_component =
        config->max_requests_per_component;
    service->max_response_streams = config->max_response_streams;
    service->pending_free_head = 0;
    service->pending_active_head = PXA_NET_SLOT_NONE;
    service->stream_free_head = 0;
    memset(service->pending, 0,
           (size_t)service->max_pending_requests *
               sizeof(service->pending[0]));
    for (index = 0; index < service->max_pending_requests; ++index) {
        service->pending[index].next =
            index + 1u < service->max_pending_requests
                ? (uint16_t)(index + 1u)
                : PXA_NET_SLOT_NONE;
    }
    memset(service->streams, 0,
           (size_t)service->max_response_streams *
               sizeof(service->streams[0]));
    for (index = 0; index < service->max_response_streams; ++index) {
        service->streams[index].magic = PXA_NET_STREAM_MAGIC;
        service->streams[index].service = service;
        service->streams[index].next_free =
            index + 1u < service->max_response_streams
                ? (uint16_t)(index + 1u)
                : PXA_NET_SLOT_NONE;
    }
    service->magic = PXA_NET_MAGIC;
    *output = service;
    return PXA_STATUS_OK;
}

static int service_valid(const pxa_net_service_t *service) {
    return service != NULL && service->magic == PXA_NET_MAGIC;
}

static uint16_t pending_for_component(const pxa_net_service_t *service,
                                      pxa_component_t component) {
    uint16_t index = service->pending_active_head;
    uint16_t count = 0;
    while (index != PXA_NET_SLOT_NONE) {
        const pxa_net_pending_t *pending = &service->pending[index];
        if (pending->component == component) count++;
        index = pending->next;
    }
    return count;
}

static int operation_in_use(const pxa_net_service_t *service,
                            uint64_t operation) {
    uint16_t index = service->pending_active_head;
    while (index != PXA_NET_SLOT_NONE) {
        const pxa_net_pending_t *pending = &service->pending[index];
        if (pending->operation == operation) return 1;
        index = pending->next;
    }
    return 0;
}

static pxa_net_pending_t *allocate_pending(pxa_net_service_t *service) {
    uint16_t index = service->pending_free_head;
    pxa_net_pending_t *pending;
    if (index == PXA_NET_SLOT_NONE) return NULL;
    pending = &service->pending[index];
    service->pending_free_head = pending->next;
    memset(pending, 0, sizeof(*pending));
    pending->next = service->pending_active_head;
    pending->previous_active = PXA_NET_SLOT_NONE;
    if (pending->next != PXA_NET_SLOT_NONE) {
        service->pending[pending->next].previous_active = index;
    }
    service->pending_active_head = index;
    service->pending_count++;
    return pending;
}

static void release_pending(pxa_net_service_t *service,
                            pxa_net_pending_t *pending) {
    uint16_t index = (uint16_t)(pending - service->pending);
    if (pending->previous_active == PXA_NET_SLOT_NONE) {
        service->pending_active_head = pending->next;
    } else {
        service->pending[pending->previous_active].next = pending->next;
    }
    if (pending->next != PXA_NET_SLOT_NONE) {
        service->pending[pending->next].previous_active =
            pending->previous_active;
    }
    memset(pending, 0, sizeof(*pending));
    pending->next = service->pending_free_head;
    service->pending_free_head = index;
    if (service->pending_count != 0) service->pending_count--;
}

static pxa_net_stream_t *allocate_stream(pxa_net_service_t *service) {
    uint16_t index = service->stream_free_head;
    pxa_net_stream_t *stream;
    if (index == PXA_NET_SLOT_NONE) return NULL;
    stream = &service->streams[index];
    service->stream_free_head = stream->next_free;
    stream->next_free = PXA_NET_SLOT_NONE;
    stream->body_stream = NULL;
    stream->remaining = 0;
    stream->active = 1;
    return stream;
}

static void release_stream(pxa_net_stream_t *stream) {
    pxa_net_service_t *service = stream->service;
    uint16_t index = (uint16_t)(stream - service->streams);
    stream->body_stream = NULL;
    stream->remaining = 0;
    stream->active = 0;
    stream->next_free = service->stream_free_head;
    service->stream_free_head = index;
}

static void close_stream(void *context) {
    pxa_net_stream_t *stream = (pxa_net_stream_t *)context;
    if (stream == NULL || stream->magic != PXA_NET_STREAM_MAGIC ||
        !stream->active) {
        return;
    }
    stream->service->backend.close_body(stream->service->backend.context,
                                        stream->body_stream);
    release_stream(stream);
}

static int32_t stream_io(void *context, uint32_t operation, uint8_t *data,
                         size_t size) {
    pxa_net_stream_t *stream = (pxa_net_stream_t *)context;
    size_t read_size = 0;
    size_t read_capacity;
    pxa_status_t status;
    if (stream == NULL || stream->magic != PXA_NET_STREAM_MAGIC ||
        !stream->active) {
        return PXA_STATUS_NOT_FOUND;
    }
    if (operation != PXA_NET_IO_READ) return PXA_STATUS_UNSUPPORTED;
    if (stream->remaining == 0) return 0;
    read_capacity = size < stream->remaining ? size : stream->remaining;
    status = pxa_status_normalize(stream->service->backend.read_body(
        stream->service->backend.context, stream->body_stream, data,
        read_capacity, &read_size));
    if (status != PXA_STATUS_OK) return status;
    if (read_size > read_capacity || read_size > (size_t)INT32_MAX) {
        return PXA_STATUS_INTERNAL;
    }
    stream->remaining -= (uint32_t)read_size;
    return (int32_t)read_size;
}

static const pxa_resource_ops_t k_stream_resource_ops = {
    sizeof(pxa_resource_ops_t), stream_io,
};

static pxa_status_t complete_immediate(pxa_net_service_t *service,
                                       pxa_component_t component,
                                       uint32_t request_id,
                                       pxa_status_t result) {
    pxa_status_t status = pxa_request_complete(
        service->runtime, component, request_id, result, NULL, 0);
    if (status != PXA_STATUS_OK) {
        (void)pxa_request_cancel(service->runtime, component, request_id);
    }
    return PXA_STATUS_OK;
}

static pxa_status_t net_control(void *context, pxa_runtime_t *runtime,
                                pxa_component_t component,
                                const pxa_message_view_t *message) {
    static const uint8_t permission_name[] = "net.client";
    pxa_net_service_t *service = (pxa_net_service_t *)context;
    pxa_net_parsed_request_t parsed;
    pxa_net_pending_t *pending = NULL;
    pxa_authority_t authority = 0;
    uint64_t operation = 0;
    pxa_status_t status;
    pxa_status_t begin;
    (void)runtime;
    if (!service_valid(service) || message->request_id == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (message->opcode != PXA_NET_FETCH &&
        message->opcode != PXA_NET_HTTP_REQUEST) {
        return PXA_STATUS_UNSUPPORTED;
    }
    status = pxa_net_request_parse(&service->request_limits, message->opcode,
                                   message->payload, &parsed);
    if (status == PXA_STATUS_OK) {
        status = pxa_permission_resolve(
            service->permissions, component, parsed.permission_handle,
            (pxa_bytes_t){permission_name, sizeof(permission_name) - 1},
            parsed.request.origin, &authority);
    }
    begin = pxa_request_begin(service->runtime, component,
                              message->request_id, PXA_NET_SERVICE_ID,
                              message->opcode,
                              status == PXA_STATUS_OK ? authority : 0);
    if (begin != PXA_STATUS_OK) return begin;
    if (status == PXA_STATUS_OK &&
        parsed.request.max_response_bytes > service->max_response_bytes) {
        status = PXA_STATUS_INVALID_ARGUMENT;
    }
    if (status == PXA_STATUS_OK &&
        pending_for_component(service, component) >=
            service->max_requests_per_component) {
        status = PXA_STATUS_RESOURCE_LIMIT;
    }
    if (status == PXA_STATUS_OK) {
        pending = allocate_pending(service);
        if (pending == NULL) status = PXA_STATUS_RESOURCE_LIMIT;
    }
    if (status == PXA_STATUS_OK) {
        status = pxa_status_normalize(service->backend.start(
            service->backend.context, &parsed.request, &operation));
        if (status != PXA_STATUS_OK && operation != 0) {
            service->backend.cancel(service->backend.context, operation);
            operation = 0;
        }
        if (status == PXA_STATUS_OK &&
            (operation == 0 || operation_in_use(service, operation))) {
            if (operation != 0) {
                service->backend.cancel(service->backend.context, operation);
            }
            status = PXA_STATUS_INTERNAL;
        }
    }
    if (status != PXA_STATUS_OK) {
        if (pending != NULL) release_pending(service, pending);
        return complete_immediate(service, component, message->request_id,
                                  status);
    }
    pending->component = component;
    pending->permission_handle = parsed.permission_handle;
    pending->authority = authority;
    pending->operation = operation;
    pending->request_id = message->request_id;
    pending->max_response_bytes = parsed.request.max_response_bytes;
    pending->opcode = message->opcode;
    return PXA_STATUS_OK;
}

static void append_affected(pxa_component_t component,
                            pxa_component_t *affected, size_t capacity,
                            size_t *count) {
    size_t index;
    for (index = 0; index < *count && index < capacity; ++index) {
        if (affected[index] == component) return;
    }
    if (*count < capacity && affected != NULL) affected[*count] = component;
    (*count)++;
}

static void cancel_pending(pxa_net_service_t *service,
                           pxa_net_pending_t *pending) {
    service->backend.cancel(service->backend.context, pending->operation);
    release_pending(service, pending);
}

static void net_component_stopped(void *context, pxa_runtime_t *runtime,
                                  pxa_component_t component) {
    pxa_net_service_t *service = (pxa_net_service_t *)context;
    uint16_t index = service->pending_active_head;
    (void)runtime;
    while (index != PXA_NET_SLOT_NONE) {
        pxa_net_pending_t *pending = &service->pending[index];
        uint16_t next = pending->next;
        if (pending->component == component) cancel_pending(service, pending);
        index = next;
    }
}

pxa_status_t pxa_net_service_register(pxa_net_service_t *service) {
    pxa_service_ops_t operations;
    pxa_status_t status;
    if (!service_valid(service)) return PXA_STATUS_INVALID_ARGUMENT;
    if (service->registered) return PXA_STATUS_BAD_STATE;
    memset(&operations, 0, sizeof(operations));
    operations.struct_size = sizeof(operations);
    operations.service_id = PXA_NET_SERVICE_ID;
    operations.major = PXA_NET_SERVICE_MAJOR;
    operations.minor = PXA_NET_SERVICE_MINOR;
    operations.context = service;
    operations.control = net_control;
    operations.component_stopped = net_component_stopped;
    status = pxa_service_register(service->runtime, &operations);
    if (status == PXA_STATUS_OK) service->registered = 1;
    return status;
}

int pxa_net_has_pending_requests(const pxa_net_service_t *service) {
    return service_valid(service) && service->pending_count != 0;
}

pxa_status_t pxa_net_poll(
    pxa_net_service_t *service, pxa_component_t *affected, size_t capacity,
    size_t *count) {
    uint16_t index;
    if (count == NULL || !service_valid(service) ||
        (affected == NULL && capacity != 0)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *count = 0;
    index = service->pending_active_head;
    while (index != PXA_NET_SLOT_NONE) {
        pxa_net_pending_t *pending = &service->pending[index];
        uint16_t next = pending->next;
        pxa_net_response_t response;
        pxa_resource_t permission_resource;
        pxa_net_stream_t *stream = NULL;
        pxa_resource_t resource;
        pxa_handle_t handle = PXA_HANDLE_INVALID;
        size_t result_size = 0;
        pxa_status_t status;
        pxa_status_t complete;
        if (!pxa_request_is_active(service->runtime, pending->component,
                                   pending->request_id) ||
            pxa_handle_get(service->runtime, pending->component,
                           pending->permission_handle,
                           PXA_RESOURCE_PERMISSION,
                           &permission_resource) != PXA_STATUS_OK) {
            pxa_component_t component = pending->component;
            cancel_pending(service, pending);
            append_affected(component, affected, capacity, count);
            index = next;
            continue;
        }
        memset(&response, 0, sizeof(response));
        response.struct_size = sizeof(response);
        status = pxa_status_normalize(service->backend.poll(
            service->backend.context, pending->operation, &response));
        if (status == PXA_STATUS_WOULD_BLOCK) {
            index = next;
            continue;
        }
        if (status != PXA_STATUS_OK) {
            service->backend.cancel(service->backend.context,
                                    pending->operation);
        } else {
            status = pxa_net_response_validate(
                service->request_limits.max_headers,
                service->max_response_header_bytes, pending->opcode,
                pending->max_response_bytes, &response);
        }
        if (status != PXA_STATUS_OK && response.body_stream != NULL) {
            if (response.body_stream != NULL) {
                service->backend.close_body(service->backend.context,
                                            response.body_stream);
            }
        }
        if (status == PXA_STATUS_OK && response.body_stream != NULL &&
            (pending->opcode == PXA_NET_FETCH ||
             (response.flags & PXA_NET_RESPONSE_BODY_PRESENT) != 0)) {
            stream = allocate_stream(service);
            if (stream == NULL) {
                service->backend.close_body(service->backend.context,
                                            response.body_stream);
                status = PXA_STATUS_RESOURCE_LIMIT;
            } else {
                stream->body_stream = response.body_stream;
                stream->remaining =
                    (response.flags & PXA_NET_RESPONSE_BODY_LENGTH_KNOWN) != 0
                        ? (uint32_t)response.body_length
                        : pending->max_response_bytes;
                resource.context = stream;
                resource.operations = &k_stream_resource_ops;
                resource.close = close_stream;
                status = pxa_handle_open(
                    service->runtime, pending->component, PXA_RESOURCE_STREAM,
                    pending->authority, &resource, &handle);
                if (status != PXA_STATUS_OK) close_stream(stream);
            }
        }
        if (status == PXA_STATUS_OK) {
            status = pxa_net_response_encode(
                service->result_buffer, service->result_capacity,
                pending->opcode, &response, handle, &result_size);
        }
        if (pending->opcode == PXA_NET_HTTP_REQUEST &&
            (response.flags & PXA_NET_RESPONSE_BODY_PRESENT) == 0 &&
            response.body_stream != NULL) {
            service->backend.close_body(service->backend.context,
                                        response.body_stream);
            response.body_stream = NULL;
        }
        complete = pxa_request_complete(
            service->runtime, pending->component, pending->request_id, status,
            status == PXA_STATUS_OK ? service->result_buffer : NULL,
            status == PXA_STATUS_OK ? result_size : 0);
        if (complete != PXA_STATUS_OK) {
            if (handle != PXA_HANDLE_INVALID) {
                (void)pxa_handle_close(service->runtime, pending->component,
                                       handle);
            }
            (void)pxa_request_cancel(service->runtime, pending->component,
                                     pending->request_id);
        }
        append_affected(pending->component, affected, capacity, count);
        release_pending(service, pending);
        index = next;
    }
    return *count > capacity ? PXA_STATUS_RESOURCE_LIMIT : PXA_STATUS_OK;
}
