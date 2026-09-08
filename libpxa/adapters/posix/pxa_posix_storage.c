#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "pxa/posix/pxa_posix_storage.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pxa/wire.h"

#ifdef ESP_PLATFORM
#include "pxa/esp/pxa_esp_posix_shim.h"
#define dup pxa_esp_dup
#define fstat pxa_esp_fstat
#define openat pxa_esp_openat
#define fsync pxa_esp_fsync
#endif

#define PXA_POSIX_STORAGE_MAGIC UINT32_C(0x50505354)
#define PXA_POSIX_STORAGE_ALIGNMENT ((size_t)16)
#define PXA_POSIX_STORAGE_HEADER_BYTES ((size_t)24)
#define PXA_POSIX_STORAGE_FORMAT_VERSION UINT16_C(1)
#define PXA_POSIX_STORAGE_SLOT_A ".pxa-kv-a"
#define PXA_POSIX_STORAGE_SLOT_B ".pxa-kv-b"

static const uint8_t pxa_posix_storage_file_magic[4] = {'P', 'X', 'K', 'V'};

typedef struct {
    pxa_bytes_t key;
    pxa_bytes_t value;
} pxa_posix_storage_entry_t;

struct pxa_posix_storage {
    uint32_t magic;
    int root_fd;
    uint16_t max_keys;
    size_t max_value_bytes;
    size_t quota_bytes;
    size_t body_capacity;
    size_t body_size;
    uint64_t generation;
    uint8_t active_slot;
    uint8_t *body;
    uint8_t *candidate;
};

static uintptr_t align_up(uintptr_t value, size_t alignment) {
    uintptr_t mask = (uintptr_t)alignment - 1u;
    return (value + mask) & ~mask;
}

static pxa_status_t status_from_errno(int error) {
    if (error == ENOENT) return PXA_STATUS_NOT_FOUND;
    if (error == EEXIST) return PXA_STATUS_BUSY;
    if (error == ENOSPC || error == EDQUOT || error == EFBIG) {
        return PXA_STATUS_QUOTA_EXCEEDED;
    }
    return PXA_STATUS_INTERNAL;
}

static int bytes_compare(pxa_bytes_t left, pxa_bytes_t right) {
    size_t common = left.size < right.size ? left.size : right.size;
    int result = common == 0 ? 0 : memcmp(left.data, right.data, common);
    if (result != 0) return result;
    if (left.size < right.size) return -1;
    if (left.size > right.size) return 1;
    return 0;
}

static uint32_t crc32(const uint8_t *bytes, size_t size) {
    uint32_t value = UINT32_MAX;
    size_t index;
    for (index = 0; index < size; ++index) {
        uint8_t bit;
        value ^= bytes[index];
        for (bit = 0; bit < 8; ++bit) {
            value = (value >> 1) ^
                    (UINT32_C(0xedb88320) & (0u - (value & 1u)));
        }
    }
    return ~value;
}

static pxa_status_t read_exact(int file, uint8_t *output, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        ssize_t count;
        do {
            count = read(file, output + offset, size - offset);
        } while (count < 0 && errno == EINTR);
        if (count <= 0) return PXA_STATUS_INTERNAL;
        offset += (size_t)count;
    }
    return PXA_STATUS_OK;
}

static pxa_status_t write_all(int file, const uint8_t *input, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        ssize_t count;
        do {
            count = write(file, input + offset, size - offset);
        } while (count < 0 && errno == EINTR);
        if (count <= 0) return status_from_errno(errno);
        offset += (size_t)count;
    }
    return PXA_STATUS_OK;
}

static pxa_status_t parse_entry(const uint8_t *body, size_t body_size,
                                size_t *offset,
                                pxa_posix_storage_entry_t *entry) {
    size_t key_size;
    size_t value_size;
    if (*offset >= body_size) return PXA_STATUS_INVALID_ARGUMENT;
    key_size = body[(*offset)++];
    if (key_size == 0 || key_size > body_size - *offset) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    entry->key.data = body + *offset;
    entry->key.size = key_size;
    *offset += key_size;
    if (body_size - *offset < 2) return PXA_STATUS_INVALID_ARGUMENT;
    value_size = pxa_read_u16(body + *offset);
    *offset += 2;
    if (value_size > body_size - *offset) return PXA_STATUS_INVALID_ARGUMENT;
    entry->value.data = body + *offset;
    entry->value.size = value_size;
    *offset += value_size;
    return PXA_STATUS_OK;
}

static pxa_status_t validate_body(const pxa_posix_storage_t *storage,
                                  const uint8_t *body, size_t body_size) {
    pxa_bytes_t previous = {NULL, 0};
    uint16_t count;
    size_t value_total = 0;
    size_t offset = 2;
    uint16_t index;
    if (body_size < 2) return PXA_STATUS_INVALID_ARGUMENT;
    count = pxa_read_u16(body);
    if (count > storage->max_keys) return PXA_STATUS_RESOURCE_LIMIT;
    for (index = 0; index < count; ++index) {
        pxa_posix_storage_entry_t entry;
        pxa_status_t status = parse_entry(body, body_size, &offset, &entry);
        if (status != PXA_STATUS_OK ||
            !pxa_storage_key_is_valid(entry.key) ||
            entry.value.size > storage->max_value_bytes ||
            (previous.size != 0 && bytes_compare(previous, entry.key) >= 0)) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        if (entry.value.size > storage->quota_bytes - value_total) {
            return PXA_STATUS_QUOTA_EXCEEDED;
        }
        value_total += entry.value.size;
        previous = entry.key;
    }
    return offset == body_size ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
}

static pxa_status_t read_slot(pxa_posix_storage_t *storage, const char *name,
                              uint8_t *body, size_t *body_size,
                              uint64_t *generation, int *exists) {
    uint8_t header[PXA_POSIX_STORAGE_HEADER_BYTES];
    struct stat metadata;
    uint32_t encoded_body_size;
    int file;
    pxa_status_t status;
    *body_size = 0;
    *generation = 0;
    *exists = 0;
    file = openat(storage->root_fd, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (file < 0) {
        if (errno == ENOENT) return PXA_STATUS_NOT_FOUND;
        *exists = 1;
        return status_from_errno(errno);
    }
    *exists = 1;
    if (fstat(file, &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
        metadata.st_nlink != 1 || metadata.st_size < 0 ||
        (uint64_t)metadata.st_size < PXA_POSIX_STORAGE_HEADER_BYTES ||
        (uint64_t)metadata.st_size >
            PXA_POSIX_STORAGE_HEADER_BYTES + storage->body_capacity) {
        close(file);
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    status = read_exact(file, header, sizeof(header));
    if (status != PXA_STATUS_OK ||
        memcmp(header, pxa_posix_storage_file_magic,
               sizeof(pxa_posix_storage_file_magic)) != 0 ||
        pxa_read_u16(header + 4) != PXA_POSIX_STORAGE_FORMAT_VERSION ||
        pxa_read_u16(header + 6) != 0) {
        close(file);
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    encoded_body_size = pxa_read_u32(header + 16);
    if ((size_t)encoded_body_size > storage->body_capacity ||
        (uint64_t)metadata.st_size !=
            PXA_POSIX_STORAGE_HEADER_BYTES + encoded_body_size) {
        close(file);
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    status = read_exact(file, body, encoded_body_size);
    if (close(file) != 0 && status == PXA_STATUS_OK) {
        status = PXA_STATUS_INTERNAL;
    }
    if (status != PXA_STATUS_OK ||
        crc32(body, encoded_body_size) != pxa_read_u32(header + 20)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    status = validate_body(storage, body, encoded_body_size);
    if (status != PXA_STATUS_OK) return status;
    *body_size = encoded_body_size;
    *generation = pxa_read_u64(header + 8);
    return PXA_STATUS_OK;
}

static pxa_status_t append_entry(uint8_t *body, size_t capacity,
                                 size_t *offset, pxa_bytes_t key,
                                 pxa_bytes_t value) {
    size_t required = 1u + key.size + 2u + value.size;
    if (*offset > capacity || required > capacity - *offset) {
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    body[(*offset)++] = (uint8_t)key.size;
    memcpy(body + *offset, key.data, key.size);
    *offset += key.size;
    pxa_write_u16(body + *offset, (uint16_t)value.size);
    *offset += 2;
    if (value.size != 0) memcpy(body + *offset, value.data, value.size);
    *offset += value.size;
    return PXA_STATUS_OK;
}

static pxa_status_t persist_candidate(pxa_posix_storage_t *storage,
                                      size_t candidate_size) {
    uint8_t header[PXA_POSIX_STORAGE_HEADER_BYTES] = {0};
    struct stat metadata;
    uint64_t generation;
    uint8_t next_slot;
    const char* name;
    int file;
    int directory;
    pxa_status_t status;
    uint8_t* temporary;
    if (storage->generation == UINT64_MAX)
        return PXA_STATUS_RESOURCE_LIMIT;
    generation = storage->generation + 1u;
    next_slot = storage->active_slot == 0 ? 1 : 0;
    name = next_slot == 0 ? PXA_POSIX_STORAGE_SLOT_A : PXA_POSIX_STORAGE_SLOT_B;
    memcpy(header, pxa_posix_storage_file_magic, sizeof(pxa_posix_storage_file_magic));
    pxa_write_u16(header + 4, PXA_POSIX_STORAGE_FORMAT_VERSION);
    pxa_write_u64(header + 8, generation);
    pxa_write_u32(header + 16, (uint32_t)candidate_size);
    pxa_write_u32(header + 20, crc32(storage->candidate, candidate_size));
    file = openat(storage->root_fd, name, O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (file < 0)
        return status_from_errno(errno);
    if (fstat(file, &metadata) != 0) {
        const int error = errno;
        close(file);
        return status_from_errno(error);
    }
    if (!S_ISREG(metadata.st_mode) || metadata.st_nlink != 1) {
        close(file);
        return PXA_STATUS_DENIED;
    }
    if (ftruncate(file, 0) != 0) {
        const int error = errno;
        close(file);
        return status_from_errno(error);
    }
    status = write_all(file, header, sizeof(header));
    if (status == PXA_STATUS_OK) {
        status = write_all(file, storage->candidate, candidate_size);
    }
    if (status == PXA_STATUS_OK && fsync(file) != 0) {
        status = PXA_STATUS_INTERNAL;
    }
    if (close(file) != 0 && status == PXA_STATUS_OK) {
        status = PXA_STATUS_INTERNAL;
    }
    if (status != PXA_STATUS_OK)
        return status;
    directory = dup(storage->root_fd);
    if (directory < 0)
        return PXA_STATUS_INTERNAL;
    if (fsync(directory) != 0)
        status = PXA_STATUS_INTERNAL;
    if (close(directory) != 0 && status == PXA_STATUS_OK) {
        status = PXA_STATUS_INTERNAL;
    }
    if (status != PXA_STATUS_OK)
        return status;
    temporary = storage->body;
    storage->body = storage->candidate;
    storage->candidate = temporary;
    storage->body_size = candidate_size;
    storage->generation = generation;
    storage->active_slot = next_slot;
    return PXA_STATUS_OK;
}

static pxa_status_t find_entry(const pxa_posix_storage_t* storage, pxa_bytes_t key,
                               pxa_posix_storage_entry_t* output, size_t* value_total) {
    uint16_t count = pxa_read_u16(storage->body);
    size_t offset = 2;
    uint16_t index;
    if (value_total != NULL)
        *value_total = 0;
    for (index = 0; index < count; ++index) {
        pxa_posix_storage_entry_t entry;
        if (parse_entry(storage->body, storage->body_size, &offset, &entry) != PXA_STATUS_OK) {
            return PXA_STATUS_INTERNAL;
        }
        if (value_total != NULL)
            *value_total += entry.value.size;
        if (bytes_compare(entry.key, key) == 0 && output != NULL) {
            *output = entry;
        }
    }
    if (output != NULL && output->key.data == NULL) {
        return PXA_STATUS_NOT_FOUND;
    }
    return PXA_STATUS_OK;
}

static pxa_status_t get_backend(void *context, pxa_bytes_t key,
                                uint8_t *output, size_t capacity,
                                size_t *size) {
    pxa_posix_storage_t *storage = (pxa_posix_storage_t *)context;
    pxa_posix_storage_entry_t entry = {{NULL, 0}, {NULL, 0}};
    pxa_status_t status;
    if (size == NULL || (output == NULL && capacity != 0) ||
        !pxa_storage_key_is_valid(key)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *size = 0;
    status = find_entry(storage, key, &entry, NULL);
    if (status != PXA_STATUS_OK) return status;
    if (entry.value.size > capacity) return PXA_STATUS_QUOTA_EXCEEDED;
    if (entry.value.size != 0) {
        memcpy(output, entry.value.data, entry.value.size);
    }
    *size = entry.value.size;
    return PXA_STATUS_OK;
}

static pxa_status_t set_backend(void *context, pxa_bytes_t key,
                                pxa_bytes_t value) {
    pxa_posix_storage_t *storage = (pxa_posix_storage_t *)context;
    pxa_posix_storage_entry_t existing = {{NULL, 0}, {NULL, 0}};
    uint16_t old_count;
    uint16_t new_count;
    size_t value_total;
    size_t offset = 2;
    size_t source_offset = 2;
    uint16_t index;
    int inserted = 0;
    pxa_status_t status;
    if (!pxa_storage_key_is_valid(key) ||
        (value.data == NULL && value.size != 0) ||
        value.size > storage->max_value_bytes) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    status = find_entry(storage, key, &existing, &value_total);
    if (status != PXA_STATUS_OK && status != PXA_STATUS_NOT_FOUND) return status;
    if (status == PXA_STATUS_OK && existing.value.size == value.size &&
        (value.size == 0 || memcmp(existing.value.data, value.data, value.size) == 0))
        return PXA_STATUS_OK;
    old_count = pxa_read_u16(storage->body);
    new_count = old_count;
    if (status == PXA_STATUS_NOT_FOUND) {
        if (old_count >= storage->max_keys || old_count == UINT16_MAX) {
            return PXA_STATUS_RESOURCE_LIMIT;
        }
        new_count = (uint16_t)(old_count + 1u);
    } else {
        value_total -= existing.value.size;
    }
    if (value.size > storage->quota_bytes - value_total) {
        return PXA_STATUS_QUOTA_EXCEEDED;
    }
    pxa_write_u16(storage->candidate, new_count);
    for (index = 0; index < old_count; ++index) {
        pxa_posix_storage_entry_t entry;
        status = parse_entry(storage->body, storage->body_size, &source_offset,
                             &entry);
        if (status != PXA_STATUS_OK) return PXA_STATUS_INTERNAL;
        if (!inserted && bytes_compare(key, entry.key) < 0) {
            status = append_entry(storage->candidate, storage->body_capacity,
                                  &offset, key, value);
            if (status != PXA_STATUS_OK) return status;
            inserted = 1;
        }
        if (bytes_compare(key, entry.key) == 0) {
            status = append_entry(storage->candidate, storage->body_capacity,
                                  &offset, key, value);
            inserted = 1;
        } else {
            status = append_entry(storage->candidate, storage->body_capacity,
                                  &offset, entry.key, entry.value);
        }
        if (status != PXA_STATUS_OK) return status;
    }
    if (!inserted) {
        status = append_entry(storage->candidate, storage->body_capacity,
                              &offset, key, value);
        if (status != PXA_STATUS_OK) return status;
    }
    return persist_candidate(storage, offset);
}

static pxa_status_t remove_backend(void *context, pxa_bytes_t key) {
    pxa_posix_storage_t *storage = (pxa_posix_storage_t *)context;
    pxa_posix_storage_entry_t existing = {{NULL, 0}, {NULL, 0}};
    uint16_t old_count;
    size_t offset = 2;
    size_t source_offset = 2;
    uint16_t index;
    pxa_status_t status;
    if (!pxa_storage_key_is_valid(key)) return PXA_STATUS_INVALID_ARGUMENT;
    status = find_entry(storage, key, &existing, NULL);
    if (status != PXA_STATUS_OK) return status;
    old_count = pxa_read_u16(storage->body);
    pxa_write_u16(storage->candidate, (uint16_t)(old_count - 1u));
    for (index = 0; index < old_count; ++index) {
        pxa_posix_storage_entry_t entry;
        status = parse_entry(storage->body, storage->body_size, &source_offset,
                             &entry);
        if (status != PXA_STATUS_OK) return PXA_STATUS_INTERNAL;
        if (bytes_compare(key, entry.key) == 0) continue;
        status = append_entry(storage->candidate, storage->body_capacity,
                              &offset, entry.key, entry.value);
        if (status != PXA_STATUS_OK) return status;
    }
    return persist_candidate(storage, offset);
}

static pxa_status_t list_backend(void *context, pxa_bytes_t cursor,
                                 pxa_storage_emit_key_fn emit,
                                 void *emit_context) {
    pxa_posix_storage_t *storage = (pxa_posix_storage_t *)context;
    uint16_t count;
    size_t offset = 2;
    uint16_t index;
    if (emit == NULL || (cursor.data == NULL && cursor.size != 0)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    count = pxa_read_u16(storage->body);
    for (index = 0; index < count; ++index) {
        pxa_posix_storage_entry_t entry;
        pxa_status_t status =
            parse_entry(storage->body, storage->body_size, &offset, &entry);
        if (status != PXA_STATUS_OK) return PXA_STATUS_INTERNAL;
        if (cursor.size != 0 && bytes_compare(entry.key, cursor) <= 0) continue;
        status = emit(emit_context, entry.key);
        if (status != PXA_STATUS_OK) return status;
    }
    return PXA_STATUS_OK;
}

static int config_valid(const pxa_posix_storage_config_t *config) {
    size_t metadata_bytes;
    return config != NULL && config->struct_size >= sizeof(*config) &&
           config->root_path != NULL && config->root_path[0] != '\0' &&
           config->max_keys != 0 && config->max_value_bytes != 0 &&
           config->max_value_bytes <= PXA_STORAGE_MAX_VALUE_BYTES &&
           config->quota_bytes != 0 &&
           (metadata_bytes = 2u +
                             (size_t)config->max_keys *
                                 (1u + PXA_STORAGE_MAX_KEY_BYTES + 2u),
            metadata_bytes <= UINT32_MAX &&
                config->quota_bytes <= UINT32_MAX - metadata_bytes);
}

static size_t body_capacity(const pxa_posix_storage_config_t *config) {
    return 2u + (size_t)config->max_keys *
                    (1u + PXA_STORAGE_MAX_KEY_BYTES + 2u) +
           config->quota_bytes;
}

size_t pxa_posix_storage_workspace_size(
    const pxa_posix_storage_config_t *config) {
    size_t capacity;
    size_t size;
    if (!config_valid(config))
        return 0;
    capacity = body_capacity(config);
    size = PXA_POSIX_STORAGE_ALIGNMENT - 1u;
    if (sizeof(pxa_posix_storage_t) > SIZE_MAX - size)
        return 0;
    size += sizeof(pxa_posix_storage_t);
    if (capacity > (SIZE_MAX - size) / 2u)
        return 0;
    return size + capacity * 2u;
}

pxa_status_t pxa_posix_storage_init(void* workspace, size_t workspace_size,
                                    const pxa_posix_storage_config_t* config,
                                    pxa_posix_storage_t** output,
                                    pxa_storage_backend_t* backend_output) {
    pxa_posix_storage_t* storage;
    uintptr_t cursor;
    uintptr_t end;
    size_t required;
    size_t left_size = 0;
    size_t right_size = 0;
    uint64_t left_generation = 0;
    uint64_t right_generation = 0;
    int left_exists = 0;
    int right_exists = 0;
    pxa_status_t left_status;
    pxa_status_t right_status;
    struct stat metadata;
    if (output == NULL || backend_output == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *output = NULL;
    memset(backend_output, 0, sizeof(*backend_output));
    required = pxa_posix_storage_workspace_size(config);
    if (workspace == NULL || required == 0 || workspace_size < required ||
        (uintptr_t)workspace > UINTPTR_MAX - workspace_size) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    end = (uintptr_t)workspace + workspace_size;
    cursor = align_up((uintptr_t)workspace, PXA_POSIX_STORAGE_ALIGNMENT);
    storage = (pxa_posix_storage_t*)cursor;
    cursor += sizeof(*storage);
    memset(storage, 0, sizeof(*storage));
    storage->root_fd = -1;
    storage->max_keys = config->max_keys;
    storage->max_value_bytes = config->max_value_bytes;
    storage->quota_bytes = config->quota_bytes;
    storage->body_capacity = body_capacity(config);
    if (cursor > end || storage->body_capacity > end - cursor ||
        storage->body_capacity > end - cursor - storage->body_capacity) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    storage->body = (uint8_t*)cursor;
    cursor += storage->body_capacity;
    storage->candidate = (uint8_t*)cursor;
    if (mkdir(config->root_path, 0700) != 0 && errno != EEXIST) {
        return status_from_errno(errno);
    }
    storage->root_fd = open(config->root_path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (storage->root_fd < 0 || fstat(storage->root_fd, &metadata) != 0 ||
        !S_ISDIR(metadata.st_mode)) {
        if (storage->root_fd >= 0)
            close(storage->root_fd);
        storage->root_fd = -1;
        return PXA_STATUS_DENIED;
    }
    storage->magic = PXA_POSIX_STORAGE_MAGIC;
    left_status = read_slot(storage, PXA_POSIX_STORAGE_SLOT_A, storage->body, &left_size,
                            &left_generation, &left_exists);
    right_status = read_slot(storage, PXA_POSIX_STORAGE_SLOT_B, storage->candidate, &right_size,
                             &right_generation, &right_exists);
    if (left_status != PXA_STATUS_OK && right_status != PXA_STATUS_OK &&
        (left_exists || right_exists)) {
        pxa_posix_storage_deinit(storage);
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (right_status == PXA_STATUS_OK &&
        (left_status != PXA_STATUS_OK || right_generation > left_generation)) {
        uint8_t* temporary = storage->body;
        storage->body = storage->candidate;
        storage->candidate = temporary;
        storage->body_size = right_size;
        storage->generation = right_generation;
        storage->active_slot = 1;
    } else if (left_status == PXA_STATUS_OK) {
        storage->body_size = left_size;
        storage->generation = left_generation;
        storage->active_slot = 0;
    } else {
        pxa_write_u16(storage->body, 0);
        storage->body_size = 2;
        storage->generation = 0;
        storage->active_slot = 0;
    }
    backend_output->struct_size = sizeof(*backend_output);
    backend_output->context = storage;
    backend_output->get = get_backend;
    backend_output->set = set_backend;
    backend_output->remove = remove_backend;
    backend_output->list = list_backend;
    *output = storage;
    return PXA_STATUS_OK;
}

void pxa_posix_storage_deinit(pxa_posix_storage_t *storage) {
    if (storage == NULL || storage->magic != PXA_POSIX_STORAGE_MAGIC) return;
    if (storage->root_fd >= 0) close(storage->root_fd);
    storage->root_fd = -1;
    storage->magic = 0;
}
