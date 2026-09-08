#include "pxa/fs.h"
#include "common/checked_math.h"
#include "common/status_internal.h"
#include "common/bytes_internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#define PXA_FS_MAGIC UINT32_C(0x50584653)
#define PXA_FS_SLOT_MAGIC UINT32_C(0x50584652)
#define PXA_FS_SLOT_NONE UINT16_MAX
#define PXA_FS_KNOWN_OPEN_FLAGS UINT32_C(0x7f)

typedef struct pxa_fs_resource_slot pxa_fs_resource_slot_t;

struct pxa_fs_service {
    uint32_t magic;
    pxa_runtime_t *runtime;
    pxa_fs_backend_t backend;
    void *authorization_context;
    pxa_fs_authorize_fn authorize;
    pxa_fs_resource_slot_t *slots;
    uint16_t slot_count;
    uint16_t free_head;
    uint8_t registered;
};

struct pxa_fs_resource_slot {
    uint32_t magic;
    pxa_fs_service_t *service;
    void *backend_resource;
    uint16_t next_free;
    uint8_t kind;
    uint8_t occupied;
};

typedef struct {
    pxa_bytes_t path;
    pxa_bytes_t destination;
    uint32_t flags;
    uint8_t has_path;
    uint8_t has_destination;
    uint8_t has_flags;
} pxa_fs_paths_t;

static const pxa_resource_ops_t k_file_resource_ops;
static const pxa_resource_ops_t k_directory_resource_ops;

static int segment_is_valid(pxa_bytes_t segment) {
    size_t index;
    if (segment.data == NULL || segment.size == 0 ||
        segment.size > PXA_FS_MAX_SEGMENT_BYTES ||
        (segment.size == 1 && segment.data[0] == '.') ||
        (segment.size == 2 && segment.data[0] == '.' &&
         segment.data[1] == '.') ||
        (segment.size >= 5 && memcmp(segment.data, ".pxa-", 5) == 0)) {
        return 0;
    }
    for (index = 0; index < segment.size; ++index) {
        uint8_t value = segment.data[index];
        if (value == 0 || value < UINT8_C(0x20) || value == UINT8_C(0x7f) ||
            value == '/' || value == '\\') {
            return 0;
        }
    }
    return pxa_utf8_validate(segment.data, segment.size, 0);
}

int pxa_fs_path_is_valid(pxa_bytes_t path) {
    size_t begin = 0;
    size_t index;
    if (path.data == NULL || path.size == 0 ||
        path.size > PXA_FS_MAX_PATH_BYTES || path.data[0] == '/' ||
        path.data[path.size - 1] == '/') {
        return 0;
    }
    for (index = 0; index <= path.size; ++index) {
        if (index != path.size && path.data[index] != '/') continue;
        if (!segment_is_valid(
                (pxa_bytes_t){path.data + begin, index - begin})) {
            return 0;
        }
        begin = index + 1;
    }
    return 1;
}

static int config_valid(const pxa_fs_config_t *config) {
    const pxa_fs_backend_t *backend;
    if (config == NULL || config->struct_size < sizeof(*config) ||
        config->max_open_resources == 0) {
        return 0;
    }
    backend = &config->backend;
    return backend->struct_size >= sizeof(*backend) && backend->open != NULL &&
           backend->make_directory != NULL && backend->remove != NULL &&
           backend->rename != NULL && backend->stat != NULL &&
           backend->read != NULL && backend->write != NULL &&
           backend->seek != NULL && backend->read_directory != NULL &&
           backend->close != NULL;
}

size_t pxa_fs_service_workspace_size(const pxa_fs_config_t *config) {
    size_t slots_size;
    size_t size;
    if (!config_valid(config) ||
        sizeof(pxa_fs_resource_slot_t) >
            SIZE_MAX / (size_t)config->max_open_resources) {
        return 0;
    }
    slots_size = (size_t)config->max_open_resources *
                 sizeof(pxa_fs_resource_slot_t);
    size = PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    if (sizeof(pxa_fs_service_t) > SIZE_MAX - size) return 0;
    size += sizeof(pxa_fs_service_t);
    if (slots_size > SIZE_MAX - size) return 0;
    return size + slots_size;
}

pxa_status_t pxa_fs_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_fs_config_t *config, pxa_fs_service_t **output) {
    uintptr_t cursor;
    uintptr_t end;
    size_t required;
    uint16_t index;
    pxa_fs_service_t *service;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    required = pxa_fs_service_workspace_size(config);
    if (workspace == NULL || runtime == NULL || required == 0 ||
        workspace_size < required ||
        (uintptr_t)workspace > UINTPTR_MAX - workspace_size) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    end = (uintptr_t)workspace + workspace_size;
    cursor = pxa_internal_align_pointer((uintptr_t)workspace, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    service = (pxa_fs_service_t *)cursor;
    cursor += sizeof(*service);
    if (cursor > end ||
        (size_t)config->max_open_resources * sizeof(pxa_fs_resource_slot_t) >
            end - cursor) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    memset(service, 0, sizeof(*service));
    service->runtime = runtime;
    service->backend = config->backend;
    service->authorization_context = config->authorization_context;
    service->authorize = config->authorize;
    service->slots = (pxa_fs_resource_slot_t *)cursor;
    service->slot_count = config->max_open_resources;
    service->free_head = 0;
    memset(service->slots, 0,
           (size_t)service->slot_count * sizeof(service->slots[0]));
    for (index = 0; index < service->slot_count; ++index) {
        service->slots[index].magic = PXA_FS_SLOT_MAGIC;
        service->slots[index].service = service;
        service->slots[index].next_free =
            index + 1u < service->slot_count ? (uint16_t)(index + 1u)
                                             : PXA_FS_SLOT_NONE;
    }
    service->magic = PXA_FS_MAGIC;
    *output = service;
    return PXA_STATUS_OK;
}

static int service_valid(const pxa_fs_service_t *service) {
    return service != NULL && service->magic == PXA_FS_MAGIC;
}

static int open_flags_valid(uint32_t flags) {
    int directory = (flags & PXA_FS_OPEN_DIRECTORY) != 0;
    int readable = (flags & PXA_FS_OPEN_READ) != 0;
    int writable = (flags & PXA_FS_OPEN_WRITE) != 0;
    if (flags == 0 || (flags & ~PXA_FS_KNOWN_OPEN_FLAGS) != 0) return 0;
    if (directory) {
        return flags == (PXA_FS_OPEN_READ | PXA_FS_OPEN_DIRECTORY);
    }
    return (readable || writable) &&
           ((flags & PXA_FS_OPEN_EXCLUSIVE) == 0 ||
            (flags & PXA_FS_OPEN_CREATE) != 0) &&
           ((flags & (PXA_FS_OPEN_TRUNCATE | PXA_FS_OPEN_APPEND)) == 0 ||
            writable);
}

static pxa_status_t parse_paths(pxa_bytes_t payload,
                                int destination_required,
                                int flags_required, pxa_fs_paths_t *output) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    pxa_status_t status;
    memset(output, 0, sizeof(*output));
    pxa_record_iterator_init(&iterator, payload);
    for (;;) {
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) return status;
        if (record.raw_tag < previous) return PXA_STATUS_INVALID_ARGUMENT;
        previous = record.raw_tag;
        if (record.tag == 1) {
            if (record.optional || output->has_path ||
                !pxa_fs_path_is_valid(record.payload)) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            output->path = record.payload;
            output->has_path = 1;
        } else if (record.tag == 2) {
            if (record.optional || output->has_destination ||
                !pxa_fs_path_is_valid(record.payload)) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            output->destination = record.payload;
            output->has_destination = 1;
        } else if (record.tag == 3) {
            if (record.optional || output->has_flags ||
                record.payload.size != 4) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            output->flags = pxa_read_u32(record.payload.data);
            output->has_flags = 1;
        } else if (!record.optional) {
            return PXA_STATUS_UNSUPPORTED;
        }
    }
    if (!output->has_path ||
        output->has_destination != (uint8_t)destination_required ||
        output->has_flags != (uint8_t)flags_required) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (flags_required && !open_flags_valid(output->flags)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (destination_required && output->path.size == output->destination.size &&
        memcmp(output->path.data, output->destination.data,
               output->path.size) == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    return PXA_STATUS_OK;
}

static pxa_fs_resource_slot_t *allocate_resource(pxa_fs_service_t *service) {
    pxa_fs_resource_slot_t *slot;
    uint16_t index = service->free_head;
    if (index == PXA_FS_SLOT_NONE) return NULL;
    slot = &service->slots[index];
    service->free_head = slot->next_free;
    slot->next_free = PXA_FS_SLOT_NONE;
    slot->backend_resource = NULL;
    slot->kind = 0;
    slot->occupied = 1;
    return slot;
}

static void release_resource(pxa_fs_resource_slot_t *slot) {
    pxa_fs_service_t *service = slot->service;
    uint16_t index = (uint16_t)(slot - service->slots);
    slot->backend_resource = NULL;
    slot->kind = 0;
    slot->occupied = 0;
    slot->next_free = service->free_head;
    service->free_head = index;
}

static void close_resource(void *context) {
    pxa_fs_resource_slot_t *slot = (pxa_fs_resource_slot_t *)context;
    if (slot == NULL || slot->magic != PXA_FS_SLOT_MAGIC || !slot->occupied) {
        return;
    }
    slot->service->backend.close(slot->service->backend.context,
                                 slot->backend_resource, slot->kind);
    release_resource(slot);
}

static int32_t file_io(void *context, uint32_t operation, uint8_t *data,
                       size_t size) {
    pxa_fs_resource_slot_t *slot = (pxa_fs_resource_slot_t *)context;
    pxa_status_t status;
    size_t transferred = 0;
    if (slot == NULL || slot->magic != PXA_FS_SLOT_MAGIC || !slot->occupied ||
        slot->kind != PXA_FS_KIND_REGULAR) {
        return PXA_STATUS_NOT_FOUND;
    }
    if (operation == PXA_FS_IO_READ) {
        status = slot->service->backend.read(
            slot->service->backend.context, slot->backend_resource, data, size,
            &transferred);
    } else if (operation == PXA_FS_IO_WRITE) {
        status = slot->service->backend.write(
            slot->service->backend.context, slot->backend_resource, data, size,
            &transferred);
    } else {
        return PXA_STATUS_UNSUPPORTED;
    }
    status = pxa_status_normalize(status);
    if (status != PXA_STATUS_OK) return status;
    if (transferred > size || transferred > (size_t)INT32_MAX) {
        return PXA_STATUS_INTERNAL;
    }
    return (int32_t)transferred;
}

static int32_t directory_io(void *context, uint32_t operation, uint8_t *data,
                            size_t size) {
    (void)context;
    (void)operation;
    (void)data;
    (void)size;
    return PXA_STATUS_UNSUPPORTED;
}

static const pxa_resource_ops_t k_file_resource_ops = {
    sizeof(pxa_resource_ops_t), file_io,
};

static const pxa_resource_ops_t k_directory_resource_ops = {
    sizeof(pxa_resource_ops_t), directory_io,
};

static pxa_status_t authorize_request(pxa_fs_service_t *service,
                                      pxa_component_t component,
                                      pxa_authority_t *authority) {
    pxa_status_t status;
    *authority = 0;
    if (service->authorize == NULL) return PXA_STATUS_OK;
    status = pxa_status_normalize(service->authorize(
        service->authorization_context, component, authority));
    if (status == PXA_STATUS_OK && *authority == 0) {
        return PXA_STATUS_INTERNAL;
    }
    if (status != PXA_STATUS_OK) *authority = 0;
    return status;
}

static pxa_status_t handle_open(pxa_fs_service_t *service,
                                pxa_component_t component,
                                pxa_authority_t authority,
                                const pxa_message_view_t *message,
                                uint8_t *result, size_t *result_size,
                                pxa_handle_t *opened_handle) {
    pxa_fs_paths_t paths;
    pxa_fs_resource_slot_t *slot;
    pxa_resource_t resource;
    pxa_status_t status = parse_paths(message->payload, 0, 1, &paths);
    if (status != PXA_STATUS_OK) return status;
    slot = allocate_resource(service);
    if (slot == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    status = pxa_status_normalize(service->backend.open(
        service->backend.context, paths.path, paths.flags,
        &slot->backend_resource, &slot->kind));
    if (status != PXA_STATUS_OK) {
        release_resource(slot);
        return status;
    }
    if (slot->backend_resource == NULL ||
        (slot->kind != PXA_FS_KIND_REGULAR &&
         slot->kind != PXA_FS_KIND_DIRECTORY) ||
        ((paths.flags & PXA_FS_OPEN_DIRECTORY) != 0) !=
            (slot->kind == PXA_FS_KIND_DIRECTORY)) {
        service->backend.close(service->backend.context,
                               slot->backend_resource, slot->kind);
        release_resource(slot);
        return PXA_STATUS_INTERNAL;
    }
    resource.context = slot;
    resource.operations = slot->kind == PXA_FS_KIND_REGULAR
                              ? (const void *)&k_file_resource_ops
                              : (const void *)&k_directory_resource_ops;
    resource.close = close_resource;
    status = pxa_handle_open(service->runtime, component,
                             slot->kind == PXA_FS_KIND_REGULAR
                                 ? PXA_RESOURCE_FILE
                                 : PXA_RESOURCE_DIRECTORY,
                             authority, &resource, opened_handle);
    if (status != PXA_STATUS_OK) {
        close_resource(slot);
        return status;
    }
    pxa_write_u32(result, *opened_handle);
    *result_size = 4;
    return PXA_STATUS_OK;
}

static pxa_status_t handle_path_operation(
    pxa_fs_service_t *service, const pxa_message_view_t *message,
    uint8_t *result, size_t *result_size) {
    pxa_fs_paths_t paths;
    pxa_status_t status;
    status = parse_paths(message->payload, message->opcode == PXA_FS_RENAME,
                         0, &paths);
    if (status != PXA_STATUS_OK) return status;
    if (message->opcode == PXA_FS_MAKE_DIRECTORY) {
        return pxa_status_normalize(service->backend.make_directory(
            service->backend.context, paths.path));
    }
    if (message->opcode == PXA_FS_REMOVE) {
        return pxa_status_normalize(service->backend.remove(
            service->backend.context, paths.path));
    }
    if (message->opcode == PXA_FS_RENAME) {
        return pxa_status_normalize(service->backend.rename(
            service->backend.context, paths.path, paths.destination));
    }
    if (message->opcode == PXA_FS_STAT) {
        pxa_fs_entry_t entry;
        memset(&entry, 0, sizeof(entry));
        status = pxa_status_normalize(service->backend.stat(
            service->backend.context, paths.path, &entry));
        if (status != PXA_STATUS_OK) return status;
        if (entry.kind != PXA_FS_KIND_REGULAR &&
            entry.kind != PXA_FS_KIND_DIRECTORY) {
            return PXA_STATUS_INTERNAL;
        }
        result[0] = entry.kind;
        pxa_write_u64(result + 1, entry.size);
        *result_size = 9;
        return PXA_STATUS_OK;
    }
    return PXA_STATUS_UNSUPPORTED;
}

static pxa_status_t handle_seek(pxa_fs_service_t *service,
                                pxa_component_t component,
                                const pxa_message_view_t *message,
                                uint8_t *result, size_t *result_size) {
    pxa_resource_t resource;
    pxa_fs_resource_slot_t *slot;
    pxa_handle_t handle;
    uint64_t offset_bits;
    int64_t offset;
    uint64_t position = 0;
    uint8_t origin;
    pxa_status_t status;
    if (message->payload.size != 13) return PXA_STATUS_INVALID_ARGUMENT;
    handle = pxa_read_u32(message->payload.data);
    origin = message->payload.data[12];
    if (handle == PXA_HANDLE_INVALID || origin > PXA_FS_SEEK_END) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    status = pxa_handle_get(service->runtime, component, handle,
                            PXA_RESOURCE_FILE, &resource);
    if (status != PXA_STATUS_OK) return status;
    slot = (pxa_fs_resource_slot_t *)resource.context;
    if (slot == NULL || slot->magic != PXA_FS_SLOT_MAGIC || !slot->occupied ||
        slot->service != service || slot->kind != PXA_FS_KIND_REGULAR) {
        return PXA_STATUS_INTERNAL;
    }
    offset_bits = pxa_read_u64(message->payload.data + 4);
    memcpy(&offset, &offset_bits, sizeof(offset));
    status = pxa_status_normalize(service->backend.seek(
        service->backend.context, slot->backend_resource, offset, origin,
        &position));
    if (status != PXA_STATUS_OK) return status;
    pxa_write_u64(result, position);
    *result_size = 8;
    return PXA_STATUS_OK;
}

static pxa_status_t handle_read_directory(
    pxa_fs_service_t *service, pxa_component_t component,
    const pxa_message_view_t *message, uint8_t *result, size_t capacity,
    size_t *result_size) {
    pxa_resource_t resource;
    pxa_fs_resource_slot_t *slot;
    pxa_fs_entry_t entry;
    pxa_writer_t writer;
    pxa_handle_t handle;
    pxa_status_t status;
    uint8_t end = 0;
    uint8_t size_bytes[8];
    if (message->payload.size != 4) return PXA_STATUS_INVALID_ARGUMENT;
    handle = pxa_read_u32(message->payload.data);
    if (handle == PXA_HANDLE_INVALID) return PXA_STATUS_INVALID_ARGUMENT;
    status = pxa_handle_get(service->runtime, component, handle,
                            PXA_RESOURCE_DIRECTORY, &resource);
    if (status != PXA_STATUS_OK) return status;
    slot = (pxa_fs_resource_slot_t *)resource.context;
    if (slot == NULL || slot->magic != PXA_FS_SLOT_MAGIC || !slot->occupied ||
        slot->service != service || slot->kind != PXA_FS_KIND_DIRECTORY) {
        return PXA_STATUS_INTERNAL;
    }
    memset(&entry, 0, sizeof(entry));
    status = pxa_status_normalize(service->backend.read_directory(
        service->backend.context, slot->backend_resource, &entry, &end));
    if (status != PXA_STATUS_OK) return status;
    if (end > 1) return PXA_STATUS_INTERNAL;
    pxa_writer_init(&writer, result, capacity);
    if (end) {
        status = pxa_writer_record(&writer, 7, NULL, 0);
    } else {
        if (!segment_is_valid(entry.name) ||
            (entry.kind != PXA_FS_KIND_REGULAR &&
             entry.kind != PXA_FS_KIND_DIRECTORY)) {
            return PXA_STATUS_INTERNAL;
        }
        status = pxa_writer_record(&writer, 4, entry.name.data,
                                   entry.name.size);
        if (status == PXA_STATUS_OK) {
            status = pxa_writer_record(&writer, 5, &entry.kind, 1);
        }
        if (status == PXA_STATUS_OK) {
            pxa_write_u64(size_bytes, entry.size);
            status = pxa_writer_record(&writer, 6, size_bytes, 8);
        }
    }
    if (status != PXA_STATUS_OK) return status;
    *result_size = writer.size;
    return PXA_STATUS_OK;
}

static pxa_status_t fs_control(void *context, pxa_runtime_t *runtime,
                               pxa_component_t component,
                               const pxa_message_view_t *message) {
    pxa_fs_service_t *service = (pxa_fs_service_t *)context;
    uint8_t result[96];
    size_t result_size = 0;
    pxa_authority_t authority = 0;
    pxa_handle_t opened_handle = PXA_HANDLE_INVALID;
    pxa_status_t status;
    pxa_status_t complete;
    (void)runtime;
    if (!service_valid(service) || message->request_id == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (message->opcode < PXA_FS_OPEN ||
        message->opcode > PXA_FS_READ_DIRECTORY) {
        return PXA_STATUS_UNSUPPORTED;
    }
    status = authorize_request(service, component, &authority);
    complete = pxa_request_begin(service->runtime, component,
                                 message->request_id, PXA_FS_SERVICE_ID,
                                 message->opcode,
                                 status == PXA_STATUS_OK ? authority : 0);
    if (complete != PXA_STATUS_OK) return complete;
    if (status == PXA_STATUS_OK && message->opcode == PXA_FS_OPEN) {
        status = handle_open(service, component, authority, message, result,
                             &result_size, &opened_handle);
    } else if (status == PXA_STATUS_OK &&
               message->opcode >= PXA_FS_MAKE_DIRECTORY &&
               message->opcode <= PXA_FS_STAT) {
        status = handle_path_operation(service, message, result, &result_size);
    } else if (status == PXA_STATUS_OK && message->opcode == PXA_FS_SEEK) {
        status = handle_seek(service, component, message, result, &result_size);
    } else if (status == PXA_STATUS_OK) {
        status = handle_read_directory(service, component, message, result,
                                       sizeof(result), &result_size);
    }
    complete = pxa_request_complete(
        service->runtime, component, message->request_id, status,
        status == PXA_STATUS_OK ? result : NULL,
        status == PXA_STATUS_OK ? result_size : 0);
    if (complete != PXA_STATUS_OK) {
        if (opened_handle != PXA_HANDLE_INVALID) {
            (void)pxa_handle_close(service->runtime, component, opened_handle);
        }
        (void)pxa_request_cancel(service->runtime, component,
                                 message->request_id);
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_fs_service_register(pxa_fs_service_t *service) {
    pxa_service_ops_t operations;
    pxa_status_t status;
    if (!service_valid(service)) return PXA_STATUS_INVALID_ARGUMENT;
    if (service->registered) return PXA_STATUS_BAD_STATE;
    memset(&operations, 0, sizeof(operations));
    operations.struct_size = sizeof(operations);
    operations.service_id = PXA_FS_SERVICE_ID;
    operations.major = PXA_FS_SERVICE_MAJOR;
    operations.minor = PXA_FS_SERVICE_MINOR;
    operations.context = service;
    operations.control = fs_control;
    status = pxa_service_register(service->runtime, &operations);
    if (status == PXA_STATUS_OK) service->registered = 1;
    return status;
}
