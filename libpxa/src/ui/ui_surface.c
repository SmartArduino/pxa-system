#include "ui/ui_internal.h"

#include <string.h>

static pxa_status_t encode_environment(pxa_writer_t *writer,
                                       const pxa_ui_environment_t *value) {
    uint8_t u32[4];
    uint8_t u64[8];
    uint8_t insets[16];
    uint8_t index;
#define RECORD_U32(tag, field)                                                 \
    do {                                                                       \
        pxa_write_u32(u32, value->field);                                      \
        if (pxa_writer_record(writer, tag, u32, sizeof(u32)) != PXA_STATUS_OK) \
            return writer->status;                                             \
    } while (0)
    RECORD_U32(1, surface);
    RECORD_U32(2, width);
    RECORD_U32(3, height);
    RECORD_U32(4, density_q16);
    RECORD_U32(5, font_scale_q16);
    for (index = 0; index < 4; ++index)
        pxa_write_u32(insets + (size_t)index * 4u, value->safe_insets[index]);
    if (pxa_writer_record(writer, 6, insets, sizeof(insets)) != PXA_STATUS_OK)
        return writer->status;
    if (pxa_writer_record(writer, 7, &value->color_scheme, 1) != PXA_STATUS_OK ||
        pxa_writer_record(writer, 8, &value->direction, 1) != PXA_STATUS_OK)
        return writer->status;
    pxa_write_u64(u64, value->input_capabilities);
    if (pxa_writer_record(writer, 9, u64, sizeof(u64)) != PXA_STATUS_OK)
        return writer->status;
    pxa_write_u64(u64, value->features);
    if (pxa_writer_record(writer, 10, u64, sizeof(u64)) != PXA_STATUS_OK)
        return writer->status;
    RECORD_U32(11, recommended_write_bytes);
#undef RECORD_U32
    return PXA_STATUS_OK;
}

pxa_status_t pxa_ui_encode_environment(
    const pxa_ui_environment_t *environment, void *buffer, size_t capacity,
    size_t *encoded_size) {
    pxa_writer_t writer;
    pxa_status_t status;
    if (environment == NULL || buffer == NULL || encoded_size == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    pxa_writer_init(&writer, buffer, capacity);
    status = encode_environment(&writer, environment);
    if (status != PXA_STATUS_OK) return status;
    *encoded_size = writer.size;
    return PXA_STATUS_OK;
}

static pxa_status_t post_environment_event(
    pxa_ui_service_t *service, pxa_ui_entry_t *entry,
    uint16_t opcode, uint32_t request, pxa_status_t result,
    const pxa_ui_environment_t *environment) {
    uint8_t payload[PXA_UI_SURFACE_READY_PAYLOAD_BYTES];
    pxa_writer_t payload_writer;
    size_t payload_size;
    if (opcode == PXA_UI_SURFACE_READY) {
        pxa_write_u32(payload, request);
        pxa_write_u32(payload + 4,
                      result == PXA_STATUS_OK ? environment->surface : 0);
        pxa_write_u32(payload + 8, (uint32_t)result);
        pxa_writer_init(
            &payload_writer, payload + PXA_UI_SURFACE_READY_PREFIX_BYTES,
            sizeof(payload) - PXA_UI_SURFACE_READY_PREFIX_BYTES);
    } else {
        pxa_writer_init(&payload_writer, payload, sizeof(payload));
    }
    if (result == PXA_STATUS_OK &&
        encode_environment(&payload_writer, environment) != PXA_STATUS_OK)
        return PXA_STATUS_INTERNAL;
    if (result == PXA_STATUS_OK &&
        payload_writer.size != PXA_UI_ENVIRONMENT_WIRE_BYTES)
        return PXA_STATUS_INTERNAL;
    payload_size = payload_writer.size +
                   (opcode == PXA_UI_SURFACE_READY
                        ? PXA_UI_SURFACE_READY_PREFIX_BYTES
                        : 0u);
    return pxa_event_post_message(
        service->runtime, entry->component, PXA_UI_SERVICE_ID, opcode, 0,
        (pxa_bytes_t){payload, payload_size}, 1, 0);
}

static uint32_t allocate_surface_id(pxa_ui_entry_t *entry) {
    size_t attempt;
    uint32_t candidate = entry->next_surface;
    for (attempt = 0; attempt <= entry->surface_count; ++attempt) {
        ++candidate;
        if (candidate == 0) candidate = PXA_UI_PRIMARY_SURFACE;
        if (pxa_ui_find_surface(entry, candidate) == NULL) {
            entry->next_surface = candidate;
            return candidate;
        }
    }
    return 0;
}

pxa_status_t pxa_ui_surface_open(pxa_ui_service_t *service,
                                    pxa_ui_entry_t *entry,
                                    const pxa_message_view_t *message) {
    pxa_ui_surface_t *surface;
    pxa_ui_environment_t environment;
    uint32_t request;
    uint8_t role;
    pxa_status_t status;
    if (message->request_id != 0 || message->payload.size != 8 ||
        message->payload.data[5] != 0 || message->payload.data[6] != 0 ||
        message->payload.data[7] != 0)
        return PXA_STATUS_INVALID_ARGUMENT;
    if ((service->config.features & PXA_UI_FEATURE_MULTIPLE_SURFACES) == 0 ||
        entry->backend.surface_open == NULL ||
        entry->backend.surface_close == NULL)
        return PXA_STATUS_UNSUPPORTED;
    request = pxa_read_u32(message->payload.data);
    role = message->payload.data[4];
    if (request == 0 || role < PXA_UI_SURFACE_APPLICATION ||
        role > PXA_UI_SURFACE_EXTERNAL)
        return PXA_STATUS_INVALID_ARGUMENT;
    environment = entry->primary.environment;
    environment.surface = allocate_surface_id(entry);
    if (environment.surface == 0) return PXA_STATUS_RESOURCE_LIMIT;
    surface = (pxa_ui_surface_t *)pxa_ui_alloc(service, sizeof(*surface));
    if (surface == NULL) {
        status = PXA_STATUS_RESOURCE_LIMIT;
        return post_environment_event(service, entry, PXA_UI_SURFACE_READY,
                                      request, status, &environment);
    }
    memset(surface, 0, sizeof(*surface));
    surface->id = environment.surface;
    status = entry->backend.surface_open(entry->backend.context, role,
                                         &environment);
    environment.surface = surface->id;
    if (status != PXA_STATUS_OK) {
        pxa_ui_free(service, surface);
        return post_environment_event(service, entry, PXA_UI_SURFACE_READY,
                                      request, status, &environment);
    }
    surface->environment = environment;
    surface->occupied = 1;
    surface->next = entry->surfaces;
    entry->surfaces = surface;
    ++entry->surface_count;
    status = post_environment_event(service, entry, PXA_UI_SURFACE_READY,
                                    request, PXA_STATUS_OK, &environment);
    if (status != PXA_STATUS_OK) {
        entry->surfaces = surface->next;
        --entry->surface_count;
        entry->backend.surface_close(entry->backend.context, surface->id);
        pxa_ui_free(service, surface);
    }
    return status;
}

pxa_status_t pxa_ui_surface_close(pxa_ui_service_t *service,
                                     pxa_ui_entry_t *entry,
                                     const pxa_message_view_t *message) {
    pxa_ui_surface_t **cursor;
    pxa_ui_surface_t *surface;
    pxa_ui_node_chunk_t *chunk;
    uint32_t id;
    uint32_t index;
    if (message->request_id != 0 || message->payload.size != 4)
        return PXA_STATUS_INVALID_ARGUMENT;
    id = pxa_read_u32(message->payload.data);
    if (id == PXA_UI_PRIMARY_SURFACE) return PXA_STATUS_DENIED;
    if (entry->transaction.active && entry->transaction.surface == id)
        return PXA_STATUS_BUSY;
    for (cursor = &entry->surfaces; *cursor != NULL;
         cursor = &(*cursor)->next) {
        if ((*cursor)->id != id) continue;
        surface = *cursor;
        entry->backend.surface_close(entry->backend.context, id);
        for (chunk = entry->chunks; chunk != NULL; chunk = chunk->next) {
            for (index = 0; index < PXA_UI_NODE_CHUNK_COUNT; ++index) {
                pxa_ui_node_t *node = &chunk->nodes[index];
                if (node->occupied && node->surface == id && node->parent == 0) {
                    (void)pxa_ui_registry_remove_subtree(service, entry, id,
                                                            node->id);
                    break;
                }
            }
        }
        pxa_ui_canvas_clear_surface(service, entry, id);
        *cursor = surface->next;
        --entry->surface_count;
        pxa_ui_free(service, surface);
        return PXA_STATUS_OK;
    }
    return PXA_STATUS_NOT_FOUND;
}

pxa_status_t pxa_ui_get_environment(
    const pxa_ui_service_t *service, pxa_component_t component,
    uint32_t surface, pxa_ui_environment_t *output) {
    const pxa_ui_entry_t *entry;
    const pxa_ui_surface_t *value;
    if (service == NULL || service->magic != PXA_UI_MAGIC || output == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    entry = pxa_ui_find_entry_const(service, component);
    value = pxa_ui_find_surface_const(entry, surface);
    if (value == NULL) return PXA_STATUS_NOT_FOUND;
    *output = value->environment;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_ui_update_environment(
    pxa_ui_service_t *service, pxa_component_t component,
    const pxa_ui_environment_t *environment) {
    pxa_ui_entry_t *entry;
    pxa_ui_surface_t *surface;
    pxa_status_t status;
    if (service == NULL || service->magic != PXA_UI_MAGIC ||
        environment == NULL || environment->surface == 0 ||
        environment->density_q16 == 0 || environment->font_scale_q16 == 0)
        return PXA_STATUS_INVALID_ARGUMENT;
    entry = pxa_ui_find_entry(service, component);
    surface = pxa_ui_find_surface(entry, environment->surface);
    if (surface == NULL) return PXA_STATUS_NOT_FOUND;
    if (entry->backend.environment_changed != NULL) {
        status = entry->backend.environment_changed(entry->backend.context,
                                                    environment);
        if (status != PXA_STATUS_OK) return status;
    }
    surface->environment = *environment;
    surface->environment.features &= service->config.features;
    return post_environment_event(service, entry,
                                  PXA_UI_ENVIRONMENT_CHANGED, 0,
                                  PXA_STATUS_OK, &surface->environment);
}
