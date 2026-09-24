#include "pxa/window.h"
#include "common/checked_math.h"
#include "common/bytes_internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#define PXA_WINDOW_MAGIC UINT32_C(0x5058574e)
#define PXA_WINDOW_SLOT_NONE UINT16_MAX
#define PXA_WINDOW_METRICS_KEY \
    ((((uint64_t)PXA_WINDOW_SERVICE_ID) << 32) | PXA_WINDOW_METRICS_CHANGED)

typedef struct {
    pxa_component_t component;
    pxa_window_backend_t backend;
    pxa_window_configuration_t configuration;
    pxa_window_snapshot_t snapshot;
    uint16_t next;
    uint8_t has_snapshot;
    uint8_t metrics_dirty;
} pxa_window_entry_t;

struct pxa_window_service {
    uint32_t magic;
    pxa_runtime_t *runtime;
    pxa_window_entry_t *entries;
    uint16_t free_head;
    uint16_t active_head;
};

static int service_valid(const pxa_window_service_t *service) {
    return service != NULL && service->magic == PXA_WINDOW_MAGIC;
}

static uint16_t find_entry_index(const pxa_window_service_t *service,
                                 pxa_component_t component,
                                 uint16_t *previous_output) {
    uint16_t index;
    uint16_t previous = PXA_WINDOW_SLOT_NONE;
    if (previous_output != NULL) *previous_output = PXA_WINDOW_SLOT_NONE;
    if (!service_valid(service) || component == PXA_COMPONENT_INVALID) {
        return PXA_WINDOW_SLOT_NONE;
    }
    index = service->active_head;
    while (index != PXA_WINDOW_SLOT_NONE) {
        const pxa_window_entry_t *entry = &service->entries[index];
        if (entry->component == component) {
            if (previous_output != NULL) *previous_output = previous;
            return index;
        }
        previous = index;
        index = entry->next;
    }
    return PXA_WINDOW_SLOT_NONE;
}

static pxa_window_entry_t *find_entry(pxa_window_service_t *service,
                                      pxa_component_t component) {
    uint16_t index = find_entry_index(service, component, NULL);
    return index == PXA_WINDOW_SLOT_NONE ? NULL : &service->entries[index];
}

static const pxa_window_entry_t *find_entry_const(
    const pxa_window_service_t *service, pxa_component_t component) {
    uint16_t index = find_entry_index(service, component, NULL);
    return index == PXA_WINDOW_SLOT_NONE ? NULL : &service->entries[index];
}

size_t pxa_window_service_workspace_size(uint16_t max_windows) {
    size_t size = PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    if (max_windows == 0) return 0;
    if (!pxa_internal_add_array_size(
            &size, 1, sizeof(pxa_window_service_t),
            PXA_INTERNAL_WORKSPACE_ALIGNMENT) ||
        !pxa_internal_add_array_size(
            &size, max_windows, sizeof(pxa_window_entry_t),
            PXA_INTERNAL_WORKSPACE_ALIGNMENT)) {
        return 0;
    }
    return size;
}

pxa_status_t pxa_window_service_init(void *workspace, size_t workspace_size,
                                     pxa_runtime_t *runtime,
                                     uint16_t max_windows,
                                     pxa_window_service_t **output) {
    uint8_t *cursor;
    const uint8_t *end;
    uint16_t index;
    pxa_window_service_t *service;
    pxa_window_entry_t *entries;
    size_t required;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    required = pxa_window_service_workspace_size(max_windows);
    if (workspace == NULL || runtime == NULL || required == 0 ||
        workspace_size < required ||
        (uintptr_t)workspace > UINTPTR_MAX - workspace_size) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    cursor = (uint8_t *)workspace;
    end = (const uint8_t *)((uintptr_t)workspace + workspace_size);
    service = (pxa_window_service_t *)pxa_internal_layout_take(
        &cursor, end, 1, sizeof(*service), PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    if (service == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    entries = (pxa_window_entry_t *)pxa_internal_layout_take(
        &cursor, end, max_windows, sizeof(*service->entries),
        PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    if (entries == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    memset(service, 0, sizeof(*service));
    service->entries = entries;
    memset(service->entries, 0,
           (size_t)max_windows * sizeof(*service->entries));
    service->runtime = runtime;
    service->free_head = 0;
    service->active_head = PXA_WINDOW_SLOT_NONE;
    for (index = 0; index < max_windows; ++index) {
        service->entries[index].next =
            index + 1u < max_windows ? (uint16_t)(index + 1u)
                                    : PXA_WINDOW_SLOT_NONE;
    }
    service->magic = PXA_WINDOW_MAGIC;
    *output = service;
    return PXA_STATUS_OK;
}

static pxa_status_t encode_snapshot(const pxa_window_entry_t *entry,
                                    uint8_t *output, size_t capacity,
                                    size_t *output_size) {
    uint8_t value8[16];
    pxa_writer_t writer;
    const pxa_window_snapshot_t *snapshot;
    if (entry == NULL || output == NULL || output_size == NULL ||
        !entry->has_snapshot) {
        return PXA_STATUS_BAD_STATE;
    }
    snapshot = &entry->snapshot;
    if (snapshot->revision == 0 || snapshot->logical_width == 0 ||
        snapshot->logical_height == 0 || snapshot->pixel_width == 0 ||
        snapshot->pixel_height == 0 || snapshot->density_numerator == 0 ||
        snapshot->density_denominator == 0 || snapshot->orientation > 2) {
        return PXA_STATUS_BAD_STATE;
    }
    pxa_writer_init(&writer, output, capacity);
    pxa_write_u64(value8, snapshot->revision);
    if (pxa_writer_record(&writer, 1, value8, 8) != PXA_STATUS_OK) goto fail;
    pxa_write_u32(value8, snapshot->logical_width);
    pxa_write_u32(value8 + 4, snapshot->logical_height);
    if (pxa_writer_record(&writer, 2, value8, 8) != PXA_STATUS_OK) goto fail;
    pxa_write_u32(value8, snapshot->pixel_width);
    pxa_write_u32(value8 + 4, snapshot->pixel_height);
    if (pxa_writer_record(&writer, 3, value8, 8) != PXA_STATUS_OK) goto fail;
    pxa_write_u32(value8, snapshot->density_numerator);
    pxa_write_u32(value8 + 4, snapshot->density_denominator);
    if (pxa_writer_record(&writer, 4, value8, 8) != PXA_STATUS_OK) goto fail;
    pxa_write_u32(value8, snapshot->safe_insets.left);
    pxa_write_u32(value8 + 4, snapshot->safe_insets.top);
    pxa_write_u32(value8 + 8, snapshot->safe_insets.right);
    pxa_write_u32(value8 + 12, snapshot->safe_insets.bottom);
    if (pxa_writer_record(&writer, 5, value8, 16) != PXA_STATUS_OK) goto fail;
    pxa_write_u32(value8, snapshot->system_bar_insets.left);
    pxa_write_u32(value8 + 4, snapshot->system_bar_insets.top);
    pxa_write_u32(value8 + 8, snapshot->system_bar_insets.right);
    pxa_write_u32(value8 + 12, snapshot->system_bar_insets.bottom);
    if (pxa_writer_record(&writer, 6, value8, 16) != PXA_STATUS_OK) goto fail;
    value8[0] = snapshot->orientation;
    if (pxa_writer_record(&writer, 7, value8, 1) != PXA_STATUS_OK) goto fail;
    value8[0] = snapshot->focused != 0;
    if (pxa_writer_record(&writer, 8, value8, 1) != PXA_STATUS_OK) goto fail;
    *output_size = writer.size;
    return PXA_STATUS_OK;

fail:
    return writer.status;
}

static pxa_status_t configure(pxa_window_entry_t *entry,
                              pxa_bytes_t records) {
    pxa_window_configuration_t candidate = entry->configuration;
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint8_t seen[8] = {0};
    pxa_status_t status;
    pxa_record_iterator_init(&iterator, records);
    for (;;) {
        uint8_t value = 0;
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) return status;
        if (record.tag > 7) {
            if (record.optional) continue;
            return PXA_STATUS_UNSUPPORTED;
        }
        if (seen[record.tag]) return PXA_STATUS_INVALID_ARGUMENT;
        seen[record.tag] = 1;
        if (record.tag <= 5) {
            if (record.payload.size != 1) return PXA_STATUS_INVALID_ARGUMENT;
            value = record.payload.data[0];
        } else if (record.payload.size != 4) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        switch (record.tag) {
            case 1:
                if (value > 1) return PXA_STATUS_INVALID_ARGUMENT;
                candidate.edge_to_edge = value;
                break;
            case 2:
                if (value > 2) return PXA_STATUS_INVALID_ARGUMENT;
                candidate.status_bar_mode = value;
                break;
            case 3:
                if (value > 2) return PXA_STATUS_INVALID_ARGUMENT;
                candidate.navigation_bar_mode = value;
                break;
            case 4:
                if (value > 2) return PXA_STATUS_INVALID_ARGUMENT;
                candidate.status_bar_icons = value;
                break;
            case 5:
                if (value > 2) return PXA_STATUS_INVALID_ARGUMENT;
                candidate.navigation_bar_icons = value;
                break;
            case 6:
                candidate.status_bar_color = pxa_read_u32(record.payload.data);
                break;
            case 7:
                candidate.navigation_bar_color =
                    pxa_read_u32(record.payload.data);
                break;
            default:
                return PXA_STATUS_INTERNAL;
        }
    }
    status = entry->backend.apply(entry->backend.context, &candidate);
    if (status != PXA_STATUS_OK) return status;
    entry->configuration = candidate;
    return PXA_STATUS_OK;
}

static pxa_status_t window_control(void *context, pxa_runtime_t *runtime,
                                   pxa_component_t component,
                                   const pxa_message_view_t *message) {
    pxa_window_service_t *service = (pxa_window_service_t *)context;
    pxa_window_entry_t *entry = find_entry(service, component);
    uint8_t payload[128];
    size_t payload_size = 0;
    pxa_status_t status;
    (void)runtime;
    if (entry == NULL) return PXA_STATUS_DENIED;
    switch (message->opcode) {
        case PXA_WINDOW_CONFIGURE:
            if (message->request_id != 0) return PXA_STATUS_INVALID_ARGUMENT;
            return configure(entry, message->payload);
        case PXA_WINDOW_SHOW_TOAST: {
            char text[PXA_WINDOW_TOAST_MAX_BYTES + 1u];
            uint16_t duration;
            size_t length = message->payload.size;
            if (message->request_id != 0 || length < 3u ||
                length > PXA_WINDOW_TOAST_MAX_BYTES + 2u ||
                !pxa_utf8_validate(message->payload.data + 2u, length - 2u, 0))
                return PXA_STATUS_INVALID_ARGUMENT;
            for (size_t index = 2u; index < length; ++index)
                if (message->payload.data[index] == 0)
                    return PXA_STATUS_INVALID_ARGUMENT;
            duration = pxa_read_u16(message->payload.data);
            if (duration < 500u || duration > 5000u)
                return PXA_STATUS_INVALID_ARGUMENT;
            if (entry->backend.show_toast == NULL) return PXA_STATUS_UNSUPPORTED;
            memcpy(text, message->payload.data + 2u, length - 2u);
            text[length - 2u] = '\0';
            return entry->backend.show_toast(entry->backend.context, text, duration);
        }
        case PXA_WINDOW_GET_SNAPSHOT:
            if (message->request_id == 0 || message->payload.size != 0) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            status = encode_snapshot(entry, payload, sizeof(payload),
                                     &payload_size);
            if (status != PXA_STATUS_OK) return status;
            status = pxa_request_begin(service->runtime, component,
                                       message->request_id,
                                       PXA_WINDOW_SERVICE_ID,
                                       PXA_WINDOW_GET_SNAPSHOT, 0);
            if (status != PXA_STATUS_OK) return status;
            return pxa_request_complete(service->runtime, component,
                                        message->request_id, PXA_STATUS_OK,
                                        payload, payload_size);
        default:
            return PXA_STATUS_UNSUPPORTED;
    }
}

static void release_entry(pxa_window_service_t *service, uint16_t index,
                          uint16_t previous) {
    pxa_window_entry_t *entry = &service->entries[index];
    if (previous == PXA_WINDOW_SLOT_NONE) {
        service->active_head = entry->next;
    } else {
        service->entries[previous].next = entry->next;
    }
    memset(entry, 0, sizeof(*entry));
    entry->next = service->free_head;
    service->free_head = index;
}

static void window_component_stopped(void *context, pxa_runtime_t *runtime,
                                     pxa_component_t component) {
    pxa_window_service_t *service = (pxa_window_service_t *)context;
    uint16_t previous;
    uint16_t index = find_entry_index(service, component, &previous);
    (void)runtime;
    if (index != PXA_WINDOW_SLOT_NONE) {
        release_entry(service, index, previous);
    }
}

pxa_status_t pxa_window_service_register(pxa_window_service_t *service) {
    pxa_service_ops_t operations;
    if (!service_valid(service)) return PXA_STATUS_INVALID_ARGUMENT;
    memset(&operations, 0, sizeof(operations));
    operations.struct_size = sizeof(operations);
    operations.service_id = PXA_WINDOW_SERVICE_ID;
    operations.major = PXA_WINDOW_SERVICE_MAJOR;
    operations.minor = PXA_WINDOW_SERVICE_MINOR;
    operations.context = service;
    operations.control = window_control;
    operations.component_stopped = window_component_stopped;
    return pxa_service_register(service->runtime, &operations);
}

pxa_status_t pxa_window_bind(pxa_window_service_t *service,
                             pxa_component_t component,
                             const pxa_window_backend_t *backend) {
    uint16_t index;
    pxa_component_snapshot_t snapshot;
    pxa_window_entry_t *entry;
    if (!service_valid(service) || backend == NULL ||
        backend->struct_size < offsetof(pxa_window_backend_t, show_toast) ||
        backend->apply == NULL ||
        component == PXA_COMPONENT_INVALID) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (find_entry(service, component) != NULL) return PXA_STATUS_BAD_STATE;
    if (pxa_component_snapshot(service->runtime, component, &snapshot) !=
        PXA_STATUS_OK) {
        return PXA_STATUS_NOT_FOUND;
    }
    index = service->free_head;
    if (index == PXA_WINDOW_SLOT_NONE) return PXA_STATUS_RESOURCE_LIMIT;
    entry = &service->entries[index];
    service->free_head = entry->next;
    memset(entry, 0, sizeof(*entry));
    entry->next = service->active_head;
    service->active_head = index;
    entry->component = component;
    memcpy(&entry->backend, backend,
           backend->struct_size < sizeof(entry->backend) ?
               backend->struct_size : sizeof(entry->backend));
    entry->configuration.status_bar_color = UINT32_C(0x000000ff);
    entry->configuration.navigation_bar_color = UINT32_C(0x000000ff);
    return PXA_STATUS_OK;
}

pxa_status_t pxa_window_unbind(pxa_window_service_t *service,
                               pxa_component_t component) {
    uint16_t previous;
    uint16_t index = find_entry_index(service, component, &previous);
    if (index == PXA_WINDOW_SLOT_NONE) return PXA_STATUS_NOT_FOUND;
    release_entry(service, index, previous);
    return PXA_STATUS_OK;
}

pxa_status_t pxa_window_negotiate_version(pxa_version_range_t requested,
                                          pxa_version_t *selected) {
    pxa_version_t supported;
    if (selected == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    memset(selected, 0, sizeof(*selected));
    supported.major = PXA_WINDOW_SERVICE_MAJOR;
    supported.minor = PXA_WINDOW_SERVICE_MINOR;
    if (requested.min_major > requested.max_major ||
        (requested.min_major == requested.max_major &&
         requested.min_minor > requested.max_minor)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (supported.major < requested.min_major ||
        supported.major > requested.max_major ||
        (requested.min_major == supported.major &&
         requested.min_minor > supported.minor)) {
        return PXA_STATUS_UNSUPPORTED;
    }
    *selected = supported;
    return PXA_STATUS_OK;
}

static int insets_same(const pxa_window_insets_t *left,
                       const pxa_window_insets_t *right) {
    return left->left == right->left && left->top == right->top &&
           left->right == right->right && left->bottom == right->bottom;
}

static int snapshot_same(const pxa_window_snapshot_t *left,
                         const pxa_window_snapshot_t *right) {
    return left->logical_width == right->logical_width &&
           left->logical_height == right->logical_height &&
           left->pixel_width == right->pixel_width &&
           left->pixel_height == right->pixel_height &&
           left->density_numerator == right->density_numerator &&
           left->density_denominator == right->density_denominator &&
           insets_same(&left->safe_insets, &right->safe_insets) &&
           insets_same(&left->system_bar_insets,
                       &right->system_bar_insets) &&
           left->orientation == right->orientation &&
           left->focused == right->focused;
}

static pxa_status_t flush_metrics_entry(pxa_window_service_t *service,
                                        pxa_component_t component,
                                        pxa_window_entry_t *entry) {
    uint8_t payload[128];
    size_t payload_size = 0;
    pxa_status_t status;
    if (!entry->metrics_dirty) return PXA_STATUS_OK;
    status = encode_snapshot(entry, payload, sizeof(payload), &payload_size);
    if (status != PXA_STATUS_OK) return status;
    status = pxa_event_post_message(
        service->runtime, component, PXA_WINDOW_SERVICE_ID,
        PXA_WINDOW_METRICS_CHANGED, 0,
        (pxa_bytes_t){payload, payload_size}, 0, PXA_WINDOW_METRICS_KEY);
    if (status == PXA_STATUS_OK) entry->metrics_dirty = 0;
    return status;
}

pxa_status_t pxa_window_update_snapshot(pxa_window_service_t *service,
                                        pxa_component_t component,
                                        const pxa_window_snapshot_t *snapshot) {
    pxa_window_entry_t *entry = find_entry(service, component);
    uint64_t revision;
    if (entry == NULL) return PXA_STATUS_NOT_FOUND;
    if (snapshot == NULL || snapshot->logical_width == 0 ||
        snapshot->logical_height == 0 || snapshot->pixel_width == 0 ||
        snapshot->pixel_height == 0 || snapshot->density_numerator == 0 ||
        snapshot->density_denominator == 0 || snapshot->orientation > 2 ||
        snapshot->focused > 1 ||
        (entry->has_snapshot && entry->snapshot.revision == UINT64_MAX)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (entry->has_snapshot && snapshot_same(snapshot, &entry->snapshot)) {
        return PXA_STATUS_OK;
    }
    revision = entry->has_snapshot ? entry->snapshot.revision + 1u : 1u;
    entry->snapshot = *snapshot;
    entry->snapshot.revision = revision;
    entry->has_snapshot = 1;
    entry->metrics_dirty = 1;
    /* Initial snapshots may be installed while a component is STARTING.
     * Event delivery begins only once it is RUNNING, so retain the dirty flag
     * and let the host flush the initial metrics after startup completes. */
    {
        pxa_status_t status = flush_metrics_entry(service, component, entry);
        return status == PXA_STATUS_BAD_STATE ? PXA_STATUS_OK : status;
    }
}

pxa_status_t pxa_window_flush_metrics(pxa_window_service_t *service,
                                      pxa_component_t component) {
    pxa_window_entry_t *entry = find_entry(service, component);
    if (entry == NULL) return PXA_STATUS_NOT_FOUND;
    return flush_metrics_entry(service, component, entry);
}

pxa_status_t pxa_window_queue_back(pxa_window_service_t *service,
                                   pxa_component_t component) {
    if (find_entry(service, component) == NULL) return PXA_STATUS_NOT_FOUND;
    return pxa_event_post_message(
        service->runtime, component, PXA_WINDOW_SERVICE_ID,
        PXA_WINDOW_BACK_REQUESTED, 0, (pxa_bytes_t){NULL, 0}, 1, 0);
}

pxa_status_t pxa_window_get_configuration(
    const pxa_window_service_t *service, pxa_component_t component,
    pxa_window_configuration_t *output) {
    const pxa_window_entry_t *entry = find_entry_const(service, component);
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    if (entry == NULL) return PXA_STATUS_NOT_FOUND;
    *output = entry->configuration;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_window_get_snapshot(const pxa_window_service_t *service,
                                     pxa_component_t component,
                                     pxa_window_snapshot_t *output) {
    const pxa_window_entry_t *entry = find_entry_const(service, component);
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    if (entry == NULL) return PXA_STATUS_NOT_FOUND;
    if (!entry->has_snapshot) return PXA_STATUS_BAD_STATE;
    *output = entry->snapshot;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_window_resolve_event_result(
    const pxa_message_view_t *event, int32_t guest_result,
    uint8_t *close_requested) {
    if (event == NULL || close_requested == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *close_requested = 0;
    if (event->service != PXA_WINDOW_SERVICE_ID || event->request_id != 0 ||
        guest_result > 1 ||
        (guest_result < 0 && !pxa_status_is_known(guest_result))) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (event->opcode == PXA_WINDOW_BACK_REQUESTED) {
        if (event->payload.size != 0) return PXA_STATUS_INVALID_ARGUMENT;
        if (guest_result < 0) return guest_result;
        *close_requested = guest_result == 0;
        return PXA_STATUS_OK;
    }
    if (event->opcode == PXA_WINDOW_METRICS_CHANGED) {
        return guest_result < 0 ? guest_result : PXA_STATUS_OK;
    }
    return PXA_STATUS_UNSUPPORTED;
}
