#include "pxa/surface.h"
#include "pxa/service.h"

#include "common/checked_math.h"
#include "common/status_internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#define PXA_SURFACE_MAGIC UINT32_C(0x50584753)
#define PXA_SURFACE_SLOT_MAGIC UINT32_C(0x50584746)
#define PXA_SURFACE_SLOT_NONE UINT16_MAX

typedef struct pxa_surface_resource pxa_surface_resource_t;

struct pxa_surface_service {
    uint32_t magic;
    pxa_runtime_t *runtime;
    pxa_surface_backend_t backend;
    pxa_surface_resource_t *surfaces;
    uint16_t max_surfaces;
    uint16_t max_surfaces_per_component;
    uint16_t max_width;
    uint16_t max_height;
    uint32_t max_frame_bytes;
    uint16_t free_head;
    uint16_t active_head;
    uint8_t min_buffer_count;
    uint8_t max_buffer_count;
    uint8_t registered;
};

struct pxa_surface_resource {
    uint32_t magic;
    pxa_surface_service_t *service;
    pxa_component_t component;
    pxa_handle_t handle;
    uint64_t provider_surface;
    uint32_t stride_bytes;
    uint32_t frame_bytes;
    pxa_surface_desc_t desc;
    uint16_t next;
    uint16_t previous;
};

static int config_valid(const pxa_surface_config_t *config) {
    return config != NULL && config->struct_size >= sizeof(*config) &&
           config->max_surfaces != 0 &&
           config->max_surfaces_per_component != 0 &&
           config->max_surfaces_per_component <= config->max_surfaces &&
           config->max_width != 0 && config->max_height != 0 &&
           config->max_frame_bytes != 0 && config->min_buffer_count != 0 &&
           config->min_buffer_count <= config->max_buffer_count &&
           config->backend.struct_size >= sizeof(config->backend) &&
           config->backend.create != NULL && config->backend.write != NULL &&
           config->backend.queue != NULL &&
           config->backend.configure != NULL &&
           config->backend.query != NULL && config->backend.close != NULL;
}

size_t pxa_surface_service_workspace_size(
    const pxa_surface_config_t *config) {
    size_t slots_size;
    size_t size = PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    if (!config_valid(config) ||
        sizeof(pxa_surface_resource_t) >
            SIZE_MAX / (size_t)config->max_surfaces)
        return 0;
    slots_size = (size_t)config->max_surfaces *
                 sizeof(pxa_surface_resource_t);
    if (sizeof(pxa_surface_service_t) > SIZE_MAX - size) return 0;
    size += sizeof(pxa_surface_service_t);
    if (slots_size > SIZE_MAX - size) return 0;
    return size + slots_size;
}

pxa_status_t pxa_surface_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_surface_config_t *config, pxa_surface_service_t **output) {
    size_t required;
    uintptr_t cursor;
    uintptr_t end;
    uint16_t index;
    pxa_surface_service_t *service;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    required = pxa_surface_service_workspace_size(config);
    if (workspace == NULL || runtime == NULL || required == 0 ||
        workspace_size < required ||
        (uintptr_t)workspace > UINTPTR_MAX - workspace_size)
        return PXA_STATUS_INVALID_ARGUMENT;
    end = (uintptr_t)workspace + workspace_size;
    cursor = pxa_internal_align_pointer((uintptr_t)workspace,
                                        PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    service = (pxa_surface_service_t *)cursor;
    memset(service, 0, sizeof(*service));
    cursor += sizeof(*service);
    service->surfaces = (pxa_surface_resource_t *)cursor;
    cursor += (size_t)config->max_surfaces * sizeof(service->surfaces[0]);
    if (cursor > end) return PXA_STATUS_INVALID_ARGUMENT;
    service->runtime = runtime;
    service->backend = config->backend;
    service->max_surfaces = config->max_surfaces;
    service->max_surfaces_per_component =
        config->max_surfaces_per_component;
    service->max_width = config->max_width;
    service->max_height = config->max_height;
    service->max_frame_bytes = config->max_frame_bytes;
    service->min_buffer_count = config->min_buffer_count;
    service->max_buffer_count = config->max_buffer_count;
    service->free_head = 0;
    service->active_head = PXA_SURFACE_SLOT_NONE;
    memset(service->surfaces, 0,
           (size_t)service->max_surfaces * sizeof(service->surfaces[0]));
    for (index = 0; index < service->max_surfaces; ++index) {
        service->surfaces[index].magic = PXA_SURFACE_SLOT_MAGIC;
        service->surfaces[index].service = service;
        service->surfaces[index].next =
            index + 1u < service->max_surfaces
                ? (uint16_t)(index + 1u)
                : PXA_SURFACE_SLOT_NONE;
        service->surfaces[index].previous = PXA_SURFACE_SLOT_NONE;
    }
    service->magic = PXA_SURFACE_MAGIC;
    *output = service;
    return PXA_STATUS_OK;
}

static int service_valid(const pxa_surface_service_t *service) {
    return service != NULL && service->magic == PXA_SURFACE_MAGIC;
}

static uint16_t surface_index(const pxa_surface_resource_t *surface) {
    return (uint16_t)(surface - surface->service->surfaces);
}

static pxa_surface_resource_t *allocate_surface(
    pxa_surface_service_t *service, pxa_component_t component) {
    pxa_surface_resource_t *surface;
    uint16_t cursor;
    uint16_t count = 0;
    for (cursor = service->active_head;
         cursor != PXA_SURFACE_SLOT_NONE;
         cursor = service->surfaces[cursor].next) {
        if (service->surfaces[cursor].component == component) ++count;
    }
    if (count >= service->max_surfaces_per_component ||
        service->free_head == PXA_SURFACE_SLOT_NONE)
        return NULL;
    cursor = service->free_head;
    surface = &service->surfaces[cursor];
    service->free_head = surface->next;
    surface->previous = PXA_SURFACE_SLOT_NONE;
    surface->next = service->active_head;
    if (service->active_head != PXA_SURFACE_SLOT_NONE)
        service->surfaces[service->active_head].previous = cursor;
    service->active_head = cursor;
    surface->component = component;
    return surface;
}

static void release_surface(pxa_surface_resource_t *surface) {
    pxa_surface_service_t *service = surface->service;
    uint16_t index = surface_index(surface);
    if (surface->previous != PXA_SURFACE_SLOT_NONE)
        service->surfaces[surface->previous].next = surface->next;
    else
        service->active_head = surface->next;
    if (surface->next != PXA_SURFACE_SLOT_NONE)
        service->surfaces[surface->next].previous = surface->previous;
    surface->component = PXA_COMPONENT_INVALID;
    surface->handle = PXA_HANDLE_INVALID;
    surface->provider_surface = 0;
    surface->stride_bytes = 0;
    surface->frame_bytes = 0;
    memset(&surface->desc, 0, sizeof(surface->desc));
    surface->previous = PXA_SURFACE_SLOT_NONE;
    surface->next = service->free_head;
    service->free_head = index;
}

static int surface_valid(const pxa_surface_resource_t *surface) {
    return surface != NULL && surface->magic == PXA_SURFACE_SLOT_MAGIC &&
           service_valid(surface->service) &&
           surface->component != PXA_COMPONENT_INVALID &&
           surface->provider_surface != 0;
}

static uint8_t surface_bytes_per_pixel(
    const pxa_surface_desc_t *desc) {
    if ((desc->flags & ~PXA_SURFACE_FLAG_KNOWN_MASK) != 0)
        return 0;
    if (desc->format == PXA_SURFACE_FORMAT_RGB565 &&
        (desc->flags & PXA_SURFACE_FLAG_PREMULTIPLIED_ALPHA) == 0 &&
        (desc->flags & ~PXA_SURFACE_FLAG_KNOWN_MASK) == 0)
        return 2;
    if (desc->format == PXA_SURFACE_FORMAT_ARGB8888_PREMULTIPLIED &&
        (desc->flags & PXA_SURFACE_FLAG_PREMULTIPLIED_ALPHA) != 0 &&
        (desc->flags & (PXA_SURFACE_FLAG_PREFER_DIRECT_SCANOUT |
                        PXA_SURFACE_FLAG_GUEST_MAPPED)) == 0)
        return 4;
    return 0;
}

static int32_t surface_io(void *context, uint32_t operation, uint8_t *data,
                          size_t size) {
    pxa_surface_resource_t *surface = (pxa_surface_resource_t *)context;
    pxa_status_t status;
    if (!surface_valid(surface)) return PXA_STATUS_BAD_STATE;
    if (data == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    if (operation == PXA_IO_WRITE) {
        if (size != surface->frame_bytes ||
            (surface->desc.flags & PXA_SURFACE_FLAG_GUEST_MAPPED) != 0)
            return PXA_STATUS_INVALID_ARGUMENT;
        if (size > (size_t)INT32_MAX) return PXA_STATUS_LIMIT_EXCEEDED;
        status = pxa_status_normalize(surface->service->backend.write(
            surface->service->backend.context, surface->provider_surface, data,
            size));
    } else if (operation == PXA_SURFACE_IO_REGISTER_BUFFERS) {
        uint64_t required =
            (uint64_t)surface->frame_bytes * surface->desc.buffer_count;
        if ((surface->desc.flags & PXA_SURFACE_FLAG_GUEST_MAPPED) == 0 ||
            surface->service->backend.register_buffers == NULL)
            return PXA_STATUS_UNSUPPORTED;
        if (required > SIZE_MAX || size != (size_t)required)
            return PXA_STATUS_INVALID_ARGUMENT;
        status = pxa_status_normalize(
            surface->service->backend.register_buffers(
                surface->service->backend.context,
                surface->provider_surface, data, size));
    } else if (operation == PXA_SURFACE_IO_ACQUIRE) {
        if ((surface->desc.flags & PXA_SURFACE_FLAG_GUEST_MAPPED) == 0 ||
            surface->service->backend.acquire_buffer == NULL)
            return PXA_STATUS_UNSUPPORTED;
        if (size != PXA_SURFACE_ACQUIRE_RECORD_BYTES)
            return PXA_STATUS_INVALID_ARGUMENT;
        status = pxa_status_normalize(
            surface->service->backend.acquire_buffer(
                surface->service->backend.context,
                surface->provider_surface, data));
        if (status == PXA_STATUS_OK) data[1] = data[2] = data[3] = 0;
    } else if (operation == PXA_SURFACE_IO_PRESENT) {
        uint64_t frame_id;
        if ((surface->desc.flags & PXA_SURFACE_FLAG_GUEST_MAPPED) == 0 ||
            surface->service->backend.present_buffer == NULL)
            return PXA_STATUS_UNSUPPORTED;
        if (size != PXA_SURFACE_PRESENT_RECORD_BYTES || data[1] != 0 ||
            data[2] != 0 || data[3] != 0 || data[4] != 0 || data[5] != 0 ||
            data[6] != 0 || data[7] != 0)
            return PXA_STATUS_INVALID_ARGUMENT;
        frame_id = pxa_read_u64(data + 8);
        if (frame_id == 0) return PXA_STATUS_INVALID_ARGUMENT;
        status = pxa_status_normalize(
            surface->service->backend.present_buffer(
                surface->service->backend.context,
                surface->provider_surface, data[0], frame_id));
    } else {
        return PXA_STATUS_UNSUPPORTED;
    }
    return status == PXA_STATUS_OK ? (int32_t)size : (int32_t)status;
}

static const pxa_resource_ops_t k_surface_resource_ops = {
    sizeof(pxa_resource_ops_t), surface_io};

static void close_surface(void *context) {
    pxa_surface_resource_t *surface = (pxa_surface_resource_t *)context;
    if (!surface_valid(surface)) return;
    surface->service->backend.close(surface->service->backend.context,
                                    surface->provider_surface);
    release_surface(surface);
}

static pxa_status_t resolve_surface(pxa_surface_service_t *service,
                                    pxa_component_t component,
                                    pxa_handle_t handle,
                                    pxa_surface_resource_t **output) {
    pxa_resource_t resource;
    pxa_status_t status;
    *output = NULL;
    status = pxa_handle_get(service->runtime, component, handle,
                            PXA_RESOURCE_SURFACE, &resource);
    if (status != PXA_STATUS_OK) return status;
    *output = (pxa_surface_resource_t *)resource.context;
    if (!surface_valid(*output) || (*output)->service != service ||
        (*output)->component != component || (*output)->handle != handle) {
        *output = NULL;
        return PXA_STATUS_INTERNAL;
    }
    return PXA_STATUS_OK;
}

static pxa_status_t create_surface(pxa_surface_service_t *service,
                                   pxa_component_t component,
                                   pxa_bytes_t payload, uint8_t result[16],
                                   size_t *result_size,
                                   pxa_handle_t *opened_handle) {
    pxa_surface_desc_t desc;
    pxa_surface_resource_t *surface;
    pxa_resource_t resource;
    uint64_t provider_surface = 0;
    uint64_t frame_bytes;
    uint32_t stride = 0;
    uint8_t bytes_per_pixel;
    pxa_status_t status;
    if (payload.size != 8) return PXA_STATUS_INVALID_ARGUMENT;
    desc.width = pxa_read_u16(payload.data);
    desc.height = pxa_read_u16(payload.data + 2);
    desc.format = pxa_read_u16(payload.data + 4);
    desc.buffer_count = payload.data[6];
    desc.flags = payload.data[7];
    bytes_per_pixel = surface_bytes_per_pixel(&desc);
    if (desc.width == 0 || desc.width > service->max_width ||
        desc.height == 0 || desc.height > service->max_height ||
        bytes_per_pixel == 0 ||
        desc.buffer_count < service->min_buffer_count ||
        desc.buffer_count > service->max_buffer_count)
        return PXA_STATUS_UNSUPPORTED;
    frame_bytes = (uint64_t)desc.width * desc.height * bytes_per_pixel;
    if (frame_bytes > service->max_frame_bytes || frame_bytes > UINT32_MAX)
        return PXA_STATUS_LIMIT_EXCEEDED;
    surface = allocate_surface(service, component);
    if (surface == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    status = pxa_status_normalize(service->backend.create(
        service->backend.context, &desc, &provider_surface, &stride));
    if (status != PXA_STATUS_OK) {
        release_surface(surface);
        return status;
    }
    frame_bytes = (uint64_t)stride * desc.height;
    if (provider_surface == 0 ||
        stride < (uint32_t)desc.width * bytes_per_pixel ||
        frame_bytes > service->max_frame_bytes || frame_bytes > UINT32_MAX) {
        if (provider_surface != 0)
            service->backend.close(service->backend.context,
                                   provider_surface);
        release_surface(surface);
        return PXA_STATUS_INTERNAL;
    }
    surface->provider_surface = provider_surface;
    surface->stride_bytes = stride;
    surface->frame_bytes = (uint32_t)frame_bytes;
    surface->desc = desc;
    memset(&resource, 0, sizeof(resource));
    resource.context = surface;
    resource.operations = &k_surface_resource_ops;
    resource.close = close_surface;
    status = pxa_handle_open(service->runtime, component,
                             PXA_RESOURCE_SURFACE, 0, &resource,
                             opened_handle);
    if (status != PXA_STATUS_OK) {
        close_surface(surface);
        return status;
    }
    surface->handle = *opened_handle;
    pxa_write_u32(result, *opened_handle);
    pxa_write_u32(result + 4, stride);
    pxa_write_u32(result + 8, surface->frame_bytes);
    result[12] = desc.buffer_count;
    result[13] = result[14] = result[15] = 0;
    *result_size = 16;
    return PXA_STATUS_OK;
}

static pxa_status_t configure_layer(pxa_surface_service_t *service,
                                    pxa_component_t component,
                                    pxa_bytes_t payload) {
    pxa_surface_resource_t *surface;
    pxa_surface_layer_t layer;
    pxa_handle_t handle;
    if (payload.size != 20) return PXA_STATUS_INVALID_ARGUMENT;
    handle = pxa_read_u32(payload.data);
    layer.x = (int32_t)pxa_read_u32(payload.data + 4);
    layer.y = (int32_t)pxa_read_u32(payload.data + 8);
    layer.width = pxa_read_u16(payload.data + 12);
    layer.height = pxa_read_u16(payload.data + 14);
    layer.z = (int16_t)pxa_read_u16(payload.data + 16);
    layer.visible = payload.data[18];
    layer.reserved = payload.data[19];
    if (layer.reserved != 0 || layer.visible > 1 ||
        layer.width == 0 || layer.height == 0)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (resolve_surface(service, component, handle, &surface) != PXA_STATUS_OK)
        return PXA_STATUS_NOT_FOUND;
    if (layer.width != surface->desc.width ||
        layer.height != surface->desc.height)
        return PXA_STATUS_UNSUPPORTED;
    return pxa_status_normalize(service->backend.configure(
        service->backend.context, surface->provider_surface, &layer));
}

static pxa_status_t queue_frame(pxa_surface_service_t *service,
                                pxa_component_t component,
                                const pxa_message_view_t *message) {
    pxa_surface_damage_rect_t damage[PXA_SURFACE_MAX_DAMAGE_RECTS];
    pxa_surface_resource_t *surface;
    pxa_handle_t handle;
    uint64_t frame_id;
    uint8_t count;
    uint8_t index;
    if (message->request_id != 0 || message->payload.size < 16)
        return PXA_STATUS_INVALID_ARGUMENT;
    handle = pxa_read_u32(message->payload.data);
    frame_id = pxa_read_u64(message->payload.data + 4);
    count = message->payload.data[12];
    if (frame_id == 0 || count > PXA_SURFACE_MAX_DAMAGE_RECTS ||
        message->payload.data[13] != 0 || message->payload.data[14] != 0 ||
        message->payload.data[15] != 0 ||
        message->payload.size != 16u + (size_t)count * 8u)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (resolve_surface(service, component, handle, &surface) != PXA_STATUS_OK)
        return PXA_STATUS_NOT_FOUND;
    if ((surface->desc.flags & PXA_SURFACE_FLAG_GUEST_MAPPED) != 0)
        return PXA_STATUS_BAD_STATE;
    for (index = 0; index < count; ++index) {
        const uint8_t *record = message->payload.data + 16u + index * 8u;
        uint32_t right;
        uint32_t bottom;
        damage[index].x = pxa_read_u16(record);
        damage[index].y = pxa_read_u16(record + 2);
        damage[index].width = pxa_read_u16(record + 4);
        damage[index].height = pxa_read_u16(record + 6);
        right = (uint32_t)damage[index].x + damage[index].width;
        bottom = (uint32_t)damage[index].y + damage[index].height;
        if (damage[index].width == 0 || damage[index].height == 0 ||
            right > surface->desc.width || bottom > surface->desc.height)
            return PXA_STATUS_INVALID_ARGUMENT;
    }
    return pxa_status_normalize(service->backend.queue(
        service->backend.context, surface->provider_surface, frame_id,
        damage, count));
}

static pxa_status_t configure_opaque_ui_regions(pxa_surface_service_t *service,
                                         pxa_component_t component,
                                         pxa_bytes_t payload) {
    pxa_surface_damage_rect_t regions[PXA_SURFACE_MAX_OPAQUE_UI_REGIONS];
    pxa_surface_resource_t *surface;
    pxa_handle_t handle;
    uint8_t count;
    uint8_t index;
    if (payload.size < 8 || service->backend.configure_opaque_ui_regions == NULL)
        return PXA_STATUS_UNSUPPORTED;
    handle = pxa_read_u32(payload.data);
    count = payload.data[4];
    if (count > PXA_SURFACE_MAX_OPAQUE_UI_REGIONS ||
        payload.data[5] != 0 || payload.data[6] != 0 || payload.data[7] != 0 ||
        payload.size != 8u + (size_t)count * 8u)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (resolve_surface(service, component, handle, &surface) != PXA_STATUS_OK)
        return PXA_STATUS_NOT_FOUND;
    for (index = 0; index < count; ++index) {
        const uint8_t *record = payload.data + 8u + (size_t)index * 8u;
        uint32_t right;
        uint32_t bottom;
        regions[index].x = pxa_read_u16(record);
        regions[index].y = pxa_read_u16(record + 2);
        regions[index].width = pxa_read_u16(record + 4);
        regions[index].height = pxa_read_u16(record + 6);
        right = (uint32_t)regions[index].x + regions[index].width;
        bottom = (uint32_t)regions[index].y + regions[index].height;
        if (regions[index].width == 0 || regions[index].height == 0 ||
            right > surface->desc.width || bottom > surface->desc.height)
            return PXA_STATUS_INVALID_ARGUMENT;
    }
    return pxa_status_normalize(service->backend.configure_opaque_ui_regions(
        service->backend.context, surface->provider_surface, regions, count));
}

static pxa_status_t query_state(pxa_surface_service_t *service,
                                pxa_component_t component,
                                pxa_bytes_t payload, uint8_t result[48],
                                size_t *result_size) {
    pxa_surface_resource_t *surface;
    pxa_surface_state_t state;
    pxa_status_t status;
    if (payload.size != 4) return PXA_STATUS_INVALID_ARGUMENT;
    status = resolve_surface(service, component,
                             pxa_read_u32(payload.data), &surface);
    if (status != PXA_STATUS_OK) return status;
    memset(&state, 0, sizeof(state));
    status = pxa_status_normalize(service->backend.query(
        service->backend.context, surface->provider_surface, &state));
    if (status != PXA_STATUS_OK) return status;
    pxa_write_u64(result, state.submitted_frames);
    pxa_write_u64(result + 8, state.presented_frames);
    pxa_write_u64(result + 16, state.dropped_frames);
    pxa_write_u64(result + 24, state.replaced_frames);
    pxa_write_u64(result + 32, state.released_frames);
    pxa_write_u32(result + 40, state.free_buffers);
    pxa_write_u32(result + 44, state.flags);
    *result_size = 48;
    return PXA_STATUS_OK;
}

static pxa_status_t surface_control(void *context, pxa_runtime_t *runtime,
                                     pxa_component_t component,
                                     const pxa_message_view_t *message) {
    pxa_surface_service_t *service = (pxa_surface_service_t *)context;
    uint8_t result[48] = {0};
    size_t result_size = 0;
    pxa_handle_t opened_handle = PXA_HANDLE_INVALID;
    pxa_status_t status;
    pxa_status_t complete;
    (void)runtime;
    if (!service_valid(service)) return PXA_STATUS_INVALID_ARGUMENT;
    if (message->opcode == PXA_SURFACE_QUEUE_FRAME)
        return queue_frame(service, component, message);
    if (message->request_id == 0) return PXA_STATUS_INVALID_ARGUMENT;
    if (message->opcode != PXA_SURFACE_CREATE &&
        message->opcode != PXA_SURFACE_CONFIGURE_LAYER &&
        message->opcode != PXA_SURFACE_CONFIGURE_OPAQUE_UI_REGIONS &&
        message->opcode != PXA_SURFACE_QUERY_STATE)
        return PXA_STATUS_UNSUPPORTED;
    status = pxa_request_begin(service->runtime, component,
                               message->request_id,
                               PXA_SURFACE_SERVICE_ID, message->opcode, 0);
    if (status != PXA_STATUS_OK) return status;
    if (message->opcode == PXA_SURFACE_CREATE)
        status = create_surface(service, component, message->payload, result,
                                &result_size, &opened_handle);
    else if (message->opcode == PXA_SURFACE_CONFIGURE_LAYER)
        status = configure_layer(service, component, message->payload);
    else if (message->opcode == PXA_SURFACE_CONFIGURE_OPAQUE_UI_REGIONS)
        status = configure_opaque_ui_regions(service, component, message->payload);
    else
        status = query_state(service, component, message->payload, result,
                             &result_size);
    complete = pxa_request_complete(
        service->runtime, component, message->request_id, status,
        status == PXA_STATUS_OK && result_size != 0 ? result : NULL,
        status == PXA_STATUS_OK ? result_size : 0);
    if (complete != PXA_STATUS_OK) {
        if (opened_handle != PXA_HANDLE_INVALID)
            (void)pxa_handle_close(service->runtime, component,
                                   opened_handle);
        (void)pxa_request_cancel(service->runtime, component,
                                 message->request_id);
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_surface_service_register(pxa_surface_service_t *service) {
    pxa_service_ops_t operations;
    pxa_status_t status;
    if (!service_valid(service)) return PXA_STATUS_INVALID_ARGUMENT;
    if (service->registered) return PXA_STATUS_BAD_STATE;
    memset(&operations, 0, sizeof(operations));
    operations.struct_size = sizeof(operations);
    operations.service_id = PXA_SURFACE_SERVICE_ID;
    operations.major = PXA_SURFACE_SERVICE_MAJOR;
    operations.minor = PXA_SURFACE_SERVICE_MINOR;
    operations.context = service;
    operations.control = surface_control;
    status = pxa_service_register(service->runtime, &operations);
    if (status == PXA_STATUS_OK) service->registered = 1;
    return status;
}

int pxa_surface_has_active_surfaces(
    const pxa_surface_service_t *service) {
    return service_valid(service) &&
           service->active_head != PXA_SURFACE_SLOT_NONE;
}

int32_t pxa_surface_service_flush_releases(pxa_surface_service_t *service) {
    int32_t posted = 0;
    uint16_t cursor;
    if (!service_valid(service)) return PXA_STATUS_INVALID_ARGUMENT;
    if (service->backend.peek_release == NULL ||
        service->backend.consume_release == NULL)
        return 0;
    for (cursor = service->active_head; cursor != PXA_SURFACE_SLOT_NONE;
         cursor = service->surfaces[cursor].next) {
        pxa_surface_resource_t *surface = &service->surfaces[cursor];
        for (;;) {
            pxa_surface_release_t release;
            uint8_t payload[PXA_SURFACE_RELEASED_PAYLOAD_BYTES] = {0};
            pxa_status_t status = pxa_status_normalize(
                service->backend.peek_release(
                    service->backend.context, surface->provider_surface,
                    &release));
            if (status == PXA_STATUS_WOULD_BLOCK) break;
            if (status != PXA_STATUS_OK) return status;
            pxa_write_u32(payload, surface->handle);
            payload[4] = release.buffer_index;
            pxa_write_u64(payload + 8, release.frame_id);
            status = pxa_event_post_message(
                service->runtime, surface->component,
                PXA_SURFACE_SERVICE_ID, PXA_SURFACE_RELEASED, 0,
                (pxa_bytes_t){payload, sizeof(payload)}, 1, 0);
            if (status != PXA_STATUS_OK) return status;
            service->backend.consume_release(
                service->backend.context, surface->provider_surface);
            ++posted;
        }
    }
    return posted;
}
