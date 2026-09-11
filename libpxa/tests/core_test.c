#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pxa/runtime.h"
#include "pxa/service.h"
#include "pxa/audio.h"
#include "pxa/device.h"
#include "pxa/fs.h"
#include "pxa/lease.h"
#include "pxa/permission.h"
#include "pxa/ipc.h"
#include "pxa/net.h"
#include "pxa/scheduler.h"
#include "pxa/sensor.h"
#include "pxa/storage.h"
#include "pxa/window.h"

typedef struct {
    void *workspace;
    pxa_runtime_t *runtime;
} test_runtime_t;

typedef struct {
    pxa_runtime_t *runtime;
    pxa_component_t component;
    unsigned *close_count;
    int *observed_clean;
} test_resource_t;

typedef struct {
    unsigned controls;
    unsigned stops;
    uint16_t last_opcode;
} test_service_t;

typedef struct {
    unsigned apply_count;
    pxa_status_t next_status;
    pxa_window_configuration_t last;
} test_window_backend_t;

typedef struct {
    pxa_permission_decision_t decisions[2];
    uint8_t present[2];
    pxa_status_t save_status;
    unsigned loads;
    unsigned saves;
} test_permission_store_t;

typedef struct {
    unsigned calls;
    pxa_component_t component;
    uint32_t request_id;
    pxa_status_t status;
} test_permission_prompt_t;

#define TEST_STORAGE_CAPACITY ((size_t)16)
#define TEST_STORAGE_VALUE_CAPACITY ((size_t)64)

typedef struct {
    uint8_t key[PXA_STORAGE_MAX_KEY_BYTES];
    uint8_t value[TEST_STORAGE_VALUE_CAPACITY];
    size_t key_size;
    size_t value_size;
} test_storage_entry_t;

typedef struct {
    test_storage_entry_t entries[TEST_STORAGE_CAPACITY];
    size_t count;
    int corrupt_list_order;
} test_storage_backend_t;

typedef struct {
    uint8_t bytes[64];
    size_t size;
    size_t position;
} test_fs_file_t;

typedef struct {
    size_t position;
} test_fs_directory_t;

typedef struct {
    test_fs_file_t file;
    test_fs_directory_t directory;
    unsigned opens;
    unsigned closes;
    unsigned mutations;
} test_fs_backend_t;

typedef struct {
    unsigned subscribes;
    unsigned unsubscribes;
    unsigned reads;
    int token;
} test_sensor_provider_t;

typedef struct {
    unsigned calls;
} test_device_provider_t;

typedef struct {
    pxa_scheduler_entry_t entries[4];
    size_t count;
    pxa_status_t save_status;
    unsigned loads;
    unsigned saves;
} test_scheduler_store_t;

typedef struct {
    pxa_component_t component;
    uint32_t work_id;
    pxa_work_result_t result;
    unsigned calls;
} test_work_completion_t;

typedef struct {
    uint8_t body[8];
    size_t body_size;
    size_t body_offset;
    uint64_t next_operation;
    unsigned starts;
    unsigned polls;
    unsigned cancels;
    unsigned closes;
    uint16_t expected_method;
    uint16_t expected_headers;
    uint16_t expected_wanted_headers;
    uint16_t expected_body_size;
} test_net_backend_t;

typedef struct {
    unsigned opens;
    unsigned commits;
    unsigned submits;
    unsigned tones;
    unsigned queries;
    unsigned flushes;
    unsigned closes;
    uint64_t session;
} test_audio_backend_t;

static const uint8_t k_close_handle_golden[] = {
    0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01,
};

static const uint8_t k_lease_records_golden[] = {
    0x01, 0x00, 0x02, 0x00, 0x02, 0x00,
    0x02, 0x00, 0x04, 0x00, 0x30, 0x75, 0x00, 0x00,
    0x07, 0x80, 0x15, 0x00, 0x69, 0x67, 0x6e, 0x6f,
    0x72, 0x65, 0x64, 0x2d, 0x62, 0x79, 0x2d, 0x64,
    0x72, 0x61, 0x66, 0x74, 0x2d, 0x68, 0x6f, 0x73,
    0x74,
};

static test_runtime_t make_runtime_with_services(
    uint16_t components, uint16_t requests, uint16_t per_component,
    uint16_t handles, uint16_t events, uint16_t mailbox, uint16_t reserve,
    uint16_t services) {
    pxa_runtime_limits_t limits;
    test_runtime_t result;
    size_t size;
    pxa_runtime_limits_init(&limits);
    limits.max_components = components;
    limits.max_requests = requests;
    limits.max_requests_per_component = per_component;
    limits.max_handles = handles;
    limits.max_events = events;
    limits.mailbox_capacity = mailbox;
    limits.reliable_event_reserve = reserve;
    limits.max_revoked_authorities_per_component = 8;
    limits.max_services = services;
    limits.event_block_size = 32;
    limits.event_block_count = 64;
    size = pxa_runtime_workspace_size(&limits);
    assert(size != 0);
    result.workspace = malloc(size);
    assert(result.workspace != NULL);
    result.runtime = NULL;
    assert(pxa_runtime_init(result.workspace, size, &limits,
                            &result.runtime) == PXA_STATUS_OK);
    assert(result.runtime != NULL);
    return result;
}

static test_runtime_t make_runtime(uint16_t components, uint16_t requests,
                                   uint16_t per_component,
                                   uint16_t handles, uint16_t events,
                                   uint16_t mailbox, uint16_t reserve) {
    return make_runtime_with_services(components, requests, per_component,
                                      handles, events, mailbox, reserve, 24);
}

static void destroy_runtime(test_runtime_t *runtime) {
    pxa_runtime_deinit(runtime->runtime);
    free(runtime->workspace);
    runtime->runtime = NULL;
    runtime->workspace = NULL;
}

static void test_runtime_workspace_contract(void) {
    pxa_runtime_limits_t limits;
    uint8_t *storage;
    size_t required;
    size_t offset;
    pxa_runtime_limits_init(&limits);
    limits.max_components = 2;
    limits.max_requests = 4;
    limits.max_requests_per_component = 2;
    limits.max_handles = 4;
    limits.max_events = 4;
    limits.mailbox_capacity = 2;
    limits.reliable_event_reserve = 1;
    limits.max_revoked_authorities_per_component = 4;
    limits.max_services = 2;
    limits.event_block_size = 32;
    limits.event_block_count = 8;
    required = pxa_runtime_workspace_size(&limits);
    assert(required != 0);
    storage = (uint8_t *)malloc(required + 15u);
    assert(storage != NULL);
    for (offset = 0; offset < 16; ++offset) {
        pxa_runtime_t *runtime = (pxa_runtime_t *)(uintptr_t)1;
        assert(pxa_runtime_init(storage + offset, required - 1u, &limits,
                                &runtime) == PXA_STATUS_INVALID_ARGUMENT &&
               runtime == NULL);
        assert(pxa_runtime_init(storage + offset, required, &limits,
                                &runtime) == PXA_STATUS_OK &&
               runtime != NULL);
        pxa_runtime_deinit(runtime);
    }
    free(storage);
}

static pxa_component_t create_started(pxa_runtime_t *runtime,
                                      uint64_t instance_id) {
    pxa_component_t component = PXA_COMPONENT_INVALID;
    assert(pxa_component_create(runtime, instance_id, &component) ==
           PXA_STATUS_OK);
    assert(component != PXA_COMPONENT_INVALID);
    assert(pxa_component_begin_start(runtime, component) == PXA_STATUS_OK);
    assert(pxa_component_validate_import(runtime, component) == PXA_STATUS_OK);
    assert(pxa_component_finish_start(runtime, component, PXA_STATUS_OK) ==
           PXA_STATUS_OK);
    return component;
}

static size_t make_message(uint8_t *output, size_t capacity, uint16_t service,
                           uint16_t opcode, uint32_t request_id,
                           uint8_t marker) {
    pxa_writer_t writer;
    const void *payload = marker == 0 ? NULL : &marker;
    size_t payload_size = marker == 0 ? 0 : 1;
    pxa_writer_init(&writer, output, capacity);
    assert(pxa_writer_message(&writer, service, opcode, request_id,
                              payload, payload_size) == PXA_STATUS_OK);
    return writer.size;
}

static pxa_status_t usage_test_control(void *context, pxa_runtime_t *runtime,
                                       pxa_component_t component,
                                       const pxa_message_view_t *message) {
    (void)context;
    (void)runtime;
    (void)component;
    (void)message;
    return PXA_STATUS_OK;
}

static void test_runtime_usage(void) {
    test_runtime_t test = make_runtime_with_services(2, 4, 2, 4, 4, 2, 0, 2);
    pxa_runtime_usage_t usage;
    pxa_component_t component;
    pxa_handle_t handle;
    pxa_resource_t resource;
    pxa_service_ops_t service;
    uint8_t message[32];
    size_t message_size;
    size_t event_size;

    assert(pxa_runtime_usage_snapshot(test.runtime, &usage) == PXA_STATUS_OK);
    assert(usage.current_components == 0 && usage.peak_components == 0 &&
           usage.current_requests == 0 && usage.current_handles == 0 &&
           usage.current_events == 0 && usage.current_event_blocks == 0 &&
           usage.registered_services == 0);

    memset(&service, 0, sizeof(service));
    service.struct_size = sizeof(service);
    service.service_id = 2;
    service.major = 0;
    service.minor = 1;
    service.control = usage_test_control;
    assert(pxa_service_register(test.runtime, &service) == PXA_STATUS_OK);
    component = create_started(test.runtime, 99);
    assert(pxa_request_begin(test.runtime, component, 1, 2, 1, 0) ==
           PXA_STATUS_OK);
    memset(&resource, 0, sizeof(resource));
    assert(pxa_handle_open(test.runtime, component, PXA_RESOURCE_FILE, 0,
                           &resource, &handle) == PXA_STATUS_OK);
    message_size = make_message(message, sizeof(message), 2, 1, 0, 1);
    assert(pxa_event_post(test.runtime, component, message, message_size, 1,
                          0) == PXA_STATUS_OK);

    assert(pxa_runtime_usage_snapshot(test.runtime, &usage) == PXA_STATUS_OK);
    assert(usage.current_components == 1 && usage.peak_components == 1 &&
           usage.current_requests == 1 && usage.peak_requests == 1 &&
           usage.current_handles == 1 && usage.peak_handles == 1 &&
           usage.current_events == 1 && usage.peak_events == 1 &&
           usage.current_event_blocks == 1 && usage.peak_event_blocks == 1 &&
           usage.registered_services == 1);

    assert(pxa_request_cancel(test.runtime, component, 1) == PXA_STATUS_OK);
    assert(pxa_handle_close(test.runtime, component, handle) == PXA_STATUS_OK);
    assert(pxa_event_pop(test.runtime, component, message, sizeof(message),
                         &event_size) == PXA_STATUS_OK);
    assert(pxa_event_pop(test.runtime, component, message, sizeof(message),
                         &event_size) == PXA_STATUS_OK);
    assert(pxa_runtime_usage_snapshot(test.runtime, &usage) == PXA_STATUS_OK);
    assert(usage.current_requests == 0 && usage.current_handles == 0 &&
           usage.current_events == 0 && usage.current_event_blocks == 0 &&
           usage.peak_requests == 1 && usage.peak_handles == 1 &&
           usage.peak_events == 2 && usage.peak_event_blocks == 2);

    assert(pxa_component_abort(test.runtime, component, PXA_STOP_NORMAL) ==
           PXA_STATUS_OK);
    assert(pxa_component_remove(test.runtime, component) == PXA_STATUS_OK);
    assert(pxa_runtime_usage_snapshot(test.runtime, &usage) == PXA_STATUS_OK);
    assert(usage.current_components == 0 && usage.peak_components == 1);
    destroy_runtime(&test);
}

static size_t read_head_event(pxa_runtime_t *runtime,
                              pxa_component_t component, uint8_t *output,
                              size_t capacity, pxa_event_view_t *view) {
    size_t read_size = 0;
    assert(pxa_event_peek(runtime, component, view) == PXA_STATUS_OK);
    assert(view->size <= capacity);
    assert(pxa_event_read(runtime, view->token, 0, output, capacity,
                          &read_size) == PXA_STATUS_OK);
    assert(read_size == view->size);
    return read_size;
}

static size_t dispatch_control_completion(
    pxa_runtime_t *runtime, pxa_component_t component,
    const uint8_t *command, size_t command_size, uint8_t *event_bytes,
    size_t event_capacity, pxa_message_view_t *event) {
    pxa_event_view_t view;
    size_t event_size;
    assert(pxa_component_begin_event(runtime, component) == PXA_STATUS_OK);
    assert(pxa_runtime_control(runtime, component, command, command_size) ==
           PXA_STATUS_OK);
    assert(pxa_component_finish_event(runtime, component, 1) ==
           PXA_STATUS_OK);
    event_size = read_head_event(runtime, component, event_bytes,
                                 event_capacity, &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              event) == PXA_STATUS_OK);
    assert(pxa_event_consume(runtime, component, view.token) == PXA_STATUS_OK);
    return event_size;
}

static int32_t dispatch_io(pxa_runtime_t *runtime, pxa_component_t component,
                           pxa_handle_t handle, uint32_t operation,
                           uint8_t *data, size_t size) {
    int32_t result;
    assert(pxa_component_begin_event(runtime, component) == PXA_STATUS_OK);
    result = pxa_runtime_io(runtime, component, handle, operation, data, size);
    assert(pxa_component_finish_event(runtime, component, 1) ==
           PXA_STATUS_OK);
    return result;
}

static void close_resource(void *context) {
    test_resource_t *resource = (test_resource_t *)context;
    pxa_component_snapshot_t snapshot;
    if (resource->close_count != NULL) (*resource->close_count)++;
    if (resource->observed_clean != NULL) {
        *resource->observed_clean =
            pxa_component_snapshot(resource->runtime, resource->component,
                                   &snapshot) == PXA_STATUS_OK &&
            snapshot.state == PXA_COMPONENT_STOPPED &&
            snapshot.pending_requests == 0 && snapshot.open_handles == 0 &&
            snapshot.queued_events == 0;
    }
}

static pxa_status_t service_control(void *context, pxa_runtime_t *runtime,
                                    pxa_component_t component,
                                    const pxa_message_view_t *message) {
    test_service_t *service = (test_service_t *)context;
    (void)runtime;
    (void)component;
    service->controls++;
    service->last_opcode = message->opcode;
    return PXA_STATUS_OK;
}

static void service_stopped(void *context, pxa_runtime_t *runtime,
                            pxa_component_t component) {
    test_service_t *service = (test_service_t *)context;
    pxa_component_snapshot_t snapshot;
    assert(pxa_component_snapshot(runtime, component, &snapshot) ==
           PXA_STATUS_OK);
    assert(snapshot.state == PXA_COMPONENT_STOPPED);
    service->stops++;
}

static int32_t resource_io(void *context, uint32_t operation, uint8_t *data,
                           size_t size) {
    unsigned *calls = (unsigned *)context;
    (*calls)++;
    if (operation != 9 || size < 1) return PXA_STATUS_UNSUPPORTED;
    data[0] = 0x5a;
    return 1;
}

static pxa_status_t window_apply(
    void *context, const pxa_window_configuration_t *configuration) {
    test_window_backend_t *backend = (test_window_backend_t *)context;
    backend->apply_count++;
    backend->last = *configuration;
    return backend->next_status;
}

static int test_bytes_compare(pxa_bytes_t left, pxa_bytes_t right) {
    size_t common = left.size < right.size ? left.size : right.size;
    int result = common == 0 ? 0 : memcmp(left.data, right.data, common);
    if (result != 0) return result;
    if (left.size < right.size) return -1;
    if (left.size > right.size) return 1;
    return 0;
}

static size_t storage_lower_bound(const test_storage_backend_t *backend,
                                  pxa_bytes_t key) {
    size_t begin = 0;
    size_t end = backend->count;
    while (begin < end) {
        size_t middle = begin + (end - begin) / 2;
        pxa_bytes_t candidate = {
            backend->entries[middle].key,
            backend->entries[middle].key_size,
        };
        if (test_bytes_compare(candidate, key) < 0) {
            begin = middle + 1;
        } else {
            end = middle;
        }
    }
    return begin;
}

static pxa_status_t storage_get(void *context, pxa_bytes_t key,
                                uint8_t *output, size_t capacity,
                                size_t *size) {
    test_storage_backend_t *backend = (test_storage_backend_t *)context;
    size_t index = storage_lower_bound(backend, key);
    if (index == backend->count ||
        test_bytes_compare(
            (pxa_bytes_t){backend->entries[index].key,
                          backend->entries[index].key_size},
            key) != 0) {
        return PXA_STATUS_NOT_FOUND;
    }
    if (backend->entries[index].value_size > capacity) {
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    memcpy(output, backend->entries[index].value,
           backend->entries[index].value_size);
    *size = backend->entries[index].value_size;
    return PXA_STATUS_OK;
}

static pxa_status_t storage_set(void *context, pxa_bytes_t key,
                                pxa_bytes_t value) {
    test_storage_backend_t *backend = (test_storage_backend_t *)context;
    size_t index = storage_lower_bound(backend, key);
    int exists = index < backend->count &&
                 test_bytes_compare(
                     (pxa_bytes_t){backend->entries[index].key,
                                   backend->entries[index].key_size},
                     key) == 0;
    if (value.size > TEST_STORAGE_VALUE_CAPACITY) {
        return PXA_STATUS_QUOTA_EXCEEDED;
    }
    if (!exists) {
        if (backend->count == TEST_STORAGE_CAPACITY) {
            return PXA_STATUS_QUOTA_EXCEEDED;
        }
        memmove(&backend->entries[index + 1], &backend->entries[index],
                (backend->count - index) * sizeof(backend->entries[0]));
        backend->count++;
        backend->entries[index].key_size = key.size;
        memcpy(backend->entries[index].key, key.data, key.size);
    }
    backend->entries[index].value_size = value.size;
    memcpy(backend->entries[index].value, value.data, value.size);
    return PXA_STATUS_OK;
}

static pxa_status_t storage_remove(void *context, pxa_bytes_t key) {
    test_storage_backend_t *backend = (test_storage_backend_t *)context;
    size_t index = storage_lower_bound(backend, key);
    if (index == backend->count ||
        test_bytes_compare(
            (pxa_bytes_t){backend->entries[index].key,
                          backend->entries[index].key_size},
            key) != 0) {
        return PXA_STATUS_NOT_FOUND;
    }
    memmove(&backend->entries[index], &backend->entries[index + 1],
            (backend->count - index - 1) * sizeof(backend->entries[0]));
    backend->count--;
    return PXA_STATUS_OK;
}

static pxa_status_t storage_list(void *context, pxa_bytes_t cursor,
                                 pxa_storage_emit_key_fn emit,
                                 void *emit_context) {
    test_storage_backend_t *backend = (test_storage_backend_t *)context;
    size_t index = cursor.size == 0 ? 0 : storage_lower_bound(backend, cursor);
    if (backend->corrupt_list_order && cursor.size == 0 &&
        backend->count >= 2) {
        pxa_status_t status = emit(
            emit_context,
            (pxa_bytes_t){backend->entries[1].key,
                          backend->entries[1].key_size});
        if (status != PXA_STATUS_OK) return status;
        return emit(emit_context,
                    (pxa_bytes_t){backend->entries[0].key,
                                  backend->entries[0].key_size});
    }
    if (index < backend->count && cursor.size != 0 &&
        test_bytes_compare(
            (pxa_bytes_t){backend->entries[index].key,
                          backend->entries[index].key_size},
            cursor) == 0) {
        index++;
    }
    for (; index < backend->count; ++index) {
        pxa_status_t status = emit(
            emit_context,
            (pxa_bytes_t){backend->entries[index].key,
                          backend->entries[index].key_size});
        if (status != PXA_STATUS_OK) return status;
    }
    return PXA_STATUS_OK;
}

static int test_path_equal(pxa_bytes_t path, const char *expected) {
    size_t size = strlen(expected);
    return path.size == size && memcmp(path.data, expected, size) == 0;
}

static pxa_status_t fs_open(void *context, pxa_bytes_t path, uint32_t flags,
                            void **resource, uint8_t *kind) {
    test_fs_backend_t *backend = (test_fs_backend_t *)context;
    backend->opens++;
    if ((flags & PXA_FS_OPEN_DIRECTORY) != 0) {
        if (!test_path_equal(path, "notes")) return PXA_STATUS_NOT_FOUND;
        backend->directory.position = 0;
        *resource = &backend->directory;
        *kind = PXA_FS_KIND_DIRECTORY;
        return PXA_STATUS_OK;
    }
    if (!test_path_equal(path, "notes/today.txt")) {
        return PXA_STATUS_NOT_FOUND;
    }
    if ((flags & PXA_FS_OPEN_TRUNCATE) != 0) backend->file.size = 0;
    backend->file.position = (flags & PXA_FS_OPEN_APPEND) != 0
                                 ? backend->file.size
                                 : 0;
    *resource = &backend->file;
    *kind = PXA_FS_KIND_REGULAR;
    return PXA_STATUS_OK;
}

static pxa_status_t fs_mutation(void *context, pxa_bytes_t path) {
    test_fs_backend_t *backend = (test_fs_backend_t *)context;
    assert(path.size != 0);
    backend->mutations++;
    return PXA_STATUS_OK;
}

static pxa_status_t fs_rename(void *context, pxa_bytes_t source,
                              pxa_bytes_t destination) {
    test_fs_backend_t *backend = (test_fs_backend_t *)context;
    assert(source.size != 0 && destination.size != 0);
    backend->mutations++;
    return PXA_STATUS_OK;
}

static pxa_status_t fs_stat(void *context, pxa_bytes_t path,
                            pxa_fs_entry_t *entry) {
    test_fs_backend_t *backend = (test_fs_backend_t *)context;
    if (test_path_equal(path, "notes")) {
        entry->kind = PXA_FS_KIND_DIRECTORY;
        entry->size = 0;
        return PXA_STATUS_OK;
    }
    if (!test_path_equal(path, "notes/today.txt")) {
        return PXA_STATUS_NOT_FOUND;
    }
    entry->kind = PXA_FS_KIND_REGULAR;
    entry->size = backend->file.size;
    return PXA_STATUS_OK;
}

static pxa_status_t fs_read(void *context, void *resource, uint8_t *output,
                            size_t capacity, size_t *size) {
    test_fs_backend_t *backend = (test_fs_backend_t *)context;
    test_fs_file_t *file = (test_fs_file_t *)resource;
    size_t available;
    size_t copied;
    assert(file == &backend->file);
    available = file->size - file->position;
    copied = capacity < available ? capacity : available;
    memcpy(output, file->bytes + file->position, copied);
    file->position += copied;
    *size = copied;
    return PXA_STATUS_OK;
}

static pxa_status_t fs_write(void *context, void *resource,
                             const uint8_t *input, size_t size,
                             size_t *written) {
    test_fs_backend_t *backend = (test_fs_backend_t *)context;
    test_fs_file_t *file = (test_fs_file_t *)resource;
    assert(file == &backend->file);
    if (size > sizeof(file->bytes) - file->position) {
        return PXA_STATUS_QUOTA_EXCEEDED;
    }
    memcpy(file->bytes + file->position, input, size);
    file->position += size;
    if (file->position > file->size) file->size = file->position;
    *written = size;
    return PXA_STATUS_OK;
}

static pxa_status_t fs_seek(void *context, void *resource, int64_t offset,
                            uint8_t origin, uint64_t *position) {
    test_fs_backend_t *backend = (test_fs_backend_t *)context;
    test_fs_file_t *file = (test_fs_file_t *)resource;
    int64_t base;
    int64_t next;
    assert(file == &backend->file);
    base = origin == PXA_FS_SEEK_START
               ? 0
               : (origin == PXA_FS_SEEK_CURRENT ? (int64_t)file->position
                                                 : (int64_t)file->size);
    if ((offset > 0 && base > INT64_MAX - offset) ||
        (offset < 0 && base < INT64_MIN - offset)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    next = base + offset;
    if (next < 0 || (uint64_t)next > sizeof(file->bytes)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    file->position = (size_t)next;
    *position = file->position;
    return PXA_STATUS_OK;
}

static pxa_status_t fs_read_directory(void *context, void *resource,
                                      pxa_fs_entry_t *entry, uint8_t *end) {
    static const uint8_t name[] = "today.txt";
    test_fs_backend_t *backend = (test_fs_backend_t *)context;
    test_fs_directory_t *directory = (test_fs_directory_t *)resource;
    assert(directory == &backend->directory);
    if (directory->position != 0) {
        *end = 1;
        return PXA_STATUS_OK;
    }
    directory->position++;
    entry->name = (pxa_bytes_t){name, sizeof(name) - 1};
    entry->kind = PXA_FS_KIND_REGULAR;
    entry->size = backend->file.size;
    *end = 0;
    return PXA_STATUS_OK;
}

static void fs_close(void *context, void *resource, uint8_t kind) {
    test_fs_backend_t *backend = (test_fs_backend_t *)context;
    assert((kind == PXA_FS_KIND_REGULAR && resource == &backend->file) ||
           (kind == PXA_FS_KIND_DIRECTORY &&
            resource == &backend->directory));
    backend->closes++;
}

static pxa_status_t sensor_permission_load(
    void *context, pxa_bytes_t identity, pxa_bytes_t name,
    pxa_bytes_t scope, pxa_permission_decision_t *decision) {
    unsigned *loads = (unsigned *)context;
    assert(identity.size != 0 && name.size != 0 && scope.size != 0);
    (*loads)++;
    *decision = PXA_PERMISSION_ALLOW;
    return PXA_STATUS_OK;
}

static pxa_status_t sensor_permission_save(
    void *context, pxa_bytes_t identity, pxa_bytes_t name,
    pxa_bytes_t scope, pxa_permission_decision_t decision) {
    (void)context;
    assert(identity.size != 0 && name.size != 0 && scope.size != 0 &&
           decision <= PXA_PERMISSION_ALLOW);
    return PXA_STATUS_OK;
}

static pxa_status_t sensor_subscribe(void *context, uint16_t descriptor_id,
                                     uint32_t period_ms,
                                     void **subscription) {
    test_sensor_provider_t *provider = (test_sensor_provider_t *)context;
    assert(descriptor_id == 1 && period_ms == 100);
    provider->subscribes++;
    *subscription = &provider->token;
    return PXA_STATUS_OK;
}

static pxa_status_t sensor_read(
    void *context, void *subscription, uint16_t descriptor_id,
    int32_t values[PXA_SENSOR_MAX_DIMENSIONS]) {
    test_sensor_provider_t *provider = (test_sensor_provider_t *)context;
    assert(subscription == &provider->token && descriptor_id == 1);
    provider->reads++;
    values[0] = 21500;
    return PXA_STATUS_OK;
}

static void sensor_unsubscribe(void *context, void *subscription,
                               uint16_t descriptor_id) {
    test_sensor_provider_t *provider = (test_sensor_provider_t *)context;
    assert(subscription == &provider->token && descriptor_id == 1);
    provider->unsubscribes++;
}

static pxa_status_t device_get_mac(void *context, uint16_t kind,
                                   uint8_t output[6], uint32_t *flags) {
    static const uint8_t mac[6] = {0x24, 0x6f, 0x28, 0x70, 0x14, 0x01};
    test_device_provider_t *provider = (test_device_provider_t *)context;
    if (kind != PXA_DEVICE_MAC_KIND_WIFI_STATION_HARDWARE) {
        return PXA_STATUS_NOT_FOUND;
    }
    assert(output != NULL && flags != NULL);
    provider->calls++;
    memcpy(output, mac, sizeof(mac));
    *flags = PXA_DEVICE_MAC_FLAG_HARDWARE;
    return PXA_STATUS_OK;
}

static pxa_status_t scheduler_load(void *context,
                                   pxa_scheduler_entry_t *entries,
                                   size_t capacity, size_t *count) {
    test_scheduler_store_t *store = (test_scheduler_store_t *)context;
    store->loads++;
    *count = store->count;
    if (store->count > capacity) return PXA_STATUS_RESOURCE_LIMIT;
    memcpy(entries, store->entries,
           store->count * sizeof(store->entries[0]));
    return PXA_STATUS_OK;
}

static pxa_status_t scheduler_save(
    void *context, const pxa_scheduler_entry_t *entries, size_t count) {
    test_scheduler_store_t *store = (test_scheduler_store_t *)context;
    store->saves++;
    if (store->save_status != PXA_STATUS_OK) return store->save_status;
    assert(count <= sizeof(store->entries) / sizeof(store->entries[0]));
    memcpy(store->entries, entries, count * sizeof(store->entries[0]));
    store->count = count;
    return PXA_STATUS_OK;
}

static uint64_t scheduler_clock(void *context) {
    return *(uint64_t *)context;
}

static pxa_status_t scheduler_complete_work(void *context,
                                            pxa_component_t component,
                                            uint32_t work_id,
                                            pxa_work_result_t result) {
    test_work_completion_t *completion =
        (test_work_completion_t *)context;
    completion->component = component;
    completion->work_id = work_id;
    completion->result = result;
    completion->calls++;
    return PXA_STATUS_OK;
}

static pxa_status_t net_start(void *context,
                              const pxa_net_request_t *request,
                              uint64_t *operation) {
    static const uint8_t expected_url[] = "https://example.test/hello";
    static const uint8_t expected_origin[] = "https://example.test";
    test_net_backend_t *backend = (test_net_backend_t *)context;
    static const uint8_t post_url[] = "https://example.test?mode=post";
    assert(request->origin.size == sizeof(expected_origin) - 1 &&
           memcmp(request->origin.data, expected_origin,
                  sizeof(expected_origin) - 1) == 0);
    assert(request->struct_size == sizeof(*request));
    assert(request->method == backend->expected_method);
    if (request->method == PXA_NET_METHOD_GET) {
        assert(request->url.size == sizeof(expected_url) - 1 &&
               memcmp(request->url.data, expected_url,
                      sizeof(expected_url) - 1) == 0 &&
               request->max_response_bytes == 2 && request->header_count == 0 &&
               request->body.size == 0);
    } else {
        assert(request->url.size == sizeof(post_url) - 1 &&
               memcmp(request->url.data, post_url, sizeof(post_url) - 1) == 0 &&
               request->max_response_bytes == 8 && request->timeout_ms == 2500 &&
               request->header_count == backend->expected_headers &&
               request->wanted_response_header_count ==
                   backend->expected_wanted_headers &&
               request->body.size == backend->expected_body_size &&
               memcmp(request->body.data, "{}", 2) == 0);
        assert(request->headers[0].name.size == sizeof("content-type") - 1 &&
               memcmp(request->headers[0].name.data, "content-type",
                      sizeof("content-type") - 1) == 0);
    }
    backend->starts++;
    backend->polls = 0;
    backend->body_offset = 0;
    *operation = ++backend->next_operation;
    return PXA_STATUS_OK;
}

static pxa_status_t net_poll(void *context, uint64_t operation,
                             pxa_net_response_t *response) {
    static const uint8_t content_type[] = "text/plain";
    static const uint8_t etag_name[] = "etag";
    static const uint8_t etag_value[] = "\"test\"";
    static const pxa_net_header_t headers[] = {
        {{etag_name, sizeof(etag_name) - 1},
         {etag_value, sizeof(etag_value) - 1}},
    };
    test_net_backend_t *backend = (test_net_backend_t *)context;
    assert(operation != 0 && operation <= backend->next_operation);
    backend->polls++;
    if (backend->polls == 1) return PXA_STATUS_WOULD_BLOCK;
    response->status_code = 200;
    response->content_type =
        (pxa_bytes_t){content_type, sizeof(content_type) - 1};
    response->body_stream = backend;
    if (backend->expected_method == PXA_NET_METHOD_POST) {
        response->headers = headers;
        response->header_count = 1;
        response->body_length = backend->body_size;
        response->flags = PXA_NET_RESPONSE_BODY_PRESENT |
                          PXA_NET_RESPONSE_BODY_LENGTH_KNOWN;
    }
    return PXA_STATUS_OK;
}

static void net_cancel(void *context, uint64_t operation) {
    test_net_backend_t *backend = (test_net_backend_t *)context;
    assert(operation != 0);
    backend->cancels++;
}

static pxa_status_t net_read_body(void *context, void *body_stream,
                                  uint8_t *output, size_t capacity,
                                  size_t *size) {
    test_net_backend_t *backend = (test_net_backend_t *)context;
    size_t remaining;
    size_t copied;
    assert(body_stream == backend);
    remaining = backend->body_size - backend->body_offset;
    copied = capacity < remaining ? capacity : remaining;
    memcpy(output, backend->body + backend->body_offset, copied);
    backend->body_offset += copied;
    *size = copied;
    return PXA_STATUS_OK;
}

static void net_close_body(void *context, void *body_stream) {
    test_net_backend_t *backend = (test_net_backend_t *)context;
    assert(body_stream == backend);
    backend->closes++;
}

static pxa_status_t audio_open(void *context, uint16_t usage,
                               pxa_audio_format_t *format,
                               uint64_t *provider_session) {
    test_audio_backend_t *backend = (test_audio_backend_t *)context;
    assert(usage == PXA_AUDIO_USAGE_MEDIA);
    backend->opens++;
    format->sample_rate = 16000;
    format->channels = 1;
    format->frame_ms = 20;
    *provider_session = backend->session;
    return PXA_STATUS_OK;
}

static pxa_status_t audio_commit(void *context, uint64_t provider_session,
                                 const pxa_audio_graph_t *graph) {
    test_audio_backend_t *backend = (test_audio_backend_t *)context;
    assert(provider_session == backend->session &&
           graph->gain_db_q8 == -256 &&
           graph->route == PXA_AUDIO_ROUTE_SPEAKER &&
           graph->eq_band_count == 1 &&
           graph->eq_bands[0].frequency_hz == 1500 &&
           graph->eq_bands[0].gain_db_q8 == 256 &&
           graph->eq_bands[0].q_q8 == 256);
    backend->commits++;
    return PXA_STATUS_OK;
}

static pxa_status_t audio_submit(void *context, uint64_t provider_session,
                                 const uint8_t *pcm, size_t size) {
    test_audio_backend_t *backend = (test_audio_backend_t *)context;
    static const uint8_t expected[] = {0, 0, 1, 0};
    assert(provider_session == backend->session && size == sizeof(expected) &&
           memcmp(pcm, expected, sizeof(expected)) == 0);
    backend->submits++;
    return PXA_STATUS_OK;
}

static pxa_status_t audio_play_tone(void *context,
                                    uint64_t provider_session,
                                    const pxa_audio_tone_t *tone) {
    test_audio_backend_t *backend = (test_audio_backend_t *)context;
    assert(provider_session == backend->session && tone != NULL &&
           tone->frequency_hz == 440 && tone->duration_ms == 80 &&
           tone->gain_db_q8 == -6 * 256 &&
           tone->waveform == PXA_AUDIO_TONE_TRIANGLE);
    backend->tones++;
    return PXA_STATUS_OK;
}

static void audio_close(void *context, uint64_t provider_session) {
    test_audio_backend_t *backend = (test_audio_backend_t *)context;
    assert(provider_session == backend->session);
    backend->closes++;
}

static pxa_status_t audio_query(void *context, uint64_t provider_session,
                                pxa_audio_state_t *state) {
    test_audio_backend_t *backend = (test_audio_backend_t *)context;
    assert(provider_session == backend->session && state != NULL);
    backend->queries++;
    state->submitted_samples = 320;
    state->accepted_samples = 256;
    state->queued_samples = 64;
    state->flags = PXA_AUDIO_STATE_ACCEPTED_IS_SINK_SUBMITTED;
    return PXA_STATUS_OK;
}

static pxa_status_t audio_flush(void *context, uint64_t provider_session) {
    test_audio_backend_t *backend = (test_audio_backend_t *)context;
    assert(provider_session == backend->session);
    backend->flushes++;
    return PXA_STATUS_OK;
}

static uint64_t lease_clock(void *context) {
    return *(uint64_t *)context;
}

static int permission_slot(pxa_bytes_t name) {
    static const uint8_t fs_name[] = "fs.private";
    static const uint8_t net_name[] = "net.client";
    if (name.size == sizeof(fs_name) - 1 &&
        memcmp(name.data, fs_name, sizeof(fs_name) - 1) == 0) return 0;
    if (name.size == sizeof(net_name) - 1 &&
        memcmp(name.data, net_name, sizeof(net_name) - 1) == 0) return 1;
    return -1;
}

static pxa_status_t permission_load(
    void *context, pxa_bytes_t identity, pxa_bytes_t name,
    pxa_bytes_t scope, pxa_permission_decision_t *decision) {
    test_permission_store_t *store = (test_permission_store_t *)context;
    int slot = permission_slot(name);
    (void)scope;
    assert(identity.size != 0);
    store->loads++;
    if (slot < 0 || !store->present[slot]) return PXA_STATUS_NOT_FOUND;
    *decision = store->decisions[slot];
    return PXA_STATUS_OK;
}

static pxa_status_t permission_save(
    void *context, pxa_bytes_t identity, pxa_bytes_t name,
    pxa_bytes_t scope, pxa_permission_decision_t decision) {
    test_permission_store_t *store = (test_permission_store_t *)context;
    int slot = permission_slot(name);
    (void)scope;
    assert(identity.size != 0 && slot >= 0);
    store->saves++;
    if (store->save_status != PXA_STATUS_OK) return store->save_status;
    store->present[slot] = 1;
    store->decisions[slot] = decision;
    return PXA_STATUS_OK;
}

static pxa_status_t permission_prompt(
    void *context, pxa_component_t component, uint32_t request_id,
    pxa_bytes_t identity, pxa_bytes_t name, pxa_bytes_t scope) {
    test_permission_prompt_t *prompt = (test_permission_prompt_t *)context;
    assert(identity.size != 0 && name.size != 0);
    assert(scope.data != NULL || scope.size == 0);
    prompt->calls++;
    prompt->component = component;
    prompt->request_id = request_id;
    return prompt->status;
}

static size_t make_permission_request(
    uint8_t *output, size_t capacity, uint16_t opcode, uint32_t request_id,
    const uint8_t *name, size_t name_size,
    const uint8_t *scope, size_t scope_size) {
    uint8_t payload[128];
    pxa_writer_t records;
    pxa_writer_t message;
    pxa_writer_init(&records, payload, sizeof(payload));
    assert(pxa_writer_record(&records, 1, name, name_size) == PXA_STATUS_OK);
    if (scope_size != 0) {
        assert(pxa_writer_record(&records, 2, scope, scope_size) ==
               PXA_STATUS_OK);
    }
    pxa_writer_init(&message, output, capacity);
    assert(pxa_writer_message(&message, PXA_PERMISSION_SERVICE_ID, opcode,
                              request_id, payload, records.size) ==
           PXA_STATUS_OK);
    return message.size;
}

static size_t make_device_get_mac(uint8_t *output, size_t capacity,
                                  uint32_t request_id, uint16_t kind,
                                  pxa_handle_t permission_handle) {
    uint8_t payload[16];
    uint8_t value[4];
    pxa_writer_t records;
    pxa_writer_t message;
    pxa_writer_init(&records, payload, sizeof(payload));
    pxa_write_u16(value, kind);
    assert(pxa_writer_record(&records, PXA_DEVICE_TAG_MAC_KIND, value, 2) ==
           PXA_STATUS_OK);
    pxa_write_u32(value, permission_handle);
    assert(pxa_writer_record(&records, PXA_DEVICE_TAG_PERMISSION_HANDLE,
                             value, 4) == PXA_STATUS_OK);
    pxa_writer_init(&message, output, capacity);
    assert(pxa_writer_message(&message, PXA_DEVICE_SERVICE_ID,
                              PXA_DEVICE_GET_MAC, request_id, payload,
                              records.size) == PXA_STATUS_OK);
    return message.size;
}

static size_t make_ipc_call(uint8_t *output, size_t capacity,
                            uint32_t request_id, const char *endpoint,
                            const char *body) {
    uint8_t payload[1100];
    pxa_writer_t records;
    pxa_writer_t message;
    pxa_writer_init(&records, payload, sizeof(payload));
    assert(pxa_writer_record(&records, 1, endpoint, strlen(endpoint)) ==
           PXA_STATUS_OK);
    if (body != NULL) {
        assert(pxa_writer_record(&records, 2, body, strlen(body)) ==
               PXA_STATUS_OK);
    }
    pxa_writer_init(&message, output, capacity);
    assert(pxa_writer_message(&message, PXA_IPC_SERVICE_ID, PXA_IPC_CALL,
                              request_id, payload, records.size) ==
           PXA_STATUS_OK);
    return message.size;
}

static size_t make_ipc_reply(uint8_t *output, size_t capacity,
                             uint32_t request_id, uint32_t call_id,
                             pxa_status_t status, const char *body) {
    uint8_t payload[1100];
    uint8_t value[4];
    pxa_writer_t records;
    pxa_writer_t message;
    pxa_writer_init(&records, payload, sizeof(payload));
    pxa_write_u32(value, call_id);
    assert(pxa_writer_record(&records, 1, value, 4) == PXA_STATUS_OK);
    pxa_write_u32(value, (uint32_t)status);
    assert(pxa_writer_record(&records, 2, value, 4) == PXA_STATUS_OK);
    if (body != NULL) {
        assert(pxa_writer_record(&records, 3, body, strlen(body)) ==
               PXA_STATUS_OK);
    }
    pxa_writer_init(&message, output, capacity);
    assert(pxa_writer_message(&message, PXA_IPC_SERVICE_ID, PXA_IPC_REPLY,
                              request_id, payload, records.size) ==
           PXA_STATUS_OK);
    return message.size;
}

typedef struct {
    pxa_status_t status;
    unsigned calls;
} test_ipc_resolver_t;

typedef struct {
    unsigned allocations;
    unsigned releases;
} test_ipc_allocator_t;

static pxa_status_t test_ipc_resolve(void *context, pxa_bytes_t endpoint,
                                     pxa_component_t *provider) {
    test_ipc_resolver_t *resolver = (test_ipc_resolver_t *)context;
    (void)endpoint;
    assert(resolver != NULL);
    assert(provider != NULL);
    *provider = PXA_COMPONENT_INVALID;
    resolver->calls++;
    return resolver->status;
}

static void *test_ipc_allocate(void *context, size_t size) {
    test_ipc_allocator_t *allocator = (test_ipc_allocator_t *)context;
    void *memory = malloc(size);
    if (memory != NULL) allocator->allocations++;
    return memory;
}

static void test_ipc_release(void *context, void *memory) {
    test_ipc_allocator_t *allocator = (test_ipc_allocator_t *)context;
    allocator->releases++;
    free(memory);
}

static size_t make_lease_acquire(uint8_t *output, size_t capacity,
                                 uint32_t request_id, uint16_t kind,
                                 uint32_t duration_ms) {
    uint8_t payload[32];
    uint8_t value[4];
    pxa_writer_t records;
    pxa_writer_t message;
    pxa_writer_init(&records, payload, sizeof(payload));
    pxa_write_u16(value, kind);
    assert(pxa_writer_record(&records, 1, value, 2) == PXA_STATUS_OK);
    pxa_write_u32(value, duration_ms);
    assert(pxa_writer_record(&records, 2, value, 4) == PXA_STATUS_OK);
    pxa_writer_init(&message, output, capacity);
    assert(pxa_writer_message(&message, PXA_SERVICE_CORE, PXA_LEASE_ACQUIRE,
                              request_id, payload, records.size) ==
           PXA_STATUS_OK);
    return message.size;
}

static size_t make_storage_request(uint8_t *output, size_t capacity,
                                   uint16_t opcode, uint32_t request_id,
                                   const char *key, const char *value) {
    uint8_t payload[160];
    pxa_writer_t records;
    pxa_writer_t message;
    pxa_writer_init(&records, payload, sizeof(payload));
    if (key != NULL) {
        assert(pxa_writer_record(&records, 1, key, strlen(key)) ==
               PXA_STATUS_OK);
    }
    if (value != NULL) {
        assert(pxa_writer_record(&records, 2, value, strlen(value)) ==
               PXA_STATUS_OK);
    }
    pxa_writer_init(&message, output, capacity);
    assert(pxa_writer_message(&message, PXA_STORAGE_SERVICE_ID, opcode,
                              request_id, payload, records.size) ==
           PXA_STATUS_OK);
    return message.size;
}

static size_t make_fs_path_request(uint8_t *output, size_t capacity,
                                   uint16_t opcode, uint32_t request_id,
                                   const char *path,
                                   const char *destination) {
    uint8_t payload[560];
    pxa_writer_t records;
    pxa_writer_t message;
    pxa_writer_init(&records, payload, sizeof(payload));
    assert(pxa_writer_record(&records, 1, path, strlen(path)) ==
           PXA_STATUS_OK);
    if (destination != NULL) {
        assert(pxa_writer_record(&records, 2, destination,
                                 strlen(destination)) == PXA_STATUS_OK);
    }
    pxa_writer_init(&message, output, capacity);
    assert(pxa_writer_message(&message, PXA_FS_SERVICE_ID, opcode, request_id,
                              payload, records.size) == PXA_STATUS_OK);
    return message.size;
}

static size_t make_fs_open(uint8_t *output, size_t capacity,
                           uint32_t request_id, const char *path,
                           uint32_t flags) {
    uint8_t payload[300];
    uint8_t flag_bytes[4];
    pxa_writer_t records;
    pxa_writer_t message;
    pxa_writer_init(&records, payload, sizeof(payload));
    assert(pxa_writer_record(&records, 1, path, strlen(path)) ==
           PXA_STATUS_OK);
    pxa_write_u32(flag_bytes, flags);
    assert(pxa_writer_record(&records, 3, flag_bytes, sizeof(flag_bytes)) ==
           PXA_STATUS_OK);
    pxa_writer_init(&message, output, capacity);
    assert(pxa_writer_message(&message, PXA_FS_SERVICE_ID, PXA_FS_OPEN,
                              request_id, payload, records.size) ==
           PXA_STATUS_OK);
    return message.size;
}

static size_t make_fs_handle_request(uint8_t *output, size_t capacity,
                                     uint16_t opcode, uint32_t request_id,
                                     pxa_handle_t handle) {
    uint8_t payload[13];
    pxa_writer_t message;
    pxa_write_u32(payload, handle);
    pxa_writer_init(&message, output, capacity);
    assert(pxa_writer_message(&message, PXA_FS_SERVICE_ID, opcode, request_id,
                              payload, 4) == PXA_STATUS_OK);
    return message.size;
}

static size_t make_fs_seek(uint8_t *output, size_t capacity,
                           uint32_t request_id, pxa_handle_t handle,
                           int64_t offset, uint8_t origin) {
    uint8_t payload[13];
    uint64_t bits;
    pxa_writer_t message;
    pxa_write_u32(payload, handle);
    memcpy(&bits, &offset, sizeof(bits));
    pxa_write_u64(payload + 4, bits);
    payload[12] = origin;
    pxa_writer_init(&message, output, capacity);
    assert(pxa_writer_message(&message, PXA_FS_SERVICE_ID, PXA_FS_SEEK,
                              request_id, payload, sizeof(payload)) ==
           PXA_STATUS_OK);
    return message.size;
}

static size_t make_sensor_subscribe(uint8_t *output, size_t capacity,
                                    uint32_t request_id, uint16_t sensor_id,
                                    uint32_t period_ms,
                                    pxa_handle_t permission_handle) {
    uint8_t payload[32];
    uint8_t value[4];
    pxa_writer_t records;
    pxa_writer_t message;
    pxa_writer_init(&records, payload, sizeof(payload));
    pxa_write_u16(value, sensor_id);
    assert(pxa_writer_record(&records, 1, value, 2) == PXA_STATUS_OK);
    pxa_write_u32(value, period_ms);
    assert(pxa_writer_record(&records, 2, value, 4) == PXA_STATUS_OK);
    pxa_write_u32(value, permission_handle);
    assert(pxa_writer_record(&records, 3, value, 4) == PXA_STATUS_OK);
    pxa_writer_init(&message, output, capacity);
    assert(pxa_writer_message(&message, PXA_SENSOR_SERVICE_ID,
                              PXA_SENSOR_SUBSCRIBE, request_id, payload,
                              records.size) == PXA_STATUS_OK);
    return message.size;
}

static size_t make_work_cancel(uint8_t *output, size_t capacity,
                               uint32_t request_id, uint32_t id) {
    uint8_t payload[8];
    uint8_t value[4];
    pxa_writer_t records;
    pxa_writer_t message;
    pxa_write_u32(value, id);
    pxa_writer_init(&records, payload, sizeof(payload));
    assert(pxa_writer_record(&records, 4, value, 4) == PXA_STATUS_OK);
    pxa_writer_init(&message, output, capacity);
    assert(pxa_writer_message(&message, PXA_WORK_SERVICE_ID,
                              PXA_WORK_CANCEL, request_id, payload,
                              records.size) == PXA_STATUS_OK);
    return message.size;
}

static size_t make_work_enqueue(uint8_t *output, size_t capacity,
                                uint32_t request_id,
                                const char *component_id, uint32_t delay_ms,
                                uint32_t execution_ms) {
    static const uint8_t input[] = {1, 2, 3};
    uint8_t payload[128];
    uint8_t value[4];
    uint8_t attempts = 2;
    pxa_writer_t records;
    pxa_writer_t message;
    pxa_writer_init(&records, payload, sizeof(payload));
    assert(pxa_writer_record(&records, 1, component_id,
                             strlen(component_id)) == PXA_STATUS_OK);
    pxa_write_u32(value, delay_ms);
    assert(pxa_writer_record(&records, 2, value, 4) == PXA_STATUS_OK);
    pxa_write_u32(value, execution_ms);
    assert(pxa_writer_record(&records, 3, value, 4) == PXA_STATUS_OK);
    assert(pxa_writer_record(&records, 5, input, sizeof(input)) ==
           PXA_STATUS_OK);
    pxa_write_u32(value, 1000);
    assert(pxa_writer_record(&records, 6, value, 4) == PXA_STATUS_OK);
    assert(pxa_writer_record(&records, 7, &attempts, 1) == PXA_STATUS_OK);
    pxa_writer_init(&message, output, capacity);
    assert(pxa_writer_message(&message, PXA_WORK_SERVICE_ID,
                              PXA_WORK_ENQUEUE, request_id, payload,
                              records.size) == PXA_STATUS_OK);
    return message.size;
}

static size_t make_work_complete(uint8_t *output, size_t capacity,
                                 uint32_t request_id, uint32_t work_id,
                                 pxa_work_result_t result) {
    uint8_t payload[16];
    uint8_t value[4];
    pxa_writer_t records;
    pxa_writer_t message;
    pxa_writer_init(&records, payload, sizeof(payload));
    pxa_write_u32(value, work_id);
    assert(pxa_writer_record(&records, 4, value, 4) == PXA_STATUS_OK);
    assert(pxa_writer_record(&records, 9, &result, 1) == PXA_STATUS_OK);
    pxa_writer_init(&message, output, capacity);
    assert(pxa_writer_message(&message, PXA_WORK_SERVICE_ID,
                              PXA_WORK_COMPLETE, request_id, payload,
                              records.size) == PXA_STATUS_OK);
    return message.size;
}

static size_t make_net_fetch(uint8_t *output, size_t capacity,
                             uint32_t request_id,
                             pxa_handle_t permission_handle) {
    static const uint8_t url[] = "https://example.test/hello";
    uint8_t payload[96];
    uint8_t value[4];
    pxa_writer_t records;
    pxa_writer_t message;
    pxa_writer_init(&records, payload, sizeof(payload));
    assert(pxa_writer_record(&records, 1, url, sizeof(url) - 1) ==
           PXA_STATUS_OK);
    pxa_write_u16(value, PXA_NET_METHOD_GET);
    assert(pxa_writer_record(&records, 2, value, 2) == PXA_STATUS_OK);
    pxa_write_u32(value, permission_handle);
    assert(pxa_writer_record(&records, 3, value, 4) == PXA_STATUS_OK);
    pxa_write_u32(value, 2);
    assert(pxa_writer_record(&records, 4, value, 4) == PXA_STATUS_OK);
    pxa_writer_init(&message, output, capacity);
    assert(pxa_writer_message(&message, PXA_NET_SERVICE_ID, PXA_NET_FETCH,
                              request_id, payload, records.size) ==
           PXA_STATUS_OK);
    return message.size;
}

static size_t make_net_http_request(uint8_t *output, size_t capacity,
                                    uint32_t request_id,
                                    pxa_handle_t permission_handle) {
    static const uint8_t url[] = "https://example.test?mode=post";
    static const uint8_t header_name[] = "content-type";
    static const uint8_t header_value[] = "application/json";
    static const uint8_t wanted_header[] = "etag";
    static const uint8_t request_body[] = "{}";
    uint8_t payload[192];
    uint8_t header_payload[64];
    uint8_t value[4];
    pxa_writer_t records;
    pxa_writer_t header;
    pxa_writer_t message;
    pxa_writer_init(&records, payload, sizeof(payload));
    assert(pxa_writer_record(&records, 1, url, sizeof(url) - 1) ==
           PXA_STATUS_OK);
    pxa_write_u16(value, PXA_NET_METHOD_POST);
    assert(pxa_writer_record(&records, 2, value, 2) == PXA_STATUS_OK);
    pxa_write_u32(value, permission_handle);
    assert(pxa_writer_record(&records, 3, value, 4) == PXA_STATUS_OK);
    pxa_write_u32(value, 8);
    assert(pxa_writer_record(&records, 4, value, 4) == PXA_STATUS_OK);
    pxa_write_u32(value, 2500);
    assert(pxa_writer_record(&records, 8, value, 4) == PXA_STATUS_OK);
    pxa_writer_init(&header, header_payload, sizeof(header_payload));
    assert(pxa_writer_record(&header, 1, header_name,
                             sizeof(header_name) - 1) == PXA_STATUS_OK);
    assert(pxa_writer_record(&header, 2, header_value,
                             sizeof(header_value) - 1) == PXA_STATUS_OK);
    assert(pxa_writer_record(&records, 9, header_payload, header.size) ==
           PXA_STATUS_OK);
    assert(pxa_writer_record(&records, 10, request_body,
                             sizeof(request_body) - 1) == PXA_STATUS_OK);
    assert(pxa_writer_record(&records, 11, wanted_header,
                             sizeof(wanted_header) - 1) == PXA_STATUS_OK);
    pxa_writer_init(&message, output, capacity);
    assert(pxa_writer_message(&message, PXA_NET_SERVICE_ID,
                              PXA_NET_HTTP_REQUEST, request_id, payload,
                              records.size) == PXA_STATUS_OK);
    return message.size;
}

static size_t make_audio_open(uint8_t *output, size_t capacity,
                              uint32_t request_id,
                              pxa_handle_t permission_handle) {
    uint8_t payload[24];
    uint8_t value[4];
    pxa_writer_t records;
    pxa_writer_t message;
    pxa_writer_init(&records, payload, sizeof(payload));
    pxa_write_u32(value, permission_handle);
    assert(pxa_writer_record(&records, 1, value, 4) == PXA_STATUS_OK);
    pxa_write_u16(value, PXA_AUDIO_USAGE_MEDIA);
    assert(pxa_writer_record(&records, 2, value, 2) == PXA_STATUS_OK);
    pxa_writer_init(&message, output, capacity);
    assert(pxa_writer_message(&message, PXA_AUDIO_SERVICE_ID,
                              PXA_AUDIO_OPEN_SESSION, request_id, payload,
                              records.size) == PXA_STATUS_OK);
    return message.size;
}

static size_t make_audio_graph(uint8_t *output, size_t capacity,
                               uint32_t request_id,
                               pxa_handle_t session_handle) {
    uint8_t payload[48];
    uint8_t value[6];
    pxa_writer_t records;
    pxa_writer_t message;
    pxa_writer_init(&records, payload, sizeof(payload));
    pxa_write_u32(value, session_handle);
    assert(pxa_writer_record(&records, 1, value, 4) == PXA_STATUS_OK);
    pxa_write_u16(value, UINT16_C(0xff00));
    assert(pxa_writer_record(&records, 2, value, 2) == PXA_STATUS_OK);
    pxa_write_u16(value, 1500);
    pxa_write_u16(value + 2, 256);
    pxa_write_u16(value + 4, 256);
    assert(pxa_writer_record(&records, 3, value, 6) == PXA_STATUS_OK);
    pxa_write_u16(value, PXA_AUDIO_ROUTE_SPEAKER);
    assert(pxa_writer_record(&records, 4, value, 2) == PXA_STATUS_OK);
    pxa_writer_init(&message, output, capacity);
    assert(pxa_writer_message(&message, PXA_AUDIO_SERVICE_ID,
                              PXA_AUDIO_COMMIT_GRAPH, request_id, payload,
                              records.size) == PXA_STATUS_OK);
    return message.size;
}

static size_t make_audio_session_command(uint8_t *output, size_t capacity,
                                         uint16_t opcode,
                                         uint32_t request_id,
                                         pxa_handle_t session_handle) {
    uint8_t payload[8];
    uint8_t value[4];
    pxa_writer_t records;
    pxa_writer_t message;
    pxa_writer_init(&records, payload, sizeof(payload));
    pxa_write_u32(value, session_handle);
    assert(pxa_writer_record(&records, 1, value, 4) == PXA_STATUS_OK);
    pxa_writer_init(&message, output, capacity);
    assert(pxa_writer_message(&message, PXA_AUDIO_SERVICE_ID, opcode,
                              request_id, payload, records.size) ==
           PXA_STATUS_OK);
    return message.size;
}

static void test_wire(void) {
    pxa_message_view_t message;
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint8_t encoded[32];
    pxa_writer_t writer;
    size_t record_count = 0;
    int saw_optional = 0;

    assert(pxa_message_decode(k_close_handle_golden,
                              sizeof(k_close_handle_golden),
                              PXA_MAX_CONTROL_MESSAGE,
                              &message) == PXA_STATUS_OK);
    assert(message.service == 1 && message.opcode == 2 &&
           message.request_id == 0 && message.payload.size == 4);
    assert(pxa_read_u32(message.payload.data) == UINT32_C(0x01020304));
    assert(pxa_message_decode(k_close_handle_golden,
                              sizeof(k_close_handle_golden) - 1,
                              PXA_MAX_CONTROL_MESSAGE,
                              &message) == PXA_STATUS_INVALID_ARGUMENT);

    pxa_record_iterator_init(
        &iterator,
        (pxa_bytes_t){k_lease_records_golden, sizeof(k_lease_records_golden)});
    while (pxa_record_next(&iterator, &record) == PXA_STATUS_OK) {
        record_count++;
        saw_optional |= record.optional != 0;
    }
    assert(record_count == 3 && saw_optional);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_WOULD_BLOCK);

    pxa_writer_init(&writer, encoded, sizeof(encoded));
    assert(pxa_writer_message(&writer, 1, 2, 0,
                              k_close_handle_golden + 12, 4) == PXA_STATUS_OK);
    assert(writer.size == sizeof(k_close_handle_golden));
    assert(memcmp(encoded, k_close_handle_golden, writer.size) == 0);

    pxa_writer_init(&writer, encoded, sizeof(encoded));
    assert(pxa_writer_u64(&writer, UINT64_C(0x0102030405060708)) ==
           PXA_STATUS_OK);
    assert(writer.size == 8 &&
           pxa_read_u64(encoded) == UINT64_C(0x0102030405060708));
}

static void test_lifecycle(void) {
    test_runtime_t test = make_runtime(2, 4, 2, 2, 4, 2, 1);
    pxa_component_t component = PXA_COMPONENT_INVALID;
    pxa_component_snapshot_t snapshot;
    assert(pxa_component_create(test.runtime, 7, &component) == PXA_STATUS_OK);
    assert(pxa_component_validate_import(test.runtime, component) ==
           PXA_STATUS_BAD_STATE);
    assert(pxa_component_begin_start(test.runtime, component) == PXA_STATUS_OK);
    assert(pxa_component_validate_import(test.runtime, component) ==
           PXA_STATUS_OK);
    assert(pxa_component_finish_start(test.runtime, component,
                                      PXA_STATUS_OK) == PXA_STATUS_OK);
    assert(pxa_component_begin_event(test.runtime, component) == PXA_STATUS_OK);
    assert(pxa_component_request_stop(test.runtime, component,
                                      PXA_STOP_NORMAL) == PXA_STATUS_OK);
    assert(pxa_component_begin_stop(test.runtime, component) ==
           PXA_STATUS_BAD_STATE);
    assert(pxa_component_finish_event(test.runtime, component, 1) ==
           PXA_STATUS_OK);
    assert(pxa_component_begin_stop(test.runtime, component) == PXA_STATUS_OK);
    assert(pxa_component_validate_import(test.runtime, component) ==
           PXA_STATUS_BAD_STATE);
    assert(pxa_component_finish_stop(test.runtime, component) == PXA_STATUS_OK);
    assert(pxa_component_snapshot(test.runtime, component, &snapshot) ==
           PXA_STATUS_OK);
    assert(snapshot.state == PXA_COMPONENT_STOPPED &&
           snapshot.stop_reason == PXA_STOP_NORMAL);
    assert(pxa_component_remove(test.runtime, component) == PXA_STATUS_OK);
    assert(pxa_component_snapshot(test.runtime, component, &snapshot) ==
           PXA_STATUS_NOT_FOUND);
    destroy_runtime(&test);
}

static void test_requests_and_mailbox(void) {
    test_runtime_t test = make_runtime(1, 4, 2, 2, 8, 2, 1);
    pxa_component_t component = create_started(test.runtime, 1);
    pxa_component_snapshot_t snapshot;
    pxa_event_view_t view;
    pxa_message_view_t message;
    uint8_t event[64];
    uint8_t transient[32];
    const uint8_t result[] = {0xaa, 0xbb};
    size_t transient_size = make_message(transient, sizeof(transient),
                                         4, 0x8001, 0, 1);
    size_t event_size;

    assert(pxa_request_begin(test.runtime, component, 0, 5, 1, 0) ==
           PXA_STATUS_INVALID_ARGUMENT);
    assert(pxa_request_begin(test.runtime, component, 7, 5, 1, 0) ==
           PXA_STATUS_OK);
    assert(pxa_request_begin(test.runtime, component, 7, 5, 1, 0) ==
           PXA_STATUS_BUSY);
    assert(pxa_request_begin(test.runtime, component, 8, 5, 2, 0) ==
           PXA_STATUS_OK);
    assert(pxa_request_begin(test.runtime, component, 9, 5, 3, 0) ==
           PXA_STATUS_RESOURCE_LIMIT);
    assert(pxa_event_post(test.runtime, component, transient, transient_size,
                          0, 99) == PXA_STATUS_OK);
    assert(pxa_request_complete(test.runtime, component, 7, PXA_STATUS_OK,
                                result, sizeof(result)) == PXA_STATUS_OK);
    assert(pxa_request_complete(test.runtime, component, 8,
                                PXA_STATUS_CANCELLED, NULL, 0) ==
           PXA_STATUS_OK);
    assert(pxa_component_snapshot(test.runtime, component, &snapshot) ==
           PXA_STATUS_OK);
    assert(snapshot.pending_requests == 1 && snapshot.queued_events == 2);

    assert(pxa_event_peek(test.runtime, component, &view) == PXA_STATUS_OK);
    assert(pxa_event_consume(test.runtime, component, view.token) ==
           PXA_STATUS_OK);
    event_size = read_head_event(test.runtime, component, event,
                                 sizeof(event), &view);
    assert(pxa_message_decode(event, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &message) == PXA_STATUS_OK);
    assert(message.request_id == 7 && message.payload.size == 6 &&
           (int32_t)pxa_read_u32(message.payload.data) == PXA_STATUS_OK);
    assert(pxa_event_consume(test.runtime, component, view.token) ==
           PXA_STATUS_OK);
    event_size = read_head_event(test.runtime, component, event,
                                 sizeof(event), &view);
    assert(pxa_message_decode(event, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &message) == PXA_STATUS_OK);
    assert(message.request_id == 8 && message.payload.size == 4 &&
           (int32_t)pxa_read_u32(message.payload.data) ==
               PXA_STATUS_CANCELLED);
    assert(pxa_event_consume(test.runtime, component, view.token) ==
           PXA_STATUS_OK);
    assert(pxa_event_peek(test.runtime, component, &view) ==
           PXA_STATUS_WOULD_BLOCK);
    destroy_runtime(&test);
}

static void test_request_table_collision_and_completion_order(void) {
    test_runtime_t test = make_runtime(1, 4, 4, 1, 6, 2, 1);
    pxa_component_t component = create_started(test.runtime, 1);
    pxa_component_snapshot_t snapshot;
    pxa_event_view_t view;
    pxa_message_view_t completion;
    uint8_t event[64];
    const uint8_t payload[] = {0x44};
    size_t event_size;

    assert(pxa_request_begin(test.runtime, component, 1, 9, 10, 0) ==
           PXA_STATUS_OK);
    assert(pxa_request_begin(test.runtime, component, 9, 9, 11, 0) ==
           PXA_STATUS_OK);
    assert(pxa_request_is_active(test.runtime, component, 1));
    assert(pxa_request_is_active(test.runtime, component, 9));
    assert(pxa_request_commit(test.runtime, component, 9) == PXA_STATUS_OK);
    assert(pxa_request_cancel(test.runtime, component, 9) == PXA_STATUS_OK);
    assert(pxa_request_is_active(test.runtime, component, 9));

    assert(pxa_request_complete(test.runtime, component, 1, PXA_STATUS_OK,
                                payload, sizeof(payload)) == PXA_STATUS_OK);
    assert(pxa_request_complete(test.runtime, component, 9, PXA_STATUS_OK,
                                NULL, 0) == PXA_STATUS_OK);
    assert(pxa_component_snapshot(test.runtime, component, &snapshot) ==
           PXA_STATUS_OK);
    assert(snapshot.pending_requests == 0 && snapshot.queued_events == 2);

    event_size = read_head_event(test.runtime, component, event,
                                 sizeof(event), &view);
    assert(pxa_message_decode(event, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &completion) == PXA_STATUS_OK);
    assert(completion.request_id == 1 && completion.opcode == 10 &&
           completion.payload.size == 5 &&
           completion.payload.data[4] == payload[0]);
    assert(pxa_event_consume(test.runtime, component, view.token) ==
           PXA_STATUS_OK);

    event_size = read_head_event(test.runtime, component, event,
                                 sizeof(event), &view);
    assert(pxa_message_decode(event, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &completion) == PXA_STATUS_OK);
    assert(completion.request_id == 9 && completion.opcode == 11 &&
           completion.payload.size == 4);
    assert(pxa_event_consume(test.runtime, component, view.token) ==
           PXA_STATUS_OK);
    destroy_runtime(&test);
}

static void test_coalescing_and_reserve(void) {
    test_runtime_t test = make_runtime(1, 1, 1, 1, 8, 3, 1);
    pxa_component_t component = create_started(test.runtime, 1);
    uint8_t first[32];
    uint8_t replacement[32];
    uint8_t second[32];
    uint8_t blocked[32];
    uint8_t reliable[32];
    uint8_t output[32];
    size_t first_size = make_message(first, sizeof(first), 3, 0x8001, 0, 1);
    size_t replacement_size = make_message(replacement, sizeof(replacement),
                                           3, 0x8001, 0, 2);
    size_t second_size = make_message(second, sizeof(second),
                                      3, 0x8001, 0, 3);
    size_t blocked_size = make_message(blocked, sizeof(blocked),
                                       3, 0x8001, 0, 4);
    size_t reliable_size = make_message(reliable, sizeof(reliable),
                                        2, 0x8001, 0, 5);
    pxa_event_view_t view;
    pxa_message_view_t message;
    size_t output_size;
    assert(pxa_event_post(test.runtime, component, first, first_size,
                          0, 100) == PXA_STATUS_OK);
    assert(pxa_event_post(test.runtime, component, replacement,
                          replacement_size, 0, 100) == PXA_STATUS_OK);
    assert(pxa_event_post(test.runtime, component, second, second_size,
                          0, 101) == PXA_STATUS_OK);
    assert(pxa_event_post(test.runtime, component, blocked, blocked_size,
                          0, 102) == PXA_STATUS_BUSY);
    assert(pxa_event_post(test.runtime, component, reliable, reliable_size,
                          1, 0) == PXA_STATUS_OK);
    output_size = read_head_event(test.runtime, component, output,
                                  sizeof(output), &view);
    assert(pxa_message_decode(output, output_size, PXA_MAX_CONTROL_MESSAGE,
                              &message) == PXA_STATUS_OK);
    assert(message.payload.size == 1 && message.payload.data[0] == 2);
    {
        size_t popped_size = 0;
        assert(pxa_event_pop(test.runtime, component, output, output_size - 1u,
                             &popped_size) == PXA_STATUS_RESOURCE_LIMIT &&
               popped_size == output_size);
        assert(pxa_event_pop(test.runtime, component, output, sizeof(output),
                             &popped_size) == PXA_STATUS_OK &&
               popped_size == output_size);
        assert(pxa_message_decode(output, popped_size, PXA_MAX_CONTROL_MESSAGE,
                                  &message) == PXA_STATUS_OK &&
               message.payload.data[0] == 2);
    }
    destroy_runtime(&test);
}

static void test_event_block_boundaries_and_stale_tokens(void) {
    test_runtime_t test = make_runtime(1, 1, 1, 1, 4, 2, 1);
    pxa_component_t component = create_started(test.runtime, 1);
    uint8_t payload[90];
    uint8_t message[128];
    uint8_t slice[40];
    pxa_writer_t writer;
    pxa_event_view_t first;
    pxa_event_view_t second;
    size_t read_size = 0;
    size_t index;

    for (index = 0; index < sizeof(payload); ++index) {
        payload[index] = (uint8_t)(index + 1u);
    }
    pxa_writer_init(&writer, message, sizeof(message));
    assert(pxa_writer_message(&writer, 7, 0x8001, 0, payload,
                              sizeof(payload)) == PXA_STATUS_OK);
    assert(writer.size > 3u * 32u);

    assert(pxa_event_post(test.runtime, component, message, writer.size,
                          1, 0) == PXA_STATUS_OK);
    assert(pxa_event_peek(test.runtime, component, &first) == PXA_STATUS_OK);
    assert(pxa_event_read(test.runtime, first.token, 29, slice, sizeof(slice),
                          &read_size) == PXA_STATUS_OK);
    assert(read_size == sizeof(slice));
    assert(memcmp(slice, message + 29, sizeof(slice)) == 0);
    assert(pxa_event_read(test.runtime, first.token, writer.size, NULL, 0,
                          &read_size) == PXA_STATUS_OK &&
           read_size == 0);

    assert(pxa_event_consume(test.runtime, component, first.token) ==
           PXA_STATUS_OK);
    assert(pxa_event_read(test.runtime, first.token, 0, slice, sizeof(slice),
                          &read_size) == PXA_STATUS_NOT_FOUND);
    assert(pxa_event_post_message(
               test.runtime, component, 7, 0x8001, 0,
               (pxa_bytes_t){payload, sizeof(payload)}, 1, 0) ==
           PXA_STATUS_OK);
    assert(pxa_event_peek(test.runtime, component, &second) == PXA_STATUS_OK);
    assert(second.token != first.token);
    assert(second.size == writer.size);
    assert(pxa_event_read(test.runtime, second.token, 29, slice, sizeof(slice),
                          &read_size) == PXA_STATUS_OK);
    assert(read_size == sizeof(slice));
    assert(memcmp(slice, message + 29, sizeof(slice)) == 0);
    destroy_runtime(&test);
}

static void test_event_generation_exhaustion(void) {
    test_runtime_t test = make_runtime(1, 1, 1, 1, 1, 1, 1);
    pxa_component_t component = create_started(test.runtime, 1);
    uint8_t message[32];
    size_t message_size = make_message(message, sizeof(message), 1, 1, 0, 0);
    pxa_event_token_t previous = 0;
    uint32_t generation;

    for (generation = 1; generation <= UINT16_MAX; ++generation) {
        pxa_event_view_t view;
        assert(pxa_event_post(test.runtime, component, message, message_size,
                              1, 0) == PXA_STATUS_OK);
        assert(pxa_event_peek(test.runtime, component, &view) == PXA_STATUS_OK);
        assert(view.token != previous &&
               (uint16_t)(view.token >> 16) == generation);
        assert(pxa_event_consume(test.runtime, component, view.token) ==
               PXA_STATUS_OK);
        previous = view.token;
    }
    assert(pxa_event_post(test.runtime, component, message, message_size, 1,
                          0) == PXA_STATUS_RESOURCE_LIMIT);
    destroy_runtime(&test);
}

static void test_handles_authority_and_cleanup(void) {
    test_runtime_t test = make_runtime(1, 4, 4, 2, 8, 4, 2);
    pxa_component_t component = create_started(test.runtime, 1);
    unsigned close_count = 0;
    int observed_clean = 0;
    test_resource_t context = {
        test.runtime, component, &close_count, NULL,
    };
    test_resource_t inspecting = {
        test.runtime, component, &close_count, &observed_clean,
    };
    pxa_resource_t resource = {&context, NULL, close_resource};
    pxa_resource_t inspect_resource = {&inspecting, NULL, close_resource};
    pxa_resource_t output;
    pxa_handle_t handle = PXA_HANDLE_INVALID;
    pxa_handle_t inspect_handle = PXA_HANDLE_INVALID;
    pxa_component_snapshot_t snapshot;

    assert(pxa_handle_open(test.runtime, component, PXA_RESOURCE_SENSOR, 44,
                           &resource, &handle) == PXA_STATUS_OK);
    assert(pxa_handle_get(test.runtime, component, handle,
                          PXA_RESOURCE_SENSOR, &output) == PXA_STATUS_OK);
    assert(output.context == &context);
    assert(pxa_request_begin(test.runtime, component, 10, 8, 1, 44) ==
           PXA_STATUS_OK);
    assert(pxa_request_begin(test.runtime, component, 11, 5, 2, 44) ==
           PXA_STATUS_OK);
    assert(pxa_request_commit(test.runtime, component, 11) == PXA_STATUS_OK);
    assert(pxa_authority_revoke(test.runtime, component, 44) == PXA_STATUS_OK);
    assert(close_count == 1);
    assert(pxa_handle_get(test.runtime, component, handle,
                          PXA_RESOURCE_SENSOR, &output) ==
           PXA_STATUS_NOT_FOUND);
    assert(pxa_request_begin(test.runtime, component, 12, 8, 1, 44) ==
           PXA_STATUS_DENIED);
    assert(pxa_request_complete(test.runtime, component, 11, PXA_STATUS_OK,
                                NULL, 0) == PXA_STATUS_OK);
    assert(pxa_handle_open(test.runtime, component, PXA_RESOURCE_LEASE, 0,
                           &inspect_resource, &inspect_handle) == PXA_STATUS_OK);
    assert(pxa_component_abort(test.runtime, component, PXA_STOP_FAULT) ==
           PXA_STATUS_OK);
    assert(observed_clean && close_count == 2);
    assert(pxa_component_snapshot(test.runtime, component, &snapshot) ==
           PXA_STATUS_OK);
    assert(snapshot.state == PXA_COMPONENT_STOPPED &&
           snapshot.pending_requests == 0 && snapshot.open_handles == 0 &&
           snapshot.queued_events == 0);
    destroy_runtime(&test);
}

static void test_handle_generation_exhaustion(void) {
    test_runtime_t test = make_runtime(1, 1, 1, 1, 2, 1, 1);
    pxa_component_t component = create_started(test.runtime, 1);
    pxa_resource_t resource = {NULL, NULL, NULL};
    pxa_handle_t previous = 0;
    uint32_t generation;
    for (generation = 1; generation <= UINT16_MAX; ++generation) {
        pxa_handle_t handle = PXA_HANDLE_INVALID;
        assert(pxa_handle_open(test.runtime, component, PXA_RESOURCE_FILE, 0,
                               &resource, &handle) == PXA_STATUS_OK);
        assert(handle != previous && (uint16_t)(handle >> 16) == generation);
        assert(pxa_handle_close(test.runtime, component, handle) ==
               PXA_STATUS_OK);
        previous = handle;
    }
    previous = 123;
    assert(pxa_handle_open(test.runtime, component, PXA_RESOURCE_FILE, 0,
                           &resource, &previous) == PXA_STATUS_RESOURCE_LIMIT);
    assert(previous == PXA_HANDLE_INVALID);
    destroy_runtime(&test);
}

static void test_service_dispatch_and_io(void) {
    test_runtime_t test = make_runtime(1, 2, 2, 2, 4, 2, 1);
    pxa_component_t component = create_started(test.runtime, 1);
    test_service_t state = {0, 0, 0};
    pxa_service_ops_t service;
    pxa_resource_ops_t io_ops;
    pxa_resource_t resource;
    pxa_handle_t handle = PXA_HANDLE_INVALID;
    unsigned io_calls = 0;
    uint8_t message[32];
    uint8_t io_byte = 0;
    size_t message_size;

    memset(&service, 0, sizeof(service));
    service.struct_size = sizeof(service);
    service.service_id = 33;
    service.major = 0;
    service.minor = 1;
    service.context = &state;
    service.control = service_control;
    service.component_stopped = service_stopped;
    assert(pxa_service_register(test.runtime, &service) == PXA_STATUS_OK);
    assert(pxa_service_register(test.runtime, &service) == PXA_STATUS_BAD_STATE);

    io_ops.struct_size = sizeof(io_ops);
    io_ops.io = resource_io;
    resource.context = &io_calls;
    resource.operations = &io_ops;
    resource.close = NULL;
    assert(pxa_handle_open(test.runtime, component, PXA_RESOURCE_STREAM, 0,
                           &resource, &handle) == PXA_STATUS_OK);

    message_size = make_message(message, sizeof(message), 33, 7, 0, 0);
    assert(pxa_runtime_control(test.runtime, component, message, message_size) ==
           PXA_STATUS_BAD_STATE);
    assert(pxa_component_begin_event(test.runtime, component) == PXA_STATUS_OK);
    assert(pxa_runtime_control(test.runtime, component, message, message_size) ==
           PXA_STATUS_OK);
    assert(state.controls == 1 && state.last_opcode == 7);
    assert(pxa_runtime_io(test.runtime, component, handle, 9, &io_byte, 1) == 1);
    assert(io_byte == 0x5a && io_calls == 1);
    assert(pxa_component_finish_event(test.runtime, component, 1) ==
           PXA_STATUS_OK);
    assert(pxa_component_abort(test.runtime, component, PXA_STOP_NORMAL) ==
           PXA_STATUS_OK);
    assert(state.stops == 1);
    destroy_runtime(&test);
}

static void test_service_registry_collision_and_capacity(void) {
    test_runtime_t test = make_runtime_with_services(1, 2, 2, 1, 2, 1, 1, 2);
    pxa_component_t component = create_started(test.runtime, 1);
    test_service_t first_state = {0, 0, 0};
    test_service_t second_state = {0, 0, 0};
    test_service_t rejected_state = {0, 0, 0};
    pxa_service_ops_t first;
    pxa_service_ops_t second;
    pxa_service_ops_t rejected;
    uint8_t message[32];
    size_t message_size;

    memset(&first, 0, sizeof(first));
    first.struct_size = sizeof(first);
    first.service_id = 33;
    first.major = 0;
    first.minor = 1;
    first.context = &first_state;
    first.control = service_control;
    first.component_stopped = service_stopped;
    second = first;
    second.service_id = 37;
    second.context = &second_state;
    rejected = first;
    rejected.service_id = 41;
    rejected.context = &rejected_state;

    assert(pxa_service_register(test.runtime, &first) == PXA_STATUS_OK);
    assert(pxa_service_register(test.runtime, &second) == PXA_STATUS_OK);
    assert(pxa_service_register(test.runtime, &second) ==
           PXA_STATUS_BAD_STATE);
    assert(pxa_service_register(test.runtime, &rejected) ==
           PXA_STATUS_RESOURCE_LIMIT);

    assert(pxa_component_begin_event(test.runtime, component) == PXA_STATUS_OK);
    message_size = make_message(message, sizeof(message), 33, 3, 0, 0);
    assert(pxa_runtime_control(test.runtime, component, message, message_size) ==
           PXA_STATUS_OK);
    message_size = make_message(message, sizeof(message), 37, 5, 0, 0);
    assert(pxa_runtime_control(test.runtime, component, message, message_size) ==
           PXA_STATUS_OK);
    message_size = make_message(message, sizeof(message), 41, 7, 0, 0);
    assert(pxa_runtime_control(test.runtime, component, message, message_size) ==
           PXA_STATUS_UNSUPPORTED);
    assert(pxa_component_finish_event(test.runtime, component, 1) ==
           PXA_STATUS_OK);
    assert(first_state.controls == 1 && first_state.last_opcode == 3);
    assert(second_state.controls == 1 && second_state.last_opcode == 5);
    assert(rejected_state.controls == 0);

    assert(pxa_component_abort(test.runtime, component, PXA_STOP_NORMAL) ==
           PXA_STATUS_OK);
    assert(first_state.stops == 1 && second_state.stops == 1);
    assert(rejected_state.stops == 0);
    destroy_runtime(&test);
    assert(first_state.stops == 1 && second_state.stops == 1);
}

static void test_window_service(void) {
    test_runtime_t test = make_runtime(3, 8, 8, 2, 12, 6, 1);
    pxa_component_t component = create_started(test.runtime, 1);
    pxa_component_t other = create_started(test.runtime, 2);
    pxa_component_t overflow = create_started(test.runtime, 3);
    size_t window_size = pxa_window_service_workspace_size(2);
    void *window_workspace = malloc(window_size);
    pxa_window_service_t *window = NULL;
    test_window_backend_t backend_state;
    pxa_window_backend_t backend;
    pxa_window_snapshot_t snapshot;
    pxa_window_snapshot_t same_snapshot;
    pxa_window_snapshot_t observed_snapshot;
    pxa_window_configuration_t configuration;
    pxa_version_t selected;
    pxa_writer_t records;
    pxa_writer_t message_writer;
    pxa_event_view_t event_view;
    pxa_message_view_t event;
    uint8_t record_bytes[64];
    uint8_t message[96];
    uint8_t event_bytes[192];
    uint8_t value;
    size_t event_size;
    uint8_t close_requested;

    assert(window_workspace != NULL);
    assert(pxa_window_service_init(window_workspace, window_size, test.runtime,
                                   2, &window) == PXA_STATUS_OK);
    assert(pxa_window_service_register(window) == PXA_STATUS_OK);
    memset(&backend_state, 0, sizeof(backend_state));
    backend_state.next_status = PXA_STATUS_OK;
    backend.struct_size = sizeof(backend);
    backend.context = &backend_state;
    backend.apply = window_apply;
    assert(pxa_window_bind(window, component, &backend) == PXA_STATUS_OK);
    assert(pxa_window_bind(window, component, &backend) ==
           PXA_STATUS_BAD_STATE);
    assert(pxa_window_bind(window, other, &backend) == PXA_STATUS_OK);
    assert(pxa_window_bind(window, overflow, &backend) ==
           PXA_STATUS_RESOURCE_LIMIT);
    assert(pxa_window_get_configuration(window, other, &configuration) ==
           PXA_STATUS_OK);

    assert(pxa_window_negotiate_version((pxa_version_range_t){0, 1, 0, 9},
                                        &selected) == PXA_STATUS_OK);
    assert(selected.major == 0 && selected.minor == 1);
    assert(pxa_window_negotiate_version((pxa_version_range_t){0, 2, 1, 0},
                                        &selected) == PXA_STATUS_UNSUPPORTED);
    assert(pxa_window_negotiate_version((pxa_version_range_t){1, 0, 1, 9},
                                        &selected) == PXA_STATUS_UNSUPPORTED);

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.logical_width = 320;
    snapshot.logical_height = 240;
    snapshot.pixel_width = 640;
    snapshot.pixel_height = 480;
    snapshot.density_numerator = 2;
    snapshot.density_denominator = 1;
    snapshot.safe_insets.top = 24;
    snapshot.safe_insets.bottom = 12;
    snapshot.system_bar_insets.top = 20;
    snapshot.system_bar_insets.bottom = 10;
    snapshot.orientation = PXA_WINDOW_ORIENTATION_LANDSCAPE;
    snapshot.focused = 1;
    assert(pxa_window_update_snapshot(window, component, &snapshot) ==
           PXA_STATUS_OK);
    snapshot.logical_width = 400;
    assert(pxa_window_update_snapshot(window, component, &snapshot) ==
           PXA_STATUS_OK);
    assert(pxa_window_get_snapshot(window, component, &observed_snapshot) ==
           PXA_STATUS_OK);
    assert(observed_snapshot.revision == 2 &&
           observed_snapshot.logical_width == 400);

    memset(&same_snapshot, 0xa5, sizeof(same_snapshot));
    same_snapshot.logical_width = snapshot.logical_width;
    same_snapshot.logical_height = snapshot.logical_height;
    same_snapshot.pixel_width = snapshot.pixel_width;
    same_snapshot.pixel_height = snapshot.pixel_height;
    same_snapshot.density_numerator = snapshot.density_numerator;
    same_snapshot.density_denominator = snapshot.density_denominator;
    same_snapshot.safe_insets = snapshot.safe_insets;
    same_snapshot.system_bar_insets = snapshot.system_bar_insets;
    same_snapshot.orientation = snapshot.orientation;
    same_snapshot.focused = snapshot.focused;
    assert(pxa_window_update_snapshot(window, component, &same_snapshot) ==
           PXA_STATUS_OK);
    assert(pxa_window_get_snapshot(window, component, &observed_snapshot) ==
           PXA_STATUS_OK && observed_snapshot.revision == 2);

    event_size = read_head_event(test.runtime, component, event_bytes,
                                 sizeof(event_bytes), &event_view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    assert(event.service == PXA_WINDOW_SERVICE_ID &&
           event.opcode == PXA_WINDOW_METRICS_CHANGED);
    assert(pxa_event_consume(test.runtime, component, event_view.token) ==
           PXA_STATUS_OK);

    pxa_writer_init(&records, record_bytes, sizeof(record_bytes));
    value = 1;
    assert(pxa_writer_record(&records, 1, &value, 1) == PXA_STATUS_OK);
    value = PXA_WINDOW_BAR_TRANSIENT;
    assert(pxa_writer_record(&records, 2, &value, 1) == PXA_STATUS_OK);
    value = PXA_WINDOW_ICON_DARK;
    assert(pxa_writer_record(&records, 4, &value, 1) == PXA_STATUS_OK);
    value = 9;
    assert(pxa_writer_record(&records, UINT16_C(0x800f), &value, 1) ==
           PXA_STATUS_OK);
    pxa_writer_init(&message_writer, message, sizeof(message));
    assert(pxa_writer_message(&message_writer, PXA_WINDOW_SERVICE_ID,
                              PXA_WINDOW_CONFIGURE, 0, record_bytes,
                              records.size) == PXA_STATUS_OK);
    assert(pxa_component_begin_event(test.runtime, component) == PXA_STATUS_OK);
    assert(pxa_runtime_control(test.runtime, component, message,
                               message_writer.size) == PXA_STATUS_OK);
    assert(pxa_component_finish_event(test.runtime, component, 1) ==
           PXA_STATUS_OK);
    assert(backend_state.apply_count == 1 && backend_state.last.edge_to_edge &&
           backend_state.last.status_bar_mode == PXA_WINDOW_BAR_TRANSIENT &&
           backend_state.last.status_bar_icons == PXA_WINDOW_ICON_DARK);
    assert(pxa_window_get_configuration(window, component, &configuration) ==
           PXA_STATUS_OK);
    assert(configuration.edge_to_edge);

    pxa_writer_init(&records, record_bytes, sizeof(record_bytes));
    value = 0;
    assert(pxa_writer_record(&records, 1, &value, 1) == PXA_STATUS_OK);
    pxa_writer_init(&message_writer, message, sizeof(message));
    assert(pxa_writer_message(&message_writer, PXA_WINDOW_SERVICE_ID,
                              PXA_WINDOW_CONFIGURE, 0, record_bytes,
                              records.size) == PXA_STATUS_OK);
    backend_state.next_status = PXA_STATUS_DENIED;
    assert(pxa_component_begin_event(test.runtime, component) == PXA_STATUS_OK);
    assert(pxa_runtime_control(test.runtime, component, message,
                               message_writer.size) == PXA_STATUS_DENIED);
    assert(pxa_component_finish_event(test.runtime, component, 0) ==
           PXA_STATUS_OK);
    assert(pxa_window_get_configuration(window, component, &configuration) ==
           PXA_STATUS_OK && configuration.edge_to_edge);

    pxa_writer_init(&message_writer, message, sizeof(message));
    assert(pxa_writer_message(&message_writer, PXA_WINDOW_SERVICE_ID,
                              PXA_WINDOW_GET_SNAPSHOT, 51, NULL, 0) ==
           PXA_STATUS_OK);
    assert(pxa_component_begin_event(test.runtime, component) == PXA_STATUS_OK);
    assert(pxa_runtime_control(test.runtime, component, message,
                               message_writer.size) == PXA_STATUS_OK);
    assert(pxa_component_finish_event(test.runtime, component, 1) ==
           PXA_STATUS_OK);
    event_size = read_head_event(test.runtime, component, event_bytes,
                                 sizeof(event_bytes), &event_view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    assert(event.request_id == 51 && event.payload.size > 4 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK);
    assert(pxa_event_consume(test.runtime, component, event_view.token) ==
           PXA_STATUS_OK);

    assert(pxa_window_queue_back(window, component) == PXA_STATUS_OK);
    event_size = read_head_event(test.runtime, component, event_bytes,
                                 sizeof(event_bytes), &event_view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    assert(event.opcode == PXA_WINDOW_BACK_REQUESTED);
    assert(pxa_window_resolve_event_result(&event, 0, &close_requested) ==
           PXA_STATUS_OK && close_requested);
    assert(pxa_window_resolve_event_result(&event, 1, &close_requested) ==
               PXA_STATUS_OK && !close_requested);

    assert(pxa_window_unbind(window, component) == PXA_STATUS_OK);
    assert(pxa_window_get_configuration(window, component, &configuration) ==
           PXA_STATUS_NOT_FOUND);
    assert(pxa_window_get_configuration(window, other, &configuration) ==
           PXA_STATUS_OK);
    assert(pxa_window_unbind(window, component) == PXA_STATUS_NOT_FOUND);
    assert(pxa_window_bind(window, component, &backend) == PXA_STATUS_OK);
    assert(pxa_component_abort(test.runtime, component, PXA_STOP_NORMAL) ==
           PXA_STATUS_OK);
    assert(pxa_window_get_configuration(window, component, &configuration) ==
           PXA_STATUS_NOT_FOUND);
    assert(pxa_window_get_configuration(window, other, &configuration) ==
           PXA_STATUS_OK);
    assert(pxa_component_abort(test.runtime, other, PXA_STOP_NORMAL) ==
           PXA_STATUS_OK);
    assert(pxa_window_get_configuration(window, other, &configuration) ==
           PXA_STATUS_NOT_FOUND);
    destroy_runtime(&test);
    free(window_workspace);
}

static void test_lease_service(void) {
    test_runtime_t test = make_runtime(2, 8, 4, 8, 10, 3, 1);
    pxa_component_t component = create_started(test.runtime, 7);
    pxa_component_t other = create_started(test.runtime, 8);
    pxa_lease_limits_t limits;
    pxa_lease_service_t *lease = NULL;
    void *lease_workspace;
    size_t lease_size;
    uint64_t now_ms = 100;
    uint8_t command[64];
    uint8_t event_bytes[64];
    size_t command_size;
    size_t event_size;
    pxa_event_view_t view;
    pxa_message_view_t event;
    pxa_handle_t handle;
    pxa_handle_t other_handle;
    pxa_resource_t resource;
    pxa_component_t affected[2];
    size_t affected_count;

    pxa_lease_limits_init(&limits);
    limits.allowed_kinds = 1;
    limits.max_leases_per_component = 1;
    limits.max_leases = 2;
    limits.default_duration_ms = 10;
    limits.max_duration_ms = 100;
    limits.clock_context = &now_ms;
    limits.clock = lease_clock;
    lease_size = pxa_lease_service_workspace_size(&limits);
    lease_workspace = malloc(lease_size);
    assert(lease_workspace != NULL);
    assert(pxa_lease_service_init(lease_workspace, lease_size, test.runtime,
                                  &limits, &lease) == PXA_STATUS_OK);
    assert(pxa_lease_service_register(lease) == PXA_STATUS_OK);

    command_size = make_lease_acquire(command, sizeof(command), 11, 1, 20);
    assert(pxa_component_begin_event(test.runtime, component) == PXA_STATUS_OK);
    assert(pxa_runtime_control(test.runtime, component, command, command_size) ==
           PXA_STATUS_OK);
    assert(pxa_component_finish_event(test.runtime, component, 1) ==
           PXA_STATUS_OK);
    assert(pxa_lease_has_active(lease));
    assert(pxa_lease_active_components(lease, affected, 2, &affected_count) ==
               PXA_STATUS_OK &&
           affected_count == 1 && affected[0] == component);
    event_size = read_head_event(test.runtime, component, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    assert(event.opcode == PXA_LEASE_ACQUIRE && event.request_id == 11 &&
           event.payload.size == 12 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           pxa_read_u16(event.payload.data + 4) == 4 &&
           pxa_read_u16(event.payload.data + 6) == 4);
    handle = pxa_read_u32(event.payload.data + 8);
    assert(pxa_handle_get(test.runtime, component, handle,
                          PXA_RESOURCE_LEASE, &resource) == PXA_STATUS_OK);
    assert(pxa_event_consume(test.runtime, component, view.token) ==
           PXA_STATUS_OK);

    now_ms = 119;
    assert(pxa_lease_revoke_expired(lease, affected, 2, &affected_count) ==
           PXA_STATUS_OK && affected_count == 0);
    now_ms = 120;
    assert(pxa_lease_revoke_expired(lease, affected, 2, &affected_count) ==
           PXA_STATUS_OK && affected_count == 1 && affected[0] == component);
    assert(!pxa_lease_has_active(lease));
    assert(pxa_handle_get(test.runtime, component, handle,
                          PXA_RESOURCE_LEASE, &resource) ==
           PXA_STATUS_NOT_FOUND);
    event_size = read_head_event(test.runtime, component, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    assert(event.opcode == PXA_LEASE_REVOKED && event.payload.size == 8 &&
           pxa_read_u32(event.payload.data) == handle &&
           (int32_t)pxa_read_u32(event.payload.data + 4) ==
               PXA_STATUS_CANCELLED);
    assert(pxa_event_consume(test.runtime, component, view.token) ==
           PXA_STATUS_OK);

    command_size = make_lease_acquire(command, sizeof(command), 12, 2, 5);
    assert(pxa_component_begin_event(test.runtime, component) == PXA_STATUS_OK);
    assert(pxa_runtime_control(test.runtime, component, command, command_size) ==
           PXA_STATUS_OK);
    assert(pxa_component_finish_event(test.runtime, component, 1) ==
           PXA_STATUS_OK);
    event_size = read_head_event(test.runtime, component, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    assert((int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_DENIED);
    assert(pxa_event_consume(test.runtime, component, view.token) ==
           PXA_STATUS_OK);

    command_size = make_lease_acquire(command, sizeof(command), 13, 1, 10);
    assert(pxa_component_begin_event(test.runtime, component) == PXA_STATUS_OK);
    assert(pxa_runtime_control(test.runtime, component, command, command_size) ==
           PXA_STATUS_OK);
    assert(pxa_component_finish_event(test.runtime, component, 1) ==
           PXA_STATUS_OK);
    event_size = read_head_event(test.runtime, component, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    handle = pxa_read_u32(event.payload.data + 8);
    assert(pxa_event_consume(test.runtime, component, view.token) ==
           PXA_STATUS_OK);
    assert(pxa_handle_close(test.runtime, component, handle) == PXA_STATUS_OK);
    now_ms += 10;
    assert(pxa_lease_revoke_expired(lease, affected, 2, &affected_count) ==
           PXA_STATUS_OK && affected_count == 0);
    assert(pxa_event_peek(test.runtime, component, &view) ==
           PXA_STATUS_WOULD_BLOCK);

    command_size = make_lease_acquire(command, sizeof(command), 14, 1, 10);
    assert(pxa_component_begin_event(test.runtime, component) == PXA_STATUS_OK);
    assert(pxa_runtime_control(test.runtime, component, command, command_size) ==
           PXA_STATUS_OK);
    assert(pxa_component_finish_event(test.runtime, component, 1) ==
           PXA_STATUS_OK);
    event_size = read_head_event(test.runtime, component, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    handle = pxa_read_u32(event.payload.data + 8);
    assert(pxa_event_consume(test.runtime, component, view.token) ==
           PXA_STATUS_OK);

    command_size = make_lease_acquire(command, sizeof(command), 15, 1, 10);
    assert(pxa_component_begin_event(test.runtime, other) == PXA_STATUS_OK);
    assert(pxa_runtime_control(test.runtime, other, command, command_size) ==
           PXA_STATUS_OK);
    assert(pxa_component_finish_event(test.runtime, other, 1) ==
           PXA_STATUS_OK);
    event_size = read_head_event(test.runtime, other, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK &&
           event.opcode == PXA_LEASE_ACQUIRE && event.request_id == 15 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK);
    other_handle = pxa_read_u32(event.payload.data + 8);
    assert(pxa_event_consume(test.runtime, other, view.token) ==
           PXA_STATUS_OK);

    assert(pxa_lease_revoke_all(lease, PXA_STATUS_DENIED, affected, 2,
                                &affected_count) == PXA_STATUS_OK &&
           affected_count == 2 && affected[0] == other &&
           affected[1] == component &&
           !pxa_lease_has_active(lease));
    event_size = read_head_event(test.runtime, component, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    assert(event.opcode == PXA_LEASE_REVOKED && event.payload.size == 8 &&
           pxa_read_u32(event.payload.data) == handle &&
           (int32_t)pxa_read_u32(event.payload.data + 4) == PXA_STATUS_DENIED);
    assert(pxa_event_consume(test.runtime, component, view.token) ==
           PXA_STATUS_OK);
    event_size = read_head_event(test.runtime, other, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    assert(event.opcode == PXA_LEASE_REVOKED && event.payload.size == 8 &&
           pxa_read_u32(event.payload.data) == other_handle &&
           (int32_t)pxa_read_u32(event.payload.data + 4) == PXA_STATUS_DENIED);
    assert(pxa_event_consume(test.runtime, other, view.token) ==
           PXA_STATUS_OK);

    destroy_runtime(&test);
    free(lease_workspace);
}

static void test_permission_service(void) {
    static const uint8_t identity[] = "42.sample";
    static const uint8_t fs_name[] = "fs.private";
    static const uint8_t net_name[] = "net.client";
    static const uint8_t net_scope[] = "api";
    uint8_t max_name[96];
    uint8_t max_scope[1024];
    pxa_permission_declaration_t declarations[3];
    pxa_permission_config_t config;
    test_permission_store_t store;
    test_permission_prompt_t prompt;
    test_runtime_t test = make_runtime(1, 8, 8, 8, 12, 6, 2);
    pxa_component_t component = create_started(test.runtime, 7);
    size_t permission_size;
    void *permission_workspace;
    pxa_permission_service_t *permission = NULL;
    uint8_t command[192];
    size_t command_size;
    uint8_t event_bytes[192];
    size_t event_size;
    pxa_event_view_t view;
    pxa_message_view_t event;
    pxa_handle_t permission_handle;
    pxa_handle_t net_permission_handle;
    pxa_authority_t authority = 0;
    pxa_handle_t file_handle = PXA_HANDLE_INVALID;
    unsigned close_count = 0;
    test_resource_t file_context = {
        test.runtime, component, &close_count, NULL,
    };
    pxa_resource_t file_resource = {&file_context, NULL, close_resource};
    pxa_component_t affected[1];
    size_t affected_count;
    unsigned iteration;

    memset(&store, 0, sizeof(store));
    store.save_status = PXA_STATUS_OK;
    memset(&prompt, 0, sizeof(prompt));
    prompt.status = PXA_STATUS_OK;
    memset(max_name, 'n', sizeof(max_name));
    memset(max_scope, 's', sizeof(max_scope));
    declarations[0].name = (pxa_bytes_t){fs_name, sizeof(fs_name) - 1};
    declarations[0].scope = (pxa_bytes_t){NULL, 0};
    declarations[0].required = 1;
    declarations[1].name = (pxa_bytes_t){net_name, sizeof(net_name) - 1};
    declarations[1].scope = (pxa_bytes_t){net_scope, sizeof(net_scope) - 1};
    declarations[1].required = 0;
    declarations[2].name = (pxa_bytes_t){max_name, sizeof(max_name)};
    declarations[2].scope = (pxa_bytes_t){max_scope, sizeof(max_scope)};
    declarations[2].required = 0;
    memset(&config, 0, sizeof(config));
    config.struct_size = sizeof(config);
    config.app_identity = (pxa_bytes_t){identity, sizeof(identity) - 1};
    config.declarations = declarations;
    config.declaration_count = 3;
    config.max_authorities = 4;
    config.max_pending_prompts = 1;
    config.prompt_context = &prompt;
    config.prompt = permission_prompt;
    config.store.struct_size = sizeof(config.store);
    config.store.context = &store;
    config.store.load = permission_load;
    config.store.save = permission_save;
    {
        pxa_permission_config_t empty_config = config;
        pxa_permission_service_t *empty_permission = NULL;
        void *empty_workspace;
        size_t empty_size;
        empty_config.declarations = NULL;
        empty_config.declaration_count = 0;
        empty_size = pxa_permission_service_workspace_size(&empty_config);
        assert(empty_size != 0);
        empty_workspace = malloc(empty_size);
        assert(empty_workspace != NULL);
        assert(pxa_permission_service_init(
                   empty_workspace, empty_size, test.runtime, &empty_config,
                   &empty_permission) == PXA_STATUS_OK);
        assert(pxa_permission_policy_load(empty_permission) == PXA_STATUS_OK);
        assert(pxa_permission_can_activate(empty_permission));
        assert(store.loads == 0);
        free(empty_workspace);
    }
    permission_size = pxa_permission_service_workspace_size(&config);
    permission_workspace = malloc(permission_size);
    assert(permission_workspace != NULL);
    assert(pxa_permission_service_init(permission_workspace, permission_size,
                                       test.runtime, &config,
                                       &permission) == PXA_STATUS_OK);
    assert(pxa_permission_policy_load(permission) == PXA_STATUS_OK);
    assert(store.loads == 3 && !pxa_permission_can_activate(permission));
    assert(pxa_permission_set(
               permission,
               (pxa_bytes_t){fs_name, sizeof(fs_name) - 1},
               (pxa_bytes_t){NULL, 0}, PXA_PERMISSION_ALLOW) ==
           PXA_STATUS_OK);
    assert(pxa_permission_can_activate(permission));
    assert(pxa_permission_get(
               permission,
               (pxa_bytes_t){net_name, sizeof(net_name) - 1},
               (pxa_bytes_t){net_scope, sizeof(net_scope) - 1}) ==
           PXA_PERMISSION_DENY);
    store.save_status = PXA_STATUS_INTERNAL;
    assert(pxa_permission_set(
               permission,
               (pxa_bytes_t){fs_name, sizeof(fs_name) - 1},
               (pxa_bytes_t){NULL, 0}, PXA_PERMISSION_DENY) ==
           PXA_STATUS_INTERNAL);
    assert(pxa_permission_get(
               permission,
               (pxa_bytes_t){fs_name, sizeof(fs_name) - 1},
               (pxa_bytes_t){NULL, 0}) == PXA_PERMISSION_ALLOW);
    store.save_status = PXA_STATUS_OK;
    assert(pxa_permission_service_register(permission) == PXA_STATUS_OK);

    command_size = make_permission_request(
        command, sizeof(command), PXA_PERMISSION_ACQUIRE, 11,
        fs_name, sizeof(fs_name) - 1, NULL, 0);
    assert(pxa_component_begin_event(test.runtime, component) == PXA_STATUS_OK);
    assert(pxa_runtime_control(test.runtime, component, command, command_size) ==
           PXA_STATUS_OK);
    assert(pxa_component_finish_event(test.runtime, component, 1) ==
           PXA_STATUS_OK);
    event_size = read_head_event(test.runtime, component, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    assert(event.request_id == 11 && event.payload.size == 8 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK);
    permission_handle = pxa_read_u32(event.payload.data + 4);
    assert(pxa_event_consume(test.runtime, component, view.token) ==
           PXA_STATUS_OK);
    assert(pxa_permission_resolve(
               permission, component, permission_handle,
               (pxa_bytes_t){fs_name, sizeof(fs_name) - 1},
               (pxa_bytes_t){NULL, 0}, &authority) == PXA_STATUS_OK);
    assert(authority != 0);
    assert(pxa_handle_open(test.runtime, component, PXA_RESOURCE_FILE,
                           authority, &file_resource, &file_handle) ==
           PXA_STATUS_OK);

    /* Closing an acquired permission must immediately recycle its bounded
     * authority slot and revoke resources derived from it. Exercise more
     * cycles than max_authorities. */
    for (iteration = 0; iteration < 10; ++iteration) {
        pxa_handle_t closed_handle = permission_handle;
        assert(pxa_handle_close(test.runtime, component, closed_handle) ==
               PXA_STATUS_OK);
        assert(pxa_permission_resolve(
                   permission, component, closed_handle,
                   (pxa_bytes_t){fs_name, sizeof(fs_name) - 1},
                   (pxa_bytes_t){NULL, 0}, &authority) ==
               PXA_STATUS_NOT_FOUND);
        if (iteration == 0) {
            assert(pxa_handle_get(test.runtime, component, file_handle,
                                  PXA_RESOURCE_FILE,
                                  &(pxa_resource_t){0}) ==
                   PXA_STATUS_NOT_FOUND);
            assert(close_count == 1);
            file_handle = PXA_HANDLE_INVALID;
        }
        command_size = make_permission_request(
            command, sizeof(command), PXA_PERMISSION_ACQUIRE,
            20u + iteration, fs_name, sizeof(fs_name) - 1, NULL, 0);
        assert(pxa_component_begin_event(test.runtime, component) ==
               PXA_STATUS_OK);
        assert(pxa_runtime_control(test.runtime, component, command,
                                   command_size) == PXA_STATUS_OK);
        assert(pxa_component_finish_event(test.runtime, component, 1) ==
               PXA_STATUS_OK);
        event_size = read_head_event(test.runtime, component, event_bytes,
                                     sizeof(event_bytes), &view);
        assert(pxa_message_decode(event_bytes, event_size,
                                  PXA_MAX_CONTROL_MESSAGE,
                                  &event) == PXA_STATUS_OK);
        assert(event.request_id == 20u + iteration && event.payload.size == 8 &&
               (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK);
        permission_handle = pxa_read_u32(event.payload.data + 4);
        assert(pxa_event_consume(test.runtime, component, view.token) ==
               PXA_STATUS_OK);
    }

    command_size = make_permission_request(
        command, sizeof(command), PXA_PERMISSION_ACQUIRE, 12,
        net_name, sizeof(net_name) - 1, net_scope, sizeof(net_scope) - 1);
    assert(pxa_component_begin_event(test.runtime, component) == PXA_STATUS_OK);
    assert(pxa_runtime_control(test.runtime, component, command, command_size) ==
           PXA_STATUS_OK);
    assert(pxa_component_finish_event(test.runtime, component, 1) ==
           PXA_STATUS_OK);
    assert(prompt.calls == 1 && prompt.component == component &&
           prompt.request_id == 12);
    assert(pxa_event_peek(test.runtime, component, &view) ==
           PXA_STATUS_WOULD_BLOCK);
    store.save_status = PXA_STATUS_IO_ERROR;
    assert(pxa_permission_prompt_complete(permission, component, 12,
                                          PXA_PERMISSION_ALLOW) ==
           PXA_STATUS_IO_ERROR);
    assert(pxa_event_peek(test.runtime, component, &view) ==
           PXA_STATUS_WOULD_BLOCK);
    store.save_status = PXA_STATUS_OK;
    assert(pxa_permission_prompt_complete(permission, component, 12,
                                          PXA_PERMISSION_ALLOW) ==
           PXA_STATUS_OK);
    event_size = read_head_event(test.runtime, component, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    assert(event.request_id == 12 && event.payload.size == 8 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK);
    net_permission_handle = pxa_read_u32(event.payload.data + 4);
    assert(pxa_event_consume(test.runtime, component, view.token) ==
           PXA_STATUS_OK);
    assert(pxa_permission_resolve(
               permission, component, net_permission_handle,
               (pxa_bytes_t){net_name, sizeof(net_name) - 1},
               (pxa_bytes_t){net_scope, sizeof(net_scope) - 1},
               &authority) == PXA_STATUS_OK);
    assert(authority != 0);
    assert(pxa_permission_set(
               permission, (pxa_bytes_t){net_name, sizeof(net_name) - 1},
               (pxa_bytes_t){net_scope, sizeof(net_scope) - 1},
               PXA_PERMISSION_DENY) == PXA_STATUS_OK);
    command_size = make_permission_request(
        command, sizeof(command), PXA_PERMISSION_ACQUIRE, 13,
        net_name, sizeof(net_name) - 1, net_scope, sizeof(net_scope) - 1);
    assert(pxa_component_begin_event(test.runtime, component) == PXA_STATUS_OK);
    assert(pxa_runtime_control(test.runtime, component, command, command_size) ==
           PXA_STATUS_OK);
    assert(pxa_component_finish_event(test.runtime, component, 1) ==
           PXA_STATUS_OK);
    assert(prompt.calls == 2 && prompt.request_id == 13);
    assert(pxa_permission_prompt_complete(permission, component, 13,
                                          PXA_PERMISSION_DENY) ==
           PXA_STATUS_OK);
    event_size = read_head_event(test.runtime, component, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    assert(event.request_id == 13 && event.payload.size == 4 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_DENIED);
    assert(pxa_event_consume(test.runtime, component, view.token) ==
           PXA_STATUS_OK);

    assert(pxa_permission_resolve(
               permission, component, permission_handle,
               (pxa_bytes_t){net_name, sizeof(net_name) - 1},
               (pxa_bytes_t){net_scope, sizeof(net_scope) - 1},
               &authority) == PXA_STATUS_DENIED);
    assert(authority == 0);
    assert(pxa_permission_resolve(
               permission, component, permission_handle,
               (pxa_bytes_t){fs_name, sizeof(fs_name) - 1},
               (pxa_bytes_t){NULL, 0}, &authority) == PXA_STATUS_OK);
    assert(pxa_handle_open(test.runtime, component, PXA_RESOURCE_FILE,
                           authority, &file_resource, &file_handle) ==
           PXA_STATUS_OK);

    assert(pxa_permission_revoke(
               permission,
               (pxa_bytes_t){fs_name, sizeof(fs_name) - 1},
               (pxa_bytes_t){NULL, 0}, affected, 1,
               &affected_count) == PXA_STATUS_OK);
    assert(affected_count == 1 && affected[0] == component && close_count == 2);
    assert(pxa_handle_get(test.runtime, component, file_handle,
                          PXA_RESOURCE_FILE, &file_resource) ==
           PXA_STATUS_NOT_FOUND);
    event_size = read_head_event(test.runtime, component, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    assert(event.service == PXA_PERMISSION_SERVICE_ID &&
           event.opcode == PXA_PERMISSION_REVOKED);
    assert(pxa_event_consume(test.runtime, component, view.token) ==
           PXA_STATUS_OK);

    assert(pxa_permission_revoke(
               permission, (pxa_bytes_t){max_name, sizeof(max_name)},
               (pxa_bytes_t){max_scope, sizeof(max_scope)}, affected, 1,
               &affected_count) == PXA_STATUS_OK);
    assert(affected_count == 0);

    destroy_runtime(&test);
    free(permission_workspace);
}

static void test_ipc_service(void) {
    static const uint8_t endpoint_name[] = "example.echo";
    static const uint8_t lazy_endpoint_name[] = "example.lazy";
    test_runtime_t test = make_runtime(3, 16, 8, 4, 24, 8, 2);
    pxa_component_t caller = create_started(test.runtime, 7);
    pxa_component_t provider = create_started(test.runtime, 8);
    pxa_component_t stranger = create_started(test.runtime, 9);
    pxa_ipc_limits_t limits;
    size_t ipc_size;
    void *ipc_workspace;
    pxa_ipc_broker_t *ipc = NULL;
    uint8_t command[1200];
    uint8_t event_bytes[1200];
    size_t command_size;
    size_t event_size;
    pxa_event_view_t view;
    pxa_message_view_t event;
    uint32_t call_id;
    uint32_t second_call_id;
    unsigned reuse;
    test_ipc_resolver_t resolver = {PXA_STATUS_RESOURCE_LIMIT, 0};
    test_ipc_allocator_t allocator = {0, 0};
    pxa_component_snapshot_t caller_snapshot;

    pxa_ipc_limits_init(&limits);
    limits.max_endpoints = 2;
    limits.max_pending_calls = 2;
    ipc_size = pxa_ipc_broker_workspace_size(&limits);
    ipc_workspace = malloc(ipc_size);
    assert(ipc_workspace != NULL);
    assert(pxa_ipc_broker_init(ipc_workspace, ipc_size, test.runtime,
                               &limits, &ipc) == PXA_STATUS_OK);
    assert(pxa_ipc_broker_register(ipc) == PXA_STATUS_OK);
    assert(pxa_ipc_broker_set_allocator(
               ipc, &allocator, test_ipc_allocate, test_ipc_release) ==
           PXA_STATUS_OK);
    assert(pxa_ipc_broker_set_endpoint_resolver(
               ipc, &resolver, test_ipc_resolve) == PXA_STATUS_OK);
    assert(pxa_ipc_endpoint_declare(
               ipc, (pxa_bytes_t){lazy_endpoint_name,
                                  sizeof(lazy_endpoint_name) - 1}) ==
           PXA_STATUS_OK);
    assert(pxa_ipc_endpoint_declare(
               ipc, (pxa_bytes_t){lazy_endpoint_name,
                                  sizeof(lazy_endpoint_name) - 1}) ==
           PXA_STATUS_BUSY);
    assert(pxa_ipc_endpoint_is_valid(
        (pxa_bytes_t){endpoint_name, sizeof(endpoint_name) - 1}));
    assert(!pxa_ipc_endpoint_is_valid(
        (pxa_bytes_t){(const uint8_t *)"1bad", 4}));
    assert(pxa_ipc_endpoint_register(
               ipc, (pxa_bytes_t){endpoint_name, sizeof(endpoint_name) - 1},
               provider) == PXA_STATUS_OK);
    for (reuse = 0; reuse < 4; ++reuse) {
        assert(pxa_ipc_endpoint_unregister(
                   ipc,
                   (pxa_bytes_t){endpoint_name,
                                 sizeof(endpoint_name) - 1},
                   provider) == PXA_STATUS_OK);
        assert(pxa_ipc_endpoint_register(
                   ipc,
                   (pxa_bytes_t){endpoint_name,
                                 sizeof(endpoint_name) - 1},
                   provider) == PXA_STATUS_OK);
    }
    assert(pxa_ipc_endpoint_register(
               ipc, (pxa_bytes_t){endpoint_name, sizeof(endpoint_name) - 1},
               stranger) == PXA_STATUS_BUSY);

    command_size = make_ipc_call(command, sizeof(command), 10,
                                 "example.lazy", "x");
    assert(pxa_component_begin_event(test.runtime, caller) == PXA_STATUS_OK);
    assert(pxa_runtime_control(test.runtime, caller, command, command_size) ==
           PXA_STATUS_OK);
    assert(pxa_component_finish_event(test.runtime, caller, 1) ==
           PXA_STATUS_OK);
    assert(pxa_ipc_flush(ipc) == PXA_STATUS_OK);
    assert(resolver.calls == 1);
    assert(allocator.allocations == 1 && allocator.releases == 1);
    event_size = read_head_event(test.runtime, caller, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    assert(event.service == PXA_IPC_SERVICE_ID &&
           event.opcode == PXA_IPC_CALL && event.request_id == 10 &&
           event.payload.size == 4 &&
           (int32_t)pxa_read_u32(event.payload.data) ==
               PXA_STATUS_RESOURCE_LIMIT);
    assert(pxa_event_consume(test.runtime, caller, view.token) ==
           PXA_STATUS_OK);
    assert(pxa_component_snapshot(test.runtime, caller, &caller_snapshot) ==
           PXA_STATUS_OK);
    assert(caller_snapshot.state == PXA_COMPONENT_RUNNING);

    command_size = make_ipc_call(command, sizeof(command), 11,
                                 "example.echo", "hello");
    assert(pxa_component_begin_event(test.runtime, caller) == PXA_STATUS_OK);
    assert(pxa_runtime_control(test.runtime, caller, command, command_size) ==
           PXA_STATUS_OK);
    assert(pxa_component_finish_event(test.runtime, caller, 1) ==
           PXA_STATUS_OK);
    event_size = read_head_event(test.runtime, provider, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    assert(event.opcode == PXA_IPC_REQUEST_EVENT && event.request_id != 0);
    call_id = event.request_id;
    assert(pxa_event_consume(test.runtime, provider, view.token) ==
           PXA_STATUS_OK);
    event_size = read_head_event(test.runtime, caller, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    assert(event.opcode == PXA_IPC_CALL && event.request_id == 11 &&
           event.payload.size == 8 &&
           pxa_read_u32(event.payload.data + 4) == call_id);
    assert(pxa_event_consume(test.runtime, caller, view.token) ==
           PXA_STATUS_OK);

    command_size = make_ipc_reply(command, sizeof(command), 12, call_id,
                                  PXA_STATUS_OK, NULL);
    assert(pxa_component_begin_event(test.runtime, stranger) == PXA_STATUS_OK);
    assert(pxa_runtime_control(test.runtime, stranger, command, command_size) ==
           PXA_STATUS_OK);
    assert(pxa_component_finish_event(test.runtime, stranger, 1) ==
           PXA_STATUS_OK);
    event_size = read_head_event(test.runtime, stranger, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    assert(event.request_id == 12 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_DENIED);
    assert(pxa_event_consume(test.runtime, stranger, view.token) ==
           PXA_STATUS_OK);

    command_size = make_ipc_reply(command, sizeof(command), 13, call_id,
                                  PXA_STATUS_OK, "world");
    assert(pxa_component_begin_event(test.runtime, provider) == PXA_STATUS_OK);
    assert(pxa_runtime_control(test.runtime, provider, command, command_size) ==
           PXA_STATUS_OK);
    assert(pxa_component_finish_event(test.runtime, provider, 1) ==
           PXA_STATUS_OK);
    event_size = read_head_event(test.runtime, caller, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    assert(event.opcode == PXA_IPC_REPLY_EVENT && event.request_id == call_id &&
           event.payload.size == 13 &&
           memcmp(event.payload.data + 8, "world", 5) == 0);
    assert(pxa_event_consume(test.runtime, caller, view.token) ==
           PXA_STATUS_OK);
    event_size = read_head_event(test.runtime, provider, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK && event.request_id == 13);
    assert(pxa_event_consume(test.runtime, provider, view.token) ==
           PXA_STATUS_OK);

    assert(pxa_ipc_endpoint_unregister(
               ipc, (pxa_bytes_t){endpoint_name, sizeof(endpoint_name) - 1},
               stranger) == PXA_STATUS_NOT_FOUND);
    assert(pxa_ipc_endpoint_unregister(
               ipc, (pxa_bytes_t){endpoint_name, sizeof(endpoint_name) - 1},
               provider) == PXA_STATUS_OK);
    assert(pxa_ipc_endpoint_unregister(
               ipc, (pxa_bytes_t){endpoint_name, sizeof(endpoint_name) - 1},
               provider) == PXA_STATUS_NOT_FOUND);
    assert(pxa_ipc_endpoint_register(
               ipc, (pxa_bytes_t){endpoint_name, sizeof(endpoint_name) - 1},
               provider) == PXA_STATUS_OK);

    command_size = make_ipc_call(command, sizeof(command), 14,
                                 "example.echo", NULL);
    assert(pxa_component_begin_event(test.runtime, caller) == PXA_STATUS_OK);
    assert(pxa_runtime_control(test.runtime, caller, command, command_size) ==
           PXA_STATUS_OK);
    assert(pxa_component_finish_event(test.runtime, caller, 1) ==
           PXA_STATUS_OK);
    event_size = read_head_event(test.runtime, provider, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    call_id = event.request_id;
    assert(pxa_event_consume(test.runtime, provider, view.token) ==
           PXA_STATUS_OK);
    assert(pxa_event_peek(test.runtime, caller, &view) == PXA_STATUS_OK);
    assert(pxa_event_consume(test.runtime, caller, view.token) ==
           PXA_STATUS_OK);

    command_size = make_ipc_call(command, sizeof(command), 15,
                                 "example.echo", "second");
    assert(pxa_component_begin_event(test.runtime, caller) == PXA_STATUS_OK);
    assert(pxa_runtime_control(test.runtime, caller, command, command_size) ==
           PXA_STATUS_OK);
    assert(pxa_component_finish_event(test.runtime, caller, 1) ==
           PXA_STATUS_OK);
    event_size = read_head_event(test.runtime, provider, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK &&
           event.opcode == PXA_IPC_REQUEST_EVENT);
    second_call_id = event.request_id;
    assert(second_call_id != call_id);
    assert(pxa_event_consume(test.runtime, provider, view.token) ==
           PXA_STATUS_OK);
    event_size = read_head_event(test.runtime, caller, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK &&
           event.opcode == PXA_IPC_CALL && event.request_id == 15 &&
           pxa_read_u32(event.payload.data + 4) == second_call_id);
    assert(pxa_event_consume(test.runtime, caller, view.token) ==
           PXA_STATUS_OK);

    assert(pxa_component_abort(test.runtime, provider, PXA_STOP_FAULT) ==
           PXA_STATUS_OK);
    event_size = read_head_event(test.runtime, caller, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    assert(event.opcode == PXA_IPC_REPLY_EVENT &&
           event.request_id == second_call_id &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_CANCELLED);
    assert(pxa_event_consume(test.runtime, caller, view.token) ==
           PXA_STATUS_OK);
    event_size = read_head_event(test.runtime, caller, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    assert(event.opcode == PXA_IPC_REPLY_EVENT && event.request_id == call_id &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_CANCELLED);
    assert(pxa_event_consume(test.runtime, caller, view.token) ==
           PXA_STATUS_OK);

    destroy_runtime(&test);
    free(ipc_workspace);
}

static void test_storage_service(void) {
    test_runtime_t test = make_runtime(1, 8, 8, 2, 12, 8, 2);
    pxa_component_t component = create_started(test.runtime, 10);
    test_storage_backend_t backend;
    pxa_storage_config_t config;
    size_t storage_size;
    void *storage_workspace;
    pxa_storage_service_t *storage = NULL;
    uint8_t command[256];
    uint8_t event_bytes[1200];
    size_t command_size;
    pxa_message_view_t event;
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;

    memset(&backend, 0, sizeof(backend));
    memset(&config, 0, sizeof(config));
    config.struct_size = sizeof(config);
    config.max_value_bytes = TEST_STORAGE_VALUE_CAPACITY;
    config.backend.struct_size = sizeof(config.backend);
    config.backend.context = &backend;
    config.backend.get = storage_get;
    config.backend.set = storage_set;
    config.backend.remove = storage_remove;
    config.backend.list = storage_list;
    storage_size = pxa_storage_service_workspace_size(&config);
    assert(storage_size != 0);
    storage_workspace = malloc(storage_size);
    assert(storage_workspace != NULL);
    assert(pxa_storage_service_init(storage_workspace, storage_size,
                                    test.runtime, &config, &storage) ==
           PXA_STATUS_OK);
    assert(pxa_storage_service_register(storage) == PXA_STATUS_OK);
    assert(pxa_storage_key_is_valid(
        (pxa_bytes_t){(const uint8_t *)"alpha", 5}));
    assert(!pxa_storage_key_is_valid(
        (pxa_bytes_t){(const uint8_t *)"1bad", 4}));

    command_size = make_storage_request(command, sizeof(command),
                                        PXA_STORAGE_SET, 11, "gamma", "g");
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.request_id == 11 && event.payload.size == 4 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK);
    command_size = make_storage_request(command, sizeof(command),
                                        PXA_STORAGE_SET, 12, "alpha", "a1");
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert((int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK);
    command_size = make_storage_request(command, sizeof(command),
                                        PXA_STORAGE_SET, 13, "beta", "b");
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert((int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           backend.count == 3);

    command_size = make_storage_request(command, sizeof(command),
                                        PXA_STORAGE_GET, 14, "alpha", NULL);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert((int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK);
    pxa_record_iterator_init(
        &iterator,
        (pxa_bytes_t){event.payload.data + 4, event.payload.size - 4});
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 2 && record.payload.size == 2 &&
           memcmp(record.payload.data, "a1", 2) == 0);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_WOULD_BLOCK);

    command_size = make_storage_request(command, sizeof(command),
                                        PXA_STORAGE_LIST, 15, "alpha", NULL);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert((int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK);
    pxa_record_iterator_init(
        &iterator,
        (pxa_bytes_t){event.payload.data + 4, event.payload.size - 4});
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 1 && record.payload.size == 4 &&
           memcmp(record.payload.data, "beta", 4) == 0);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 1 && record.payload.size == 5 &&
           memcmp(record.payload.data, "gamma", 5) == 0);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_WOULD_BLOCK);

    backend.corrupt_list_order = 1;
    command_size = make_storage_request(command, sizeof(command),
                                        PXA_STORAGE_LIST, 16, NULL, NULL);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 4 &&
           (int32_t)pxa_read_u32(event.payload.data) ==
               PXA_STATUS_INVALID_ARGUMENT);
    backend.corrupt_list_order = 0;

    command_size = make_storage_request(command, sizeof(command),
                                        PXA_STORAGE_REMOVE, 17, "beta", NULL);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert((int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           backend.count == 2);
    command_size = make_storage_request(command, sizeof(command),
                                        PXA_STORAGE_GET, 18, "beta", NULL);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 4 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_NOT_FOUND);

    command_size = make_storage_request(command, sizeof(command),
                                        PXA_STORAGE_GET, 19, "1bad", NULL);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 4 &&
           (int32_t)pxa_read_u32(event.payload.data) ==
               PXA_STATUS_INVALID_ARGUMENT);

    destroy_runtime(&test);
    free(storage_workspace);
}

static void test_fs_service(void) {
    static const uint8_t valid_utf8_path[] = {
        'n', 'o', 't', 'e', 's', '/', 0xe6, 0x97, 0xa5, 0xe8, 0xae, 0xb0,
    };
    static const uint8_t invalid_utf8_path[] = {'b', 'a', 'd', 0xc0, 0xaf};
    test_runtime_t test = make_runtime(1, 12, 12, 4, 16, 10, 2);
    pxa_component_t component = create_started(test.runtime, 11);
    test_fs_backend_t backend;
    pxa_fs_config_t config;
    size_t fs_size;
    void *fs_workspace;
    pxa_fs_service_t *fs = NULL;
    uint8_t command[640];
    uint8_t event_bytes[256];
    uint8_t io[8];
    size_t command_size;
    pxa_message_view_t event;
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    pxa_handle_t file_handle;
    pxa_handle_t directory_handle;
    unsigned opens_before;

    assert(pxa_fs_path_is_valid(
        (pxa_bytes_t){valid_utf8_path, sizeof(valid_utf8_path)}));
    assert(!pxa_fs_path_is_valid(
        (pxa_bytes_t){invalid_utf8_path, sizeof(invalid_utf8_path)}));
    assert(!pxa_fs_path_is_valid(
        (pxa_bytes_t){(const uint8_t *)"../escape", 9}));
    assert(!pxa_fs_path_is_valid(
        (pxa_bytes_t){(const uint8_t *)"notes/.pxa-state", 16}));
    assert(!pxa_fs_path_is_valid(
        (pxa_bytes_t){(const uint8_t *)"notes//file", 11}));

    memset(&backend, 0, sizeof(backend));
    memset(&config, 0, sizeof(config));
    config.struct_size = sizeof(config);
    config.max_open_resources = 1;
    config.backend.struct_size = sizeof(config.backend);
    config.backend.context = &backend;
    config.backend.open = fs_open;
    config.backend.make_directory = fs_mutation;
    config.backend.remove = fs_mutation;
    config.backend.rename = fs_rename;
    config.backend.stat = fs_stat;
    config.backend.read = fs_read;
    config.backend.write = fs_write;
    config.backend.seek = fs_seek;
    config.backend.read_directory = fs_read_directory;
    config.backend.close = fs_close;
    fs_size = pxa_fs_service_workspace_size(&config);
    assert(fs_size != 0);
    fs_workspace = malloc(fs_size);
    assert(fs_workspace != NULL);
    assert(pxa_fs_service_init(fs_workspace, fs_size, test.runtime, &config,
                               &fs) == PXA_STATUS_OK);
    assert(pxa_fs_service_register(fs) == PXA_STATUS_OK);

    command_size = make_fs_open(
        command, sizeof(command), 21, "notes/today.txt",
        PXA_FS_OPEN_READ | PXA_FS_OPEN_WRITE | PXA_FS_OPEN_CREATE);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.opcode == PXA_FS_OPEN && event.request_id == 21 &&
           event.payload.size == 8 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK);
    file_handle = pxa_read_u32(event.payload.data + 4);
    memcpy(io, "hello", 5);
    assert(dispatch_io(test.runtime, component, file_handle, PXA_FS_IO_WRITE,
                       io, 5) == 5);

    command_size = make_fs_seek(command, sizeof(command), 22, file_handle, 0,
                                PXA_FS_SEEK_START);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 12 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           pxa_read_u64(event.payload.data + 4) == 0);
    memset(io, 0, sizeof(io));
    assert(dispatch_io(test.runtime, component, file_handle, PXA_FS_IO_READ,
                       io, 5) == 5 &&
           memcmp(io, "hello", 5) == 0);

    command_size = make_fs_path_request(command, sizeof(command), PXA_FS_STAT,
                                        23, "notes/today.txt", NULL);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 13 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           event.payload.data[4] == PXA_FS_KIND_REGULAR &&
           pxa_read_u64(event.payload.data + 5) == 5);

    opens_before = backend.opens;
    command_size = make_fs_open(
        command, sizeof(command), 24, "notes",
        PXA_FS_OPEN_READ | PXA_FS_OPEN_DIRECTORY);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 4 &&
           (int32_t)pxa_read_u32(event.payload.data) ==
               PXA_STATUS_RESOURCE_LIMIT &&
           backend.opens == opens_before);
    assert(pxa_handle_close(test.runtime, component, file_handle) ==
           PXA_STATUS_OK &&
           backend.closes == 1);

    command_size = make_fs_open(
        command, sizeof(command), 25, "notes",
        PXA_FS_OPEN_READ | PXA_FS_OPEN_DIRECTORY);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert((int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK);
    directory_handle = pxa_read_u32(event.payload.data + 4);
    assert(dispatch_io(test.runtime, component, directory_handle,
                       PXA_FS_IO_READ, io, sizeof(io)) ==
           PXA_STATUS_UNSUPPORTED);

    command_size = make_fs_handle_request(
        command, sizeof(command), PXA_FS_READ_DIRECTORY, 26,
        directory_handle);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert((int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK);
    pxa_record_iterator_init(
        &iterator,
        (pxa_bytes_t){event.payload.data + 4, event.payload.size - 4});
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 4 && record.payload.size == 9 &&
           memcmp(record.payload.data, "today.txt", 9) == 0);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 5 && record.payload.size == 1 &&
           record.payload.data[0] == PXA_FS_KIND_REGULAR);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 6 && record.payload.size == 8 &&
           pxa_read_u64(record.payload.data) == 5);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_WOULD_BLOCK);

    command_size = make_fs_handle_request(
        command, sizeof(command), PXA_FS_READ_DIRECTORY, 27,
        directory_handle);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    pxa_record_iterator_init(
        &iterator,
        (pxa_bytes_t){event.payload.data + 4, event.payload.size - 4});
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 7 && record.payload.size == 0);
    assert(pxa_handle_close(test.runtime, component, directory_handle) ==
           PXA_STATUS_OK &&
           backend.closes == 2);

    command_size = make_fs_path_request(command, sizeof(command),
                                        PXA_FS_MAKE_DIRECTORY, 28,
                                        "../escape", NULL);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 4 &&
           (int32_t)pxa_read_u32(event.payload.data) ==
               PXA_STATUS_INVALID_ARGUMENT &&
           backend.mutations == 0);
    command_size = make_fs_path_request(command, sizeof(command), PXA_FS_RENAME,
                                        29, "notes", "notes");
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert((int32_t)pxa_read_u32(event.payload.data) ==
               PXA_STATUS_INVALID_ARGUMENT &&
           backend.mutations == 0);

    command_size = make_fs_open(
        command, sizeof(command), 30, "notes/today.txt", PXA_FS_OPEN_READ);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert((int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK);
    assert(pxa_component_abort(test.runtime, component, PXA_STOP_FAULT) ==
           PXA_STATUS_OK);
    assert(backend.closes == 3);

    destroy_runtime(&test);
    assert(backend.closes == 3);
    free(fs_workspace);
}

static void test_sensor_service(void) {
    static const uint8_t identity[] = "sensor.app";
    static const uint8_t permission_name[] = "sensor.read";
    static const uint8_t semantic[] = "ambient.temperature";
    pxa_permission_declaration_t declaration;
    pxa_permission_config_t permission_config;
    pxa_sensor_descriptor_t descriptor;
    pxa_sensor_config_t sensor_config;
    test_sensor_provider_t provider;
    unsigned permission_loads = 0;
    test_runtime_t test = make_runtime(1, 10, 10, 6, 12, 8, 2);
    pxa_component_t component = create_started(test.runtime, 12);
    size_t permission_size;
    size_t sensor_size;
    void *permission_workspace;
    void *sensor_workspace;
    pxa_permission_service_t *permission = NULL;
    pxa_sensor_service_t *sensor = NULL;
    uint8_t command[192];
    uint8_t event_bytes[256];
    size_t command_size;
    size_t event_size;
    pxa_message_view_t event;
    pxa_event_view_t view;
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    pxa_handle_t permission_handle;
    pxa_handle_t sensor_handle;
    pxa_handle_t second_sensor_handle;
    pxa_handle_t reused_sensor_handle;
    pxa_authority_t authority;
    pxa_component_t affected[1];
    size_t affected_count;

    memset(&permission_config, 0, sizeof(permission_config));
    declaration.name =
        (pxa_bytes_t){permission_name, sizeof(permission_name) - 1};
    declaration.scope = (pxa_bytes_t){semantic, sizeof(semantic) - 1};
    declaration.required = 1;
    permission_config.struct_size = sizeof(permission_config);
    permission_config.app_identity =
        (pxa_bytes_t){identity, sizeof(identity) - 1};
    permission_config.declarations = &declaration;
    permission_config.declaration_count = 1;
    permission_config.max_authorities = 2;
    permission_config.store.struct_size = sizeof(permission_config.store);
    permission_config.store.context = &permission_loads;
    permission_config.store.load = sensor_permission_load;
    permission_config.store.save = sensor_permission_save;
    permission_size =
        pxa_permission_service_workspace_size(&permission_config);
    permission_workspace = malloc(permission_size);
    assert(permission_workspace != NULL);
    assert(pxa_permission_service_init(
               permission_workspace, permission_size, test.runtime,
               &permission_config, &permission) == PXA_STATUS_OK);
    assert(pxa_permission_policy_load(permission) == PXA_STATUS_OK &&
           permission_loads == 1 && pxa_permission_can_activate(permission));
    assert(pxa_permission_service_register(permission) == PXA_STATUS_OK);

    /* An empty physical sensor catalog must not prevent Host activation. */
    memset(&sensor_config, 0, sizeof(sensor_config));
    sensor_config.struct_size = sizeof(sensor_config);
    sensor_config.max_subscriptions = 2;
    sensor_config.max_subscriptions_per_component = 1;
    sensor_config.subscribe = sensor_subscribe;
    sensor_config.read = sensor_read;
    sensor_config.unsubscribe = sensor_unsubscribe;
    sensor_config.permissions = permission;
    sensor_size = pxa_sensor_service_workspace_size(&sensor_config);
    assert(sensor_size != 0);
    sensor_workspace = malloc(sensor_size);
    assert(sensor_workspace != NULL);
    assert(pxa_sensor_service_init(sensor_workspace, sensor_size, test.runtime,
                                   &sensor_config, &sensor) == PXA_STATUS_OK);
    free(sensor_workspace);
    sensor_workspace = NULL;
    sensor = NULL;

    memset(&provider, 0, sizeof(provider));
    memset(&sensor_config, 0, sizeof(sensor_config));
    descriptor.id = 1;
    descriptor.semantic = (pxa_bytes_t){semantic, sizeof(semantic) - 1};
    descriptor.unit = PXA_SENSOR_UNIT_MILLI_CELSIUS;
    descriptor.dimensions = 1;
    descriptor.min_period_ms = 100;
    descriptor.max_period_ms = 1000;
    sensor_config.struct_size = sizeof(sensor_config);
    sensor_config.descriptors = &descriptor;
    sensor_config.descriptor_count = 1;
    sensor_config.max_subscriptions = 2;
    sensor_config.max_subscriptions_per_component = 2;
    sensor_config.provider_context = &provider;
    sensor_config.subscribe = sensor_subscribe;
    sensor_config.read = sensor_read;
    sensor_config.unsubscribe = sensor_unsubscribe;
    sensor_config.permissions = permission;
    sensor_size = pxa_sensor_service_workspace_size(&sensor_config);
    assert(sensor_size != 0);
    sensor_workspace = malloc(sensor_size);
    assert(sensor_workspace != NULL);
    assert(pxa_sensor_service_init(sensor_workspace, sensor_size, test.runtime,
                                   &sensor_config, &sensor) == PXA_STATUS_OK);
    assert(pxa_sensor_service_register(sensor) == PXA_STATUS_OK);

    command_size = make_message(command, sizeof(command),
                                PXA_SENSOR_SERVICE_ID, PXA_SENSOR_LIST, 31, 0);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size > 4 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK);
    pxa_record_iterator_init(
        &iterator,
        (pxa_bytes_t){event.payload.data + 4, event.payload.size - 4});
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 1 && record.payload.size > sizeof(semantic) - 1);

    command_size = make_permission_request(
        command, sizeof(command), PXA_PERMISSION_ACQUIRE, 32,
        permission_name, sizeof(permission_name) - 1,
        semantic, sizeof(semantic) - 1);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 8 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK);
    permission_handle = pxa_read_u32(event.payload.data + 4);
    assert(pxa_permission_resolve(
               permission, component, permission_handle,
               (pxa_bytes_t){permission_name, sizeof(permission_name) - 1},
               (pxa_bytes_t){semantic, sizeof(semantic) - 1},
               &authority) == PXA_STATUS_OK);

    command_size = make_sensor_subscribe(command, sizeof(command), 33, 1, 100,
                                         permission_handle);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 8 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           provider.subscribes == 1);
    sensor_handle = pxa_read_u32(event.payload.data + 4);
    assert(pxa_sensor_has_active_subscriptions(sensor));

    command_size = make_sensor_subscribe(command, sizeof(command), 34, 1, 100,
                                         permission_handle);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 8 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           provider.subscribes == 2);
    second_sensor_handle = pxa_read_u32(event.payload.data + 4);

    command_size = make_sensor_subscribe(command, sizeof(command), 35, 1, 100,
                                         permission_handle);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 4 &&
           (int32_t)pxa_read_u32(event.payload.data) ==
               PXA_STATUS_RESOURCE_LIMIT &&
           provider.subscribes == 2);

    assert(pxa_handle_close(test.runtime, component, sensor_handle) ==
           PXA_STATUS_OK);
    assert(provider.unsubscribes == 1 &&
           pxa_sensor_has_active_subscriptions(sensor));
    assert(pxa_handle_get(test.runtime, component, sensor_handle,
                          PXA_RESOURCE_SENSOR,
                          &(pxa_resource_t){0}) == PXA_STATUS_NOT_FOUND);

    command_size = make_sensor_subscribe(command, sizeof(command), 36, 1, 100,
                                         permission_handle);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 8 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           provider.subscribes == 3);
    reused_sensor_handle = pxa_read_u32(event.payload.data + 4);
    assert(reused_sensor_handle != sensor_handle);

    assert(pxa_sensor_poll(sensor, 1000, affected, 1, &affected_count) ==
               PXA_STATUS_OK &&
           affected_count == 1 && affected[0] == component);
    assert(pxa_sensor_poll(sensor, 50000, affected, 1, &affected_count) ==
               PXA_STATUS_OK &&
           affected_count == 0);
    assert(pxa_sensor_poll(sensor, 101000, affected, 1, &affected_count) ==
               PXA_STATUS_OK &&
           affected_count == 1 && provider.reads == 4);
    event_size = read_head_event(test.runtime, component, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    assert(event.service == PXA_SENSOR_SERVICE_ID &&
           event.opcode == PXA_SENSOR_SAMPLE && event.request_id == 0);
    pxa_record_iterator_init(&iterator, event.payload);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 4 && record.payload.size == 4 &&
           pxa_read_u32(record.payload.data) == reused_sensor_handle);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 2 && record.payload.size == 8 &&
           pxa_read_u64(record.payload.data) == 101000);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 3 && pxa_read_u16(record.payload.data) == 1);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 4 && record.payload.size == 4 &&
           (int32_t)pxa_read_u32(record.payload.data) == 21500);
    assert(pxa_event_consume(test.runtime, component, view.token) ==
           PXA_STATUS_OK);

    event_size = read_head_event(test.runtime, component, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    assert(event.service == PXA_SENSOR_SERVICE_ID &&
           event.opcode == PXA_SENSOR_SAMPLE && event.request_id == 0);
    pxa_record_iterator_init(&iterator, event.payload);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 4 && record.payload.size == 4 &&
           pxa_read_u32(record.payload.data) == second_sensor_handle);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 2 && record.payload.size == 8 &&
           pxa_read_u64(record.payload.data) == 101000);
    assert(pxa_event_consume(test.runtime, component, view.token) ==
           PXA_STATUS_OK);

    assert(pxa_authority_revoke(test.runtime, component, authority) ==
           PXA_STATUS_OK);
    assert(provider.unsubscribes == 3 &&
           !pxa_sensor_has_active_subscriptions(sensor));
    assert(pxa_handle_get(test.runtime, component, second_sensor_handle,
                          PXA_RESOURCE_SENSOR,
                          &(pxa_resource_t){0}) == PXA_STATUS_NOT_FOUND);
    assert(pxa_handle_get(test.runtime, component, reused_sensor_handle,
                          PXA_RESOURCE_SENSOR,
                          &(pxa_resource_t){0}) == PXA_STATUS_NOT_FOUND);
    assert(pxa_sensor_poll(sensor, 201000, affected, 1, &affected_count) ==
               PXA_STATUS_OK &&
           affected_count == 0);

    destroy_runtime(&test);
    free(sensor_workspace);
    free(permission_workspace);
}

static void test_device_service(void) {
    static const uint8_t identity[] = "device.app";
    static const uint8_t permission_name[] = "device.identity";
    static const uint8_t scope[] = "mac.wifi.station.hardware";
    pxa_permission_declaration_t declaration;
    pxa_permission_config_t permission_config;
    pxa_device_config_t device_config;
    test_device_provider_t provider;
    unsigned permission_loads = 0;
    test_runtime_t test = make_runtime(1, 8, 8, 2, 8, 6, 2);
    pxa_component_t component = create_started(test.runtime, 13);
    size_t permission_size;
    size_t device_size;
    void *permission_workspace;
    void *device_workspace;
    pxa_permission_service_t *permission = NULL;
    pxa_device_service_t *device = NULL;
    pxa_handle_t permission_handle;
    uint8_t command[128];
    uint8_t event_bytes[128];
    pxa_message_view_t event;
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    size_t command_size;

    declaration.name =
        (pxa_bytes_t){permission_name, sizeof(permission_name) - 1u};
    declaration.scope = (pxa_bytes_t){scope, sizeof(scope) - 1u};
    declaration.required = 1;
    memset(&permission_config, 0, sizeof(permission_config));
    permission_config.struct_size = sizeof(permission_config);
    permission_config.app_identity =
        (pxa_bytes_t){identity, sizeof(identity) - 1u};
    permission_config.declarations = &declaration;
    permission_config.declaration_count = 1;
    permission_config.max_authorities = 2;
    permission_config.store.struct_size = sizeof(permission_config.store);
    permission_config.store.context = &permission_loads;
    permission_config.store.load = sensor_permission_load;
    permission_config.store.save = sensor_permission_save;
    permission_size = pxa_permission_service_workspace_size(&permission_config);
    permission_workspace = malloc(permission_size);
    assert(permission_workspace != NULL);
    assert(pxa_permission_service_init(permission_workspace, permission_size,
                                       test.runtime, &permission_config,
                                       &permission) == PXA_STATUS_OK);
    assert(pxa_permission_policy_load(permission) == PXA_STATUS_OK &&
           permission_loads == 1);
    assert(pxa_permission_service_register(permission) == PXA_STATUS_OK);

    memset(&provider, 0, sizeof(provider));
    memset(&device_config, 0, sizeof(device_config));
    device_config.struct_size = sizeof(device_config);
    device_config.provider_context = &provider;
    device_config.get_mac = device_get_mac;
    device_config.permissions = permission;
    device_size = pxa_device_service_workspace_size(&device_config);
    device_workspace = malloc(device_size);
    assert(device_workspace != NULL);
    assert(pxa_device_service_init(device_workspace, device_size, test.runtime,
                                   &device_config, &device) == PXA_STATUS_OK);
    assert(pxa_device_service_register(device) == PXA_STATUS_OK);

    command_size = make_permission_request(
        command, sizeof(command), PXA_PERMISSION_ACQUIRE, 41, permission_name,
        sizeof(permission_name) - 1u, scope, sizeof(scope) - 1u);
    (void)dispatch_control_completion(test.runtime, component, command,
                                      command_size, event_bytes,
                                      sizeof(event_bytes), &event);
    assert(event.payload.size == 8 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK);
    permission_handle = pxa_read_u32(event.payload.data + 4);

    command_size = make_device_get_mac(
        command, sizeof(command), 42,
        PXA_DEVICE_MAC_KIND_WIFI_STATION_HARDWARE, permission_handle);
    (void)dispatch_control_completion(test.runtime, component, command,
                                      command_size, event_bytes,
                                      sizeof(event_bytes), &event);
    assert(event.payload.size == 28 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           provider.calls == 1);
    pxa_record_iterator_init(
        &iterator,
        (pxa_bytes_t){event.payload.data + 4, event.payload.size - 4});
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == PXA_DEVICE_TAG_MAC_KIND && record.payload.size == 2 &&
           pxa_read_u16(record.payload.data) ==
               PXA_DEVICE_MAC_KIND_WIFI_STATION_HARDWARE);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == PXA_DEVICE_TAG_MAC && record.payload.size == 6 &&
           record.payload.data[0] == 0x24 && record.payload.data[5] == 0x01);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == PXA_DEVICE_TAG_FLAGS && record.payload.size == 4 &&
           pxa_read_u32(record.payload.data) == PXA_DEVICE_MAC_FLAG_HARDWARE);

    command_size = make_device_get_mac(
        command, sizeof(command), 43,
        PXA_DEVICE_MAC_KIND_WIFI_SOFTAP_HARDWARE, permission_handle);
    (void)dispatch_control_completion(test.runtime, component, command,
                                      command_size, event_bytes,
                                      sizeof(event_bytes), &event);
    assert(event.payload.size == 4 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_DENIED &&
           provider.calls == 1);

    destroy_runtime(&test);
    free(device_workspace);
    free(permission_workspace);
}

static void test_scheduler_service(void) {
    static const uint8_t job_id[] = "sync.job";
    static const uint8_t rebound_job_id[] = "rebound.job";
    pxa_bytes_t jobs[1];
    pxa_bytes_t rebound_jobs[1];
    pxa_scheduler_config_t config;
    test_scheduler_store_t store;
    test_work_completion_t completion;
    uint64_t now_ms = 1000;
    test_runtime_t test = make_runtime(1, 10, 10, 2, 12, 8, 2);
    pxa_component_t component = create_started(test.runtime, 13);
    size_t scheduler_size;
    void *scheduler_storage;
    void *scheduler_workspace;
    pxa_scheduler_service_t *scheduler = NULL;
    uint8_t command[160];
    uint8_t event_bytes[96];
    size_t command_size;
    pxa_message_view_t event;
    pxa_scheduler_entry_t due[2];
    size_t due_count;
    uint32_t first_id;
    uint32_t second_id;
    uint32_t third_id;
    uint32_t work_id;

    memset(&store, 0, sizeof(store));
    memset(&completion, 0, sizeof(completion));
    store.save_status = PXA_STATUS_OK;
    jobs[0] = (pxa_bytes_t){job_id, sizeof(job_id) - 1};
    rebound_jobs[0] =
        (pxa_bytes_t){rebound_job_id, sizeof(rebound_job_id) - 1};
    pxa_scheduler_config_init(&config);
    config.job_components = jobs;
    config.job_component_count = 1;
    config.max_entries = 4;
    config.clock_context = &now_ms;
    config.clock = scheduler_clock;
    config.store.context = &store;
    config.store.load = scheduler_load;
    config.store.save = scheduler_save;
    config.work_context = &completion;
    config.complete_work = scheduler_complete_work;
    scheduler_size = pxa_scheduler_service_workspace_size(&config);
    assert(scheduler_size != 0);
    scheduler_storage = malloc(scheduler_size + 1u);
    assert(scheduler_storage != NULL);
    scheduler_workspace = (uint8_t *)scheduler_storage + 1u;
    assert(pxa_scheduler_service_init(
               scheduler_workspace, scheduler_size, test.runtime, &config,
               &scheduler) == PXA_STATUS_OK);
    assert(pxa_scheduler_service_register(scheduler) == PXA_STATUS_BAD_STATE);
    assert(pxa_scheduler_load(scheduler) == PXA_STATUS_OK && store.loads == 1);
    assert(pxa_scheduler_service_register(scheduler) == PXA_STATUS_OK);

    command_size = make_work_enqueue(
        command, sizeof(command), 41, "sync.job", 1000, 5000);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 20 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           pxa_read_u16(event.payload.data + 4) == 4 &&
           pxa_read_u16(event.payload.data + 6) == 4);
    first_id = pxa_read_u32(event.payload.data + 8);
    assert(first_id != 0 && store.count == 1 &&
           pxa_scheduler_has_pending(scheduler));

    store.save_status = PXA_STATUS_INTERNAL;
    command_size = make_work_cancel(command, sizeof(command), 42, first_id);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 4 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_INTERNAL &&
           store.count == 1 && pxa_scheduler_has_pending(scheduler));
    store.save_status = PXA_STATUS_OK;
    command_size = make_work_cancel(command, sizeof(command), 43, first_id);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert((int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           store.count == 0 && !pxa_scheduler_has_pending(scheduler));

    command_size = make_work_enqueue(command, sizeof(command), 50,
                                     "sync.job", 0, 70000);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 20 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           pxa_read_u16(event.payload.data + 4) == 4 &&
           pxa_read_u16(event.payload.data + 12) == 8 &&
           pxa_read_u32(event.payload.data + 16) == 60000);
    work_id = pxa_read_u32(event.payload.data + 8);
    assert(pxa_scheduler_take_due(scheduler, due, 2, &due_count) ==
               PXA_STATUS_OK &&
           due_count == 1 && due[0].id == work_id &&
           due[0].attempt == 1 && due[0].max_attempts == 2 &&
           due[0].input_size == 3 && store.count == 0);
    {
        uint8_t start_config[160];
        size_t start_config_size = 0;
        assert(pxa_scheduler_encode_start_config(
                   &due[0], 61000, start_config, sizeof(start_config),
                   &start_config_size) == PXA_STATUS_OK &&
               start_config_size == 32 &&
               pxa_read_u16(start_config) == 7 &&
               pxa_read_u16(start_config + 8) == 9 &&
               pxa_read_u16(start_config + 13) == 10);
    }
    command_size = make_work_complete(command, sizeof(command), 51, work_id,
                                      PXA_WORK_RESULT_RETRY);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert((int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           completion.calls == 1 && completion.component == component &&
           completion.work_id == work_id &&
           completion.result == PXA_WORK_RESULT_RETRY);
    assert(pxa_scheduler_retry(scheduler, &due[0]) == PXA_STATUS_OK &&
           store.count == 1 && store.entries[0].attempt == 2 &&
           store.entries[0].due_at_ms == 2000);
    command_size = make_work_cancel(command, sizeof(command), 52, work_id);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert((int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           store.count == 0);

    command_size = make_work_enqueue(
        command, sizeof(command), 44, "sync.job", 1000, 5000);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    second_id = pxa_read_u32(event.payload.data + 8);
    command_size = make_work_enqueue(
        command, sizeof(command), 45, "sync.job", 2000, 6000);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    third_id = pxa_read_u32(event.payload.data + 8);
    assert(second_id != first_id && third_id != second_id && store.count == 2);

    now_ms = 2000;
    assert(pxa_scheduler_take_due(scheduler, NULL, 0, &due_count) ==
               PXA_STATUS_RESOURCE_LIMIT &&
           due_count == 1 && store.count == 2);
    store.save_status = PXA_STATUS_INTERNAL;
    assert(pxa_scheduler_take_due(scheduler, due, 2, &due_count) ==
               PXA_STATUS_INTERNAL &&
           due_count == 0 && store.count == 2 &&
           pxa_scheduler_has_pending(scheduler));
    store.save_status = PXA_STATUS_OK;
    assert(pxa_scheduler_take_due(scheduler, due, 2, &due_count) ==
               PXA_STATUS_OK &&
           due_count == 1 && due[0].id == second_id && store.count == 1);
    now_ms = 3000;
    assert(pxa_scheduler_take_due(scheduler, due, 2, &due_count) ==
               PXA_STATUS_OK &&
           due_count == 1 && due[0].id == third_id && store.count == 0 &&
           !pxa_scheduler_has_pending(scheduler));

    command_size = make_work_enqueue(
        command, sizeof(command), 46, "missing.job", 1000, 5000);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert((int32_t)pxa_read_u32(event.payload.data) ==
               PXA_STATUS_INVALID_ARGUMENT &&
           store.count == 0);

    memset(&store.entries[0], 0, sizeof(store.entries[0]));
    store.entries[0].id = 77;
    store.entries[0].due_at_ms = 9000;
    store.entries[0].max_execution_ms = 5000;
    memcpy(store.entries[0].component_id, rebound_job_id,
           sizeof(rebound_job_id) - 1);
    store.entries[0].component_id_size = sizeof(rebound_job_id) - 1;
    store.entries[0].attempt = 1;
    store.entries[0].max_attempts = 1;
    store.count = 1;
    assert(pxa_scheduler_rebind(scheduler, rebound_jobs, 1) ==
               PXA_STATUS_OK &&
           pxa_scheduler_has_pending(scheduler));
    command_size = make_work_enqueue(
        command, sizeof(command), 47, "sync.job", 1000, 5000);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert((int32_t)pxa_read_u32(event.payload.data) ==
           PXA_STATUS_INVALID_ARGUMENT);
    assert(pxa_scheduler_rebind(scheduler, jobs, 1) == PXA_STATUS_INTERNAL);
    assert(pxa_scheduler_rebind(scheduler, rebound_jobs, 2) ==
           PXA_STATUS_INVALID_ARGUMENT);

    destroy_runtime(&test);
    free(scheduler_storage);
}

static void test_empty_scheduler_service(void) {
    pxa_scheduler_config_t config;
    test_scheduler_store_t store;
    uint64_t now_ms = 1000;
    test_runtime_t test = make_runtime(1, 10, 10, 2, 12, 8, 2);
    size_t scheduler_size;
    void *scheduler_workspace;
    pxa_scheduler_service_t *scheduler = NULL;

    memset(&store, 0, sizeof(store));
    store.save_status = PXA_STATUS_OK;
    pxa_scheduler_config_init(&config);
    config.max_entries = 2;
    config.clock_context = &now_ms;
    config.clock = scheduler_clock;
    config.store.context = &store;
    config.store.load = scheduler_load;
    config.store.save = scheduler_save;
    scheduler_size = pxa_scheduler_service_workspace_size(&config);
    assert(scheduler_size != 0);
    scheduler_workspace = malloc(scheduler_size);
    assert(scheduler_workspace != NULL);
    assert(pxa_scheduler_service_init(
               scheduler_workspace, scheduler_size, test.runtime, &config,
               &scheduler) == PXA_STATUS_OK);
    assert(pxa_scheduler_load(scheduler) == PXA_STATUS_OK && store.loads == 1);
    assert(pxa_scheduler_service_register(scheduler) == PXA_STATUS_OK);
    assert(!pxa_scheduler_has_pending(scheduler));

    destroy_runtime(&test);
    free(scheduler_workspace);
}

static void test_scheduler_sorted_load(void) {
    static const uint8_t job_id[] = "sorted.job";
    const uint32_t loaded_ids[3] = {UINT32_MAX, 2, UINT32_MAX};
    pxa_bytes_t jobs[1];
    pxa_scheduler_config_t config;
    test_scheduler_store_t store;
    uint64_t now_ms = 10000;
    test_runtime_t test = make_runtime(1, 10, 10, 2, 12, 8, 2);
    pxa_component_t component = create_started(test.runtime, 16);
    size_t scheduler_size;
    void *scheduler_workspace;
    pxa_scheduler_service_t *scheduler = NULL;
    pxa_scheduler_entry_t due[4];
    uint8_t command[160];
    uint8_t event_bytes[96];
    size_t command_size;
    size_t due_count;
    pxa_message_view_t event;
    uint16_t index;

    memset(&store, 0, sizeof(store));
    store.save_status = PXA_STATUS_OK;
    store.count = 3;
    for (index = 0; index < store.count; ++index) {
        pxa_scheduler_entry_t *entry = &store.entries[index];
        memset(entry, 0, sizeof(*entry));
        entry->id = loaded_ids[index];
        entry->due_at_ms = 11000;
        entry->max_execution_ms = 5000;
        entry->attempt = 1;
        entry->max_attempts = 1;
        memcpy(entry->component_id, job_id, sizeof(job_id) - 1u);
        entry->component_id_size = sizeof(job_id) - 1u;
    }
    jobs[0] = (pxa_bytes_t){job_id, sizeof(job_id) - 1u};
    pxa_scheduler_config_init(&config);
    config.job_components = jobs;
    config.job_component_count = 1;
    config.max_entries = 4;
    config.clock_context = &now_ms;
    config.clock = scheduler_clock;
    config.store.context = &store;
    config.store.load = scheduler_load;
    config.store.save = scheduler_save;
    scheduler_size = pxa_scheduler_service_workspace_size(&config);
    scheduler_workspace = malloc(scheduler_size);
    assert(scheduler_workspace != NULL);
    assert(pxa_scheduler_service_init(
               scheduler_workspace, scheduler_size, test.runtime, &config,
               &scheduler) == PXA_STATUS_OK);
    assert(pxa_scheduler_load(scheduler) == PXA_STATUS_INTERNAL &&
           store.loads == 1);
    store.entries[2].id = 7;
    assert(pxa_scheduler_load(scheduler) == PXA_STATUS_OK &&
           store.loads == 2 && pxa_scheduler_has_pending(scheduler));
    assert(pxa_scheduler_service_register(scheduler) == PXA_STATUS_OK);

    store.save_status = PXA_STATUS_INTERNAL;
    command_size = make_work_enqueue(
        command, sizeof(command), 48, "sorted.job", 1000, 5000);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 4 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_INTERNAL &&
           store.count == 3 && pxa_scheduler_has_pending(scheduler));

    store.save_status = PXA_STATUS_OK;
    command_size = make_work_enqueue(
        command, sizeof(command), 49, "sorted.job", 1000, 5000);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 20 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           pxa_read_u32(event.payload.data + 8) == 1 && store.count == 4 &&
           store.entries[0].id == 1 && store.entries[1].id == 2 &&
           store.entries[2].id == 7 && store.entries[3].id == UINT32_MAX);

    now_ms = 11000;
    assert(pxa_scheduler_take_due(scheduler, due, 4, &due_count) ==
               PXA_STATUS_OK &&
           due_count == 4 && due[0].id == 1 && due[1].id == 2 &&
           due[2].id == 7 && due[3].id == UINT32_MAX && store.count == 0 &&
           !pxa_scheduler_has_pending(scheduler));

    destroy_runtime(&test);
    free(scheduler_workspace);
}

static void test_net_service(void) {
    static const uint8_t identity[] = "net.app";
    static const uint8_t permission_name[] = "net.client";
    static const uint8_t origin[] = "https://example.test";
    static const uint8_t valid_url[] = "https://example.test:443/path";
    static const uint8_t invalid_url[] = "https://Example.test/path";
    static const uint8_t ipv4_url[] = "https://127.0.0.1/path";
    static const uint8_t numeric_url[] = "https://2130706433/path";
    static const uint8_t http_ipv4_url[] =
        "http://8.166.128.230:3100/api/weather";
    static const uint8_t invalid_http_ipv4_url[] =
        "http://999.166.128.230:3100/api/weather";
    pxa_permission_declaration_t declaration;
    pxa_permission_config_t permission_config;
    pxa_net_config_t net_config;
    test_net_backend_t backend;
    unsigned permission_loads = 0;
    test_runtime_t test = make_runtime(1, 10, 10, 6, 12, 8, 2);
    pxa_component_t component = create_started(test.runtime, 14);
    size_t permission_size;
    size_t net_size;
    void *permission_workspace;
    void *net_workspace;
    pxa_permission_service_t *permission = NULL;
    pxa_net_service_t *net = NULL;
    uint8_t command[192];
    uint8_t event_bytes[192];
    uint8_t body[8];
    size_t command_size;
    size_t event_size;
    pxa_message_view_t event;
    pxa_event_view_t view;
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    pxa_handle_t permission_handle;
    pxa_handle_t body_handle = PXA_HANDLE_INVALID;
    pxa_authority_t authority;
    pxa_component_t affected[1];
    size_t affected_count;
    pxa_bytes_t parsed_origin;

    assert(pxa_net_parse_https_url(
        (pxa_bytes_t){valid_url, sizeof(valid_url) - 1}, &parsed_origin));
    assert(parsed_origin.size == sizeof("https://example.test:443") - 1);
    assert(!pxa_net_parse_https_url(
        (pxa_bytes_t){invalid_url, sizeof(invalid_url) - 1},
        &parsed_origin));
    assert(!pxa_net_parse_https_url(
        (pxa_bytes_t){ipv4_url, sizeof(ipv4_url) - 1}, &parsed_origin));
    assert(!pxa_net_parse_https_url(
        (pxa_bytes_t){numeric_url, sizeof(numeric_url) - 1}, &parsed_origin));
    assert(pxa_net_parse_web_url(
        (pxa_bytes_t){http_ipv4_url, sizeof(http_ipv4_url) - 1},
        &parsed_origin));
    assert(parsed_origin.size == sizeof("http://8.166.128.230:3100") - 1);
    assert(!pxa_net_parse_web_url(
        (pxa_bytes_t){invalid_http_ipv4_url,
                      sizeof(invalid_http_ipv4_url) - 1},
        &parsed_origin));
    assert(pxa_net_parse_https_url(
        (pxa_bytes_t){(const uint8_t *)"https://example.test?x=1", 24},
        &parsed_origin));
    assert(!pxa_net_parse_https_url(
        (pxa_bytes_t){(const uint8_t *)"https://bad..test/", 18},
        &parsed_origin));
    assert(!pxa_net_parse_https_url(
        (pxa_bytes_t){(const uint8_t *)"https://example.test/%zz", 24},
        &parsed_origin));
    assert(pxa_net_header_name_valid(
        (pxa_bytes_t){(const uint8_t *)"authorization", 13}, 1));
    assert(!pxa_net_header_name_valid(
        (pxa_bytes_t){(const uint8_t *)"content-length", 14}, 1));
    assert(!pxa_net_header_value_valid(
        (pxa_bytes_t){(const uint8_t *)"bad\r\nvalue", 10}));
    assert(pxa_status_is_known(PXA_STATUS_TIMED_OUT) &&
           pxa_status_is_known(PXA_STATUS_LIMIT_EXCEEDED));

    memset(&permission_config, 0, sizeof(permission_config));
    declaration.name =
        (pxa_bytes_t){permission_name, sizeof(permission_name) - 1};
    declaration.scope = (pxa_bytes_t){origin, sizeof(origin) - 1};
    declaration.required = 1;
    permission_config.struct_size = sizeof(permission_config);
    permission_config.app_identity =
        (pxa_bytes_t){identity, sizeof(identity) - 1};
    permission_config.declarations = &declaration;
    permission_config.declaration_count = 1;
    permission_config.max_authorities = 2;
    permission_config.store.struct_size = sizeof(permission_config.store);
    permission_config.store.context = &permission_loads;
    permission_config.store.load = sensor_permission_load;
    permission_config.store.save = sensor_permission_save;
    permission_size =
        pxa_permission_service_workspace_size(&permission_config);
    permission_workspace = malloc(permission_size);
    assert(permission_workspace != NULL);
    assert(pxa_permission_service_init(
               permission_workspace, permission_size, test.runtime,
               &permission_config, &permission) == PXA_STATUS_OK);
    assert(pxa_permission_policy_load(permission) == PXA_STATUS_OK);
    assert(pxa_permission_service_register(permission) == PXA_STATUS_OK);

    memset(&backend, 0, sizeof(backend));
    memcpy(backend.body, "okay", 4);
    backend.body_size = 4;
    backend.expected_method = PXA_NET_METHOD_GET;
    memset(&net_config, 0, sizeof(net_config));
    net_config.struct_size = sizeof(net_config);
    net_config.max_pending_requests = 2;
    net_config.max_requests_per_component = 2;
    net_config.max_response_streams = 1;
    net_config.max_response_bytes = 8;
    net_config.max_headers = 8;
    net_config.max_inline_body_bytes = 2048;
    net_config.max_request_header_bytes = 2048;
    net_config.max_response_header_bytes = 2048;
    net_config.min_timeout_ms = 100;
    net_config.default_timeout_ms = 15000;
    net_config.max_timeout_ms = 60000;
    net_config.backend.struct_size = sizeof(net_config.backend);
    net_config.backend.context = &backend;
    net_config.backend.start = net_start;
    net_config.backend.poll = net_poll;
    net_config.backend.cancel = net_cancel;
    net_config.backend.read_body = net_read_body;
    net_config.backend.close_body = net_close_body;
    net_config.permissions = permission;
    net_size = pxa_net_service_workspace_size(&net_config);
    assert(net_size != 0);
    net_workspace = malloc(net_size);
    assert(net_workspace != NULL);
    assert(pxa_net_service_init(net_workspace, net_size, test.runtime,
                                &net_config, &net) == PXA_STATUS_OK);
    assert(pxa_net_service_register(net) == PXA_STATUS_OK);

    command_size = make_permission_request(
        command, sizeof(command), PXA_PERMISSION_ACQUIRE, 51,
        permission_name, sizeof(permission_name) - 1,
        origin, sizeof(origin) - 1);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert((int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK);
    permission_handle = pxa_read_u32(event.payload.data + 4);
    assert(pxa_permission_resolve(
               permission, component, permission_handle,
               (pxa_bytes_t){permission_name, sizeof(permission_name) - 1},
               (pxa_bytes_t){origin, sizeof(origin) - 1},
               &authority) == PXA_STATUS_OK);

    command_size = make_net_fetch(command, sizeof(command), 52,
                                  permission_handle);
    assert(pxa_component_begin_event(test.runtime, component) == PXA_STATUS_OK);
    assert(pxa_runtime_control(test.runtime, component, command, command_size) ==
           PXA_STATUS_OK);
    assert(pxa_component_finish_event(test.runtime, component, 1) ==
           PXA_STATUS_OK);
    assert(pxa_net_has_pending_requests(net) && backend.starts == 1);
    assert(pxa_net_poll(net, affected, 1, &affected_count) == PXA_STATUS_OK &&
           affected_count == 0);
    assert(pxa_net_poll(net, affected, 1, &affected_count) == PXA_STATUS_OK &&
           affected_count == 1 && affected[0] == component &&
           !pxa_net_has_pending_requests(net));
    event_size = read_head_event(test.runtime, component, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK);
    assert(event.opcode == PXA_NET_FETCH && event.request_id == 52 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK);
    pxa_record_iterator_init(
        &iterator,
        (pxa_bytes_t){event.payload.data + 4, event.payload.size - 4});
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 5 && pxa_read_u16(record.payload.data) == 200);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 6 && record.payload.size == 10 &&
           memcmp(record.payload.data, "text/plain", 10) == 0);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 7 && record.payload.size == 4);
    body_handle = pxa_read_u32(record.payload.data);
    assert(pxa_event_consume(test.runtime, component, view.token) ==
           PXA_STATUS_OK);
    memset(body, 0, sizeof(body));
    assert(dispatch_io(test.runtime, component, body_handle, PXA_NET_IO_READ,
                       body, sizeof(body)) == 2 &&
           memcmp(body, "ok", 2) == 0);
    assert(dispatch_io(test.runtime, component, body_handle, PXA_NET_IO_READ,
                       body, sizeof(body)) == 0);
    assert(pxa_handle_close(test.runtime, component, body_handle) ==
           PXA_STATUS_OK);
    assert(backend.closes == 1);
    body_handle = PXA_HANDLE_INVALID;

    backend.expected_method = PXA_NET_METHOD_POST;
    backend.expected_headers = 1;
    backend.expected_wanted_headers = 1;
    backend.expected_body_size = 2;
    command_size = make_net_http_request(command, sizeof(command), 53,
                                         permission_handle);
    assert(pxa_component_begin_event(test.runtime, component) == PXA_STATUS_OK);
    assert(pxa_runtime_control(test.runtime, component, command, command_size) ==
           PXA_STATUS_OK);
    assert(pxa_component_finish_event(test.runtime, component, 1) ==
           PXA_STATUS_OK);
    assert(pxa_net_poll(net, affected, 1, &affected_count) == PXA_STATUS_OK &&
           affected_count == 0);
    assert(pxa_net_poll(net, affected, 1, &affected_count) == PXA_STATUS_OK &&
           affected_count == 1);
    event_size = read_head_event(test.runtime, component, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK &&
           event.opcode == PXA_NET_HTTP_REQUEST && event.request_id == 53 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK);
    pxa_record_iterator_init(
        &iterator,
        (pxa_bytes_t){event.payload.data + 4, event.payload.size - 4});
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 5 && pxa_read_u16(record.payload.data) == 200);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 6);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 7);
    body_handle = pxa_read_u32(record.payload.data);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 9 && record.payload.size != 0);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 12 && pxa_read_u64(record.payload.data) == 4);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 13 &&
           pxa_read_u32(record.payload.data) ==
               (PXA_NET_RESPONSE_BODY_PRESENT |
                PXA_NET_RESPONSE_BODY_LENGTH_KNOWN));
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_WOULD_BLOCK);
    assert(pxa_event_consume(test.runtime, component, view.token) ==
           PXA_STATUS_OK);
    memset(body, 0, sizeof(body));
    assert(dispatch_io(test.runtime, component, body_handle, PXA_NET_IO_READ,
                       body, sizeof(body)) == 4 &&
           memcmp(body, "okay", 4) == 0);

    backend.expected_method = PXA_NET_METHOD_GET;
    command_size = make_net_fetch(command, sizeof(command), 54,
                                  permission_handle);
    assert(pxa_component_begin_event(test.runtime, component) == PXA_STATUS_OK);
    assert(pxa_runtime_control(test.runtime, component, command, command_size) ==
           PXA_STATUS_OK);
    assert(pxa_component_finish_event(test.runtime, component, 1) ==
           PXA_STATUS_OK);
    assert(pxa_request_cancel(test.runtime, component, 54) == PXA_STATUS_OK);
    assert(pxa_net_poll(net, affected, 1, &affected_count) == PXA_STATUS_OK &&
           affected_count == 1 && backend.cancels == 1 &&
           !pxa_net_has_pending_requests(net));
    event_size = read_head_event(test.runtime, component, event_bytes,
                                 sizeof(event_bytes), &view);
    assert(pxa_message_decode(event_bytes, event_size, PXA_MAX_CONTROL_MESSAGE,
                              &event) == PXA_STATUS_OK &&
           event.request_id == 54 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_CANCELLED);
    assert(pxa_event_consume(test.runtime, component, view.token) ==
           PXA_STATUS_OK);

    assert(pxa_authority_revoke(test.runtime, component, authority) ==
           PXA_STATUS_OK);
    assert(backend.closes == 2);
    assert(pxa_handle_get(test.runtime, component, body_handle,
                          PXA_RESOURCE_STREAM,
                          &(pxa_resource_t){0}) == PXA_STATUS_NOT_FOUND);

    destroy_runtime(&test);
    assert(backend.closes == 2);
    free(net_workspace);
    free(permission_workspace);
}

static void test_audio_service(void) {
    static const uint8_t identity[] = "audio.app";
    static const uint8_t permission_name[] = "audio.playback";
    static const uint8_t scope[] = "media";
    pxa_permission_declaration_t declaration;
    pxa_permission_config_t permission_config;
    pxa_audio_config_t audio_config;
    test_audio_backend_t backend;
    unsigned permission_loads = 0;
    test_runtime_t test = make_runtime(1, 10, 10, 5, 12, 8, 2);
    pxa_component_t component = create_started(test.runtime, 15);
    size_t permission_size;
    size_t audio_size;
    void *permission_workspace;
    void *audio_workspace;
    pxa_permission_service_t *permission = NULL;
    pxa_audio_service_t *audio = NULL;
    uint8_t command[128];
    uint8_t event_bytes[128];
    size_t command_size;
    pxa_message_view_t event;
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    pxa_handle_t permission_handle;
    pxa_handle_t session_handle = PXA_HANDLE_INVALID;
    pxa_handle_t second_session_handle = PXA_HANDLE_INVALID;
    pxa_handle_t reused_session_handle = PXA_HANDLE_INVALID;
    pxa_authority_t authority;

    memset(&permission_config, 0, sizeof(permission_config));
    declaration.name =
        (pxa_bytes_t){permission_name, sizeof(permission_name) - 1};
    declaration.scope = (pxa_bytes_t){scope, sizeof(scope) - 1};
    declaration.required = 1;
    permission_config.struct_size = sizeof(permission_config);
    permission_config.app_identity =
        (pxa_bytes_t){identity, sizeof(identity) - 1};
    permission_config.declarations = &declaration;
    permission_config.declaration_count = 1;
    permission_config.max_authorities = 2;
    permission_config.store.struct_size = sizeof(permission_config.store);
    permission_config.store.context = &permission_loads;
    permission_config.store.load = sensor_permission_load;
    permission_config.store.save = sensor_permission_save;
    permission_size =
        pxa_permission_service_workspace_size(&permission_config);
    permission_workspace = malloc(permission_size);
    assert(permission_workspace != NULL);
    assert(pxa_permission_service_init(
               permission_workspace, permission_size, test.runtime,
               &permission_config, &permission) == PXA_STATUS_OK);
    assert(pxa_permission_policy_load(permission) == PXA_STATUS_OK);
    assert(pxa_permission_service_register(permission) == PXA_STATUS_OK);

    memset(&backend, 0, sizeof(backend));
    backend.session = 7;
    memset(&audio_config, 0, sizeof(audio_config));
    audio_config.struct_size = sizeof(audio_config);
    audio_config.max_sessions = 2;
    audio_config.max_sessions_per_component = 2;
    audio_config.max_eq_bands = PXA_AUDIO_MAX_EQ_BANDS;
    audio_config.backend.struct_size = sizeof(audio_config.backend);
    audio_config.backend.context = &backend;
    audio_config.backend.open = audio_open;
    audio_config.backend.commit = audio_commit;
    audio_config.backend.submit = audio_submit;
    audio_config.backend.query = audio_query;
    audio_config.backend.flush = audio_flush;
    audio_config.backend.play_tone = audio_play_tone;
    audio_config.backend.close = audio_close;
    audio_config.permissions = permission;
    audio_size = pxa_audio_service_workspace_size(&audio_config);
    assert(audio_size != 0);
    audio_workspace = malloc(audio_size);
    assert(audio_workspace != NULL);
    assert(pxa_audio_service_init(audio_workspace, audio_size, test.runtime,
                                  &audio_config, &audio) == PXA_STATUS_OK);
    assert(pxa_audio_service_register(audio) == PXA_STATUS_OK);

    command_size = make_permission_request(
        command, sizeof(command), PXA_PERMISSION_ACQUIRE, 61,
        permission_name, sizeof(permission_name) - 1,
        scope, sizeof(scope) - 1);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    permission_handle = pxa_read_u32(event.payload.data + 4);
    assert(pxa_permission_resolve(
               permission, component, permission_handle,
               (pxa_bytes_t){permission_name, sizeof(permission_name) - 1},
               (pxa_bytes_t){scope, sizeof(scope) - 1},
               &authority) == PXA_STATUS_OK);

    command_size = make_audio_open(command, sizeof(command), 62,
                                   permission_handle);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 31 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           backend.opens == 1 && pxa_audio_has_active_sessions(audio));
    pxa_record_iterator_init(
        &iterator,
        (pxa_bytes_t){event.payload.data + 4, event.payload.size - 4});
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 3 && record.payload.size == 4);
    session_handle = pxa_read_u32(record.payload.data);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 4 && pxa_read_u32(record.payload.data) == 16000);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 5 && record.payload.data[0] == 1);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 6 && pxa_read_u16(record.payload.data) == 20);

    command_size = make_audio_open(command, sizeof(command), 63,
                                   permission_handle);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 31 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           backend.opens == 2 && pxa_audio_has_active_sessions(audio));
    pxa_record_iterator_init(
        &iterator,
        (pxa_bytes_t){event.payload.data + 4, event.payload.size - 4});
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 3 && record.payload.size == 4);
    second_session_handle = pxa_read_u32(record.payload.data);

    command_size = make_audio_open(command, sizeof(command), 64,
                                   permission_handle);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 4 &&
           (int32_t)pxa_read_u32(event.payload.data) ==
               PXA_STATUS_RESOURCE_LIMIT &&
           backend.opens == 2);

    assert(pxa_handle_close(test.runtime, component, session_handle) ==
           PXA_STATUS_OK);
    assert(backend.closes == 1 && pxa_audio_has_active_sessions(audio));
    assert(pxa_handle_get(test.runtime, component, session_handle,
                          PXA_RESOURCE_AUDIO_GRAPH,
                          &(pxa_resource_t){0}) == PXA_STATUS_NOT_FOUND);
    assert(pxa_handle_get(test.runtime, component, second_session_handle,
                          PXA_RESOURCE_AUDIO_GRAPH,
                          &(pxa_resource_t){0}) == PXA_STATUS_OK);

    command_size = make_audio_open(command, sizeof(command), 65,
                                   permission_handle);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 31 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           backend.opens == 3 && pxa_audio_has_active_sessions(audio));
    pxa_record_iterator_init(
        &iterator,
        (pxa_bytes_t){event.payload.data + 4, event.payload.size - 4});
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 3 && record.payload.size == 4);
    reused_session_handle = pxa_read_u32(record.payload.data);
    assert(reused_session_handle != session_handle);

    command_size = make_audio_graph(command, sizeof(command), 66,
                                    reused_session_handle);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 4 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           backend.commits == 1);

    {
        uint8_t pcm[] = {0, 0, 1, 0};
        assert(dispatch_io(test.runtime, component, reused_session_handle,
                           PXA_IO_WRITE, pcm, sizeof(pcm)) ==
               (int32_t)sizeof(pcm));
        assert(backend.submits == 1);
    }
    {
        uint8_t tone[] = {0xb8, 0x01, 80, 0, 0, 0xfa,
                          PXA_AUDIO_TONE_TRIANGLE, 0};
        assert(dispatch_io(test.runtime, component, reused_session_handle,
                           PXA_AUDIO_IO_PLAY_TONE, tone, sizeof(tone)) ==
               (int32_t)sizeof(tone));
        assert(backend.tones == 1);
        tone[7] = 1;
        assert(dispatch_io(test.runtime, component, reused_session_handle,
                           PXA_AUDIO_IO_PLAY_TONE, tone, sizeof(tone)) ==
               PXA_STATUS_INVALID_ARGUMENT);
    }

    command_size = make_audio_session_command(
        command, sizeof(command), PXA_AUDIO_QUERY_STATE, 67,
        reused_session_handle);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 44 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           backend.queries == 1);
    pxa_record_iterator_init(
        &iterator,
        (pxa_bytes_t){event.payload.data + 4, event.payload.size - 4});
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 2 && pxa_read_u64(record.payload.data) == 320);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 3 && pxa_read_u64(record.payload.data) == 256);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 4 && pxa_read_u32(record.payload.data) == 64);
    assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK &&
           record.tag == 5 && pxa_read_u32(record.payload.data) ==
                                  PXA_AUDIO_STATE_ACCEPTED_IS_SINK_SUBMITTED);

    command_size = make_audio_session_command(
        command, sizeof(command), PXA_AUDIO_FLUSH, 68,
        reused_session_handle);
    (void)dispatch_control_completion(
        test.runtime, component, command, command_size, event_bytes,
        sizeof(event_bytes), &event);
    assert(event.payload.size == 4 &&
           (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_OK &&
           backend.flushes == 1);

    assert(pxa_authority_revoke(test.runtime, component, authority) ==
           PXA_STATUS_OK);
    assert(backend.closes == 3 && !pxa_audio_has_active_sessions(audio));
    assert(pxa_handle_get(test.runtime, component, second_session_handle,
                          PXA_RESOURCE_AUDIO_GRAPH,
                          &(pxa_resource_t){0}) == PXA_STATUS_NOT_FOUND);
    assert(pxa_handle_get(test.runtime, component, reused_session_handle,
                          PXA_RESOURCE_AUDIO_GRAPH,
                          &(pxa_resource_t){0}) == PXA_STATUS_NOT_FOUND);

    destroy_runtime(&test);
    assert(backend.closes == 3);
    free(audio_workspace);
    free(permission_workspace);
}

int main(void) {
    test_runtime_workspace_contract();
    test_runtime_usage();
    test_wire();
    test_lifecycle();
    test_requests_and_mailbox();
    test_request_table_collision_and_completion_order();
    test_coalescing_and_reserve();
    test_event_block_boundaries_and_stale_tokens();
    test_event_generation_exhaustion();
    test_handles_authority_and_cleanup();
    test_handle_generation_exhaustion();
    test_service_dispatch_and_io();
    test_service_registry_collision_and_capacity();
    test_window_service();
    test_lease_service();
    test_permission_service();
    test_ipc_service();
    test_storage_service();
    test_fs_service();
    test_sensor_service();
    test_device_service();
    test_scheduler_service();
    test_empty_scheduler_service();
    test_scheduler_sorted_load();
    test_net_service();
    test_audio_service();
    return 0;
}
