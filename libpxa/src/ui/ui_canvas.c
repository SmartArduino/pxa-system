#include "ui/ui_internal.h"

#include <string.h>

static pxa_ui_canvas_t *find_canvas(pxa_ui_entry_t *entry,
                                       uint32_t surface, uint32_t node) {
    pxa_ui_canvas_t *canvas;
    for (canvas = entry->canvases; canvas != NULL; canvas = canvas->next)
        if (canvas->surface == surface && canvas->node == node) return canvas;
    return NULL;
}

typedef struct {
    pxa_ui_service_t *service;
    pxa_component_t component;
    uint32_t surface;
    uint32_t node;
    uint64_t canvas_identity;
} pxa_ui_canvas_stream_t;

static pxa_status_t append_canvas_bytes(pxa_ui_service_t *service,
                                        pxa_ui_canvas_t *canvas,
                                        const uint8_t *data, size_t size) {
    size_t required;
    void *grown;
    if (size > SIZE_MAX - canvas->size) return PXA_STATUS_RESOURCE_LIMIT;
    required = canvas->size + size;
    grown = pxa_ui_grow(service, canvas->bytes, canvas->size,
                        &canvas->capacity, required,
                        service->config.max_canvas_bytes);
    if (grown == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    canvas->bytes = (uint8_t *)grown;
    memcpy(canvas->bytes + canvas->size, data, size);
    canvas->size = required;
    return PXA_STATUS_OK;
}

static int32_t canvas_stream_io(void *context, uint32_t operation,
                                uint8_t *data, size_t size) {
    pxa_ui_canvas_stream_t *stream = (pxa_ui_canvas_stream_t *)context;
    pxa_ui_entry_t *entry;
    pxa_ui_canvas_t *canvas;
    pxa_status_t status;
    if (operation != PXA_IO_WRITE || data == NULL || size == 0)
        return PXA_STATUS_INVALID_ARGUMENT;
    entry = pxa_ui_find_entry(stream->service, stream->component);
    if (entry == NULL) return PXA_STATUS_NOT_FOUND;
    canvas = find_canvas(entry, stream->surface, stream->node);
    if (canvas == NULL || canvas->identity != stream->canvas_identity)
        return PXA_STATUS_NOT_FOUND;
    if (!canvas->staging) return PXA_STATUS_BAD_STATE;
    status = append_canvas_bytes(stream->service, canvas, data, size);
    return status == PXA_STATUS_OK ? (int32_t)size : (int32_t)status;
}

static void close_canvas_stream(void *context) {
    pxa_ui_canvas_stream_t *stream = (pxa_ui_canvas_stream_t *)context;
    pxa_ui_free(stream->service, stream);
}

pxa_status_t pxa_ui_canvas_open_stream(
    pxa_ui_service_t *service, pxa_ui_entry_t *entry,
    const pxa_message_view_t *message) {
    static const pxa_resource_ops_t operations = {
        sizeof(pxa_resource_ops_t), canvas_stream_io
    };
    pxa_ui_canvas_stream_t *stream;
    pxa_resource_t resource;
    pxa_handle_t handle = PXA_HANDLE_INVALID;
    uint8_t payload[12];
    uint32_t request;
    uint32_t surface;
    uint32_t node;
    pxa_status_t status;
    if ((service->config.features & PXA_UI_FEATURE_CANVAS_STREAM_IO) == 0)
        return PXA_STATUS_UNSUPPORTED;
    if (message->request_id != 0 || message->payload.size != 12)
        return PXA_STATUS_INVALID_ARGUMENT;
    request = pxa_read_u32(message->payload.data);
    surface = pxa_read_u32(message->payload.data + 4);
    node = pxa_read_u32(message->payload.data + 8);
    if (request == 0 || find_canvas(entry, surface, node) == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    stream = (pxa_ui_canvas_stream_t *)pxa_ui_alloc(service, sizeof(*stream));
    if (stream == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    stream->service = service;
    stream->component = entry->component;
    stream->surface = surface;
    stream->node = node;
    stream->canvas_identity = find_canvas(entry, surface, node)->identity;
    memset(&resource, 0, sizeof(resource));
    resource.context = stream;
    resource.operations = &operations;
    resource.close = close_canvas_stream;
    status = pxa_handle_open(service->runtime, entry->component,
                             PXA_RESOURCE_STREAM, 0, &resource, &handle);
    if (status != PXA_STATUS_OK) {
        pxa_ui_free(service, stream);
        return status;
    }
    pxa_write_u32(payload, request);
    pxa_write_u32(payload + 4, handle);
    pxa_write_u32(payload + 8, PXA_STATUS_OK);
    status = pxa_event_post_message(
        service->runtime, entry->component, PXA_UI_SERVICE_ID,
        PXA_UI_CANVAS_STREAM_READY, 0,
        (pxa_bytes_t){payload, sizeof(payload)}, 1, 0);
    if (status != PXA_STATUS_OK)
        (void)pxa_handle_close(service->runtime, entry->component, handle);
    return status;
}

static void release_canvas_bytes(void *context, void *memory) {
    pxa_ui_canvas_t *canvas = (pxa_ui_canvas_t *)context;
    pxa_ui_alloc_header_t *header = (pxa_ui_alloc_header_t *)memory - 1;
    --canvas->outstanding_frames;
    if (canvas->detached) {
        pxa_ui_free(canvas->service, memory);
        if (canvas->outstanding_frames == 0)
            pxa_ui_free(canvas->service, canvas);
        return;
    }
    /* The backend returns the previous frame only after it stops drawing it. */
    pxa_ui_free(canvas->service, canvas->spare_bytes);
    canvas->spare_bytes = (uint8_t *)memory;
    canvas->spare_capacity = header->size - sizeof(*header);
}

static void destroy_canvas(pxa_ui_service_t *service, pxa_ui_canvas_t *canvas) {
    pxa_ui_free(service, canvas->bytes);
    pxa_ui_free(service, canvas->spare_bytes);
    canvas->bytes = NULL;
    canvas->spare_bytes = NULL;
    canvas->detached = 1;
    if (canvas->outstanding_frames == 0) pxa_ui_free(service, canvas);
}

pxa_status_t pxa_ui_canvas_begin_frame(pxa_ui_service_t *service,
                                          pxa_ui_entry_t *entry,
                                          const pxa_message_view_t *message) {
    pxa_ui_canvas_t *canvas;
    const pxa_ui_node_t *node;
    uint32_t surface;
    uint32_t node_id;
    uint32_t frame;
    if (message->request_id != 0 || message->payload.size != 16 ||
        pxa_read_u32(message->payload.data + 12) != 0)
        return PXA_STATUS_INVALID_ARGUMENT;
    surface = pxa_read_u32(message->payload.data);
    node_id = pxa_read_u32(message->payload.data + 4);
    frame = pxa_read_u32(message->payload.data + 8);
    node = pxa_ui_registry_find_const(entry, surface, node_id);
    if (frame == 0 || node == NULL || node->type != PXA_UI_NODE_CANVAS)
        return PXA_STATUS_INVALID_ARGUMENT;
    canvas = find_canvas(entry, surface, node_id);
    if (canvas == NULL) {
        canvas = (pxa_ui_canvas_t *)pxa_ui_alloc(service,
                                                        sizeof(*canvas));
        if (canvas == NULL) return PXA_STATUS_RESOURCE_LIMIT;
        memset(canvas, 0, sizeof(*canvas));
        canvas->service = service;
        canvas->surface = surface;
        canvas->node = node_id;
        ++service->next_canvas_identity;
        if (service->next_canvas_identity == 0) ++service->next_canvas_identity;
        canvas->identity = service->next_canvas_identity;
        canvas->next = entry->canvases;
        entry->canvases = canvas;
    }
    if (frame <= canvas->generation)
        return PXA_STATUS_BAD_STATE;
    canvas->generation = frame;
    canvas->staging = 1;
    canvas->size = 0;
    if (canvas->bytes == NULL && canvas->spare_bytes != NULL) {
        canvas->bytes = canvas->spare_bytes;
        canvas->capacity = canvas->spare_capacity;
        canvas->spare_bytes = NULL;
        canvas->spare_capacity = 0;
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_ui_canvas_write_frame(pxa_ui_service_t *service,
                                          pxa_ui_entry_t *entry,
                                          const pxa_message_view_t *message) {
    pxa_ui_canvas_t *canvas;
    uint32_t node;
    uint32_t frame;
    size_t fragment;
    if (message->request_id != 0 || message->payload.size <= 12)
        return PXA_STATUS_INVALID_ARGUMENT;
    node = pxa_read_u32(message->payload.data + 4);
    frame = pxa_read_u32(message->payload.data + 8);
    canvas = find_canvas(entry, pxa_read_u32(message->payload.data), node);
    if (canvas == NULL || !canvas->staging || canvas->generation != frame)
        return PXA_STATUS_BAD_STATE;
    fragment = message->payload.size - 12u;
    return append_canvas_bytes(service, canvas,
                               message->payload.data + 12, fragment);
}

pxa_status_t pxa_ui_canvas_present_frame(
    pxa_ui_service_t *service, pxa_ui_entry_t *entry,
    const pxa_message_view_t *message) {
    pxa_ui_canvas_t *canvas;
    pxa_ui_canvas_view_t view;
    uint8_t dirty_count;
    uint8_t index;
    pxa_status_t status;
    if (message->request_id != 0 || message->payload.size < 13)
        return PXA_STATUS_INVALID_ARGUMENT;
    memset(&view, 0, sizeof(view));
    view.surface = pxa_read_u32(message->payload.data);
    view.node = pxa_read_u32(message->payload.data + 4);
    view.frame = pxa_read_u32(message->payload.data + 8);
    dirty_count = message->payload.data[12];
    if (dirty_count > PXA_UI_MAX_DIRTY_RECTS ||
        message->payload.size != 13u + (size_t)dirty_count * 16u)
        return PXA_STATUS_INVALID_ARGUMENT;
    canvas = find_canvas(entry, view.surface, view.node);
    if (canvas == NULL || !canvas->staging ||
        canvas->generation != view.frame)
        return PXA_STATUS_BAD_STATE;
    status = pxa_ui_validate_canvas(service->config.features,
                                       canvas->bytes, canvas->size);
    if (status != PXA_STATUS_OK) return status;
    view.dirty_count = dirty_count;
    view.display_list.data = canvas->bytes;
    view.display_list.size = canvas->size;
    {
        const pxa_ui_node_t *node = pxa_ui_registry_find_const(
            entry, view.surface, view.node);
        if (node == NULL || node->type != PXA_UI_NODE_CANVAS)
            return PXA_STATUS_NOT_FOUND;
        view.node_handle = node->backend_handle;
    }
    for (index = 0; index < dirty_count; ++index) {
        const uint8_t *rect = message->payload.data + 13u + index * 16u;
        view.dirty_rects[index][0] = (int32_t)pxa_read_u32(rect);
        view.dirty_rects[index][1] = (int32_t)pxa_read_u32(rect + 4);
        view.dirty_rects[index][2] = (int32_t)pxa_read_u32(rect + 8);
        view.dirty_rects[index][3] = (int32_t)pxa_read_u32(rect + 12);
        if (view.dirty_rects[index][2] <= 0 ||
            view.dirty_rects[index][3] <= 0)
            return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (canvas->outstanding_frames == SIZE_MAX) return PXA_STATUS_RESOURCE_LIMIT;
    ++canvas->outstanding_frames;
    status = entry->backend.present_canvas(
        entry->backend.context, &view, release_canvas_bytes, canvas);
    if (status != PXA_STATUS_OK) {
        --canvas->outstanding_frames;
        return status;
    }
    canvas->bytes = NULL;
    canvas->size = 0;
    canvas->capacity = 0;
    canvas->staging = 0;
    return PXA_STATUS_OK;
}

void pxa_ui_canvas_clear(pxa_ui_service_t *service,
                            pxa_ui_entry_t *entry) {
    pxa_ui_canvas_t *canvas;
    pxa_ui_canvas_t *next;
    for (canvas = entry->canvases; canvas != NULL; canvas = next) {
        next = canvas->next;
        destroy_canvas(service, canvas);
    }
    entry->canvases = NULL;
}

void pxa_ui_canvas_prune(pxa_ui_service_t *service,
                            pxa_ui_entry_t *entry) {
    pxa_ui_canvas_t **cursor;
    if (service == NULL || entry == NULL) return;
    for (cursor = &entry->canvases; *cursor != NULL;) {
        pxa_ui_canvas_t *canvas = *cursor;
        if (pxa_ui_registry_find_const(entry, canvas->surface,
                                       canvas->node) != NULL) {
            cursor = &canvas->next;
            continue;
        }
        *cursor = canvas->next;
        destroy_canvas(service, canvas);
    }
}

void pxa_ui_canvas_clear_surface(pxa_ui_service_t *service,
                                    pxa_ui_entry_t *entry,
                                    uint32_t surface) {
    pxa_ui_canvas_t **cursor;
    for (cursor = &entry->canvases; *cursor != NULL;) {
        pxa_ui_canvas_t *canvas = *cursor;
        if (canvas->surface != surface) {
            cursor = &canvas->next;
            continue;
        }
        *cursor = canvas->next;
        destroy_canvas(service, canvas);
    }
}
