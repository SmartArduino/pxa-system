#include "ui/ui_internal.h"
#include "common/checked_math.h"

#include <limits.h>
#include <string.h>

static int service_valid(const pxa_ui_service_t *service) {
    return service != NULL && service->magic == PXA_UI_MAGIC;
}

static int config_valid(const pxa_ui_config_t *config) {
    return config != NULL && config->struct_size >= sizeof(*config) &&
           config->allocate != NULL && config->release != NULL &&
           config->max_dynamic_bytes > sizeof(pxa_ui_entry_t) &&
           config->max_transaction_bytes != 0 &&
           config->max_transaction_bytes <= config->max_dynamic_bytes &&
           config->max_canvas_bytes != 0 &&
           config->max_canvas_bytes <= config->max_dynamic_bytes &&
           config->density_q16 != 0 && config->font_scale_q16 != 0 &&
           config->color_scheme <= PXA_UI_COLOR_SCHEME_DARK;
}

size_t pxa_ui_service_workspace_size(void) {
    return sizeof(pxa_ui_service_t) + PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
}

pxa_status_t pxa_ui_service_init(void *workspace, size_t workspace_size,
                                    pxa_runtime_t *runtime,
                                    const pxa_ui_config_t *config,
                                    pxa_ui_service_t **output) {
    uintptr_t address;
    uintptr_t aligned;
    pxa_ui_service_t *service;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (workspace == NULL || runtime == NULL || !config_valid(config) ||
        workspace_size < pxa_ui_service_workspace_size())
        return PXA_STATUS_INVALID_ARGUMENT;
    address = (uintptr_t)workspace;
    aligned = pxa_internal_align_pointer(
        address, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    if (aligned == UINTPTR_MAX || aligned - address > workspace_size ||
        sizeof(*service) > workspace_size - (size_t)(aligned - address))
        return PXA_STATUS_INVALID_ARGUMENT;
    service = (pxa_ui_service_t *)aligned;
    memset(service, 0, sizeof(*service));
    service->runtime = runtime;
    service->config = *config;
    service->magic = PXA_UI_MAGIC;
    *output = service;
    return PXA_STATUS_OK;
}

static void clear_entry(pxa_ui_service_t *service,
                        pxa_ui_entry_t *entry) {
    pxa_ui_surface_t *surface;
    pxa_ui_surface_t *next;
    for (surface = entry->surfaces; surface != NULL; surface = next) {
        next = surface->next;
        if (surface == &entry->primary) continue;
        if (entry->backend.surface_close != NULL)
            entry->backend.surface_close(entry->backend.context, surface->id);
        pxa_ui_free(service, surface);
    }
    entry->primary.next = NULL;
    entry->surfaces = &entry->primary;
    entry->surface_count = 1;
    if (entry->backend.reset != NULL)
        entry->backend.reset(entry->backend.context);
    pxa_ui_transaction_reset(service, &entry->transaction);
    pxa_ui_canvas_clear(service, entry);
    pxa_ui_registry_clear(service, entry);
}

void pxa_ui_service_deinit(pxa_ui_service_t *service) {
    pxa_ui_entry_t *entry;
    pxa_ui_entry_t *next;
    if (!service_valid(service)) return;
    for (entry = service->entries; entry != NULL; entry = next) {
        next = entry->next;
        clear_entry(service, entry);
        pxa_ui_free(service, entry);
    }
    service->entries = NULL;
    service->magic = 0;
}

static pxa_status_t begin_transaction(pxa_ui_service_t *service,
                                      pxa_ui_entry_t *entry,
                                      const pxa_message_view_t *message) {
    pxa_ui_transaction_t *transaction = &entry->transaction;
    uint32_t surface;
    uint32_t id;
    uint32_t generation;
    uint32_t target;
    uint8_t kind;
    uint8_t flags;
    pxa_ui_surface_t *surface_state;
    (void)service;
    if (message->request_id != 0 || message->payload.size != 20 ||
        message->payload.data[18] != 0 || message->payload.data[19] != 0 ||
        transaction->active)
        return PXA_STATUS_BAD_STATE;
    surface = pxa_read_u32(message->payload.data);
    id = pxa_read_u32(message->payload.data + 4);
    generation = pxa_read_u32(message->payload.data + 8);
    target = pxa_read_u32(message->payload.data + 12);
    kind = message->payload.data[16];
    flags = message->payload.data[17];
    surface_state = pxa_ui_find_surface(entry, surface);
    if (surface_state == NULL || id == 0 || generation == 0 ||
        generation <= surface_state->generation ||
        kind < PXA_UI_PATCH || kind > PXA_UI_REPLACE_SURFACE ||
        flags != PXA_UI_PRESERVE_ON_FAILURE ||
        (kind == PXA_UI_REPLACE_SUBTREE && target == 0) ||
        (kind != PXA_UI_REPLACE_SUBTREE && target != 0))
        return PXA_STATUS_INVALID_ARGUMENT;
    if (kind == PXA_UI_PATCH && entry->node_count == 0)
        return PXA_STATUS_BAD_STATE;
    if (kind == PXA_UI_REPLACE_SUBTREE &&
        pxa_ui_registry_find(entry, surface, target) == NULL)
        return PXA_STATUS_NOT_FOUND;
    memset(transaction, 0, sizeof(*transaction));
    transaction->id = id;
    transaction->surface = surface;
    transaction->node = target;
    transaction->generation = generation;
    transaction->kind = kind;
    transaction->flags = flags;
    transaction->active = 1;
    return PXA_STATUS_OK;
}

static pxa_status_t write_transaction(pxa_ui_service_t *service,
                                      pxa_ui_entry_t *entry,
                                      const pxa_message_view_t *message) {
    pxa_ui_transaction_t *transaction = &entry->transaction;
    size_t fragment;
    size_t required;
    void *grown;
    if (message->request_id != 0 || message->payload.size <= 4 ||
        !transaction->active ||
        pxa_read_u32(message->payload.data) != transaction->id)
        return PXA_STATUS_BAD_STATE;
    fragment = message->payload.size - 4u;
    if (fragment > SIZE_MAX - transaction->size)
        return PXA_STATUS_RESOURCE_LIMIT;
    required = transaction->size + fragment;
    grown = pxa_ui_grow(service, transaction->bytes, transaction->size,
                           &transaction->capacity, required,
                           service->config.max_transaction_bytes);
    if (grown == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    transaction->bytes = (uint8_t *)grown;
    memcpy(transaction->bytes + transaction->size,
           message->payload.data + 4, fragment);
    transaction->size = required;
    return PXA_STATUS_OK;
}

static pxa_status_t finish_transaction(pxa_ui_service_t *service,
                                       pxa_ui_entry_t *entry,
                                       const pxa_message_view_t *message,
                                       int commit) {
    pxa_status_t status;
    if (message->request_id != 0 || message->payload.size != 4 ||
        !entry->transaction.active ||
        pxa_read_u32(message->payload.data) != entry->transaction.id)
        return PXA_STATUS_BAD_STATE;
    status = commit ? pxa_ui_transaction_commit(service, entry)
                    : PXA_STATUS_OK;
    pxa_ui_transaction_reset(service, &entry->transaction);
    return status;
}

static pxa_status_t ui_control(void *context, pxa_runtime_t *runtime,
                               pxa_component_t component,
                               const pxa_message_view_t *message) {
    pxa_ui_service_t *service = (pxa_ui_service_t *)context;
    pxa_ui_entry_t *entry = pxa_ui_find_entry(service, component);
    (void)runtime;
    if (entry == NULL) return PXA_STATUS_DENIED;
    switch (message->opcode) {
        case PXA_UI_TX_BEGIN:
            return begin_transaction(service, entry, message);
        case PXA_UI_TX_WRITE:
            return write_transaction(service, entry, message);
        case PXA_UI_TX_COMMIT:
            return finish_transaction(service, entry, message, 1);
        case PXA_UI_TX_CANCEL:
            return finish_transaction(service, entry, message, 0);
        case PXA_UI_CANVAS_BEGIN:
            return pxa_ui_canvas_begin_frame(service, entry, message);
        case PXA_UI_CANVAS_WRITE:
            return pxa_ui_canvas_write_frame(service, entry, message);
        case PXA_UI_CANVAS_PRESENT:
            return pxa_ui_canvas_present_frame(service, entry, message);
        case PXA_UI_CANVAS_STREAM_OPEN:
            return pxa_ui_canvas_open_stream(service, entry, message);
        case PXA_UI_SURFACE_OPEN:
            return pxa_ui_surface_open(service, entry, message);
        case PXA_UI_SURFACE_CLOSE:
            return pxa_ui_surface_close(service, entry, message);
        default:
            return PXA_STATUS_UNSUPPORTED;
    }
}

static void component_stopped(void *context, pxa_runtime_t *runtime,
                              pxa_component_t component) {
    (void)runtime;
    (void)pxa_ui_unbind((pxa_ui_service_t *)context, component);
}

pxa_status_t pxa_ui_service_register(pxa_ui_service_t *service) {
    pxa_service_ops_t operations;
    pxa_status_t status;
    if (!service_valid(service)) return PXA_STATUS_INVALID_ARGUMENT;
    if (service->registered) return PXA_STATUS_BAD_STATE;
    memset(&operations, 0, sizeof(operations));
    operations.struct_size = sizeof(operations);
    operations.service_id = PXA_UI_SERVICE_ID;
    operations.major = PXA_UI_SERVICE_MAJOR;
    operations.minor = PXA_UI_SERVICE_MINOR;
    operations.features = service->config.features;
    operations.context = service;
    operations.control = ui_control;
    operations.component_stopped = component_stopped;
    status = pxa_service_register(service->runtime, &operations);
    if (status == PXA_STATUS_OK) service->registered = 1;
    return status;
}

pxa_status_t pxa_ui_bind(pxa_ui_service_t *service,
                            pxa_component_t component,
                            const pxa_ui_backend_t *backend) {
    pxa_ui_entry_t *entry;
    pxa_status_t status;
    if (!service_valid(service) || component == PXA_COMPONENT_INVALID ||
        backend == NULL || backend->struct_size < sizeof(*backend) ||
        backend->begin == NULL || backend->apply == NULL ||
        backend->commit == NULL || backend->cancel == NULL ||
        ((service->config.features & PXA_UI_FEATURE_CANVAS) != 0 &&
         backend->present_canvas == NULL))
        return PXA_STATUS_INVALID_ARGUMENT;
    if (pxa_ui_find_entry(service, component) != NULL)
        return PXA_STATUS_BAD_STATE;
    entry = (pxa_ui_entry_t *)pxa_ui_alloc(service, sizeof(*entry));
    if (entry == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    memset(entry, 0, sizeof(*entry));
    entry->component = component;
    entry->backend = *backend;
    entry->primary.id = PXA_UI_PRIMARY_SURFACE;
    entry->primary.occupied = 1;
    entry->primary.environment.surface = PXA_UI_PRIMARY_SURFACE;
    entry->primary.environment.width = service->config.primary_width;
    entry->primary.environment.height = service->config.primary_height;
    entry->primary.environment.density_q16 = service->config.density_q16;
    entry->primary.environment.font_scale_q16 = service->config.font_scale_q16;
    entry->primary.environment.features = service->config.features;
    entry->primary.environment.recommended_write_bytes = 1024;
    entry->primary.environment.color_scheme = service->config.color_scheme;
    for (uint8_t index = 0; index < 4; ++index)
        entry->primary.environment.safe_insets[index] =
            service->config.safe_insets[index];
    entry->surfaces = &entry->primary;
    entry->next_surface = PXA_UI_PRIMARY_SURFACE;
    entry->surface_count = 1;
    status = pxa_ui_registry_init(service, entry);
    if (status != PXA_STATUS_OK) {
        pxa_ui_free(service, entry);
        return status;
    }
    entry->next = service->entries;
    service->entries = entry;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_ui_unbind(pxa_ui_service_t *service,
                              pxa_component_t component) {
    pxa_ui_entry_t **cursor;
    pxa_ui_entry_t *entry;
    if (!service_valid(service)) return PXA_STATUS_INVALID_ARGUMENT;
    for (cursor = &service->entries; *cursor != NULL;
         cursor = &(*cursor)->next) {
        if ((*cursor)->component != component) continue;
        entry = *cursor;
        *cursor = entry->next;
        clear_entry(service, entry);
        pxa_ui_free(service, entry);
        return PXA_STATUS_OK;
    }
    return PXA_STATUS_NOT_FOUND;
}

pxa_status_t pxa_ui_find_node(const pxa_ui_service_t *service,
                                 pxa_component_t component, uint32_t surface,
                                 uint32_t node,
                                 pxa_ui_node_snapshot_t *output) {
    const pxa_ui_entry_t *entry;
    const pxa_ui_node_t *value;
    if (!service_valid(service) || output == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    entry = pxa_ui_find_entry_const(service, component);
    value = pxa_ui_registry_find_const(entry, surface, node);
    if (value == NULL) return PXA_STATUS_NOT_FOUND;
    memset(output, 0, sizeof(*output));
    output->id = value->id;
    output->surface = value->surface;
    output->parent = value->parent;
    output->event_mask = value->event_mask;
    output->type = value->type;
    output->subtype = value->subtype;
    output->backend_handle = value->backend_handle;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_ui_memory_snapshot(
    const pxa_ui_service_t *service, pxa_component_t component,
    pxa_ui_memory_snapshot_t *output) {
    const pxa_ui_entry_t *entry;
    const pxa_ui_canvas_t *canvas;
    if (!service_valid(service) || output == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    entry = pxa_ui_find_entry_const(service, component);
    if (entry == NULL) return PXA_STATUS_NOT_FOUND;
    memset(output, 0, sizeof(*output));
    output->current_bytes = service->current_bytes;
    output->peak_bytes = service->peak_bytes;
    output->transaction_bytes = entry->transaction.capacity;
    output->node_count = entry->node_count;
    output->surface_count = entry->surface_count;
    output->commit_count = entry->commit_count;
    output->last_commit_us = entry->last_commit_us;
    output->max_commit_us = entry->max_commit_us;
    for (canvas = entry->canvases; canvas != NULL; canvas = canvas->next) {
        ++output->canvas_count;
        output->canvas_bytes += canvas->capacity + canvas->spare_capacity;
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_ui_set_pressure(pxa_ui_service_t *service,
                                    pxa_component_t component,
                                    pxa_ui_pressure_t pressure) {
    pxa_ui_entry_t *entry;
    pxa_status_t status;
    uint8_t payload[4] = {0, 0, 0, 0};
    if (!service_valid(service) || pressure > PXA_UI_PRESSURE_CRITICAL)
        return PXA_STATUS_INVALID_ARGUMENT;
    entry = pxa_ui_find_entry(service, component);
    if (entry == NULL) return PXA_STATUS_NOT_FOUND;
    if (entry->pressure == pressure) return PXA_STATUS_OK;
    payload[0] = pressure;
    status = pxa_event_post_message(
        service->runtime, component, PXA_UI_SERVICE_ID,
        PXA_UI_RESOURCE_PRESSURE, 0,
        (pxa_bytes_t){payload, sizeof(payload)}, 0,
        (UINT64_C(3) << 48) | component);
    if (status == PXA_STATUS_OK) entry->pressure = pressure;
    return status;
}

pxa_status_t pxa_ui_queue_event(pxa_ui_service_t *service,
                                   pxa_component_t component,
                                   uint32_t surface, uint32_t node,
                                   uint16_t kind, uint16_t flags,
                                   uint64_t timestamp_us,
                                   const void *value, size_t value_size) {
    pxa_ui_entry_t *entry;
    const pxa_ui_node_t *target;
    uint8_t *payload;
    size_t payload_size;
    pxa_status_t status;
    uint8_t reliable;
    uint64_t key;
    if (!service_valid(service) || kind < PXA_UI_EVENT_ACTION ||
        kind > PXA_UI_EVENT_CONTROLLER_STATE ||
        (value == NULL && value_size != 0) ||
        value_size > PXA_MAX_CONTROL_MESSAGE - PXA_ENVELOPE_SIZE - 24u)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (kind == PXA_UI_EVENT_CONTROLLER_STATE &&
        ((service->config.features & PXA_UI_FEATURE_CONTROLLER_INPUT) == 0 ||
         value_size != 8 || ((const uint8_t *)value)[1] > 1 ||
         ((const uint8_t *)value)[2] != 0 ||
         ((const uint8_t *)value)[3] != 0 ||
         (pxa_read_u32((const uint8_t *)value + 4) &
          ~PXA_UI_CONTROLLER_BUTTON_MASK) != 0 ||
         (((const uint8_t *)value)[1] == 0 &&
          pxa_read_u32((const uint8_t *)value + 4) != 0)))
        return PXA_STATUS_INVALID_ARGUMENT;
    entry = pxa_ui_find_entry(service, component);
    target = pxa_ui_registry_find_const(entry, surface, node);
    if (target == NULL || (target->event_mask & (UINT64_C(1) << (kind - 1u))) == 0)
        return PXA_STATUS_DENIED;
    payload_size = 24u + value_size;
    payload = (uint8_t *)pxa_ui_alloc(service, payload_size);
    if (payload == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    pxa_write_u32(payload, surface);
    pxa_write_u32(payload + 4, node);
    {
        const pxa_ui_surface_t *surface_state =
            pxa_ui_find_surface_const(entry, surface);
        if (surface_state == NULL) {
            pxa_ui_free(service, payload);
            return PXA_STATUS_NOT_FOUND;
        }
        pxa_write_u32(payload + 8, surface_state->generation);
    }
    pxa_write_u16(payload + 12, kind);
    pxa_write_u16(payload + 14, flags);
    pxa_write_u64(payload + 16, timestamp_us);
    if (value_size != 0) memcpy(payload + 24, value, value_size);
    reliable = (flags & PXA_UI_EVENT_FLAG_RELIABLE) != 0;
    key = reliable ? 0 : ((uint64_t)kind << 48) | node;
    /* Coalescible events keep one pending instance per source. Pointer events
     * must key on the pointer id too, otherwise one finger's move overwrites
     * another finger's move aimed at the same node. */
    if (!reliable && (kind == PXA_UI_EVENT_CONTROLLER_STATE ||
                      kind == PXA_UI_EVENT_POINTER))
        key |= (uint64_t)((const uint8_t *)value)[0] << 32;
    status = pxa_event_post_message(
        service->runtime, component, PXA_UI_SERVICE_ID, PXA_UI_EVENT, 0,
        (pxa_bytes_t){payload, payload_size}, reliable, key);
    pxa_ui_free(service, payload);
    return status;
}

bool pxa_ui_accepts_event(pxa_ui_service_t *service,
                          pxa_component_t component,
                          uint32_t surface, uint32_t node,
                          uint16_t kind) {
    const pxa_ui_entry_t *entry;
    const pxa_ui_node_t *target;
    if (!service_valid(service) || kind < PXA_UI_EVENT_ACTION ||
        kind > PXA_UI_EVENT_CONTROLLER_STATE) {
        return false;
    }
    if (kind == PXA_UI_EVENT_CONTROLLER_STATE &&
        (service->config.features & PXA_UI_FEATURE_CONTROLLER_INPUT) == 0)
        return false;
    entry = pxa_ui_find_entry_const(service, component);
    target = pxa_ui_registry_find_const(entry, surface, node);
    return target != NULL &&
           (target->event_mask & (UINT64_C(1) << (kind - 1u))) != 0;
}
