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
    const pxa_fs_roots_backend_t *roots;
    pxa_permission_service_t *permissions;
    pxa_component_t picker_component;
    pxa_request_token_t picker_token;
    uint64_t picker_identity;
    pxa_authority_t picker_authority;
    uint32_t picker_rights;
    uint8_t picker_persistent;
};

struct pxa_fs_resource_slot {
    uint32_t magic;
    pxa_fs_service_t *service;
    void *backend_resource;
    const pxa_fs_backend_t *backend;
    pxa_authority_t authority;
    uint16_t next_free;
    uint8_t kind;
    uint8_t occupied;
    uint64_t retired_read_cookie;
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

int pxa_fs_scoped_path_is_valid(pxa_bytes_t path) {
    size_t begin = 0, i, j;
    if (!path.data || !path.size || path.size > PXA_FS_SCOPED_MAX_PATH_BYTES ||
        path.data[0] == '/' || path.data[path.size - 1] == '/' ||
        !pxa_utf8_validate(path.data, path.size, 0)) return 0;
    for (i = 0; i <= path.size; ++i) {
        size_t n;
        if (i < path.size && path.data[i] != '/') {
            uint8_t c = path.data[i];
            if (c < 0x20 || c == 0x7f || c == '\\' || c == ':') return 0;
            continue;
        }
        n = i - begin;
        if (!n || n > PXA_FS_SCOPED_MAX_SEGMENT_BYTES ||
            (n == 1 && path.data[begin] == '.') ||
            (n == 2 && path.data[begin] == '.' && path.data[begin+1] == '.') ||
            path.data[i-1] == '.' || path.data[i-1] == ' ') return 0;
        if (n >= 5) {
            const char reserved[] = ".pxa-";
            for (j = 0; j < 5; ++j) {
                uint8_t c = path.data[begin+j];
                if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
                if (c != (uint8_t)reserved[j]) break;
            }
            if (j == 5) return 0;
        }
        begin = i + 1;
    }
    return 1;
}

static int config_valid(const pxa_fs_config_t *config) {
    const pxa_fs_backend_t *backend;
    if (config == NULL || config->struct_size < offsetof(pxa_fs_config_t, roots) ||
        config->max_open_resources == 0) {
        return 0;
    }
    if (config->struct_size >= sizeof(*config) && config->roots) {
        const pxa_fs_roots_backend_t *r = config->roots;
        if (r->struct_size < offsetof(pxa_fs_roots_backend_t, features) || !r->open_root || !r->open_at ||
            !r->query_root || !r->files.read || !r->files.seek ||
            !r->files.close || !r->files.read_directory ||
            (r->request_access && (!r->cancel_access || !config->permissions)))
            return 0;
    }
    backend = &config->backend;
    return backend->struct_size >= offsetof(pxa_fs_backend_t, replace) && backend->open != NULL &&
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
    memcpy(&service->backend, &config->backend, offsetof(pxa_fs_backend_t, replace));
    if (config->backend.struct_size >= sizeof(config->backend))
        service->backend.replace = config->backend.replace;
    service->authorization_context = config->authorization_context;
    service->authorize = config->authorize;
    if (config->struct_size >= sizeof(*config)) {
        service->roots = config->roots;
        service->permissions = config->permissions;
    }
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
    slot->backend = &service->backend;
    slot->retired_read_cookie = 0;
    slot->authority = 0;
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
    slot->backend->close(slot->backend->context,
                                 slot->backend_resource, slot->kind);
    release_resource(slot);
}

static int roots_read_at_enabled(const pxa_fs_roots_backend_t *roots) {
    return roots && roots->struct_size >=
        offsetof(pxa_fs_roots_backend_t,read_at)+sizeof(roots->read_at) &&
        roots->read_at;
}

/* Runtime-owned calls serialize access to a handle. Older private backends
 * expose seek/read rather than pread; retain the cursor even on read errors. */
static pxa_status_t legacy_read_at(pxa_fs_resource_slot_t *slot,uint64_t offset,
    uint8_t *out,size_t size,size_t *transferred) {
    const pxa_fs_backend_t *b=slot->backend;uint64_t saved=0,position=0;
    pxa_status_t status,restore;
    if(!b->seek || !b->read)return PXA_STATUS_UNSUPPORTED;
    if(offset>INT64_MAX)return PXA_STATUS_INVALID_ARGUMENT;
    status=pxa_status_normalize(b->seek(b->context,slot->backend_resource,0,
                                     PXA_FS_SEEK_CURRENT,&saved));
    if(status!=PXA_STATUS_OK)return status;
    if(saved>INT64_MAX)return PXA_STATUS_INVALID_ARGUMENT;
    status=pxa_status_normalize(b->seek(b->context,slot->backend_resource,
                    (int64_t)offset,PXA_FS_SEEK_START,&position));
    if(status==PXA_STATUS_OK && position!=offset)status=PXA_STATUS_INTERNAL;
    if(status==PXA_STATUS_OK)status=pxa_status_normalize(b->read(b->context,
                            slot->backend_resource,out,size,transferred));
    restore=pxa_status_normalize(b->seek(b->context,slot->backend_resource,
                      (int64_t)saved,PXA_FS_SEEK_START,&position));
    if(restore!=PXA_STATUS_OK)return restore;
    return position==saved?status:PXA_STATUS_INTERNAL;
}

static int32_t file_read_at(pxa_fs_resource_slot_t *slot,uint32_t operation,
    uint8_t *data,size_t size) {
    const pxa_fs_roots_backend_t *roots=slot->service->roots;
    uint64_t cookie,offset;uint32_t requested;size_t transferred=0;
    pxa_status_t status;
    int scoped=roots && slot->backend==&roots->files;
    if(scoped ? !roots_read_at_enabled(roots) :
       (slot->backend!=&slot->service->backend || !slot->backend->read || !slot->backend->seek))
        return PXA_STATUS_UNSUPPORTED;
    if(operation==PXA_FS_IO_CANCEL_READ_AT) {
        if(size!=8 || !data || !(cookie=pxa_read_u64(data)))
            return PXA_STATUS_INVALID_ARGUMENT;
        if(cookie>slot->retired_read_cookie)slot->retired_read_cookie=cookie;
        return PXA_STATUS_OK;
    }
    if(!data || size<PXA_FS_READ_AT_HEADER_BYTES) return PXA_STATUS_INVALID_ARGUMENT;
    offset=pxa_read_u64(data);cookie=pxa_read_u64(data+8);
    requested=pxa_read_u32(data+16);
    if(!cookie || !requested || requested>PXA_FS_READ_AT_MAX_BYTES ||
        size!=PXA_FS_READ_AT_HEADER_BYTES+(size_t)requested ||
        pxa_read_u32(data+20) || offset>UINT64_MAX-requested)
        return PXA_STATUS_INVALID_ARGUMENT;
    if(cookie<=slot->retired_read_cookie) return PXA_STATUS_CANCELLED;
    slot->retired_read_cookie=cookie;
    status=scoped ? pxa_status_normalize(roots->read_at(roots->files.context,
        slot->backend_resource,offset,data+PXA_FS_READ_AT_HEADER_BYTES,
        requested,&transferred)) : legacy_read_at(slot,offset,
        data+PXA_FS_READ_AT_HEADER_BYTES,requested,&transferred);
    if(status==PXA_STATUS_WOULD_BLOCK) return PXA_STATUS_INTERNAL;
    if(status!=PXA_STATUS_OK) return status;
    return transferred<=requested ? (int32_t)transferred : PXA_STATUS_INTERNAL;
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
    if(operation==PXA_FS_IO_READ_AT || operation==PXA_FS_IO_CANCEL_READ_AT)
        return file_read_at(slot,operation,data,size);
    if (operation == PXA_FS_IO_READ) {
        status = slot->backend->read(
            slot->backend->context, slot->backend_resource, data, size,
            &transferred);
    } else if (operation == PXA_FS_IO_WRITE) {
        if (!slot->backend->write) return PXA_STATUS_DENIED;
        status = slot->backend->write(
            slot->backend->context, slot->backend_resource, data, size,
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
                                uint16_t wire_profile,
                                const pxa_message_view_t *message,
                                uint8_t *result, size_t *result_size,
                                pxa_handle64_t *opened_handle) {
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
    if (wire_profile == 1) {
        status = pxa_handle_open64(service->runtime, component,
                                   slot->kind == PXA_FS_KIND_REGULAR
                                       ? PXA_RESOURCE_FILE
                                       : PXA_RESOURCE_DIRECTORY,
                                   authority, &resource, opened_handle);
    } else {
        pxa_handle_t legacy_handle = PXA_HANDLE_INVALID;
        status = pxa_handle_open(service->runtime, component,
                                 slot->kind == PXA_FS_KIND_REGULAR
                                     ? PXA_RESOURCE_FILE
                                     : PXA_RESOURCE_DIRECTORY,
                                 authority, &resource, &legacy_handle);
        *opened_handle = legacy_handle;
    }
    if (status != PXA_STATUS_OK) {
        close_resource(slot);
        return status;
    }
    if (wire_profile == 1) {
        pxa_write_u64(result, *opened_handle);
        *result_size = 8;
    } else {
        pxa_write_u32(result, (uint32_t)*opened_handle);
        *result_size = 4;
    }
    return PXA_STATUS_OK;
}

static pxa_status_t handle_path_operation(
    pxa_fs_service_t *service, const pxa_message_view_t *message,
    uint8_t *result, size_t *result_size) {
    pxa_fs_paths_t paths;
    pxa_status_t status;
    status = parse_paths(message->payload, message->opcode == PXA_FS_RENAME ||
                         message->opcode == PXA_FS_REPLACE,
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
    if (message->opcode == PXA_FS_REPLACE) {
        if (service->backend.replace == NULL) return PXA_STATUS_UNSUPPORTED;
        return pxa_status_normalize(service->backend.replace(
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
                                uint16_t wire_profile,
                                const pxa_message_view_t *message,
                                uint8_t *result, size_t *result_size) {
    pxa_resource_t resource;
    pxa_fs_resource_slot_t *slot;
    pxa_handle64_t handle;
    size_t handle_bytes = wire_profile == 1 ? 8u : 4u;
    uint64_t offset_bits;
    int64_t offset;
    uint64_t position = 0;
    uint8_t origin;
    pxa_status_t status;
    if (message->payload.size != handle_bytes + 9u)
        return PXA_STATUS_INVALID_ARGUMENT;
    handle = wire_profile == 1 ? pxa_read_u64(message->payload.data)
                             : pxa_read_u32(message->payload.data);
    origin = message->payload.data[handle_bytes + 8u];
    if (handle == PXA_HANDLE64_INVALID ||
        (wire_profile == 1 && (handle >> 32) == 0) ||
        origin > PXA_FS_SEEK_END) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    status = wire_profile == 1
                 ? pxa_handle_get64(service->runtime, component, handle,
                                     PXA_RESOURCE_FILE, &resource)
                 : pxa_handle_get(service->runtime, component,
                                   (pxa_handle_t)handle,
                                   PXA_RESOURCE_FILE, &resource);
    if (status != PXA_STATUS_OK) return status;
    if (resource.operations != &k_file_resource_ops) return PXA_STATUS_UNSUPPORTED;
    slot = (pxa_fs_resource_slot_t *)resource.context;
    if (slot == NULL || slot->magic != PXA_FS_SLOT_MAGIC || !slot->occupied ||
        slot->service != service || slot->kind != PXA_FS_KIND_REGULAR) {
        return PXA_STATUS_INTERNAL;
    }
    offset_bits = pxa_read_u64(message->payload.data + handle_bytes);
    memcpy(&offset, &offset_bits, sizeof(offset));
    status = pxa_status_normalize(slot->backend->seek(
        slot->backend->context, slot->backend_resource, offset, origin,
        &position));
    if (status != PXA_STATUS_OK) return status;
    pxa_write_u64(result, position);
    *result_size = 8;
    return PXA_STATUS_OK;
}

static pxa_status_t handle_read_directory(
    pxa_fs_service_t *service, pxa_component_t component,
    uint16_t wire_profile,
    const pxa_message_view_t *message, uint8_t *result, size_t capacity,
    size_t *result_size) {
    pxa_resource_t resource;
    pxa_fs_resource_slot_t *slot;
    pxa_fs_entry_t entry;
    pxa_writer_t writer;
    pxa_handle64_t handle;
    size_t handle_bytes = wire_profile == 1 ? 8u : 4u;
    pxa_status_t status;
    uint8_t end = 0;
    uint8_t size_bytes[8];
    if (message->payload.size != handle_bytes)
        return PXA_STATUS_INVALID_ARGUMENT;
    handle = wire_profile == 1 ? pxa_read_u64(message->payload.data)
                             : pxa_read_u32(message->payload.data);
    if (handle == PXA_HANDLE64_INVALID ||
        (wire_profile == 1 && (handle >> 32) == 0))
        return PXA_STATUS_INVALID_ARGUMENT;
    status = wire_profile == 1
                 ? pxa_handle_get64(service->runtime, component, handle,
                                     PXA_RESOURCE_DIRECTORY, &resource)
                 : pxa_handle_get(service->runtime, component,
                                   (pxa_handle_t)handle,
                                   PXA_RESOURCE_DIRECTORY, &resource);
    if (status != PXA_STATUS_OK) return status;
    if (resource.operations != &k_directory_resource_ops) return PXA_STATUS_UNSUPPORTED;
    slot = (pxa_fs_resource_slot_t *)resource.context;
    if (slot == NULL || slot->magic != PXA_FS_SLOT_MAGIC || !slot->occupied ||
        slot->service != service || slot->kind != PXA_FS_KIND_DIRECTORY) {
        return PXA_STATUS_INTERNAL;
    }
    memset(&entry, 0, sizeof(entry));
    status = pxa_status_normalize(slot->backend->read_directory(
        slot->backend->context, slot->backend_resource, &entry, &end));
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

static pxa_status_t scoped_directory(pxa_fs_service_t *s, pxa_component_t c,
    uint64_t handle, pxa_fs_resource_slot_t **output) {
    pxa_resource_t resource;
    pxa_fs_resource_slot_t *slot;
    pxa_status_t status = pxa_handle_get64(s->runtime, c, handle,
        PXA_RESOURCE_DIRECTORY, &resource);
    if (status != PXA_STATUS_OK) return status;
    if (resource.operations != &k_directory_resource_ops)
        return PXA_STATUS_UNSUPPORTED;
    slot = resource.context;
    if (!slot || slot->service != s || !slot->occupied ||
        slot->backend != &s->roots->files) return PXA_STATUS_UNSUPPORTED;
    *output = slot;
    return PXA_STATUS_OK;
}

static pxa_status_t publish_scoped(pxa_fs_resource_slot_t *slot,
    pxa_component_t c, pxa_authority_t authority, pxa_handle64_t *handle) {
    pxa_resource_t resource;
    pxa_status_t status;
    slot->authority = authority;
    resource.context = slot;
    resource.operations = slot->kind == PXA_FS_KIND_DIRECTORY
        ? &k_directory_resource_ops : &k_file_resource_ops;
    resource.close = close_resource;
    status = pxa_handle_open64(slot->service->runtime, c,
        slot->kind == PXA_FS_KIND_DIRECTORY ? PXA_RESOURCE_DIRECTORY :
        PXA_RESOURCE_FILE, authority, &resource, handle);
    if (status != PXA_STATUS_OK) close_resource(slot);
    return status;
}

static pxa_status_t directory_permission(pxa_fs_service_t *s, pxa_component_t c,
    uint64_t handle, pxa_authority_t *authority, uint32_t *rights) {
    const pxa_bytes_t name = {(const uint8_t *)"fs.user-directory",17};
    pxa_status_t status;
    if (!s->permissions) return PXA_STATUS_DENIED;
    status = pxa_permission_resolve64(s->permissions, c, handle, name,
        (pxa_bytes_t){(const uint8_t *)"read",4}, authority);
    if (status == PXA_STATUS_OK) { *rights = 3; return status; }
    status = pxa_permission_resolve64(s->permissions, c, handle, name,
        (pxa_bytes_t){(const uint8_t *)"read-write",10}, authority);
    if (status == PXA_STATUS_OK) *rights = 31;
    return status;
}

static pxa_status_t open_scoped_root(pxa_fs_service_t *s, pxa_component_t c,
    uint8_t kind, const uint8_t id[16], pxa_authority_t authority,
    uint32_t allowed, uint8_t *result, uint64_t *handle) {
    uint32_t rights = 0;
    pxa_status_t status;
    pxa_fs_resource_slot_t *slot = allocate_resource(s);
    if (!slot) return PXA_STATUS_RESOURCE_LIMIT;
    slot->backend = &s->roots->files; slot->kind = PXA_FS_KIND_DIRECTORY;
    status = pxa_status_normalize(s->roots->open_root(s->roots->context,
        kind, id, &slot->backend_resource, &rights));
    if (status != PXA_STATUS_OK) { release_resource(slot); return status; }
    if (!slot->backend_resource || !rights || (rights & ~allowed)) {
        close_resource(slot); return PXA_STATUS_DENIED;
    }
    status = publish_scoped(slot, c, authority, handle);
    if (status == PXA_STATUS_OK) {
        pxa_write_u64(result, *handle); pxa_write_u32(result+8, rights);
    }
    return status;
}

static void clear_picker(pxa_fs_service_t *s) {
    s->picker_token = 0; s->picker_identity = 0;
    s->picker_component = PXA_COMPONENT_INVALID;
    s->picker_authority = 0; s->picker_rights = 0;
    s->picker_persistent = 0;
}
void pxa_fs_service_poll(pxa_fs_service_t *s) {
    if (service_valid(s) && s->picker_token &&
        (!pxa_request_is_active(s->runtime,s->picker_component,s->picker_token) ||
         pxa_request_identity(s->runtime,s->picker_component,s->picker_token) !=
             s->picker_identity)) {
        pxa_component_t c = s->picker_component;
        pxa_request_token_t t = s->picker_token;
        clear_picker(s);
        s->roots->cancel_access(s->roots->context,c,t);
    }
}
pxa_status_t pxa_fs_access_complete(pxa_fs_service_t *s, pxa_component_t c,
    pxa_request_token_t token, pxa_status_t status, const uint8_t id[16]) {
    uint8_t result[28]; uint64_t handle = 0;
    pxa_status_t complete;
    pxa_authority_t authority; uint32_t rights; uint8_t persistent;
    if (!service_valid(s)) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_fs_service_poll(s);
    if (!token || token != s->picker_token || c != s->picker_component)
        return PXA_STATUS_NOT_FOUND;
    authority = s->picker_authority; rights = s->picker_rights;
    persistent = s->picker_persistent;
    status = pxa_status_normalize(status);
    if (status == PXA_STATUS_OK && !id) return PXA_STATUS_INVALID_ARGUMENT;
    clear_picker(s);
    complete = pxa_request_commit(s->runtime,c,token);
    if (complete != PXA_STATUS_OK) return complete;
    if (status == PXA_STATUS_OK) {
        status = open_scoped_root(s,c,PXA_FS_ROOT_GRANT,id,authority,rights,
            result,&handle);
        if (status == PXA_STATUS_OK) {
            if(persistent) memcpy(result+12,id,16);
            else memset(result+12,0,16);
        }
    }
    complete = pxa_request_complete(s->runtime,c,token,status,
        status == PXA_STATUS_OK ? result : NULL,
        status == PXA_STATUS_OK ? sizeof(result) : 0);
    if (complete != PXA_STATUS_OK && handle)
        (void)pxa_handle_close64(s->runtime,c,handle);
    return complete;
}

static pxa_status_t scoped_control(pxa_fs_service_t *s, pxa_component_t c,
    const pxa_message_view_t *m) {
    uint8_t result[PXA_FS_ROOT_INFO_BYTES]; size_t length = 0;
    pxa_status_t status = PXA_STATUS_OK, complete;
    pxa_authority_t authority = 0; uint32_t allowed = 31;
    uint64_t handle = 0; uint16_t profile = 0;
    const uint8_t *p = m->payload.data;
    pxa_fs_resource_slot_t *parent = NULL;
    pxa_fs_service_poll(s);
    if (!s->roots) return PXA_STATUS_UNSUPPORTED;
    if (pxa_component_wire_profile(s->runtime,c,&profile) != PXA_STATUS_OK ||
        profile != 1) return PXA_STATUS_UNSUPPORTED;
    if (m->opcode == PXA_FS_OPEN_ROOT) {
        unsigned i; uint8_t any = 0;
        if (m->payload.size != 28 || p[1] || p[2] || p[3] ||
            p[0] < 1 || p[0] > PXA_FS_ROOT_APP_MEDIA) return PXA_STATUS_INVALID_ARGUMENT;
        for (i = 0; i < 16; ++i) any |= p[12+i];
        if (p[0] == PXA_FS_ROOT_GRANT) {
            if (!any) return PXA_STATUS_INVALID_ARGUMENT;
            status = directory_permission(s,c,pxa_read_u64(p+4),&authority,&allowed);
        } else {
            if (any || pxa_read_u64(p+4)) return PXA_STATUS_INVALID_ARGUMENT;
            status = authorize_request(s,c,&authority);
            if (status == PXA_STATUS_OK && p[0] == PXA_FS_ROOT_APP_MEDIA &&
                (s->roots->struct_size < offsetof(pxa_fs_roots_backend_t,features)+sizeof(s->roots->features) ||
                 !(s->roots->features & PXA_FS_FEATURE_APP_MEDIA_ROOT)))
                status = PXA_STATUS_UNSUPPORTED;
        }
    } else if (m->opcode == PXA_FS_REQUEST_ACCESS) {
        if (!s->roots->request_access) return PXA_STATUS_UNSUPPORTED;
        if (m->payload.size < 18 || pxa_read_u32(p+12) > 1 ||
            !pxa_read_u16(p+16) || pxa_read_u16(p+16) > 256 ||
            m->payload.size != (size_t)18 + pxa_read_u16(p+16) ||
            !pxa_utf8_validate(p+18,m->payload.size-18,0) ||
            !(pxa_read_u32(p+8) & 3u) || (pxa_read_u32(p+8) & ~31u))
            return PXA_STATUS_INVALID_ARGUMENT;
        if (s->picker_token) return PXA_STATUS_BUSY;
        status = directory_permission(s,c,pxa_read_u64(p),&authority,&allowed);
        if (status == PXA_STATUS_OK && (pxa_read_u32(p+8) & ~allowed))
            status = PXA_STATUS_DENIED;
    } else {
        if (m->payload.size < 8) return PXA_STATUS_INVALID_ARGUMENT;
        status = scoped_directory(s,c,pxa_read_u64(p),&parent);
        if (status == PXA_STATUS_OK) authority = parent->authority;
    }
    size_t capacity=m->opcode==PXA_FS_QUERY_ROOT ? PXA_FS_ROOT_INFO_BYTES :
        m->opcode==PXA_FS_OPEN_ROOT ? 12u : m->opcode==PXA_FS_STAT_AT ? 16u : 28u;
    complete = pxa_request_begin_reserved(s->runtime,c,m->request_token,
        PXA_FS_SERVICE_ID,m->opcode,status == PXA_STATUS_OK ? authority : 0,
        capacity);
    if (complete != PXA_STATUS_OK) return complete;
    if (status == PXA_STATUS_OK && m->opcode == PXA_FS_REQUEST_ACCESS) {
        s->picker_component = c; s->picker_token = m->request_token;
        s->picker_identity = pxa_request_identity(s->runtime,c,m->request_token);
        s->picker_authority = authority; s->picker_rights = pxa_read_u32(p+8);
        s->picker_persistent = (uint8_t)pxa_read_u32(p+12);
        status = pxa_status_normalize(s->roots->request_access(s->roots->context,
            c,m->request_token,(pxa_bytes_t){p+18,m->payload.size-18},
            s->picker_rights,(uint8_t)pxa_read_u32(p+12)));
        if (status == PXA_STATUS_OK) return status;
        clear_picker(s);
    }
    if (status == PXA_STATUS_OK)
        status = pxa_request_commit(s->runtime,c,m->request_token);
    if (status == PXA_STATUS_OK && m->opcode == PXA_FS_OPEN_ROOT) {
        status = open_scoped_root(s,c,p[0],p+12,authority,allowed,result,&handle);
        length = 12;
    } else if (status == PXA_STATUS_OK && m->opcode == PXA_FS_QUERY_ROOT) {
        pxa_fs_root_info_t info;
        if (m->payload.size != 8) status = PXA_STATUS_INVALID_ARGUMENT;
        else {
            memset(&info,0,sizeof(info));
            status = pxa_status_normalize(s->roots->query_root(s->roots->context,
                parent->backend_resource,&info));
            if (status == PXA_STATUS_OK) {
                memcpy(result,info.volume_id.bytes,16);
                pxa_write_u64(result+16,info.mount_epoch);
                pxa_write_u32(result+24,info.rights);
                pxa_write_u64(result+28,info.capabilities);
                pxa_write_u16(result+36,info.max_path_bytes);
                pxa_write_u16(result+38,info.max_segment_bytes);
                pxa_write_u32(result+40,info.max_read_at_bytes);
                result[44]=info.naming; result[45]=info.state;
                pxa_write_u16(result+46,info.space_known);
                pxa_write_u64(result+48,info.total_bytes);
                pxa_write_u64(result+56,info.free_bytes);
                pxa_write_u64(result+64,info.quota_bytes);
                length = sizeof(result);
            }
        }
    } else if (status == PXA_STATUS_OK) {
        size_t prefix = m->opcode == PXA_FS_OPEN_AT ? 14u : 10u;
        pxa_fs_entry_t info; pxa_bytes_t path;
        memset(&info,0,sizeof(info));
        if (m->payload.size < prefix ||
            (size_t)pxa_read_u16(p+prefix-2) != m->payload.size-prefix)
            status = PXA_STATUS_INVALID_ARGUMENT;
        else {
            path = (pxa_bytes_t){p+prefix,m->payload.size-prefix};
            if (!pxa_fs_scoped_path_is_valid(path)) status = PXA_STATUS_INVALID_ARGUMENT;
            else if (m->opcode == PXA_FS_STAT_AT) {
                status = s->roots->stat_at ? pxa_status_normalize(s->roots->stat_at(
                    s->roots->context,parent->backend_resource,path,&info)) : PXA_STATUS_UNSUPPORTED;
                if (status == PXA_STATUS_OK) {
                    memset(result,0,16); result[0]=info.kind;
                    pxa_write_u64(result+8,info.size); length=16;
                }
            } else {
                uint64_t features = 0; uint32_t flags = pxa_read_u32(p+8);
                pxa_fs_resource_slot_t *slot;
                if (!open_flags_valid(flags)) status = PXA_STATUS_INVALID_ARGUMENT;
                else if (!(slot=allocate_resource(s))) status = PXA_STATUS_RESOURCE_LIMIT;
                else {
                    slot->backend = &s->roots->files;
                    status = pxa_status_normalize(s->roots->open_at(s->roots->context,
                        parent->backend_resource,path,flags,&slot->backend_resource,
                        &info,&features));
                    if (status != PXA_STATUS_OK) release_resource(slot);
                    else {
                        slot->kind = info.kind;
                        if (!slot->backend_resource ||
                            (info.kind != PXA_FS_KIND_REGULAR && info.kind != PXA_FS_KIND_DIRECTORY) ||
                            ((flags & PXA_FS_OPEN_DIRECTORY)!=0) != (info.kind==PXA_FS_KIND_DIRECTORY)) {
                            close_resource(slot); status=PXA_STATUS_INTERNAL;
                        } else status=publish_scoped(slot,c,authority,&handle);
                        if (status == PXA_STATUS_OK) {
                            memset(result,0,28); pxa_write_u64(result,handle);
                            result[8]=info.kind; pxa_write_u64(result+12,features);
                            pxa_write_u64(result+20,info.size); length=28;
                        }
                    }
                }
            }
        }
    }
    complete = pxa_request_complete(s->runtime,c,m->request_token,status,
        status == PXA_STATUS_OK ? result : NULL,status == PXA_STATUS_OK ? length : 0);
    if (complete != PXA_STATUS_OK && handle)
        (void)pxa_handle_close64(s->runtime,c,handle);
    return complete == PXA_STATUS_OK ? PXA_STATUS_OK : complete;
}

static int has_media_path(const pxa_fs_roots_backend_t *roots) {
    return roots && roots->struct_size>=offsetof(pxa_fs_roots_backend_t,app_media_path)+sizeof(roots->app_media_path) &&
        (roots->features&PXA_FS_FEATURE_APP_MEDIA_PATH) && roots->app_media_path;
}
static pxa_status_t media_path_control(pxa_fs_service_t *service,pxa_component_t component,const pxa_message_view_t *message) {
    if(message->payload.size)return PXA_STATUS_INVALID_ARGUMENT;
    if(!has_media_path(service->roots))return PXA_STATUS_UNSUPPORTED;
    uint8_t payload[2+PXA_FS_DISPLAY_PATH_MAX];size_t size=0;
    pxa_status_t status=pxa_status_normalize(service->roots->app_media_path(service->roots->context,
        (char *)payload+2,PXA_FS_DISPLAY_PATH_MAX,&size));
    if(status==PXA_STATUS_OK && (!size || size>PXA_FS_DISPLAY_PATH_MAX || memchr(payload+2,0,size)))status=PXA_STATUS_PROTOCOL_ERROR;
    pxa_status_t result=pxa_request_begin_reserved(service->runtime,component,message->request_token,
        PXA_FS_SERVICE_ID,message->opcode,0,status==PXA_STATUS_OK?size+2:0);
    if(result!=PXA_STATUS_OK)return result;
    result=pxa_request_commit(service->runtime,component,message->request_token);
    if(result!=PXA_STATUS_OK)return result;
    if(status==PXA_STATUS_OK)pxa_write_u16(payload,(uint16_t)size);
    return pxa_request_complete(service->runtime,component,message->request_token,status,
        status==PXA_STATUS_OK?payload:NULL,status==PXA_STATUS_OK?size+2:0);
}

static pxa_status_t fs_control(void *context, pxa_runtime_t *runtime,
                               pxa_component_t component,
                               const pxa_message_view_t *message) {
    pxa_fs_service_t *service = (pxa_fs_service_t *)context;
    uint8_t result[96];
    size_t result_size = 0;
    pxa_authority_t authority = 0;
    pxa_handle64_t opened_handle = PXA_HANDLE64_INVALID;
    uint16_t wire_profile = 0;
    pxa_status_t status;
    pxa_status_t complete;
    (void)runtime;
    if (!service_valid(service) || message->request_token == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if(message->opcode==PXA_FS_APP_MEDIA_PATH)return media_path_control(service,component,message);
    if (message->opcode == PXA_FS_OPEN_ROOT ||
        message->opcode == PXA_FS_REQUEST_ACCESS ||
        message->opcode == PXA_FS_OPEN_AT || message->opcode == PXA_FS_STAT_AT ||
        message->opcode == PXA_FS_QUERY_ROOT)
        return scoped_control(service,component,message);
    if (message->opcode < PXA_FS_OPEN ||
        message->opcode > PXA_FS_REPLACE) {
        return PXA_STATUS_UNSUPPORTED;
    }
    status = pxa_component_wire_profile(service->runtime, component,
                                      &wire_profile);
    if (status != PXA_STATUS_OK) return status;
    status = authorize_request(service, component, &authority);
    {
        size_t result_capacity = 0;
        if (message->opcode == PXA_FS_OPEN)
            result_capacity = wire_profile == 1 ? 8u : 4u;
        else if (message->opcode == PXA_FS_STAT)
            result_capacity = 9;
        else if (message->opcode == PXA_FS_SEEK)
            result_capacity = 8;
        else if (message->opcode == PXA_FS_READ_DIRECTORY)
            result_capacity = sizeof(result);
        complete = pxa_request_begin_reserved(
            service->runtime, component, message->request_token,
            PXA_FS_SERVICE_ID, message->opcode,
            status == PXA_STATUS_OK ? authority : 0, result_capacity);
    }
    if (complete != PXA_STATUS_OK) return complete;
    if (status == PXA_STATUS_OK) {
        status = pxa_request_commit(service->runtime, component,
                                    message->request_token);
    }
    if (status == PXA_STATUS_OK && message->opcode == PXA_FS_OPEN) {
        status = handle_open(service, component, authority, wire_profile,
                             message, result,
                             &result_size, &opened_handle);
    } else if (status == PXA_STATUS_OK &&
               message->opcode >= PXA_FS_MAKE_DIRECTORY &&
               (message->opcode <= PXA_FS_STAT || message->opcode == PXA_FS_REPLACE)) {
        status = handle_path_operation(service, message, result, &result_size);
    } else if (status == PXA_STATUS_OK && message->opcode == PXA_FS_SEEK) {
        status = handle_seek(service, component, wire_profile, message,
                             result, &result_size);
    } else if (status == PXA_STATUS_OK) {
        status = handle_read_directory(service, component, wire_profile, message,
                                       result,
                                       sizeof(result), &result_size);
    }
    complete = pxa_request_complete(
        service->runtime, component, message->request_token, status,
        status == PXA_STATUS_OK ? result : NULL,
        status == PXA_STATUS_OK ? result_size : 0);
    if (complete != PXA_STATUS_OK) {
        if (opened_handle != PXA_HANDLE64_INVALID) {
            if (wire_profile == 1)
                (void)pxa_handle_close64(service->runtime, component,
                                         opened_handle);
            else
                (void)pxa_handle_close(service->runtime, component,
                                       (pxa_handle_t)opened_handle);
        }
        (void)pxa_request_cancel(service->runtime, component,
                                 message->request_token);
    }
    return PXA_STATUS_OK;
}

static void fs_component_stopped(void *context,pxa_runtime_t *runtime,
    pxa_component_t component) {
    pxa_fs_service_t *s=context; (void)runtime;
    if(s->picker_token && s->picker_component==component) {
        pxa_request_token_t token=s->picker_token;
        clear_picker(s); s->roots->cancel_access(s->roots->context,component,token);
    }
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
    if (service->roots) operations.features |= PXA_FS_FEATURE_SCOPED_ROOTS;
    if(roots_read_at_enabled(service->roots) || (service->backend.read && service->backend.seek))
        operations.features |= PXA_FS_FEATURE_READ_AT;
    if (service->roots && service->roots->request_access)
        operations.features |= PXA_FS_FEATURE_USER_DIRECTORY_GRANTS;
    if (service->roots && service->roots->struct_size >= offsetof(pxa_fs_roots_backend_t,features)+sizeof(service->roots->features))
        operations.features |= service->roots->features & PXA_FS_FEATURE_APP_MEDIA_ROOT;
    operations.context = service;
    if(has_media_path(service->roots))operations.features|=PXA_FS_FEATURE_APP_MEDIA_PATH;
    operations.control = fs_control;
    operations.component_stopped = fs_component_stopped;
    status = pxa_service_register(service->runtime, &operations);
    if (status == PXA_STATUS_OK) service->registered = 1;
    return status;
}
