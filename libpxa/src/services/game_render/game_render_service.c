#include "pxa/game_render.h"
#include "pxa/service.h"

#include "common/checked_math.h"
#include "common/status_internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#define PXA_GAME_RENDER_MAGIC UINT32_C(0x50584752)
#define PXA_GAME_RENDER_SLOT_MAGIC UINT32_C(0x50584743)
#define PXA_GAME_RENDER_SLOT_NONE UINT16_MAX

typedef struct pxa_game_render_resource pxa_game_render_resource_t;

struct pxa_game_render_service {
    uint32_t magic;
    pxa_runtime_t *runtime;
    pxa_game_render_backend_t backend;
    pxa_game_render_resource_t *contexts;
    uint16_t max_contexts;
    uint16_t max_contexts_per_component;
    uint16_t max_width;
    uint16_t max_height;
    uint16_t free_head;
    uint16_t active_head;
    uint8_t min_buffer_count;
    uint8_t max_buffer_count;
    uint8_t registered;
};

struct pxa_game_render_resource {
    uint32_t magic;
    pxa_game_render_service_t *service;
    pxa_component_t component;
    pxa_handle_t handle;
    uint64_t provider_context;
    uint16_t next;
    uint16_t previous;
};

static int config_valid(const pxa_game_render_config_t *config) {
    return config != NULL && config->struct_size >= sizeof(*config) &&
           config->max_contexts != 0 &&
           config->max_contexts_per_component != 0 &&
           config->max_contexts_per_component <= config->max_contexts &&
           config->max_width != 0 && config->max_height != 0 &&
           config->min_buffer_count >= 2 &&
           config->min_buffer_count <= config->max_buffer_count &&
           config->backend.struct_size >= sizeof(config->backend) &&
           config->backend.create != NULL && config->backend.upload != NULL &&
           config->backend.submit != NULL && config->backend.query != NULL &&
           config->backend.close != NULL;
}

size_t pxa_game_render_service_workspace_size(
    const pxa_game_render_config_t *config) {
    size_t slots_size;
    size_t size = PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    if (!config_valid(config) ||
        sizeof(pxa_game_render_resource_t) >
            SIZE_MAX / (size_t)config->max_contexts)
        return 0;
    slots_size = (size_t)config->max_contexts *
                 sizeof(pxa_game_render_resource_t);
    if (sizeof(pxa_game_render_service_t) > SIZE_MAX - size) return 0;
    size += sizeof(pxa_game_render_service_t);
    if (slots_size > SIZE_MAX - size) return 0;
    return size + slots_size;
}

pxa_status_t pxa_game_render_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_game_render_config_t *config,
    pxa_game_render_service_t **output) {
    size_t required;
    uintptr_t cursor;
    uintptr_t end;
    uint16_t index;
    pxa_game_render_service_t *service;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    required = pxa_game_render_service_workspace_size(config);
    if (workspace == NULL || runtime == NULL || required == 0 ||
        workspace_size < required ||
        (uintptr_t)workspace > UINTPTR_MAX - workspace_size)
        return PXA_STATUS_INVALID_ARGUMENT;
    end = (uintptr_t)workspace + workspace_size;
    cursor = pxa_internal_align_pointer((uintptr_t)workspace,
                                        PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    service = (pxa_game_render_service_t *)cursor;
    memset(service, 0, sizeof(*service));
    cursor += sizeof(*service);
    service->contexts = (pxa_game_render_resource_t *)cursor;
    cursor += (size_t)config->max_contexts * sizeof(service->contexts[0]);
    if (cursor > end) return PXA_STATUS_INVALID_ARGUMENT;
    service->runtime = runtime;
    service->backend = config->backend;
    service->max_contexts = config->max_contexts;
    service->max_contexts_per_component = config->max_contexts_per_component;
    service->max_width = config->max_width;
    service->max_height = config->max_height;
    service->min_buffer_count = config->min_buffer_count;
    service->max_buffer_count = config->max_buffer_count;
    service->free_head = 0;
    service->active_head = PXA_GAME_RENDER_SLOT_NONE;
    for (index = 0; index < service->max_contexts; ++index) {
        service->contexts[index].magic = PXA_GAME_RENDER_SLOT_MAGIC;
        service->contexts[index].service = service;
        service->contexts[index].next =
            index + 1u < service->max_contexts
                ? (uint16_t)(index + 1u)
                : PXA_GAME_RENDER_SLOT_NONE;
        service->contexts[index].previous = PXA_GAME_RENDER_SLOT_NONE;
    }
    service->magic = PXA_GAME_RENDER_MAGIC;
    *output = service;
    return PXA_STATUS_OK;
}

static int service_valid(const pxa_game_render_service_t *service) {
    return service != NULL && service->magic == PXA_GAME_RENDER_MAGIC;
}

static pxa_game_render_resource_t *allocate_context(
    pxa_game_render_service_t *service, pxa_component_t component) {
    pxa_game_render_resource_t *resource;
    uint16_t cursor;
    uint16_t count = 0;
    for (cursor = service->active_head; cursor != PXA_GAME_RENDER_SLOT_NONE;
         cursor = service->contexts[cursor].next) {
        if (service->contexts[cursor].component == component) ++count;
    }
    if (count >= service->max_contexts_per_component ||
        service->free_head == PXA_GAME_RENDER_SLOT_NONE)
        return NULL;
    cursor = service->free_head;
    resource = &service->contexts[cursor];
    service->free_head = resource->next;
    resource->previous = PXA_GAME_RENDER_SLOT_NONE;
    resource->next = service->active_head;
    if (service->active_head != PXA_GAME_RENDER_SLOT_NONE)
        service->contexts[service->active_head].previous = cursor;
    service->active_head = cursor;
    resource->component = component;
    return resource;
}

static void release_context(pxa_game_render_resource_t *resource) {
    pxa_game_render_service_t *service = resource->service;
    uint16_t index = (uint16_t)(resource - service->contexts);
    if (resource->previous != PXA_GAME_RENDER_SLOT_NONE)
        service->contexts[resource->previous].next = resource->next;
    else
        service->active_head = resource->next;
    if (resource->next != PXA_GAME_RENDER_SLOT_NONE)
        service->contexts[resource->next].previous = resource->previous;
    resource->component = PXA_COMPONENT_INVALID;
    resource->handle = PXA_HANDLE_INVALID;
    resource->provider_context = 0;
    resource->previous = PXA_GAME_RENDER_SLOT_NONE;
    resource->next = service->free_head;
    service->free_head = index;
}

static int context_valid(const pxa_game_render_resource_t *resource) {
    return resource != NULL && resource->magic == PXA_GAME_RENDER_SLOT_MAGIC &&
           service_valid(resource->service) &&
           resource->component != PXA_COMPONENT_INVALID &&
           resource->provider_context != 0;
}

static void close_context(void *context) {
    pxa_game_render_resource_t *resource = context;
    if (!context_valid(resource)) return;
    resource->service->backend.close(resource->service->backend.context,
                                     resource->provider_context);
    release_context(resource);
}

static int32_t context_io(void *context, uint32_t operation, uint8_t *data,
                          size_t size) {
    pxa_game_render_resource_t *resource = context;
    pxa_status_t status;
    if (!context_valid(resource)) return PXA_STATUS_BAD_STATE;
    if (data == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    if (operation == PXA_GAME_RENDER_IO_UPLOAD) {
        status = pxa_status_normalize(resource->service->backend.upload(
            resource->service->backend.context, resource->provider_context,
            data, size));
    } else if (operation == PXA_GAME_RENDER_IO_SUBMIT) {
        status = pxa_status_normalize(resource->service->backend.submit(
            resource->service->backend.context, resource->provider_context,
            data, size));
    } else if (operation == PXA_GAME_RENDER_IO_TELEMETRY) {
        pxa_raster_telemetry_t telemetry;
        if (size != PXA_GAME_RENDER_TELEMETRY_BYTES)
            return PXA_STATUS_INVALID_ARGUMENT;
        memset(&telemetry, 0, sizeof(telemetry));
        status = pxa_status_normalize(resource->service->backend.query(
            resource->service->backend.context, resource->provider_context,
            &telemetry));
        if (status == PXA_STATUS_OK) {
            pxa_write_u64(data, telemetry.submitted_frames);
            pxa_write_u64(data + 8, telemetry.draw_list_bytes);
            pxa_write_u64(data + 16, telemetry.covered_pixels);
            pxa_write_u64(data + 24, telemetry.host_raster_us);
            pxa_write_u64(data + 32, telemetry.queue_wait_us);
            pxa_write_u64(data + 40, telemetry.present_us);
            pxa_write_u64(data + 48, telemetry.dropped_frames);
            pxa_write_u32(data + 56, telemetry.clear_commands);
            pxa_write_u32(data + 60, telemetry.flat_quad_commands);
            pxa_write_u32(data + 64, telemetry.textured_quad_commands);
            pxa_write_u32(data + 68, telemetry.sprite_commands);
            pxa_write_u32(data + 72, telemetry.rejected_lists);
            pxa_write_u32(data + 76, telemetry.last_draw_list_bytes);
            pxa_write_u32(data + 80, telemetry.last_covered_pixels);
            pxa_write_u32(data + 84, telemetry.last_host_raster_us);
        }
    } else {
        return PXA_STATUS_UNSUPPORTED;
    }
    if (status != PXA_STATUS_OK) return status;
    return size <= (size_t)INT32_MAX ? (int32_t)size
                                    : PXA_STATUS_LIMIT_EXCEEDED;
}

static const pxa_resource_ops_t k_resource_ops = {
    sizeof(pxa_resource_ops_t), context_io};

static pxa_status_t create_context(pxa_game_render_service_t *service,
                                   pxa_component_t component,
                                   pxa_bytes_t payload, uint8_t result[16],
                                   size_t *result_size,
                                   pxa_handle_t *opened_handle) {
    pxa_game_render_desc_t desc;
    pxa_game_render_resource_t *context;
    pxa_resource_t handle_resource;
    uint64_t provider_context = 0;
    uint32_t capabilities = 0;
    pxa_status_t status;
    if (payload.size != 8) return PXA_STATUS_INVALID_ARGUMENT;
    desc.width = pxa_read_u16(payload.data);
    desc.height = pxa_read_u16(payload.data + 2);
    desc.buffer_count = payload.data[4];
    desc.flags = payload.data[5];
    if (payload.data[6] != 0 || payload.data[7] != 0 || desc.width == 0 ||
        desc.width > service->max_width || desc.height == 0 ||
        desc.height > service->max_height ||
        desc.buffer_count < service->min_buffer_count ||
        desc.buffer_count > service->max_buffer_count ||
        (desc.flags & ~PXA_GAME_RENDER_FLAG_KNOWN_MASK) != 0)
        return PXA_STATUS_UNSUPPORTED;
    context = allocate_context(service, component);
    if (context == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    status = pxa_status_normalize(service->backend.create(
        service->backend.context, &desc, &provider_context, &capabilities));
    if (status != PXA_STATUS_OK) {
        release_context(context);
        return status;
    }
    if (provider_context == 0 ||
        (capabilities & ~PXA_RASTER_CAP_KNOWN_MASK) != 0) {
        if (provider_context != 0)
            service->backend.close(service->backend.context, provider_context);
        release_context(context);
        return PXA_STATUS_INTERNAL;
    }
    context->provider_context = provider_context;
    memset(&handle_resource, 0, sizeof(handle_resource));
    handle_resource.context = context;
    handle_resource.operations = &k_resource_ops;
    handle_resource.close = close_context;
    status = pxa_handle_open(service->runtime, component,
                             PXA_RESOURCE_GAME_RENDER_CONTEXT, 0,
                             &handle_resource, opened_handle);
    if (status != PXA_STATUS_OK) {
        close_context(context);
        return status;
    }
    context->handle = *opened_handle;
    pxa_write_u32(result, *opened_handle);
    pxa_write_u32(result + 4, capabilities);
    pxa_write_u32(result + 8, PXA_RASTER_MAX_DRAW_BYTES);
    pxa_write_u16(result + 12, PXA_RASTER_MAX_TEXTURE_DIMENSION);
    result[14] = PXA_RASTER_MAX_TEXTURES;
    result[15] = 0;
    *result_size = 16;
    return PXA_STATUS_OK;
}

static pxa_status_t game_render_control(
    void *context, pxa_runtime_t *runtime, pxa_component_t component,
    const pxa_message_view_t *message) {
    pxa_game_render_service_t *service = context;
    uint8_t result[16] = {0};
    size_t result_size = 0;
    pxa_handle_t opened_handle = PXA_HANDLE_INVALID;
    pxa_status_t status;
    pxa_status_t complete;
    (void)runtime;
    if (!service_valid(service)) return PXA_STATUS_INVALID_ARGUMENT;
    if (message->request_id == 0) return PXA_STATUS_INVALID_ARGUMENT;
    if (message->opcode != PXA_GAME_RENDER_CREATE_CONTEXT)
        return PXA_STATUS_UNSUPPORTED;
    status = pxa_request_begin(service->runtime, component,
                               message->request_id,
                               PXA_GAME_RENDER_SERVICE_ID, message->opcode, 0);
    if (status != PXA_STATUS_OK) return status;
    status = create_context(service, component, message->payload, result,
                            &result_size, &opened_handle);
    complete = pxa_request_complete(
        service->runtime, component, message->request_id, status,
        status == PXA_STATUS_OK ? result : NULL,
        status == PXA_STATUS_OK ? result_size : 0);
    if (complete != PXA_STATUS_OK) {
        if (opened_handle != PXA_HANDLE_INVALID)
            (void)pxa_handle_close(service->runtime, component, opened_handle);
        (void)pxa_request_cancel(service->runtime, component,
                                 message->request_id);
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_game_render_service_register(
    pxa_game_render_service_t *service) {
    pxa_service_ops_t operations;
    pxa_status_t status;
    if (!service_valid(service)) return PXA_STATUS_INVALID_ARGUMENT;
    if (service->registered) return PXA_STATUS_BAD_STATE;
    memset(&operations, 0, sizeof(operations));
    operations.struct_size = sizeof(operations);
    operations.service_id = PXA_GAME_RENDER_SERVICE_ID;
    operations.major = PXA_GAME_RENDER_SERVICE_MAJOR;
    operations.minor = PXA_GAME_RENDER_SERVICE_MINOR;
    operations.context = service;
    operations.control = game_render_control;
    status = pxa_service_register(service->runtime, &operations);
    if (status == PXA_STATUS_OK) service->registered = 1;
    return status;
}
