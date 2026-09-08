#include "pxa/posix/pxa_posix_scheduler_store.h"

#include <stdint.h>
#include <string.h>

#include "pxa/wire.h"

#define PXA_POSIX_SCHEDULER_STORE_MAGIC UINT32_C(0x31575850)
#define PXA_POSIX_SCHEDULER_STORE_ALIGNMENT ((size_t)16)
#define PXA_POSIX_SCHEDULER_STORE_VERSION UINT16_C(1)
#define PXA_POSIX_SCHEDULER_STORE_HEADER_BYTES ((size_t)16)
#define PXA_POSIX_SCHEDULER_STORE_ENTRY_BYTES ((size_t)112)

struct pxa_posix_scheduler_store {
    uint32_t magic;
    pxa_storage_backend_t backend;
    uint16_t max_entries;
    uint64_t epoch;
    uint8_t key_size;
    uint8_t key[PXA_STORAGE_MAX_KEY_BYTES];
    uint8_t buffer[];
};

static uintptr_t align_up(uintptr_t value, size_t alignment) {
    uintptr_t mask = (uintptr_t)alignment - 1u;
    return (value + mask) & ~mask;
}

static int config_valid(const pxa_posix_scheduler_store_config_t *config) {
    return config != NULL && config->struct_size >= sizeof(*config) &&
           config->backend.struct_size >= sizeof(config->backend) &&
           config->backend.get != NULL && config->backend.set != NULL &&
           pxa_storage_key_is_valid(config->key) && config->max_entries != 0 &&
           config->epoch != 0;
}

static size_t encoded_capacity(uint16_t max_entries) {
    if (max_entries == 0) return 0;
    if (PXA_POSIX_SCHEDULER_STORE_ENTRY_BYTES >
        (SIZE_MAX - PXA_POSIX_SCHEDULER_STORE_HEADER_BYTES) /
            (size_t)max_entries) {
        return 0;
    }
    return PXA_POSIX_SCHEDULER_STORE_HEADER_BYTES +
           (size_t)max_entries * PXA_POSIX_SCHEDULER_STORE_ENTRY_BYTES;
}

static int store_valid(const pxa_posix_scheduler_store_t *store) {
    return store != NULL && store->magic == PXA_POSIX_SCHEDULER_STORE_MAGIC;
}

static pxa_bytes_t store_key(const pxa_posix_scheduler_store_t *store) {
    return (pxa_bytes_t){store->key, store->key_size};
}

static pxa_status_t store_load(void *context, pxa_scheduler_entry_t *entries,
                               size_t capacity, size_t *count) {
    pxa_posix_scheduler_store_t *store =
        (pxa_posix_scheduler_store_t *)context;
    size_t encoded_size = 0;
    size_t expected_size;
    uint16_t stored_count;
    uint16_t version;
    uint16_t index;
    pxa_status_t status;
    if (count == NULL || !store_valid(store) ||
        (entries == NULL && capacity != 0)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *count = 0;
    status = store->backend.get(store->backend.context, store_key(store),
                                store->buffer,
                                encoded_capacity(store->max_entries),
                                &encoded_size);
    if (status == PXA_STATUS_NOT_FOUND) return PXA_STATUS_OK;
    if (status != PXA_STATUS_OK) return status;
    if (encoded_size < PXA_POSIX_SCHEDULER_STORE_HEADER_BYTES ||
        pxa_read_u32(store->buffer) != PXA_POSIX_SCHEDULER_STORE_MAGIC) {
        return PXA_STATUS_DENIED;
    }
    version = pxa_read_u16(store->buffer + 4);
    if (version != PXA_POSIX_SCHEDULER_STORE_VERSION) {
        return PXA_STATUS_DENIED;
    }
    stored_count = pxa_read_u16(store->buffer + 6);
    if (pxa_read_u64(store->buffer + 8) != store->epoch) {
        return PXA_STATUS_OK;
    }
    if (stored_count > store->max_entries) return PXA_STATUS_DENIED;
    expected_size = PXA_POSIX_SCHEDULER_STORE_HEADER_BYTES +
                    (size_t)stored_count * PXA_POSIX_SCHEDULER_STORE_ENTRY_BYTES;
    if (encoded_size != expected_size) return PXA_STATUS_DENIED;
    if ((size_t)stored_count > capacity) return PXA_STATUS_RESOURCE_LIMIT;
    for (index = 0; index < stored_count; ++index) {
        const uint8_t *input =
            store->buffer + PXA_POSIX_SCHEDULER_STORE_HEADER_BYTES +
            (size_t)index * PXA_POSIX_SCHEDULER_STORE_ENTRY_BYTES;
        uint8_t component_id_size = input[20];
        uint8_t input_size = input[23];
        if (component_id_size == 0 ||
            component_id_size > PXA_SCHEDULER_MAX_COMPONENT_ID_BYTES) {
            return PXA_STATUS_DENIED;
        }
        memset(&entries[index], 0, sizeof(entries[index]));
        entries[index].id = pxa_read_u32(input);
        entries[index].due_at_ms = pxa_read_u64(input + 4);
        entries[index].max_execution_ms = pxa_read_u32(input + 12);
        if (input_size > PXA_WORK_MAX_INPUT_BYTES) {
            return PXA_STATUS_DENIED;
        }
        entries[index].retry_delay_ms = pxa_read_u32(input + 16);
        entries[index].component_id_size = component_id_size;
        entries[index].attempt = input[21];
        entries[index].max_attempts = input[22];
        entries[index].input_size = input_size;
        memcpy(entries[index].component_id, input + 24,
               entries[index].component_id_size);
        memcpy(entries[index].input,
               input + 24 + PXA_SCHEDULER_MAX_COMPONENT_ID_BYTES,
               input_size);
    }
    *count = stored_count;
    return PXA_STATUS_OK;
}

static pxa_status_t store_save(void *context,
                               const pxa_scheduler_entry_t *entries,
                               size_t count) {
    pxa_posix_scheduler_store_t *store =
        (pxa_posix_scheduler_store_t *)context;
    size_t encoded_size;
    size_t index;
    if (!store_valid(store) || (entries == NULL && count != 0)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (count > store->max_entries || count > UINT16_MAX) {
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    encoded_size = PXA_POSIX_SCHEDULER_STORE_HEADER_BYTES +
                   count * PXA_POSIX_SCHEDULER_STORE_ENTRY_BYTES;
    pxa_write_u32(store->buffer, PXA_POSIX_SCHEDULER_STORE_MAGIC);
    pxa_write_u16(store->buffer + 4, PXA_POSIX_SCHEDULER_STORE_VERSION);
    pxa_write_u16(store->buffer + 6, (uint16_t)count);
    pxa_write_u64(store->buffer + 8, store->epoch);
    for (index = 0; index < count; ++index) {
        uint8_t *output = store->buffer +
                          PXA_POSIX_SCHEDULER_STORE_HEADER_BYTES +
                          index * PXA_POSIX_SCHEDULER_STORE_ENTRY_BYTES;
        const pxa_scheduler_entry_t *entry = &entries[index];
        if (entry->component_id_size == 0 ||
            entry->component_id_size > PXA_SCHEDULER_MAX_COMPONENT_ID_BYTES ||
            entry->input_size > PXA_WORK_MAX_INPUT_BYTES ||
            entry->attempt == 0 || entry->max_attempts == 0 ||
            entry->attempt > entry->max_attempts ||
            entry->max_attempts > PXA_WORK_MAX_ATTEMPTS) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        pxa_write_u32(output, entry->id);
        pxa_write_u64(output + 4, entry->due_at_ms);
        pxa_write_u32(output + 12, entry->max_execution_ms);
        pxa_write_u32(output + 16, entry->retry_delay_ms);
        output[20] = entry->component_id_size;
        output[21] = entry->attempt;
        output[22] = entry->max_attempts;
        output[23] = entry->input_size;
        memcpy(output + 24, entry->component_id, entry->component_id_size);
        memset(output + 24 + entry->component_id_size, 0,
               PXA_SCHEDULER_MAX_COMPONENT_ID_BYTES -
                   entry->component_id_size);
        memcpy(output + 24 + PXA_SCHEDULER_MAX_COMPONENT_ID_BYTES,
               entry->input, entry->input_size);
        memset(output + 24 + PXA_SCHEDULER_MAX_COMPONENT_ID_BYTES +
                   entry->input_size,
               0, PXA_WORK_MAX_INPUT_BYTES - entry->input_size);
    }
    return store->backend.set(store->backend.context, store_key(store),
                              (pxa_bytes_t){store->buffer, encoded_size});
}

size_t pxa_posix_scheduler_store_workspace_size(
    const pxa_posix_scheduler_store_config_t *config) {
    size_t encoded_size;
    size_t size = PXA_POSIX_SCHEDULER_STORE_ALIGNMENT - 1u;
    if (!config_valid(config)) return 0;
    encoded_size = encoded_capacity(config->max_entries);
    if (encoded_size == 0 || sizeof(pxa_posix_scheduler_store_t) >
                                SIZE_MAX - size) {
        return 0;
    }
    size += sizeof(pxa_posix_scheduler_store_t);
    if (encoded_size > SIZE_MAX - size) return 0;
    return size + encoded_size;
}

pxa_status_t pxa_posix_scheduler_store_init(
    void *workspace, size_t workspace_size,
    const pxa_posix_scheduler_store_config_t *config,
    pxa_posix_scheduler_store_t **output,
    pxa_scheduler_store_t *scheduler_store_output) {
    pxa_posix_scheduler_store_t *store;
    size_t required;
    size_t encoded_size;
    uintptr_t workspace_start;
    uintptr_t workspace_end;
    uintptr_t store_start;
    if (output == NULL || scheduler_store_output == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *output = NULL;
    memset(scheduler_store_output, 0, sizeof(*scheduler_store_output));
    required = pxa_posix_scheduler_store_workspace_size(config);
    if (workspace == NULL || required == 0 || workspace_size < required) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    workspace_start = (uintptr_t)workspace;
    if (workspace_size > UINTPTR_MAX - workspace_start) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    workspace_end = workspace_start + workspace_size;
    store_start = align_up(workspace_start, PXA_POSIX_SCHEDULER_STORE_ALIGNMENT);
    if (store_start < workspace_start || store_start > workspace_end) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    store = (pxa_posix_scheduler_store_t *)store_start;
    encoded_size = encoded_capacity(config->max_entries);
    if (sizeof(*store) > workspace_end - store_start ||
        encoded_size > workspace_end - store_start - sizeof(*store)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    memset(store, 0, sizeof(*store) + encoded_size);
    store->magic = PXA_POSIX_SCHEDULER_STORE_MAGIC;
    store->backend = config->backend;
    store->max_entries = config->max_entries;
    store->epoch = config->epoch;
    store->key_size = (uint8_t)config->key.size;
    memcpy(store->key, config->key.data, config->key.size);
    scheduler_store_output->struct_size = sizeof(*scheduler_store_output);
    scheduler_store_output->context = store;
    scheduler_store_output->load = store_load;
    scheduler_store_output->save = store_save;
    *output = store;
    return PXA_STATUS_OK;
}

void pxa_posix_scheduler_store_deinit(pxa_posix_scheduler_store_t *store) {
    if (!store_valid(store)) return;
    store->magic = 0;
}
