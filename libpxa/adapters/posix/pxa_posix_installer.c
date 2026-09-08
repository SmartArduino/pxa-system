#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "pxa/posix/pxa_posix_installer.h"
#include "pxa/container.h"
#include "lz4.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <dirent.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "pxa/esp/pxa_esp_posix_shim.h"
#define dup pxa_esp_dup
#define fdopendir pxa_esp_fdopendir
#define fstat pxa_esp_fstat
#define fstatat pxa_esp_fstatat
#define mkdirat pxa_esp_mkdirat
#define openat pxa_esp_openat
#define renameat pxa_esp_renameat
#define unlinkat pxa_esp_unlinkat
#define fsync pxa_esp_fsync
#define PXA_POSIX_INSTALLER_TAG "PxaInstaller"
#define PXA_POSIX_INSTALLER_LOG_FAILURE(subject, stage, status)              \
    ESP_LOGW(PXA_POSIX_INSTALLER_TAG,                                        \
             "%s failed at %s: status=%d errno=%d", subject, stage,        \
             (int)status, errno)
#else
#define PXA_POSIX_INSTALLER_LOG_FAILURE(subject, stage, status)              \
    do {                                                                      \
        (void)(subject);                                                      \
        (void)(stage);                                                        \
        (void)(status);                                                       \
    } while (0)
#endif

/* ESP-IDF newlib has no lstat (LittleFS has no symlinks); stat is
 * equivalent there. */
#ifdef ESP_PLATFORM
#define lstat stat
#endif

#define PXA_POSIX_INSTALLER_MAGIC UINT32_C(0x50494e53)
#define PXA_POSIX_INSTALLER_ALIGNMENT ((size_t)16)
/* The streaming buffer is part of the caller-provided installer workspace.
 * ESP hosts allocate that workspace in PSRAM when available, keeping the main
 * task stack small while reducing LittleFS I/O calls during package sync. */
#define PXA_POSIX_INSTALLER_IO_BYTES ((size_t)4096)
#define PXA_POSIX_INSTALLER_MAX_APP_ID ((size_t)64)
#define PXA_POSIX_INSTALLER_MAX_IDENTITY_NAME \
    (PXA_PACKAGE_DIGEST_BYTES * 2u + 1u + PXA_POSIX_INSTALLER_MAX_APP_ID)
#define PXA_POSIX_INSTALLER_MAX_PACKAGE_PATH ((size_t)255)
#define PXA_POSIX_INSTALLER_OWNER_BYTES ((size_t)40)

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} pxa_string_array_t;

typedef struct {
    pxa_posix_installer_t *installer;
    char *root;
    char *session;
    uint8_t publisher_key_id[PXA_PACKAGE_DIGEST_BYTES];
    char app_id[PXA_POSIX_INSTALLER_MAX_APP_ID + 1];
    char identity_name[PXA_POSIX_INSTALLER_MAX_IDENTITY_NAME + 1];
    size_t app_id_size;
} pxa_posix_slot_ctx_t;

struct pxa_posix_installer {
    uint32_t magic;
    const char *storage_root;
    size_t storage_root_len;
    pxa_openssl_trust_t trust;
    pxa_package_limits_t limits;
    pxa_slot_faults_t faults;
    uint8_t has_faults;
    uint32_t flags;
    pxa_package_signature_verify_fn verify;
    uint8_t *scratch;
    size_t scratch_size;
    pxa_package_limits_t scratch_limits;
    pxa_package_inventory_entry_t *inventory;
    size_t inventory_capacity;
    uint8_t *io_buffer;
    size_t io_buffer_size;
    uint8_t *compressed_buffer;
    size_t compressed_buffer_size;
};

static uintptr_t align_up(uintptr_t value, size_t alignment) {
    uintptr_t mask = (uintptr_t)alignment - 1u;
    return (value + mask) & ~mask;
}

/* Manifest record counts are measured before parsing. Keep only the high-water
 * workspace so package installation does not reserve memory for every allowed
 * manifest shape at host initialization. */
static pxa_status_t ensure_manifest_workspace(
    pxa_posix_installer_t *installer, pxa_bytes_t encoded) {
    pxa_package_limits_t required_limits;
    size_t required_size;
    void *resized;
    pxa_status_t status;
    status = pxa_package_manifest_measure(encoded, &installer->limits,
                                          &required_limits);
    if (status != PXA_STATUS_OK) return status;
    required_size = pxa_package_manifest_workspace_size(&required_limits);
    if (required_size == 0) return PXA_STATUS_RESOURCE_LIMIT;
    if (installer->scratch_size < required_size) {
        resized = realloc(installer->scratch, required_size);
        if (resized == NULL) return PXA_STATUS_RESOURCE_LIMIT;
        installer->scratch = (uint8_t *)resized;
        installer->scratch_size = required_size;
    }
    installer->scratch_limits = required_limits;
    return PXA_STATUS_OK;
}

static pxa_status_t ensure_inventory_capacity(
    pxa_posix_installer_t *installer, size_t count) {
    pxa_package_inventory_entry_t *resized;
    if (count <= installer->inventory_capacity) return PXA_STATUS_OK;
    if (count > SIZE_MAX / sizeof(*installer->inventory)) {
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    resized = (pxa_package_inventory_entry_t *)realloc(
        installer->inventory, count * sizeof(*installer->inventory));
    if (resized == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    installer->inventory = resized;
    installer->inventory_capacity = count;
    return PXA_STATUS_OK;
}

static int safe_app_id(pxa_bytes_t app_id) {
    size_t index;
    if (app_id.data == NULL || app_id.size == 0 ||
        app_id.size > PXA_POSIX_INSTALLER_MAX_APP_ID ||
        app_id.data[0] < 'a' || app_id.data[0] > 'z') {
        return 0;
    }
    for (index = 0; index < app_id.size; ++index) {
        uint8_t value = app_id.data[index];
        if (!((value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '.' ||
              value == '_' || value == '-')) {
            return 0;
        }
    }
    return 1;
}

static int identity_storage_name(
    const pxa_posix_installer_t *installer,
    const pxa_posix_installer_identity_t *identity, char *output,
    size_t capacity) {
    static const char digits[] = "0123456789abcdef";
    size_t offset = 0;
    size_t index;
    if (installer == NULL || identity == NULL || output == NULL ||
        identity->publisher_key_id.data == NULL ||
        identity->publisher_key_id.size != PXA_PACKAGE_DIGEST_BYTES ||
        !safe_app_id(identity->app_id)) {
        return 0;
    }
    if ((installer->flags & PXA_POSIX_INSTALLER_FLAG_COMPOSITE_IDENTITY) == 0) {
        if (identity->app_id.size + 1u > capacity) return 0;
        memcpy(output, identity->app_id.data, identity->app_id.size);
        output[identity->app_id.size] = '\0';
        return 1;
    }
    if (PXA_PACKAGE_DIGEST_BYTES * 2u + 1u + identity->app_id.size + 1u >
        capacity) {
        return 0;
    }
    for (index = 0; index < PXA_PACKAGE_DIGEST_BYTES; ++index) {
        const uint8_t byte = identity->publisher_key_id.data[index];
        output[offset++] = digits[byte >> 4];
        output[offset++] = digits[byte & 0x0fu];
    }
    output[offset++] = '~';
    memcpy(output + offset, identity->app_id.data, identity->app_id.size);
    output[offset + identity->app_id.size] = '\0';
    return 1;
}

static char *join_path(const char *first, const char *second) {
    size_t first_len;
    size_t second_len;
    char *output;
    if (first == NULL || second == NULL) return NULL;
    first_len = strlen(first);
    second_len = strlen(second);
    if (first_len == 0 || second_len == 0 ||
        first_len + second_len + 2 > PATH_MAX) {
        return NULL;
    }
    output = (char *)malloc(first_len + second_len + 2);
    if (output == NULL) return NULL;
    memcpy(output, first, first_len);
    output[first_len] = '/';
    memcpy(output + first_len + 1, second, second_len + 1);
    return output;
}

static int stat_same(const struct stat *left, const struct stat *right) {
    return left->st_dev == right->st_dev && left->st_ino == right->st_ino &&
           left->st_mode == right->st_mode &&
           left->st_nlink == right->st_nlink &&
           left->st_size == right->st_size &&
           left->st_mtim.tv_sec == right->st_mtim.tv_sec &&
           left->st_mtim.tv_nsec == right->st_mtim.tv_nsec &&
           left->st_ctim.tv_sec == right->st_ctim.tv_sec &&
           left->st_ctim.tv_nsec == right->st_ctim.tv_nsec;
}

/* Open (creating when requested) the directory `relative` below `parent_fd`.
 * `relative` must be a slash-separated sequence of safe segments. */
static pxa_status_t open_tree_at(int parent_fd, const char *relative,
                                 int create, int *output) {
    int current = dup(parent_fd);
    const char *segment;
    *output = -1;
    if (current < 0) return PXA_STATUS_INTERNAL;
    segment = relative;
    while (*segment != '\0') {
        const char *separator = strchr(segment, '/');
        const size_t remaining =
            separator == NULL ? strlen(segment) : (size_t)(separator - segment);
        char name[PXA_POSIX_INSTALLER_MAX_PACKAGE_PATH + 1];
        struct stat metadata;
        int child;
        if (remaining == 0 ||
            remaining > PXA_POSIX_INSTALLER_MAX_PACKAGE_PATH) {
            close(current);
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        memcpy(name, segment, remaining);
        name[remaining] = '\0';
        if (fstatat(current, name, &metadata, AT_SYMLINK_NOFOLLOW) != 0) {
            if (!create || errno != ENOENT ||
                mkdirat(current, name, 0700) != 0) {
                close(current);
                return PXA_STATUS_INTERNAL;
            }
            if (fstatat(current, name, &metadata, AT_SYMLINK_NOFOLLOW) != 0) {
                close(current);
                return PXA_STATUS_INTERNAL;
            }
        }
        if (!S_ISDIR(metadata.st_mode)) {
            close(current);
            return PXA_STATUS_DENIED;
        }
        child = openat(current, name,
                       O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (child < 0) {
            close(current);
            return PXA_STATUS_DENIED;
        }
        close(current);
        current = child;
        segment += remaining;
        if (*segment == '/') ++segment;
    }
    *output = current;
    return PXA_STATUS_OK;
}

static pxa_status_t open_relative(int root, const char *relative, int *file,
                                  struct stat *metadata) {
    int current = dup(root);
    const char *segment;
    *file = -1;
    if (current < 0) return PXA_STATUS_INTERNAL;
    segment = relative;
    while (*segment != '\0') {
        const char *separator = strchr(segment, '/');
        const size_t remaining =
            separator == NULL ? strlen(segment) : (size_t)(separator - segment);
        char name[PXA_POSIX_INSTALLER_MAX_PACKAGE_PATH + 1];
        int child;
        if (remaining == 0 ||
            remaining > PXA_POSIX_INSTALLER_MAX_PACKAGE_PATH) {
            close(current);
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        memcpy(name, segment, remaining);
        name[remaining] = '\0';
        if (separator == NULL) {
            child = openat(current, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            close(current);
            if (child < 0 || fstat(child, metadata) != 0 ||
                !S_ISREG(metadata->st_mode) || metadata->st_nlink != 1) {
                if (child >= 0) close(child);
                return PXA_STATUS_DENIED;
            }
            *file = child;
            return PXA_STATUS_OK;
        }
        child = openat(current, name,
                       O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        close(current);
        if (child < 0) return PXA_STATUS_DENIED;
        current = child;
        segment += remaining;
        ++segment;
    }
    close(current);
    return PXA_STATUS_DENIED;
}

static pxa_status_t read_bounded(int root, const char *relative, size_t maximum,
                                 uint8_t *buffer, size_t capacity,
                                 size_t *size) {
    struct stat before;
    struct stat after;
    int file;
    size_t offset;
    uint8_t trailing;
    ssize_t count;
    pxa_status_t status;
    *size = 0;
    status = open_relative(root, relative, &file, &before);
    if (status != PXA_STATUS_OK) return status;
    if (before.st_size < 0 || (uint64_t)before.st_size > maximum) {
        close(file);
        return PXA_STATUS_DENIED;
    }
    if ((uint64_t)before.st_size > capacity) {
        close(file);
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    offset = 0;
    while (offset < (size_t)before.st_size) {
        do {
            count = read(file, buffer + offset,
                         (size_t)before.st_size - offset);
        } while (count < 0 && errno == EINTR);
        if (count <= 0) {
            close(file);
            return PXA_STATUS_DENIED;
        }
        offset += (size_t)count;
    }
    do {
        count = read(file, &trailing, 1);
    } while (count < 0 && errno == EINTR);
    if (count != 0 || fstat(file, &after) != 0 ||
        !stat_same(&before, &after)) {
        close(file);
        return PXA_STATUS_DENIED;
    }
    close(file);
    *size = (size_t)before.st_size;
    return PXA_STATUS_OK;
}

static pxa_status_t read_alloc_bounded(int root, const char *relative,
                                       size_t maximum, uint8_t **buffer,
                                       size_t *size) {
    struct stat metadata;
    uint8_t *bytes;
    size_t capacity;
    int file;
    pxa_status_t status;
    if (buffer == NULL || size == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *buffer = NULL;
    *size = 0;
    status = open_relative(root, relative, &file, &metadata);
    if (status != PXA_STATUS_OK) return status;
    close(file);
    if (metadata.st_size < 0 || (uint64_t)metadata.st_size > maximum) {
        return PXA_STATUS_DENIED;
    }
    capacity = (size_t)metadata.st_size;
    bytes = (uint8_t *)malloc(capacity == 0 ? 1 : capacity);
    if (bytes == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    status = read_bounded(root, relative, maximum, bytes, capacity, size);
    if (status != PXA_STATUS_OK) {
        free(bytes);
        return status;
    }
    *buffer = bytes;
    return PXA_STATUS_OK;
}

static pxa_status_t read_exact_at(int file, uint64_t position, uint8_t *buffer,
                                  size_t size) {
    size_t offset = 0;
    if (position > (uint64_t)INT64_MAX ||
        lseek(file, (off_t)position, SEEK_SET) < 0) {
        return PXA_STATUS_DENIED;
    }
    while (offset < size) {
        ssize_t count;
        do {
            count = read(file, buffer + offset, size - offset);
        } while (count < 0 && errno == EINTR);
        if (count <= 0) return PXA_STATUS_DENIED;
        offset += (size_t)count;
    }
    return PXA_STATUS_OK;
}

static pxa_status_t open_package_source(const char *path, int *file,
                                        int *is_container,
                                        struct stat *metadata) {
    struct stat path_metadata;
    int flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW;
    if (path == NULL || file == NULL || is_container == NULL ||
        metadata == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *file = -1;
    *is_container = 0;
    if (lstat(path, &path_metadata) != 0) {
        return errno == ENOENT ? PXA_STATUS_NOT_FOUND : PXA_STATUS_DENIED;
    }
    if (S_ISDIR(path_metadata.st_mode)) {
        flags |= O_DIRECTORY;
    } else if (!S_ISREG(path_metadata.st_mode)) {
        return PXA_STATUS_DENIED;
    }
    *file = open(path, flags);
    if (*file < 0) {
        return errno == ENOENT ? PXA_STATUS_NOT_FOUND : PXA_STATUS_DENIED;
    }
    if (fstat(*file, metadata) != 0 ||
        (S_ISDIR(path_metadata.st_mode) && !S_ISDIR(metadata->st_mode)) ||
        (S_ISREG(path_metadata.st_mode) && !S_ISREG(metadata->st_mode))) {
        close(*file);
        *file = -1;
        return PXA_STATUS_DENIED;
    }
    *is_container = S_ISREG(metadata->st_mode);
    return PXA_STATUS_OK;
}

static int path_append(pxa_string_array_t *array, const char *prefix,
                       const char *name, size_t name_size) {
    const size_t prefix_size = prefix == NULL ? 0 : strlen(prefix);
    char *item;
    if (name_size == 0 || prefix_size + name_size + 2 > SIZE_MAX) return 0;
    item = (char *)malloc(prefix_size + name_size + 2);
    if (item == NULL) return 0;
    if (prefix_size != 0) {
        memcpy(item, prefix, prefix_size);
        item[prefix_size] = '/';
    }
    memcpy(item + prefix_size + (prefix_size != 0 ? 1u : 0u), name, name_size);
    item[prefix_size + (prefix_size != 0 ? 1u : 0u) + name_size] = '\0';
    if (array->count == array->capacity) {
        const size_t capacity =
            array->capacity == 0 ? 8 : array->capacity * 2;
        char **grown;
        if (capacity < array->capacity ||
            capacity > SIZE_MAX / sizeof(char *)) {
            free(item);
            return 0;
        }
        grown = (char **)realloc(array->items,
                                 capacity * sizeof(char *));
        if (grown == NULL) {
            free(item);
            return 0;
        }
        array->items = grown;
        array->capacity = capacity;
    }
    array->items[array->count++] = item;
    return 1;
}

static void string_array_clear(pxa_string_array_t *array) {
    size_t index;
    for (index = 0; index < array->count; ++index) free(array->items[index]);
    free(array->items);
    memset(array, 0, sizeof(*array));
}

typedef struct {
    const char *data;
    size_t size;
} pxa_path_view_t;

static int path_view_compare(const void *left, const void *right) {
    const pxa_path_view_t *l = (const pxa_path_view_t *)left;
    const pxa_path_view_t *r = (const pxa_path_view_t *)right;
    const size_t common = l->size < r->size ? l->size : r->size;
    const int result = common == 0 ? 0 : memcmp(l->data, r->data, common);
    if (result != 0) return result;
    return l->size < r->size ? -1 : (l->size > r->size ? 1 : 0);
}

static pxa_path_view_t *collect_views(const pxa_string_array_t *strings,
                                      size_t *count) {
    size_t index;
    pxa_path_view_t *views;
    *count = strings->count;
    if (strings->count == 0) return NULL;
    views = (pxa_path_view_t *)malloc(strings->count *
                                      sizeof(pxa_path_view_t));
    if (views == NULL) return NULL;
    for (index = 0; index < strings->count; ++index) {
        views[index].data = strings->items[index];
        views[index].size = strlen(strings->items[index]);
    }
    return views;
}

static int segment_safe(const char *name, size_t size) {
    size_t index;
    if (size == 0 || size > PXA_POSIX_INSTALLER_MAX_PACKAGE_PATH ||
        (size == 1 && name[0] == '.') ||
        (size == 2 && name[0] == '.' && name[1] == '.')) {
        return 0;
    }
    for (index = 0; index < size; ++index) {
        const unsigned char value = (unsigned char)name[index];
        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '.' ||
              value == '_' || value == '-')) {
            return 0;
        }
    }
    return 1;
}

static pxa_status_t enumerate_files(int directory, const char *prefix,
                                    pxa_string_array_t *output,
                                    int *found_entry) {
    DIR *stream;
    struct dirent *item;
    pxa_status_t status = PXA_STATUS_OK;
    {
        /* Fresh open (not dup): the directory offset must not be shared. */
        const int duplicate =
            openat(directory, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW |
                                      O_CLOEXEC);
        if (duplicate < 0) return PXA_STATUS_INTERNAL;
        stream = fdopendir(duplicate);
        if (stream == NULL) {
            close(duplicate);
            return PXA_STATUS_INTERNAL;
        }
    }
    *found_entry = 0;
    errno = 0;
    while ((item = readdir(stream)) != NULL) {
        struct stat metadata;
        const size_t name_size = strlen(item->d_name);
        if (name_size == 1 && item->d_name[0] == '.') continue;
        if (name_size == 2 && item->d_name[0] == '.' &&
            item->d_name[1] == '.') {
            continue;
        }
        *found_entry = 1;
        if (!segment_safe(item->d_name, name_size) ||
            fstatat(directory, item->d_name, &metadata,
                    AT_SYMLINK_NOFOLLOW) != 0) {
            status = PXA_STATUS_DENIED;
            break;
        }
        if (S_ISREG(metadata.st_mode)) {
            if (metadata.st_nlink != 1 ||
                !path_append(output, prefix, item->d_name, name_size)) {
                status = PXA_STATUS_DENIED;
                break;
            }
        } else if (S_ISDIR(metadata.st_mode)) {
            int child;
            int child_has_entry = 0;
            char child_prefix[2 * PXA_POSIX_INSTALLER_MAX_PACKAGE_PATH + 2];
            const size_t prefix_size = prefix == NULL ? 0 : strlen(prefix);
            if (prefix_size + name_size + 2 >
                sizeof(child_prefix)) {
                status = PXA_STATUS_DENIED;
                break;
            }
            if (prefix_size == 0) {
                memcpy(child_prefix, item->d_name, name_size);
                child_prefix[name_size] = '\0';
            } else {
                memcpy(child_prefix, prefix, prefix_size);
                child_prefix[prefix_size] = '/';
                memcpy(child_prefix + prefix_size + 1, item->d_name,
                       name_size);
                child_prefix[prefix_size + 1 + name_size] = '\0';
            }
            child = openat(directory, item->d_name,
                           O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if (child < 0 ||
                enumerate_files(child, child_prefix, output,
                                &child_has_entry) != PXA_STATUS_OK ||
                !child_has_entry) {
                if (child >= 0) close(child);
                status = PXA_STATUS_DENIED;
                break;
            }
            close(child);
        } else {
            status = PXA_STATUS_DENIED;
            break;
        }
        errno = 0;
    }
    if (status == PXA_STATUS_OK && errno != 0) {
        status = PXA_STATUS_DENIED;
    }
    if (closedir(stream) != 0 && status == PXA_STATUS_OK) {
        status = PXA_STATUS_INTERNAL;
    }
    return status;
}

static pxa_status_t hash_and_copy(pxa_posix_installer_t *installer,
                                  int source_root,
                                  const pxa_package_file_t *expected,
                                  int destination_root,
                                  const char *destination) {
    struct stat before;
    struct stat after;
    uint8_t digest[PXA_PACKAGE_DIGEST_BYTES];
    pxa_openssl_sha256_stream_t stream;
    char relative[PXA_POSIX_INSTALLER_MAX_PACKAGE_PATH + 1];
    int source = -1;
    int target = -1;
    uint64_t total = 0;
    pxa_status_t status;
    if (installer == NULL || installer->io_buffer == NULL ||
        installer->io_buffer_size == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (expected->path.size > sizeof(relative) - 1) {
        return PXA_STATUS_DENIED;
    }
    memcpy(relative, expected->path.data, expected->path.size);
    relative[expected->path.size] = '\0';
    status = open_relative(source_root, relative, &source, &before);
    if (status != PXA_STATUS_OK) return status;
    if (before.st_size < 0 || (uint64_t)before.st_size != expected->size) {
        close(source);
        return PXA_STATUS_DENIED;
    }
    if (destination_root >= 0 && destination != NULL) {
        const char *separator = strrchr(destination, '/');
        char parent[PXA_POSIX_INSTALLER_MAX_PACKAGE_PATH + 32];
        int parent_fd = -1;
        if (separator == NULL) {
            parent[0] = '\0';
        } else {
            const size_t parent_size = (size_t)(separator - destination);
            if (parent_size > sizeof(parent) - 1) {
                close(source);
                return PXA_STATUS_DENIED;
            }
            memcpy(parent, destination, parent_size);
            parent[parent_size] = '\0';
        }
        status = open_tree_at(destination_root, parent, 1, &parent_fd);
        if (status != PXA_STATUS_OK) {
            close(source);
            return status;
        }
        target = openat(parent_fd,
                        separator == NULL ? destination : separator + 1,
                        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                        0600);
        close(parent_fd);
        if (target < 0) {
            close(source);
            return PXA_STATUS_INTERNAL;
        }
    }
    status = pxa_openssl_sha256_stream_begin(&stream);
    if (status != PXA_STATUS_OK) {
        close(source);
        if (target >= 0) close(target);
        return status;
    }
    for (;;) {
        ssize_t count;
        do {
            count = read(source, installer->io_buffer,
                         installer->io_buffer_size);
        } while (count < 0 && errno == EINTR);
        if (count < 0) {
            status = PXA_STATUS_DENIED;
            break;
        }
        if (count == 0) break;
        total += (uint64_t)count;
        if (total > expected->size ||
            pxa_openssl_sha256_stream_update(&stream, installer->io_buffer,
                                             (size_t)count) !=
                PXA_STATUS_OK) {
            status = PXA_STATUS_DENIED;
            break;
        }
        if (target >= 0) {
            size_t offset = 0;
            while (offset < (size_t)count) {
                ssize_t written;
                do {
                    written = write(target, installer->io_buffer + offset,
                                    (size_t)count - offset);
                } while (written < 0 && errno == EINTR);
                if (written <= 0) {
                    status = PXA_STATUS_INTERNAL;
                    break;
                }
                offset += (size_t)written;
            }
            if (status != PXA_STATUS_OK) break;
        }
    }
    if (status == PXA_STATUS_OK &&
        pxa_openssl_sha256_stream_finish(&stream, digest) != PXA_STATUS_OK) {
        status = PXA_STATUS_DENIED;
    }
    if (status == PXA_STATUS_OK && total != expected->size) {
        status = PXA_STATUS_DENIED;
    }
    if (status == PXA_STATUS_OK &&
        memcmp(digest, expected->sha256, PXA_PACKAGE_DIGEST_BYTES) != 0) {
        status = PXA_STATUS_DENIED;
    }
    if (status == PXA_STATUS_OK &&
        (fstat(source, &after) != 0 || !stat_same(&before, &after))) {
        status = PXA_STATUS_DENIED;
    }
    close(source);
    if (target >= 0) {
        /* ESP-IDF's LittleFS VFS synchronizes a writable file during close.
         * Calling fsync immediately before close repeats that flash
         * transaction. POSIX hosts retain the explicit fsync contract. */
#ifndef ESP_PLATFORM
        if (status == PXA_STATUS_OK && fsync(target) != 0) {
            status = PXA_STATUS_INTERNAL;
        }
#endif
        if (close(target) != 0 && status == PXA_STATUS_OK) {
            status = PXA_STATUS_INTERNAL;
        }
    }
    return status;
}

static pxa_status_t write_file_excl(int root, const char *relative,
                                    const uint8_t *bytes, size_t size) {
    char parent[PXA_POSIX_INSTALLER_MAX_PACKAGE_PATH + 32];
    char name[PXA_POSIX_INSTALLER_MAX_PACKAGE_PATH + 1];
    const char *separator;
    int parent_fd = -1;
    int file;
    size_t offset;
    pxa_status_t status;
    separator = strrchr(relative, '/');
    if (separator == NULL) {
        if (strlen(relative) > sizeof(name) - 1) {
            return PXA_STATUS_DENIED;
        }
        strcpy(name, relative);
        parent[0] = '\0';
    } else {
        const size_t parent_size = (size_t)(separator - relative);
        if (parent_size > sizeof(parent) - 1 ||
            strlen(separator + 1) > sizeof(name) - 1) {
            return PXA_STATUS_DENIED;
        }
        memcpy(parent, relative, parent_size);
        parent[parent_size] = '\0';
        strcpy(name, separator + 1);
    }
    status = open_tree_at(root, parent, 1, &parent_fd);
    if (status != PXA_STATUS_OK) return status;
    file = openat(parent_fd, name,
                  O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    close(parent_fd);
    if (file < 0) return PXA_STATUS_INTERNAL;
    offset = 0;
    while (offset < size) {
        ssize_t written;
        do {
            written = write(file, bytes + offset, size - offset);
        } while (written < 0 && errno == EINTR);
        if (written <= 0) {
            close(file);
            return PXA_STATUS_INTERNAL;
        }
        offset += (size_t)written;
    }
    /* See hash_and_copy: LittleFS close already performs lfs_file_sync(). */
#ifndef ESP_PLATFORM
    if (fsync(file) != 0) {
        close(file);
        return PXA_STATUS_INTERNAL;
    }
#endif
    if (close(file) != 0) return PXA_STATUS_INTERNAL;
    return PXA_STATUS_OK;
}

static pxa_status_t sync_tree(int directory) {
    DIR *stream;
    struct dirent *item;
    pxa_status_t status = PXA_STATUS_OK;
    {
        /* Fresh open (not dup): the directory offset must not be shared. */
        const int duplicate =
            openat(directory, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW |
                                      O_CLOEXEC);
        if (duplicate < 0) return PXA_STATUS_INTERNAL;
        stream = fdopendir(duplicate);
        if (stream == NULL) {
            close(duplicate);
            return PXA_STATUS_INTERNAL;
        }
    }
    while ((item = readdir(stream)) != NULL) {
        struct stat metadata;
        if (strcmp(item->d_name, ".") == 0 ||
            strcmp(item->d_name, "..") == 0) {
            continue;
        }
        if (fstatat(directory, item->d_name, &metadata,
                    AT_SYMLINK_NOFOLLOW) != 0) {
            status = PXA_STATUS_INTERNAL;
            break;
        }
        if (S_ISDIR(metadata.st_mode)) {
            int child = openat(directory, item->d_name,
                               O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if (child < 0) {
                status = PXA_STATUS_INTERNAL;
                break;
            }
            status = sync_tree(child);
            close(child);
            if (status != PXA_STATUS_OK) break;
        }
    }
    if (closedir(stream) != 0 && status == PXA_STATUS_OK) {
        status = PXA_STATUS_INTERNAL;
    }
    if (status == PXA_STATUS_OK && fsync(directory) != 0) {
        status = PXA_STATUS_INTERNAL;
    }
    return status;
}

static pxa_status_t remove_tree(const char *path) {
    struct stat metadata;
    if (lstat(path, &metadata) != 0) {
        return errno == ENOENT ? PXA_STATUS_OK : PXA_STATUS_INTERNAL;
    }
    if (!S_ISDIR(metadata.st_mode)) {
        if (unlink(path) != 0 && errno != ENOENT) {
            return PXA_STATUS_INTERNAL;
        }
        return PXA_STATUS_OK;
    }
    {
        int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        DIR *stream;
        struct dirent *item;
        pxa_status_t status = PXA_STATUS_OK;
        if (fd < 0) {
            return errno == ENOENT ? PXA_STATUS_OK : PXA_STATUS_INTERNAL;
        }
        stream = fdopendir(fd);
        if (stream == NULL) {
            close(fd);
            return PXA_STATUS_INTERNAL;
        }
        while ((item = readdir(stream)) != NULL) {
            char *child;
            if (strcmp(item->d_name, ".") == 0 ||
                strcmp(item->d_name, "..") == 0) {
                continue;
            }
            child = join_path(path, item->d_name);
            if (child == NULL) {
                status = PXA_STATUS_INTERNAL;
                break;
            }
            status = remove_tree(child);
            free(child);
            if (status != PXA_STATUS_OK) break;
        }
        if (closedir(stream) != 0 && status == PXA_STATUS_OK) {
            status = PXA_STATUS_INTERNAL;
        }
        if (status == PXA_STATUS_OK && rmdir(path) != 0 && errno != ENOENT) {
            status = PXA_STATUS_INTERNAL;
        }
        return status;
    }
}

static pxa_status_t sha256_once(const uint8_t *bytes, size_t size,
                                uint8_t digest[PXA_PACKAGE_DIGEST_BYTES]) {
    pxa_openssl_sha256_stream_t stream;
    pxa_status_t status;
    memset(&stream, 0, sizeof(stream));
    status = pxa_openssl_sha256_stream_begin(&stream);
    if (status != PXA_STATUS_OK) return status;
    status = pxa_openssl_sha256_stream_update(&stream, bytes, size);
    if (status == PXA_STATUS_OK)
        status = pxa_openssl_sha256_stream_finish(&stream, digest);
    else
        pxa_openssl_sha256_stream_abort(&stream);
    return status;
}

static void lineage_link_trust(
    const pxa_package_lineage_link_t *link,
    pxa_openssl_publisher_key_t *key, pxa_openssl_trust_t *trust) {
    key->spki = link->new_spki.data;
    key->spki_size = link->new_spki.size;
    memset(trust, 0, sizeof(*trust));
    trust->struct_size = sizeof(*trust);
    trust->keys = key;
    trust->key_count = 1;
}

static pxa_status_t verify_manifest_signature(
    pxa_posix_installer_t *installer, const pxa_package_manifest_t *manifest,
    const pxa_package_signature_t *signature,
    pxa_package_publisher_lineage_t *lineage_out) {
    static const uint8_t rotation_domain[] =
        "PXA-PUBLISHER-KEY-ROTATION\0";
    pxa_package_publisher_lineage_t lineage;
    pxa_openssl_publisher_key_t embedded_key;
    pxa_openssl_trust_t embedded_trust;
    uint16_t index;
    pxa_status_t status;
    status = pxa_package_publisher_lineage_parse(manifest, &lineage);
    if (status != PXA_STATUS_OK) return status;
    if (lineage.link_count == 0) {
        status = pxa_package_signature_verify(
            manifest, signature, &installer->trust, installer->verify);
        if (status == PXA_STATUS_OK && lineage_out != NULL)
            *lineage_out = lineage;
        return status;
    }
    for (index = 0; index < lineage.link_count; ++index) {
        const pxa_package_lineage_link_t *link = &lineage.links[index];
        uint8_t computed_key_id[PXA_PACKAGE_DIGEST_BYTES];
        uint8_t message[32 + 2 + PXA_POSIX_INSTALLER_MAX_APP_ID + 4 + 4 +
                        32 + 32 + 2 + PXA_PACKAGE_MAX_LINEAGE_SPKI_BYTES];
        size_t message_size = 0;
        void *trust_context;
        if (sha256_once(link->new_spki.data, link->new_spki.size,
                        computed_key_id) != PXA_STATUS_OK ||
            memcmp(computed_key_id, link->new_key_id,
                   PXA_PACKAGE_DIGEST_BYTES) != 0) {
            return PXA_STATUS_DENIED;
        }
        memcpy(message + message_size, lineage.root_key_id, 32);
        message_size += 32;
        pxa_write_u16(message + message_size, (uint16_t)manifest->app_id.size);
        message_size += 2;
        memcpy(message + message_size, manifest->app_id.data,
               manifest->app_id.size);
        message_size += manifest->app_id.size;
        pxa_write_u32(message + message_size, link->generation);
        message_size += 4;
        pxa_write_u32(message + message_size, link->flags);
        message_size += 4;
        memcpy(message + message_size, link->old_key_id, 32);
        message_size += 32;
        memcpy(message + message_size, link->new_key_id, 32);
        message_size += 32;
        pxa_write_u16(message + message_size,
                      (uint16_t)link->new_spki.size);
        message_size += 2;
        memcpy(message + message_size, link->new_spki.data,
               link->new_spki.size);
        message_size += link->new_spki.size;
        if (index == 0) {
            trust_context = &installer->trust;
        } else {
            lineage_link_trust(&lineage.links[index - 1], &embedded_key,
                               &embedded_trust);
            trust_context = &embedded_trust;
        }
        status = installer->verify(
            trust_context,
            (pxa_bytes_t){link->old_key_id, PXA_PACKAGE_DIGEST_BYTES},
            (pxa_bytes_t){rotation_domain, sizeof(rotation_domain) - 1},
            (pxa_bytes_t){message, message_size},
            (pxa_bytes_t){link->signature, PXA_PACKAGE_SIGNATURE_BYTES});
        if (status != PXA_STATUS_OK) return status;
    }
    status = pxa_package_signature_identity_validate(manifest, signature);
    if (status != PXA_STATUS_OK) return status;
    lineage_link_trust(&lineage.links[lineage.link_count - 1], &embedded_key,
                       &embedded_trust);
    {
        static const uint8_t manifest_domain[] = "PXA-PACKAGE-MANIFEST\0";
        status = installer->verify(
            &embedded_trust,
            (pxa_bytes_t){signature->publisher_key_id,
                          PXA_PACKAGE_DIGEST_BYTES},
            (pxa_bytes_t){manifest_domain, sizeof(manifest_domain) - 1},
            manifest->encoded,
            (pxa_bytes_t){signature->signature,
                          PXA_PACKAGE_SIGNATURE_BYTES});
    }
    if (status == PXA_STATUS_OK && lineage_out != NULL)
        *lineage_out = lineage;
    return status;
}

static pxa_status_t verify_opened_container(
    pxa_posix_installer_t *installer, int source_fd,
    uint8_t **manifest_bytes_io,
    size_t *manifest_capacity_io, size_t *manifest_size_out,
    uint8_t publisher_key_id_out[PXA_PACKAGE_DIGEST_BYTES],
    char app_id_out[PXA_POSIX_INSTALLER_MAX_APP_ID + 1],
    size_t *app_id_size_out, pxa_container_header_t *header_out) {
    static const uint8_t domain[] = "PXA-PACKAGE-CONTAINER-DIGEST\0";
    uint8_t header_bytes[PXA_CONTAINER_HEADER_BYTES];
    uint8_t package_signature_bytes[PXA_POSIX_INSTALLER_SIGNATURE_ENVELOPE_BYTES];
    uint8_t container_signature_bytes[PXA_CONTAINER_SIGNATURE_ENVELOPE_BYTES];
    uint8_t digest[PXA_PACKAGE_DIGEST_BYTES];
    uint8_t *manifest_bytes;
    pxa_container_header_t header;
    pxa_container_signature_t container_signature;
    pxa_package_signature_t package_signature;
    pxa_package_publisher_lineage_t lineage;
    pxa_openssl_publisher_key_t embedded_key;
    pxa_openssl_trust_t embedded_trust;
    pxa_package_manifest_t *manifest = NULL;
    pxa_openssl_sha256_stream_t stream;
    struct stat before;
    struct stat after;
    uint64_t unpacked_size = 0;
    uint64_t payload_position;
    uint64_t payload_end;
    size_t index;
    pxa_status_t status;

    memset(&stream, 0, sizeof(stream));
    if (manifest_bytes_io == NULL || manifest_capacity_io == NULL ||
        manifest_size_out == NULL ||
        publisher_key_id_out == NULL || app_id_out == NULL ||
        app_id_size_out == NULL || header_out == NULL ||
        fstat(source_fd, &before) != 0 || !S_ISREG(before.st_mode) ||
        before.st_nlink != 1 || before.st_size < 0) {
        return PXA_STATUS_DENIED;
    }
    *manifest_size_out = 0;
    *app_id_size_out = 0;
    status = read_exact_at(source_fd, 0, header_bytes, sizeof(header_bytes));
    if (status != PXA_STATUS_OK) return status;
    status = pxa_container_header_parse(
        (pxa_bytes_t){header_bytes, sizeof(header_bytes)},
        (uint64_t)before.st_size, &header);
    if (status != PXA_STATUS_OK ||
        header.file_count > installer->limits.max_files ||
        header.manifest_size == 0) {
        return PXA_STATUS_DENIED;
    }
    manifest_bytes = *manifest_bytes_io;
    if (*manifest_capacity_io < header.manifest_size) {
        uint8_t *resized =
            (uint8_t *)realloc(manifest_bytes, header.manifest_size);
        if (resized == NULL) return PXA_STATUS_RESOURCE_LIMIT;
        manifest_bytes = resized;
        *manifest_bytes_io = resized;
        *manifest_capacity_io = header.manifest_size;
    }
    status = read_exact_at(source_fd, header.manifest_offset, manifest_bytes,
                           header.manifest_size);
    if (status != PXA_STATUS_OK) return status;
    status = read_exact_at(source_fd, header.package_signature_offset,
                           package_signature_bytes,
                           sizeof(package_signature_bytes));
    if (status != PXA_STATUS_OK) return status;
    status = read_exact_at(source_fd, header.container_signature_offset,
                           container_signature_bytes,
                           sizeof(container_signature_bytes));
    if (status != PXA_STATUS_OK) return status;
    status = ensure_manifest_workspace(
        installer, (pxa_bytes_t){manifest_bytes, header.manifest_size});
    if (status != PXA_STATUS_OK) return status;
    status = pxa_package_manifest_parse(
        installer->scratch, installer->scratch_size,
        (pxa_bytes_t){manifest_bytes, header.manifest_size},
        &installer->scratch_limits, &manifest);
    if (status != PXA_STATUS_OK || manifest->file_count != header.file_count ||
        manifest->app_id.size > PXA_POSIX_INSTALLER_MAX_APP_ID) {
        return PXA_STATUS_DENIED;
    }
    for (index = 0; index < manifest->file_count; ++index) {
        if (unpacked_size > UINT64_MAX - manifest->files[index].size)
            return PXA_STATUS_DENIED;
        unpacked_size += manifest->files[index].size;
    }
    if (unpacked_size != header.unpacked_size) return PXA_STATUS_DENIED;
    status = pxa_package_signature_parse(
        (pxa_bytes_t){package_signature_bytes,
                      sizeof(package_signature_bytes)},
        &package_signature);
    if (status != PXA_STATUS_OK) return status;
    status = verify_manifest_signature(installer, manifest, &package_signature,
                                       &lineage);
    if (status != PXA_STATUS_OK) return status;
    status = pxa_container_signature_parse(
        (pxa_bytes_t){container_signature_bytes,
                      sizeof(container_signature_bytes)},
        &container_signature);
    if (status != PXA_STATUS_OK ||
        memcmp(manifest->publisher_key_id,
               container_signature.publisher_key_id,
               PXA_PACKAGE_DIGEST_BYTES) != 0) {
        return PXA_STATUS_DENIED;
    }

    status = pxa_openssl_sha256_stream_begin(&stream);
    if (status != PXA_STATUS_OK) return status;
    if (pxa_openssl_sha256_stream_update(&stream, header_bytes,
                                          sizeof(header_bytes)) !=
            PXA_STATUS_OK ||
        pxa_openssl_sha256_stream_update(&stream, manifest_bytes,
                                          header.manifest_size) !=
            PXA_STATUS_OK ||
        pxa_openssl_sha256_stream_update(
            &stream, package_signature_bytes,
            sizeof(package_signature_bytes)) != PXA_STATUS_OK) {
        status = PXA_STATUS_INTERNAL;
        goto digest_done;
    }
    payload_position = header.payload_offset;
    payload_end = header.payload_offset + header.payload_size;
    for (index = 0; index < manifest->file_count; ++index) {
        uint8_t file_record[16];
        uint64_t encoded_consumed = 0;
        uint64_t decoded_consumed = 0;
        uint64_t encoded_size;
        uint32_t chunk_count;
        uint32_t chunk_index;
        const uint64_t expected_size = manifest->files[index].size;
        const uint32_t expected_chunks =
            (uint32_t)((expected_size + PXA_CONTAINER_CHUNK_BYTES - 1u) /
                       PXA_CONTAINER_CHUNK_BYTES);
        if (payload_position > payload_end ||
            sizeof(file_record) > payload_end - payload_position) {
            status = PXA_STATUS_DENIED;
            goto digest_done;
        }
        status = read_exact_at(source_fd, payload_position, file_record,
                               sizeof(file_record));
        if (status != PXA_STATUS_OK) goto digest_done;
        if (pxa_openssl_sha256_stream_update(&stream, file_record,
                                              sizeof(file_record)) !=
            PXA_STATUS_OK) {
            status = PXA_STATUS_INTERNAL;
            goto digest_done;
        }
        payload_position += sizeof(file_record);
        chunk_count = pxa_read_u32(file_record + 4);
        encoded_size = pxa_read_u64(file_record + 8);
        if (pxa_read_u16(file_record) != index ||
            pxa_read_u16(file_record + 2) != 0 ||
            chunk_count != expected_chunks) {
            status = PXA_STATUS_DENIED;
            goto digest_done;
        }
        for (chunk_index = 0; chunk_index < chunk_count; ++chunk_index) {
            uint8_t chunk_header[4];
            uint16_t stored_size;
            uint16_t decoded_size;
            const uint64_t remaining = expected_size - decoded_consumed;
            const uint16_t expected_decoded =
                (uint16_t)(remaining > PXA_CONTAINER_CHUNK_BYTES
                               ? PXA_CONTAINER_CHUNK_BYTES
                               : remaining);
            if (payload_position > payload_end ||
                sizeof(chunk_header) > payload_end - payload_position) {
                status = PXA_STATUS_DENIED;
                goto digest_done;
            }
            status = read_exact_at(source_fd, payload_position, chunk_header,
                                   sizeof(chunk_header));
            if (status != PXA_STATUS_OK) goto digest_done;
            if (pxa_openssl_sha256_stream_update(&stream, chunk_header,
                                                  sizeof(chunk_header)) !=
                PXA_STATUS_OK) {
                status = PXA_STATUS_INTERNAL;
                goto digest_done;
            }
            payload_position += sizeof(chunk_header);
            encoded_consumed += sizeof(chunk_header);
            stored_size = pxa_read_u16(chunk_header);
            decoded_size = pxa_read_u16(chunk_header + 2);
            if (stored_size == 0 || decoded_size != expected_decoded ||
                stored_size > decoded_size ||
                (header.codec == PXA_CONTAINER_CODEC_STORE &&
                 stored_size != decoded_size) ||
                stored_size > installer->compressed_buffer_size ||
                payload_position > payload_end ||
                stored_size > payload_end - payload_position) {
                status = PXA_STATUS_DENIED;
                goto digest_done;
            }
            status = read_exact_at(source_fd, payload_position,
                                   installer->compressed_buffer, stored_size);
            if (status != PXA_STATUS_OK) goto digest_done;
            if (pxa_openssl_sha256_stream_update(
                    &stream, installer->compressed_buffer, stored_size) !=
                PXA_STATUS_OK) {
                status = PXA_STATUS_INTERNAL;
                goto digest_done;
            }
            payload_position += stored_size;
            encoded_consumed += stored_size;
            decoded_consumed += decoded_size;
        }
        if (encoded_consumed != encoded_size ||
            decoded_consumed != expected_size) {
            status = PXA_STATUS_DENIED;
            goto digest_done;
        }
    }
    if (payload_position != payload_end) {
        status = PXA_STATUS_DENIED;
        goto digest_done;
    }
    status = pxa_openssl_sha256_stream_finish(&stream, digest);
    if (status != PXA_STATUS_OK) return status;
    if (lineage.link_count != 0) {
        lineage_link_trust(&lineage.links[lineage.link_count - 1],
                           &embedded_key, &embedded_trust);
    }
    status = installer->verify(
        lineage.link_count == 0 ? (void *)&installer->trust
                                : (void *)&embedded_trust,
        (pxa_bytes_t){container_signature.publisher_key_id,
                      PXA_PACKAGE_DIGEST_BYTES},
        (pxa_bytes_t){domain, sizeof(domain) - 1},
        (pxa_bytes_t){digest, sizeof(digest)},
        (pxa_bytes_t){container_signature.signature,
                      PXA_PACKAGE_SIGNATURE_BYTES});
    if (status != PXA_STATUS_OK) return status;
    if (fstat(source_fd, &after) != 0 || !stat_same(&before, &after))
        return PXA_STATUS_DENIED;
    memcpy(publisher_key_id_out, lineage.root_key_id,
           PXA_PACKAGE_DIGEST_BYTES);
    memcpy(app_id_out, manifest->app_id.data, manifest->app_id.size);
    app_id_out[manifest->app_id.size] = '\0';
    *app_id_size_out = manifest->app_id.size;
    *manifest_size_out = header.manifest_size;
    *header_out = header;
    return PXA_STATUS_OK;

digest_done:
    pxa_openssl_sha256_stream_abort(&stream);
    return status;
}

static pxa_status_t verify_opened_directory(
    pxa_posix_installer_t *installer, int root_fd, int verify_contents,
    uint8_t publisher_key_id_out[PXA_PACKAGE_DIGEST_BYTES],
    char app_id_out[PXA_POSIX_INSTALLER_MAX_APP_ID + 1],
    size_t *app_id_size_out) {
    uint8_t *manifest_bytes = NULL;
    uint8_t signature_bytes[PXA_POSIX_INSTALLER_SIGNATURE_ENVELOPE_BYTES];
    pxa_package_signature_t signature;
    pxa_package_publisher_lineage_t lineage;
    pxa_package_manifest_t *manifest = NULL;
    pxa_string_array_t actual;
    pxa_path_view_t *expected = NULL;
    pxa_path_view_t *actual_views = NULL;
    size_t expected_count;
    size_t index;
    pxa_status_t status;
    const char *stage = "validate-arguments";
    size_t manifest_size;
    size_t signature_size;
    memset(&actual, 0, sizeof(actual));
    if (app_id_out == NULL || app_id_size_out == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *app_id_size_out = 0;
    stage = "read-manifest";
    status = read_alloc_bounded(root_fd, "manifest.pxm", SIZE_MAX,
                                &manifest_bytes, &manifest_size);
    if (status != PXA_STATUS_OK) {
        goto done;
    }
    stage = "read-signature";
    status = read_bounded(root_fd, "signature.pxs",
                          PXA_POSIX_INSTALLER_SIGNATURE_ENVELOPE_BYTES,
                          signature_bytes, sizeof(signature_bytes),
                          &signature_size);
    if (status != PXA_STATUS_OK) {
        goto done;
    }
    stage = "parse-signature";
    status = pxa_package_signature_parse(
        (pxa_bytes_t){signature_bytes, signature_size}, &signature);
    if (status != PXA_STATUS_OK) {
        goto done;
    }
    stage = "parse-manifest";
    status = ensure_manifest_workspace(
        installer, (pxa_bytes_t){manifest_bytes, manifest_size});
    if (status != PXA_STATUS_OK) {
        goto done;
    }
    status = pxa_package_manifest_parse(
        installer->scratch, installer->scratch_size,
        (pxa_bytes_t){manifest_bytes, manifest_size},
        &installer->scratch_limits,
        &manifest);
    if (status != PXA_STATUS_OK) {
        goto done;
    }
    stage = "validate-identity";
    status = pxa_package_signature_identity_validate(manifest, &signature);
    if (status != PXA_STATUS_OK) goto done;
    if (installer->verify == NULL) {
        status = PXA_STATUS_UNSUPPORTED;
        goto done;
    }
    stage = "verify-signature";
    status = verify_manifest_signature(installer, manifest, &signature,
                                       &lineage);
    if (status != PXA_STATUS_OK) {
        goto done;
    }
    if (!verify_contents) {
        if (manifest->app_id.size > PXA_POSIX_INSTALLER_MAX_APP_ID) {
            stage = "validate-app-id";
            status = PXA_STATUS_DENIED;
            goto done;
        }
        memcpy(publisher_key_id_out, lineage.root_key_id,
               PXA_PACKAGE_DIGEST_BYTES);
        memcpy(app_id_out, manifest->app_id.data, manifest->app_id.size);
        app_id_out[manifest->app_id.size] = '\0';
        *app_id_size_out = manifest->app_id.size;
        status = PXA_STATUS_OK;
        goto done;
    }

    {
        int found = 0;
        stage = "enumerate-files";
        status = enumerate_files(root_fd, NULL, &actual, &found);
        if (status != PXA_STATUS_OK || !found) {
            status = status == PXA_STATUS_OK ? PXA_STATUS_DENIED : status;
            goto done;
        }
    }
    expected_count = (size_t)manifest->file_count + 2;
    stage = "allocate-expected-files";
    expected = (pxa_path_view_t *)malloc(expected_count *
                                         sizeof(pxa_path_view_t));
    if (expected == NULL) {
        status = PXA_STATUS_RESOURCE_LIMIT;
        goto done;
    }
    expected[0].data = "manifest.pxm";
    expected[0].size = sizeof("manifest.pxm") - 1;
    expected[1].data = "signature.pxs";
    expected[1].size = sizeof("signature.pxs") - 1;
    status = ensure_inventory_capacity(installer, manifest->file_count);
    if (status != PXA_STATUS_OK) goto done;
    for (index = 0; index < (size_t)manifest->file_count; ++index) {
        expected[index + 2].data =
            (const char *)manifest->files[index].path.data;
        expected[index + 2].size = manifest->files[index].path.size;
    }
    qsort(expected, expected_count, sizeof(pxa_path_view_t),
          path_view_compare);
    {
        size_t actual_views_count = 0;
        stage = "collect-files";
        actual_views = collect_views(&actual, &actual_views_count);
        if (actual_views == NULL && actual_views_count != 0) {
            status = PXA_STATUS_RESOURCE_LIMIT;
            goto done;
        }
        if (actual_views_count != expected_count) {
            status = PXA_STATUS_DENIED;
            goto done;
        }
        stage = "compare-files";
        qsort(actual_views, actual_views_count, sizeof(pxa_path_view_t),
              path_view_compare);
        for (index = 0; index < actual_views_count; ++index) {
            if (actual_views[index].size != expected[index].size ||
                memcmp(actual_views[index].data, expected[index].data,
                       expected[index].size) != 0) {
                status = PXA_STATUS_DENIED;
                goto done;
            }
        }
        free(actual_views);
        actual_views = NULL;
    }
    free(expected);
    expected = NULL;
    string_array_clear(&actual);

    for (index = 0; index < (size_t)manifest->file_count; ++index) {
        installer->inventory[index].path = manifest->files[index].path;
        installer->inventory[index].size = manifest->files[index].size;
        installer->inventory[index].sha256 = manifest->files[index].sha256;
        stage = "hash-file";
        status = hash_and_copy(installer, root_fd, &manifest->files[index],
                               -1, NULL);
        if (status != PXA_STATUS_OK) goto done;
    }
    stage = "validate-inventory";
    status = pxa_package_inventory_validate(
        manifest, installer->inventory, (size_t)manifest->file_count);
    if (status != PXA_STATUS_OK) goto done;
    if (manifest->app_id.size > PXA_POSIX_INSTALLER_MAX_APP_ID) {
        stage = "validate-app-id";
        status = PXA_STATUS_DENIED;
        goto done;
    }
    memcpy(publisher_key_id_out, lineage.root_key_id,
           PXA_PACKAGE_DIGEST_BYTES);
    memcpy(app_id_out, manifest->app_id.data, manifest->app_id.size);
    app_id_out[manifest->app_id.size] = '\0';
    *app_id_size_out = manifest->app_id.size;
    status = PXA_STATUS_OK;
done:
    if (status != PXA_STATUS_OK) {
        PXA_POSIX_INSTALLER_LOG_FAILURE("Verify package", stage, status);
    }
    free(actual_views);
    free(expected);
    string_array_clear(&actual);
    free(manifest_bytes);
    return status;
}

static pxa_status_t verify_source_contents(
    pxa_posix_installer_t *installer, int source_fd,
    const uint8_t expected_key_id[PXA_PACKAGE_DIGEST_BYTES],
    const char *expected_app_id, size_t expected_app_id_size) {
    uint8_t verified_key_id[PXA_PACKAGE_DIGEST_BYTES];
    char verified_app_id[PXA_POSIX_INSTALLER_MAX_APP_ID + 1];
    size_t verified_app_id_size = 0;
    pxa_status_t status = verify_opened_directory(
        installer, source_fd, 1,
        verified_key_id, verified_app_id, &verified_app_id_size);
    if (status != PXA_STATUS_OK) return status;
    if (memcmp(verified_key_id, expected_key_id, PXA_PACKAGE_DIGEST_BYTES) !=
            0 ||
        verified_app_id_size != expected_app_id_size ||
        memcmp(verified_app_id, expected_app_id, expected_app_id_size) != 0) {
        return PXA_STATUS_DENIED;
    }
    return PXA_STATUS_OK;
}

static pxa_status_t fill_result(pxa_posix_installer_t *installer, int root_fd,
                                const char *root_path,
                                pxa_posix_installer_result_t *result) {
    pxa_package_manifest_t *manifest = NULL;
    pxa_package_limits_t required_limits;
    size_t encoded_size;
    size_t required;
    pxa_status_t status;
    if (result == NULL || result->struct_size < sizeof(*result) ||
        result->manifest_workspace == NULL || result->manifest == NULL ||
        result->encoded == NULL || result->encoded_capacity == 0 ||
        result->root == NULL || result->root_capacity == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    status = read_bounded(root_fd, "manifest.pxm", SIZE_MAX, result->encoded,
                          result->encoded_capacity, &encoded_size);
    if (status != PXA_STATUS_OK) return status;
    status = pxa_package_manifest_measure(
        (pxa_bytes_t){result->encoded, encoded_size}, &installer->limits,
        &required_limits);
    if (status != PXA_STATUS_OK) return status;
    required = pxa_package_manifest_workspace_size(&required_limits);
    if (required == 0 || result->manifest_workspace_size < required) {
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    status = pxa_package_manifest_parse(
        result->manifest_workspace, result->manifest_workspace_size,
        (pxa_bytes_t){result->encoded, encoded_size}, &required_limits,
        &manifest);
    if (status != PXA_STATUS_OK) return status;
    *result->manifest = manifest;
    if (strlen(root_path) >= result->root_capacity) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    strcpy(result->root, root_path);
    return PXA_STATUS_OK;
}

static pxa_status_t fill_result_encoded(
    pxa_posix_installer_t *installer, const uint8_t *encoded,
    size_t encoded_size, const char *root_path,
    pxa_posix_installer_result_t *result) {
    pxa_package_manifest_t *manifest = NULL;
    pxa_package_limits_t required_limits;
    size_t required;
    pxa_status_t status;
    if (result == NULL || result->struct_size < sizeof(*result) ||
        result->manifest_workspace == NULL || result->manifest == NULL ||
        result->encoded == NULL || result->encoded_capacity < encoded_size ||
        result->root == NULL || strlen(root_path) >= result->root_capacity) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    memcpy(result->encoded, encoded, encoded_size);
    status = pxa_package_manifest_measure(
        (pxa_bytes_t){result->encoded, encoded_size}, &installer->limits,
        &required_limits);
    if (status != PXA_STATUS_OK) return status;
    required = pxa_package_manifest_workspace_size(&required_limits);
    if (required == 0 || result->manifest_workspace_size < required) {
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    status = pxa_package_manifest_parse(
        result->manifest_workspace, result->manifest_workspace_size,
        (pxa_bytes_t){result->encoded, encoded_size}, &required_limits,
        &manifest);
    if (status != PXA_STATUS_OK) return status;
    *result->manifest = manifest;
    strcpy(result->root, root_path);
    return PXA_STATUS_OK;
}

static pxa_status_t lock_identity(pxa_posix_installer_t *installer,
                                  const pxa_posix_installer_identity_t *identity,
                                  int *lock_fd) {
    char identity_name[PXA_POSIX_INSTALLER_MAX_IDENTITY_NAME + 1];
    char lock_name[PXA_POSIX_INSTALLER_MAX_IDENTITY_NAME + 6];
    int root_fd = -1;
    int packages_fd = -1;
    pxa_status_t status = PXA_STATUS_INTERNAL;
    *lock_fd = -1;
    if (identity == NULL || identity->app_id.data == NULL ||
        identity->app_id.size > PXA_POSIX_INSTALLER_MAX_APP_ID ||
        identity->app_id.size == 0 ||
        identity->publisher_key_id.data == NULL ||
        identity->publisher_key_id.size != PXA_PACKAGE_DIGEST_BYTES ||
        !safe_app_id(identity->app_id)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    root_fd = open(installer->storage_root,
                   O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (root_fd < 0) goto done;
    status = open_tree_at(root_fd, "packages", 1, &packages_fd);
    if (status != PXA_STATUS_OK) goto done;
    if (!identity_storage_name(installer, identity, identity_name,
                               sizeof(identity_name))) {
        status = PXA_STATUS_INVALID_ARGUMENT;
        goto done;
    }
    snprintf(lock_name, sizeof(lock_name), "%s.lock", identity_name);
    *lock_fd = openat(packages_fd, lock_name,
                      O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (*lock_fd < 0) {
        status = PXA_STATUS_INTERNAL;
        goto done;
    }
    if ((installer->flags & PXA_POSIX_INSTALLER_FLAG_SKIP_LOCK) == 0 &&
        flock(*lock_fd, LOCK_EX) != 0) {
        close(*lock_fd);
        *lock_fd = -1;
        status = PXA_STATUS_INTERNAL;
        goto done;
    }
    status = PXA_STATUS_OK;
done:
    if (packages_fd >= 0) close(packages_fd);
    if (root_fd >= 0) close(root_fd);
    return status;
}

static pxa_status_t build_slot_paths(const pxa_posix_slot_ctx_t *ctx,
                                     pxa_package_slot_t slot, char **output) {
    char *path = NULL;
    switch (slot) {
        case PXA_PACKAGE_SLOT_CURRENT:
            path = (char *)malloc(strlen(ctx->root) + 1);
            if (path != NULL) strcpy(path, ctx->root);
            break;
        case PXA_PACKAGE_SLOT_PREVIOUS:
            path = join_path(ctx->session, "old");
            break;
        case PXA_PACKAGE_SLOT_INCOMING:
            path = join_path(ctx->session, "incoming");
            break;
        default:
            return PXA_STATUS_INVALID_ARGUMENT;
    }
    *output = path;
    return path == NULL ? PXA_STATUS_INTERNAL : PXA_STATUS_OK;
}

static pxa_status_t slot_inspect(void *context, pxa_package_slot_t slot,
                                 pxa_slot_state_t *state) {
    pxa_posix_slot_ctx_t *ctx = (pxa_posix_slot_ctx_t *)context;
    char *path = NULL;
    struct stat metadata;
    uint8_t publisher_key_id[PXA_PACKAGE_DIGEST_BYTES];
    char app_id[PXA_POSIX_INSTALLER_MAX_APP_ID + 1];
    size_t app_id_size = 0;
    pxa_status_t status;
    *state = PXA_SLOT_ABSENT;
    status = build_slot_paths(ctx, slot, &path);
    if (status != PXA_STATUS_OK) {
        *state = PXA_SLOT_CORRUPT;
        return PXA_STATUS_OK;
    }
    if (lstat(path, &metadata) != 0) {
        *state = PXA_SLOT_ABSENT;
        free(path);
        return PXA_STATUS_OK;
    }
    if (!S_ISDIR(metadata.st_mode)) {
        *state = PXA_SLOT_CORRUPT;
        free(path);
        return PXA_STATUS_OK;
    }
    {
        int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) {
            *state = PXA_SLOT_CORRUPT;
            free(path);
            return PXA_STATUS_OK;
        }
        status = verify_opened_directory(
            ctx->installer, fd, 1, publisher_key_id, app_id,
            &app_id_size);
        close(fd);
    }
    if (status != PXA_STATUS_OK ||
        memcmp(publisher_key_id, ctx->publisher_key_id,
               PXA_PACKAGE_DIGEST_BYTES) != 0 ||
        app_id_size != ctx->app_id_size ||
        memcmp(app_id, ctx->app_id, app_id_size) != 0) {
        *state = PXA_SLOT_CORRUPT;
    } else {
        *state = PXA_SLOT_VERIFIED;
    }
    free(path);
    return PXA_STATUS_OK;
}

static pxa_status_t slot_remove(void *context, pxa_package_slot_t slot) {
    pxa_posix_slot_ctx_t *ctx = (pxa_posix_slot_ctx_t *)context;
    char *path = NULL;
    pxa_status_t status = build_slot_paths(ctx, slot, &path);
    if (status != PXA_STATUS_OK) return status;
    status = remove_tree(path);
    free(path);
    return status;
}

static pxa_status_t slot_rename(void *context, pxa_package_slot_t from,
                                pxa_package_slot_t to) {
    pxa_posix_slot_ctx_t *ctx = (pxa_posix_slot_ctx_t *)context;
    char *from_path = NULL;
    char *to_path = NULL;
    pxa_status_t status;
    status = build_slot_paths(ctx, from, &from_path);
    if (status == PXA_STATUS_OK) {
        status = build_slot_paths(ctx, to, &to_path);
    }
    if (status != PXA_STATUS_OK) {
        free(from_path);
        free(to_path);
        return status;
    }
    if (rename(from_path, to_path) != 0) status = PXA_STATUS_INTERNAL;
    free(from_path);
    free(to_path);
    return status;
}

static pxa_status_t slot_barrier(void *context) {
    pxa_posix_slot_ctx_t *ctx = (pxa_posix_slot_ctx_t *)context;
    char *packages = join_path(ctx->installer->storage_root, "packages");
    int fd;
    pxa_status_t status;
    if (packages == NULL) return PXA_STATUS_INTERNAL;
    fd = open(packages, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        free(packages);
        return PXA_STATUS_INTERNAL;
    }
    status = fsync(fd) == 0 ? PXA_STATUS_OK : PXA_STATUS_INTERNAL;
    close(fd);
    free(packages);
    if (status != PXA_STATUS_OK) return status;
    {
        struct stat metadata;
        if (lstat(ctx->session, &metadata) == 0 && S_ISDIR(metadata.st_mode)) {
            fd = open(ctx->session,
                      O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if (fd < 0) return PXA_STATUS_INTERNAL;
            status = fsync(fd) == 0 ? PXA_STATUS_OK : PXA_STATUS_INTERNAL;
            close(fd);
        }
    }
    return status;
}

static pxa_status_t recover_locked(pxa_posix_installer_t *installer,
                                   const pxa_posix_slot_ctx_t *ctx) {
    pxa_slot_storage_t storage;
    pxa_status_t status;
    (void)installer;
    storage.struct_size = sizeof(storage);
    storage.context = (void *)ctx;
    storage.inspect = slot_inspect;
    storage.remove = slot_remove;
    storage.rename = slot_rename;
    storage.barrier = slot_barrier;
    status = pxa_slots_recover(&storage);
    if (status != PXA_STATUS_OK) return status;
    status = slot_remove((void *)ctx, PXA_PACKAGE_SLOT_PREVIOUS);
    if (status != PXA_STATUS_OK) return status;
    status = remove_tree(ctx->session);
    if (status != PXA_STATUS_OK) return status;
    return slot_barrier((void *)ctx);
}

static pxa_status_t reset_slots_locked(const pxa_posix_slot_ctx_t *ctx) {
    pxa_status_t status;
    status = slot_remove((void *)ctx, PXA_PACKAGE_SLOT_INCOMING);
    if (status == PXA_STATUS_OK) {
        status = slot_remove((void *)ctx, PXA_PACKAGE_SLOT_PREVIOUS);
    }
    if (status == PXA_STATUS_OK) {
        status = slot_remove((void *)ctx, PXA_PACKAGE_SLOT_CURRENT);
    }
    if (status == PXA_STATUS_OK) status = remove_tree(ctx->session);
    if (status == PXA_STATUS_OK) status = slot_barrier((void *)ctx);
    return status;
}

static pxa_status_t prepare_incoming(pxa_posix_installer_t *installer,
                                     int source_fd,
                                     const pxa_posix_slot_ctx_t *ctx) {
    uint8_t *manifest_bytes = NULL;
    uint8_t signature_bytes[PXA_POSIX_INSTALLER_SIGNATURE_ENVELOPE_BYTES];
    pxa_package_signature_t signature;
    pxa_package_publisher_lineage_t lineage;
    pxa_package_manifest_t *manifest = NULL;
    char *incoming = NULL;
    char *incoming_relative = NULL;
    pxa_string_array_t actual;
    pxa_path_view_t *expected = NULL;
    size_t expected_count;
    size_t manifest_size;
    size_t signature_size;
    size_t index;
    int found = 0;
    int incoming_fd = -1;
    pxa_status_t status;
    memset(&actual, 0, sizeof(actual));
    status = read_alloc_bounded(source_fd, "manifest.pxm", SIZE_MAX,
                                &manifest_bytes, &manifest_size);
    if (status != PXA_STATUS_OK) goto done;
    status = read_bounded(source_fd, "signature.pxs",
                          PXA_POSIX_INSTALLER_SIGNATURE_ENVELOPE_BYTES,
                          signature_bytes, sizeof(signature_bytes),
                          &signature_size);
    if (status != PXA_STATUS_OK) goto done;
    status = pxa_package_signature_parse(
        (pxa_bytes_t){signature_bytes, signature_size}, &signature);
    if (status != PXA_STATUS_OK) goto done;
    status = ensure_manifest_workspace(
        installer, (pxa_bytes_t){manifest_bytes, manifest_size});
    if (status != PXA_STATUS_OK) goto done;
    status = pxa_package_manifest_parse(
        installer->scratch, installer->scratch_size,
        (pxa_bytes_t){manifest_bytes, manifest_size},
        &installer->scratch_limits,
        &manifest);
    if (status != PXA_STATUS_OK) goto done;
    status = pxa_package_signature_identity_validate(manifest, &signature);
    if (status != PXA_STATUS_OK) goto done;
    if (installer->verify == NULL) {
        status = PXA_STATUS_UNSUPPORTED;
        goto done;
    }
    status = verify_manifest_signature(installer, manifest, &signature,
                                       &lineage);
    if (status != PXA_STATUS_OK) goto done;
    status = enumerate_files(source_fd, NULL, &actual, &found);
    if (status != PXA_STATUS_OK || !found) {
        status = status == PXA_STATUS_OK ? PXA_STATUS_DENIED : status;
        goto done;
    }
    expected_count = (size_t)manifest->file_count + 2;
    expected = (pxa_path_view_t *)malloc(expected_count *
                                         sizeof(pxa_path_view_t));
    if (expected == NULL) {
        status = PXA_STATUS_INTERNAL;
        goto done;
    }
    expected[0].data = "manifest.pxm";
    expected[0].size = sizeof("manifest.pxm") - 1;
    expected[1].data = "signature.pxs";
    expected[1].size = sizeof("signature.pxs") - 1;
    for (index = 0; index < (size_t)manifest->file_count; ++index) {
        expected[index + 2].data =
            (const char *)manifest->files[index].path.data;
        expected[index + 2].size = manifest->files[index].path.size;
    }
    qsort(expected, expected_count, sizeof(pxa_path_view_t),
          path_view_compare);
    {
        size_t actual_views_count = 0;
        pxa_path_view_t *actual_views =
            collect_views(&actual, &actual_views_count);
        if (actual_views == NULL && actual_views_count != 0) {
            status = PXA_STATUS_INTERNAL;
            goto done;
        }
        if (actual_views_count != expected_count) {
            free(actual_views);
            status = PXA_STATUS_DENIED;
            goto done;
        }
        qsort(actual_views, actual_views_count, sizeof(pxa_path_view_t),
              path_view_compare);
        for (index = 0; index < actual_views_count; ++index) {
            if (actual_views[index].size != expected[index].size ||
                memcmp(actual_views[index].data, expected[index].data,
                       expected[index].size) != 0) {
                free(actual_views);
                status = PXA_STATUS_DENIED;
                goto done;
            }
        }
        free(actual_views);
    }
    incoming = join_path(ctx->session, "incoming");
    if (incoming == NULL) {
        status = PXA_STATUS_INTERNAL;
        goto done;
    }
    {
        char relative[PXA_POSIX_INSTALLER_MAX_IDENTITY_NAME + 16];
        memcpy(relative, ".session-", 9);
        strcpy(relative + 9, ctx->identity_name);
        memcpy(relative + 9 + strlen(ctx->identity_name), "/incoming", 10);
        incoming_relative = malloc(strlen(relative) + 1);
        if (incoming_relative == NULL) {
            status = PXA_STATUS_INTERNAL;
            goto done;
        }
        strcpy(incoming_relative, relative);
    }
    {
        char *packages = join_path(installer->storage_root, "packages");
        int packages_fd = -1;
        if (packages == NULL) {
            status = PXA_STATUS_INTERNAL;
            goto done;
        }
        packages_fd = open(packages, O_RDONLY | O_DIRECTORY | O_CLOEXEC |
                                         O_NOFOLLOW);
        free(packages);
        if (packages_fd < 0) {
            status = PXA_STATUS_INTERNAL;
            goto done;
        }
        status = open_tree_at(packages_fd, incoming_relative, 1, &incoming_fd);
        close(packages_fd);
    }
    if (status != PXA_STATUS_OK) goto done;
    status = write_file_excl(incoming_fd, "manifest.pxm", manifest_bytes,
                             manifest_size);
    if (status != PXA_STATUS_OK) goto done;
    status = write_file_excl(incoming_fd, "signature.pxs", signature_bytes,
                             signature_size);
    if (status != PXA_STATUS_OK) goto done;
    for (index = 0; index < (size_t)manifest->file_count; ++index) {
        char destination[PXA_POSIX_INSTALLER_MAX_PACKAGE_PATH + 1];
        const pxa_package_file_t *file = &manifest->files[index];
        if (file->path.size > sizeof(destination) - 1) {
            status = PXA_STATUS_DENIED;
            goto done;
        }
        memcpy(destination, file->path.data, file->path.size);
        destination[file->path.size] = '\0';
        status = hash_and_copy(installer, source_fd, file, incoming_fd,
                               destination);
        if (status != PXA_STATUS_OK) goto done;
    }
    status = sync_tree(incoming_fd);
    if (status != PXA_STATUS_OK) goto done;
    status = PXA_STATUS_OK;
done:
    free(incoming);
    free(incoming_relative);
    free(expected);
    string_array_clear(&actual);
    if (incoming_fd >= 0) close(incoming_fd);
    free(manifest_bytes);
    return status;
}

static pxa_status_t open_output_excl(int root, pxa_bytes_t path,
                                     int *output) {
    char relative[PXA_POSIX_INSTALLER_MAX_PACKAGE_PATH + 1];
    char parent[PXA_POSIX_INSTALLER_MAX_PACKAGE_PATH + 1];
    const char *separator;
    int parent_fd = -1;
    pxa_status_t status;
    *output = -1;
    if (path.data == NULL || path.size == 0 ||
        path.size >= sizeof(relative)) {
        return PXA_STATUS_DENIED;
    }
    memcpy(relative, path.data, path.size);
    relative[path.size] = '\0';
    separator = strrchr(relative, '/');
    if (separator == NULL) {
        parent[0] = '\0';
    } else {
        const size_t parent_size = (size_t)(separator - relative);
        memcpy(parent, relative, parent_size);
        parent[parent_size] = '\0';
    }
    status = open_tree_at(root, parent, 1, &parent_fd);
    if (status != PXA_STATUS_OK) return status;
    *output = openat(parent_fd,
                     separator == NULL ? relative : separator + 1,
                     O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                     0600);
    close(parent_fd);
    return *output >= 0 ? PXA_STATUS_OK : PXA_STATUS_INTERNAL;
}

static pxa_status_t write_all(int file, const uint8_t *bytes, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        ssize_t written;
        do {
            written = write(file, bytes + offset, size - offset);
        } while (written < 0 && errno == EINTR);
        if (written <= 0) return PXA_STATUS_INTERNAL;
        offset += (size_t)written;
    }
    return PXA_STATUS_OK;
}

static pxa_status_t prepare_incoming_container(
    pxa_posix_installer_t *installer, int source_fd,
    const pxa_container_header_t *header, const uint8_t *manifest_bytes,
    size_t manifest_size, const pxa_posix_slot_ctx_t *ctx) {
    uint8_t package_signature[PXA_POSIX_INSTALLER_SIGNATURE_ENVELOPE_BYTES];
    pxa_package_manifest_t *manifest = NULL;
    char relative[PXA_POSIX_INSTALLER_MAX_APP_ID + 16];
    char *packages = NULL;
    uint64_t payload_position;
    size_t index;
    struct stat before;
    struct stat after;
    int packages_fd = -1;
    int incoming_fd = -1;
    pxa_status_t status;

    if (fstat(source_fd, &before) != 0 || !S_ISREG(before.st_mode) ||
        before.st_nlink != 1 || before.st_size < 0 ||
        (uint64_t)before.st_size != header->container_size) {
        return PXA_STATUS_DENIED;
    }
    status = ensure_manifest_workspace(
        installer, (pxa_bytes_t){manifest_bytes, manifest_size});
    if (status != PXA_STATUS_OK) return status;
    status = pxa_package_manifest_parse(
        installer->scratch, installer->scratch_size,
        (pxa_bytes_t){manifest_bytes, manifest_size},
        &installer->scratch_limits,
        &manifest);
    if (status != PXA_STATUS_OK) return status;
    status = read_exact_at(source_fd, header->package_signature_offset,
                           package_signature, sizeof(package_signature));
    if (status != PXA_STATUS_OK) return status;
    memcpy(relative, ".session-", 9);
    strcpy(relative + 9, ctx->identity_name);
    memcpy(relative + 9 + strlen(ctx->identity_name), "/incoming", 10);
    packages = join_path(installer->storage_root, "packages");
    if (packages == NULL) return PXA_STATUS_INTERNAL;
    packages_fd = open(packages,
                       O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    free(packages);
    if (packages_fd < 0) return PXA_STATUS_INTERNAL;
    status = open_tree_at(packages_fd, relative, 1, &incoming_fd);
    close(packages_fd);
    if (status != PXA_STATUS_OK) return status;
    status = write_file_excl(incoming_fd, "manifest.pxm", manifest_bytes,
                             manifest_size);
    if (status == PXA_STATUS_OK) {
        status = write_file_excl(incoming_fd, "signature.pxs",
                                 package_signature,
                                 sizeof(package_signature));
    }
    payload_position = header->payload_offset;
    for (index = 0; status == PXA_STATUS_OK && index < manifest->file_count;
         ++index) {
        const pxa_package_file_t *expected = &manifest->files[index];
        uint8_t file_record[16];
        pxa_openssl_sha256_stream_t hash_stream;
        uint8_t digest[PXA_PACKAGE_DIGEST_BYTES];
        uint64_t decoded_total = 0;
        uint64_t encoded_total = 0;
        uint64_t encoded_size;
        uint32_t chunk_count;
        uint32_t chunk_index;
        const uint32_t expected_chunks =
            (uint32_t)((expected->size + PXA_CONTAINER_CHUNK_BYTES - 1u) /
                       PXA_CONTAINER_CHUNK_BYTES);
        int target = -1;
        memset(&hash_stream, 0, sizeof(hash_stream));
        status = read_exact_at(source_fd, payload_position, file_record,
                               sizeof(file_record));
        if (status != PXA_STATUS_OK) break;
        payload_position += sizeof(file_record);
        chunk_count = pxa_read_u32(file_record + 4);
        encoded_size = pxa_read_u64(file_record + 8);
        if (pxa_read_u16(file_record) != index ||
            pxa_read_u16(file_record + 2) != 0 ||
            chunk_count != expected_chunks) {
            status = PXA_STATUS_DENIED;
            break;
        }
        status = open_output_excl(incoming_fd, expected->path, &target);
        if (status != PXA_STATUS_OK) break;
        status = pxa_openssl_sha256_stream_begin(&hash_stream);
        if (status != PXA_STATUS_OK) {
            close(target);
            break;
        }
        for (chunk_index = 0; status == PXA_STATUS_OK &&
                              chunk_index < chunk_count; ++chunk_index) {
            uint8_t chunk_header[4];
            uint16_t stored_size;
            uint16_t decoded_size;
            const uint8_t *decoded;
            int lz4_size;
            const uint64_t remaining = expected->size - decoded_total;
            const uint16_t expected_decoded =
                (uint16_t)(remaining > PXA_CONTAINER_CHUNK_BYTES
                               ? PXA_CONTAINER_CHUNK_BYTES
                               : remaining);
            status = read_exact_at(source_fd, payload_position, chunk_header,
                                   sizeof(chunk_header));
            if (status != PXA_STATUS_OK) break;
            payload_position += sizeof(chunk_header);
            encoded_total += sizeof(chunk_header);
            stored_size = pxa_read_u16(chunk_header);
            decoded_size = pxa_read_u16(chunk_header + 2);
            if (stored_size == 0 || decoded_size != expected_decoded ||
                stored_size > decoded_size ||
                (header->codec == PXA_CONTAINER_CODEC_STORE &&
                 stored_size != decoded_size) ||
                stored_size > installer->compressed_buffer_size ||
                decoded_size > installer->io_buffer_size ||
                decoded_total > expected->size ||
                decoded_size > expected->size - decoded_total) {
                status = PXA_STATUS_DENIED;
                break;
            }
            status = read_exact_at(source_fd, payload_position,
                                   installer->compressed_buffer, stored_size);
            if (status != PXA_STATUS_OK) break;
            payload_position += stored_size;
            encoded_total += stored_size;
            if (stored_size == decoded_size) {
                decoded = installer->compressed_buffer;
            } else if (header->codec == PXA_CONTAINER_CODEC_LZ4) {
                lz4_size = LZ4_decompress_safe(
                    (const char *)installer->compressed_buffer,
                    (char *)installer->io_buffer, stored_size, decoded_size);
                if (lz4_size != decoded_size) {
                    status = PXA_STATUS_DENIED;
                    break;
                }
                decoded = installer->io_buffer;
            } else {
                status = PXA_STATUS_DENIED;
                break;
            }
            status = pxa_openssl_sha256_stream_update(&hash_stream, decoded,
                                                       decoded_size);
            if (status == PXA_STATUS_OK)
                status = write_all(target, decoded, decoded_size);
            decoded_total += decoded_size;
        }
        if (status == PXA_STATUS_OK &&
            (decoded_total != expected->size || encoded_total != encoded_size))
            status = PXA_STATUS_DENIED;
        if (status == PXA_STATUS_OK)
            status = pxa_openssl_sha256_stream_finish(&hash_stream, digest);
        else
            pxa_openssl_sha256_stream_abort(&hash_stream);
        if (status == PXA_STATUS_OK &&
            memcmp(digest, expected->sha256, sizeof(digest)) != 0)
            status = PXA_STATUS_DENIED;
#ifndef ESP_PLATFORM
        if (status == PXA_STATUS_OK && fsync(target) != 0)
            status = PXA_STATUS_INTERNAL;
#endif
        if (close(target) != 0 && status == PXA_STATUS_OK)
            status = PXA_STATUS_INTERNAL;
    }
    if (status == PXA_STATUS_OK &&
        payload_position != header->payload_offset + header->payload_size)
        status = PXA_STATUS_DENIED;
    if (status == PXA_STATUS_OK &&
        (fstat(source_fd, &after) != 0 || !stat_same(&before, &after)))
        status = PXA_STATUS_DENIED;
    if (status == PXA_STATUS_OK) status = sync_tree(incoming_fd);
    close(incoming_fd);
    return status;
}

static char *identity_root_path(pxa_posix_installer_t *installer,
                                const char *app_id) {
    char *packages = join_path(installer->storage_root, "packages");
    char *root = NULL;
    if (packages != NULL) {
        root = join_path(packages, app_id);
        free(packages);
    }
    return root;
}

static char *session_root_path(pxa_posix_installer_t *installer,
                               const char *app_id) {
    char *packages = join_path(installer->storage_root, "packages");
    char name[PXA_POSIX_INSTALLER_MAX_IDENTITY_NAME + 10];
    char *session = NULL;
    if (strlen(app_id) > PXA_POSIX_INSTALLER_MAX_IDENTITY_NAME) return NULL;
    memcpy(name, ".session-", 9);
    strcpy(name + 9, app_id);
    if (packages != NULL) {
        session = join_path(packages, name);
        free(packages);
    }
    return session;
}

static int setup_ctx(pxa_posix_installer_t *installer,
                     const pxa_posix_installer_identity_t *identity,
                     pxa_posix_slot_ctx_t *ctx) {
    char app_id[PXA_POSIX_INSTALLER_MAX_APP_ID + 1];
    memset(ctx, 0, sizeof(*ctx));
    if (identity == NULL || identity->app_id.data == NULL ||
        identity->app_id.size == 0 ||
        identity->app_id.size > PXA_POSIX_INSTALLER_MAX_APP_ID ||
        identity->publisher_key_id.data == NULL ||
        identity->publisher_key_id.size != PXA_PACKAGE_DIGEST_BYTES ||
        !safe_app_id(identity->app_id)) {
        return 0;
    }
    memcpy(app_id, identity->app_id.data, identity->app_id.size);
    app_id[identity->app_id.size] = '\0';
    ctx->installer = installer;
    memcpy(ctx->publisher_key_id, identity->publisher_key_id.data,
           PXA_PACKAGE_DIGEST_BYTES);
    memcpy(ctx->app_id, app_id, identity->app_id.size + 1);
    ctx->app_id_size = identity->app_id.size;
    if (!identity_storage_name(installer, identity, ctx->identity_name,
                               sizeof(ctx->identity_name))) {
        return 0;
    }
    ctx->root = identity_root_path(installer, ctx->identity_name);
    ctx->session = session_root_path(installer, ctx->identity_name);
    return ctx->root != NULL && ctx->session != NULL;
}

static pxa_status_t identity_owner_check_or_claim(
    pxa_posix_installer_t *installer, const pxa_posix_slot_ctx_t *ctx) {
    static const uint8_t owner_magic[8] = {'P', 'X', 'A', 'O', 1, 0, 0, 0};
    uint8_t owner[PXA_POSIX_INSTALLER_OWNER_BYTES];
    uint8_t current_key_id[PXA_PACKAGE_DIGEST_BYTES];
    char current_app_id[PXA_POSIX_INSTALLER_MAX_APP_ID + 1];
    size_t current_app_id_size = 0;
    char *data_root = NULL;
    char *data_path = NULL;
    struct stat before;
    struct stat after;
    struct stat data_metadata;
    int state_fd = -1;
    int owners_fd = -1;
    int owner_fd = -1;
    int current_fd = -1;
    size_t offset = 0;
    pxa_status_t status;

    state_fd = open(installer->storage_root,
                    O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (state_fd < 0) return PXA_STATUS_INTERNAL;
    status = open_tree_at(state_fd, "owners", 1, &owners_fd);
    close(state_fd);
    if (status != PXA_STATUS_OK) return status;
    owner_fd = openat(owners_fd, ctx->identity_name,
                      O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (owner_fd >= 0) {
        if (fstat(owner_fd, &before) != 0 || !S_ISREG(before.st_mode) ||
            before.st_nlink != 1 ||
            before.st_size != PXA_POSIX_INSTALLER_OWNER_BYTES) {
            status = PXA_STATUS_DENIED;
            goto done;
        }
        while (offset < sizeof(owner)) {
            ssize_t count;
            do {
                count = read(owner_fd, owner + offset,
                             sizeof(owner) - offset);
            } while (count < 0 && errno == EINTR);
            if (count <= 0) {
                status = PXA_STATUS_DENIED;
                goto done;
            }
            offset += (size_t)count;
        }
        if (fstat(owner_fd, &after) != 0 || !stat_same(&before, &after) ||
            memcmp(owner, owner_magic, sizeof(owner_magic)) != 0 ||
            memcmp(owner + sizeof(owner_magic), ctx->publisher_key_id,
                   PXA_PACKAGE_DIGEST_BYTES) != 0) {
            status = PXA_STATUS_DENIED;
            goto done;
        }
        status = PXA_STATUS_OK;
        goto done;
    }
    if (errno != ENOENT) {
        status = PXA_STATUS_INTERNAL;
        goto done;
    }

    current_fd = open(ctx->root,
                      O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (current_fd >= 0) {
        status = verify_opened_directory(
            installer, current_fd, 0, current_key_id, current_app_id,
            &current_app_id_size);
        if (status != PXA_STATUS_OK ||
            memcmp(current_key_id, ctx->publisher_key_id,
                   PXA_PACKAGE_DIGEST_BYTES) != 0 ||
            current_app_id_size != ctx->app_id_size ||
            memcmp(current_app_id, ctx->app_id, ctx->app_id_size) != 0) {
            status = PXA_STATUS_DENIED;
            goto done;
        }
    } else if (errno != ENOENT) {
        status = PXA_STATUS_INTERNAL;
        goto done;
    } else {
        /* Pre-owner-format retained data has no provable publisher. Never
         * let a newly trusted publisher claim it by reusing the App ID. */
        data_root = join_path(installer->storage_root, "data");
        if (data_root == NULL) {
            status = PXA_STATUS_INTERNAL;
            goto done;
        }
        data_path = join_path(data_root, ctx->identity_name);
        if (data_path == NULL) {
            status = PXA_STATUS_INTERNAL;
            goto done;
        }
        if (lstat(data_path, &data_metadata) == 0) {
            status = PXA_STATUS_DENIED;
            goto done;
        }
        if (errno != ENOENT) {
            status = PXA_STATUS_INTERNAL;
            goto done;
        }
    }

    memcpy(owner, owner_magic, sizeof(owner_magic));
    memcpy(owner + sizeof(owner_magic), ctx->publisher_key_id,
           PXA_PACKAGE_DIGEST_BYTES);
    status = write_file_excl(owners_fd, ctx->identity_name, owner, sizeof(owner));
    if (status == PXA_STATUS_OK) status = sync_tree(owners_fd);

done:
    if (current_fd >= 0) close(current_fd);
    if (owner_fd >= 0) close(owner_fd);
    close(owners_fd);
    free(data_path);
    free(data_root);
    return status;
}

static pxa_slot_storage_t make_slot_storage(pxa_posix_slot_ctx_t *ctx) {
    pxa_slot_storage_t storage;
    storage.struct_size = sizeof(storage);
    storage.context = ctx;
    storage.inspect = slot_inspect;
    storage.remove = slot_remove;
    storage.rename = slot_rename;
    storage.barrier = slot_barrier;
    return storage;
}

static int config_valid(const pxa_posix_installer_config_t *config) {
    if (config == NULL || config->struct_size < sizeof(*config) ||
        config->storage_root == NULL || config->storage_root[0] == '\0' ||
        config->limits.struct_size < sizeof(config->limits) ||
        config->limits.max_components == 0 ||
        config->limits.max_files == 0 ||
        config->trust.struct_size < sizeof(config->trust) ||
        (config->trust.key_count != 0 && config->trust.keys == NULL)) {
        return 0;
    }
    return 1;
}

size_t pxa_posix_installer_workspace_size(
    const pxa_posix_installer_config_t *config) {
    size_t size;
    if (!config_valid(config)) return 0;
    /* The caller's allocation can start at any byte address. Account for
     * alignment before the installer and again before its I/O buffers. */
    size = 2u * (PXA_POSIX_INSTALLER_ALIGNMENT - 1u);
    if (sizeof(pxa_posix_installer_t) > SIZE_MAX - size) return 0;
    size += sizeof(pxa_posix_installer_t);
    if (PXA_POSIX_INSTALLER_IO_BYTES > SIZE_MAX - size) return 0;
    size += PXA_POSIX_INSTALLER_IO_BYTES;
    if (PXA_CONTAINER_CHUNK_BYTES > SIZE_MAX - size) return 0;
    return size + PXA_CONTAINER_CHUNK_BYTES;
}

pxa_status_t pxa_posix_installer_init(void *workspace, size_t workspace_size,
                                      const pxa_posix_installer_config_t *config,
                                      pxa_posix_installer_t **output) {
    size_t required;
    pxa_posix_installer_t *installer;
    uint8_t *cursor;
    uint8_t *workspace_end;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    required = pxa_posix_installer_workspace_size(config);
    if (workspace == NULL || required == 0 || workspace_size < required) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    installer = (pxa_posix_installer_t *)align_up((uintptr_t)workspace,
                                                  PXA_POSIX_INSTALLER_ALIGNMENT);
    if ((uintptr_t)installer > (uintptr_t)workspace + workspace_size ||
        sizeof(*installer) >
            (size_t)((uintptr_t)workspace + workspace_size -
                     (uintptr_t)installer)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    memset(installer, 0, sizeof(*installer));
    installer->magic = PXA_POSIX_INSTALLER_MAGIC;
    installer->storage_root = config->storage_root;
    installer->storage_root_len = strlen(config->storage_root);
    installer->trust = config->trust;
    installer->limits = config->limits;
    if (config->faults != NULL) {
        installer->faults = *config->faults;
        installer->has_faults = 1;
    }
    installer->flags = config->flags;
#ifdef ESP_PLATFORM
    /* ESP hosts supply their own verifier (mbedTLS adapter). */
    installer->verify = config->verify;
#else
    installer->verify = config->verify != NULL ? config->verify
                                               : pxa_openssl_p256_verify;
#endif
    cursor = (uint8_t *)installer + sizeof(*installer);
    cursor = (uint8_t *)align_up((uintptr_t)cursor,
                                 PXA_POSIX_INSTALLER_ALIGNMENT);
    workspace_end = (uint8_t *)workspace + workspace_size;
    if ((uintptr_t)cursor > (uintptr_t)workspace_end ||
        PXA_POSIX_INSTALLER_IO_BYTES >
        (size_t)(workspace_end - cursor)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    installer->io_buffer = cursor;
    installer->io_buffer_size = PXA_POSIX_INSTALLER_IO_BYTES;
    cursor += PXA_POSIX_INSTALLER_IO_BYTES;
    if ((uintptr_t)cursor > (uintptr_t)workspace_end ||
        PXA_CONTAINER_CHUNK_BYTES > (size_t)(workspace_end - cursor)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    installer->compressed_buffer = cursor;
    installer->compressed_buffer_size = PXA_CONTAINER_CHUNK_BYTES;
    *output = installer;
    return PXA_STATUS_OK;
}

void pxa_posix_installer_deinit(pxa_posix_installer_t *installer) {
    if (installer == NULL || installer->magic != PXA_POSIX_INSTALLER_MAGIC) {
        return;
    }
    free(installer->scratch);
    free(installer->inventory);
    installer->scratch = NULL;
    installer->inventory = NULL;
    installer->scratch_size = 0;
    installer->inventory_capacity = 0;
    installer->magic = 0;
}

static void free_ctx(pxa_posix_slot_ctx_t *ctx) {
    free(ctx->root);
    free(ctx->session);
    ctx->root = NULL;
    ctx->session = NULL;
}

static int lineage_links_equal(const pxa_package_lineage_link_t *left,
                               const pxa_package_lineage_link_t *right) {
    return left->generation == right->generation &&
           left->flags == right->flags &&
           memcmp(left->old_key_id, right->old_key_id,
                  PXA_PACKAGE_DIGEST_BYTES) == 0 &&
           memcmp(left->new_key_id, right->new_key_id,
                  PXA_PACKAGE_DIGEST_BYTES) == 0 &&
           left->new_spki.size == right->new_spki.size &&
           memcmp(left->new_spki.data, right->new_spki.data,
                  left->new_spki.size) == 0 &&
           memcmp(left->signature, right->signature,
                  PXA_PACKAGE_SIGNATURE_BYTES) == 0;
}

static pxa_status_t validate_lineage_transition(
    pxa_posix_installer_t *installer, const uint8_t *current_bytes,
    size_t current_size, const uint8_t *source_bytes, size_t source_size) {
    pxa_package_manifest_t *manifest = NULL;
    pxa_package_publisher_lineage_t current;
    pxa_package_publisher_lineage_t source;
    uint16_t index;
    pxa_status_t status;
    status = ensure_manifest_workspace(
        installer, (pxa_bytes_t){source_bytes, source_size});
    if (status != PXA_STATUS_OK) return status;
    status = pxa_package_manifest_parse(
        installer->scratch, installer->scratch_size,
        (pxa_bytes_t){source_bytes, source_size},
        &installer->scratch_limits,
        &manifest);
    if (status != PXA_STATUS_OK) return status;
    status = pxa_package_publisher_lineage_parse(manifest, &source);
    if (status != PXA_STATUS_OK) return status;
    status = ensure_manifest_workspace(
        installer, (pxa_bytes_t){current_bytes, current_size});
    if (status != PXA_STATUS_OK) return status;
    status = pxa_package_manifest_parse(
        installer->scratch, installer->scratch_size,
        (pxa_bytes_t){current_bytes, current_size},
        &installer->scratch_limits,
        &manifest);
    if (status != PXA_STATUS_OK) return status;
    status = pxa_package_publisher_lineage_parse(manifest, &current);
    if (status != PXA_STATUS_OK ||
        memcmp(current.root_key_id, source.root_key_id,
               PXA_PACKAGE_DIGEST_BYTES) != 0) {
        return PXA_STATUS_DENIED;
    }
    for (index = 0; index < current.link_count && index < source.link_count;
         ++index) {
        if (!lineage_links_equal(&current.links[index], &source.links[index]))
            return PXA_STATUS_DENIED;
    }
    if (source.link_count < current.link_count &&
        (current.links[source.link_count].flags & UINT32_C(1)) != 0) {
        /* The first removed link revoked the signer to which this Package
         * attempts to roll back. Recovery-authority installs are separate. */
        return PXA_STATUS_DENIED;
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_posix_installer_install_for_identity(
    pxa_posix_installer_t *installer, const char *source_dir,
    const pxa_posix_installer_identity_t *expected,
    pxa_posix_installer_result_t *result,
    pxa_posix_install_disposition_t *disposition) {
    pxa_posix_slot_ctx_t ctx;
    pxa_slot_storage_t storage;
    uint8_t source_key_id[PXA_PACKAGE_DIGEST_BYTES];
    char source_app_id[PXA_POSIX_INSTALLER_MAX_APP_ID + 1];
    size_t source_app_id_size = 0;
    pxa_posix_installer_identity_t identity;
    uint8_t *source_manifest = NULL;
    uint8_t *current_manifest = NULL;
    size_t source_manifest_size = 0;
    size_t source_manifest_capacity = 0;
    pxa_container_header_t container_header;
    struct stat source_metadata;
    int source_is_container = 0;
    int source_fd = -1;
    int lock_fd = -1;
    int current_fd = -1;
    pxa_status_t status;
    const char *stage = "open-source";
    memset(&ctx, 0, sizeof(ctx));
    if (installer == NULL || installer->magic != PXA_POSIX_INSTALLER_MAGIC ||
        source_dir == NULL) {
                return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (disposition != NULL) *disposition = PXA_POSIX_INSTALL_INSTALLED;
    status = open_package_source(source_dir, &source_fd,
                                 &source_is_container, &source_metadata);
    if (status != PXA_STATUS_OK) goto done;
    stage = "verify-source-identity";
    if (source_is_container) {
        status = verify_opened_container(
            installer, source_fd, &source_manifest, &source_manifest_capacity,
            &source_manifest_size, source_key_id, source_app_id,
            &source_app_id_size, &container_header);
    } else {
        status = verify_opened_directory(
            installer, source_fd, 0, source_key_id, source_app_id,
            &source_app_id_size);
    }
    if (status != PXA_STATUS_OK) goto done;
    if (expected != NULL &&
        (expected->publisher_key_id.data == NULL ||
         expected->publisher_key_id.size != PXA_PACKAGE_DIGEST_BYTES ||
         !safe_app_id(expected->app_id) ||
         expected->app_id.size != source_app_id_size ||
         memcmp(expected->publisher_key_id.data, source_key_id,
                PXA_PACKAGE_DIGEST_BYTES) != 0 ||
         memcmp(expected->app_id.data, source_app_id,
                source_app_id_size) != 0)) {
        stage = "match-expected-identity";
        status = PXA_STATUS_DENIED;
        goto done;
    }
    identity.publisher_key_id =
        (pxa_bytes_t){source_key_id, PXA_PACKAGE_DIGEST_BYTES};
    identity.app_id =
        (pxa_bytes_t){(const uint8_t *)source_app_id, source_app_id_size};
    stage = "lock-identity";
    status = lock_identity(installer, &identity, &lock_fd);
        if (status != PXA_STATUS_OK) goto done;
    if (!setup_ctx(installer, &identity, &ctx)) {
        stage = "setup-context";
        status = PXA_STATUS_INTERNAL;
        goto done;
    }
    storage = make_slot_storage(&ctx);
    stage = "recover-slots";
    status = recover_locked(installer, &ctx);
    if (status != PXA_STATUS_OK &&
        (installer->flags & PXA_POSIX_INSTALLER_FLAG_REPAIR_CORRUPT) != 0) {
        /* A repair may discard the committed slot, so validate every source
         * file before allowing it to replace an unrecoverable transaction. */
        stage = "verify-source-for-repair";
        if (source_is_container) {
            status = verify_opened_container(
                installer, source_fd, &source_manifest,
                &source_manifest_capacity, &source_manifest_size,
                source_key_id, source_app_id, &source_app_id_size,
                &container_header);
        } else {
            status = verify_source_contents(installer, source_fd,
                                            source_key_id, source_app_id,
                                            source_app_id_size);
        }
        if (status != PXA_STATUS_OK) goto done;
        stage = "reset-corrupt-slots";
        status = reset_slots_locked(&ctx);
    }
    if (status != PXA_STATUS_OK) goto done;

    stage = "check-identity-owner";
    status = identity_owner_check_or_claim(installer, &ctx);
    if (status != PXA_STATUS_OK) goto done;

    {
        struct stat metadata;
        if (lstat(ctx.root, &metadata) == 0 && S_ISDIR(metadata.st_mode)) {
            size_t source_size = 0;
            size_t current_size = 0;
            current_fd = open(ctx.root,
                              O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if (current_fd < 0) {
                stage = "open-current";
                status = PXA_STATUS_INTERNAL;
                goto done;
            }
            stage = "read-current-manifest";
            status = source_is_container
                         ? (source_size = source_manifest_size, PXA_STATUS_OK)
                         : read_alloc_bounded(
                               source_fd, "manifest.pxm",
                               SIZE_MAX,
                               &source_manifest, &source_size);
            if (status == PXA_STATUS_OK)
                status = read_alloc_bounded(
                    current_fd, "manifest.pxm",
                    SIZE_MAX, &current_manifest,
                    &current_size);
            if (status != PXA_STATUS_OK) goto done;
            if (source_size == current_size &&
                memcmp(source_manifest, current_manifest, source_size) == 0) {
                /* Do not report a staged package as current until its files
                 * are checked, even when its signed manifest is unchanged. */
                stage = "verify-unchanged-source";
                if (source_is_container) {
                    status = verify_opened_container(
                        installer, source_fd, &source_manifest,
                        &source_manifest_capacity, &source_manifest_size,
                        source_key_id, source_app_id, &source_app_id_size,
                        &container_header);
                } else {
                    status = verify_source_contents(
                        installer, source_fd, source_key_id, source_app_id,
                        source_app_id_size);
                }
                if (status != PXA_STATUS_OK) goto done;
                stage = "fill-current-result";
                status = fill_result(installer, current_fd, ctx.root, result);
                if (status == PXA_STATUS_OK && disposition != NULL) {
                    *disposition = PXA_POSIX_INSTALL_ALREADY_CURRENT;
                }
                goto done;
            }
            stage = "validate-publisher-lineage-transition";
            status = validate_lineage_transition(
                installer, current_manifest, current_size, source_manifest,
                source_size);
            if (status != PXA_STATUS_OK) goto done;
            close(current_fd);
            current_fd = -1;
        }
    }
    if (!source_is_container) {
        free(source_manifest);
        source_manifest = NULL;
    }
    free(current_manifest);
    current_manifest = NULL;

    {
        char *packages = join_path(installer->storage_root, "packages");
        char session_name[PXA_POSIX_INSTALLER_MAX_IDENTITY_NAME + 10];
        int packages_fd = -1;
        if (packages == NULL) {
            stage = "allocate-session-path";
            status = PXA_STATUS_INTERNAL;
            goto done;
        }
        memcpy(session_name, ".session-", 9);
        strcpy(session_name + 9, ctx.identity_name);
        packages_fd = open(packages,
                           O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        free(packages);
        if (packages_fd < 0) {
            stage = "open-packages";
            status = PXA_STATUS_INTERNAL;
            goto done;
        }
        {
            int session_fd = -1;
            stage = "create-session";
            status = open_tree_at(packages_fd, session_name, 1, &session_fd);
            close(packages_fd);
            if (status == PXA_STATUS_OK) close(session_fd);
        }
        if (status != PXA_STATUS_OK) goto done;
    }
    stage = "prepare-incoming";
    status = source_is_container
                 ? prepare_incoming_container(
                       installer, source_fd, &container_header,
                       source_manifest, source_manifest_size, &ctx)
                 : prepare_incoming(installer, source_fd, &ctx);
        if (status != PXA_STATUS_OK) {
        char *incoming = join_path(ctx.session, "incoming");
        (void)remove_tree(incoming);
        free(incoming);
        (void)remove_tree(ctx.session);
        goto done;
    }
    stage = "commit-incoming";
    status = pxa_slots_commit_incoming(
        &storage, installer->has_faults ? &installer->faults : NULL);
    if (status != PXA_STATUS_OK) goto done;
    stage = "remove-previous";
    status = slot_remove(&ctx, PXA_PACKAGE_SLOT_PREVIOUS);
    if (status == PXA_STATUS_OK) {
        stage = "remove-session";
        status = remove_tree(ctx.session);
    }
    if (status == PXA_STATUS_OK) {
        stage = "final-barrier";
        status = slot_barrier(&ctx);
    }
    if (status != PXA_STATUS_OK) goto done;
    stage = "open-installed";
    current_fd = open(ctx.root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (current_fd < 0) {
        status = PXA_STATUS_INTERNAL;
        goto done;
    }
    stage = "fill-installed-result";
    status = fill_result(installer, current_fd, ctx.root, result);
    if (status == PXA_STATUS_OK && disposition != NULL) {
        *disposition = PXA_POSIX_INSTALL_INSTALLED;
    }
done:
    if (status != PXA_STATUS_OK) {
        PXA_POSIX_INSTALLER_LOG_FAILURE(source_dir, stage, status);
    }
    if (current_fd >= 0) close(current_fd);
    if (lock_fd >= 0) close(lock_fd);
    if (source_fd >= 0) close(source_fd);
    free(source_manifest);
    free(current_manifest);
    free_ctx(&ctx);
    return status;
}

pxa_status_t pxa_posix_installer_install(
    pxa_posix_installer_t *installer, const char *source_dir,
    pxa_posix_installer_result_t *result,
    pxa_posix_install_disposition_t *disposition) {
    return pxa_posix_installer_install_for_identity(
        installer, source_dir, NULL, result, disposition);
}

pxa_status_t pxa_posix_installer_source_manifest_size(
    pxa_posix_installer_t *installer, const char *source_path, size_t *size) {
    uint8_t header_bytes[PXA_CONTAINER_HEADER_BYTES];
    pxa_container_header_t header;
    struct stat source_metadata;
    struct stat manifest_metadata;
    int source_is_container = 0;
    int source_fd = -1;
    int manifest_fd = -1;
    pxa_status_t status;
    if (installer == NULL || installer->magic != PXA_POSIX_INSTALLER_MAGIC ||
        source_path == NULL || size == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *size = 0;
    status = open_package_source(source_path, &source_fd, &source_is_container,
                                 &source_metadata);
    if (status != PXA_STATUS_OK) return status;
    if (source_is_container) {
        status = source_metadata.st_size < 0
                     ? PXA_STATUS_DENIED
                     : read_exact_at(source_fd, 0, header_bytes,
                                     sizeof(header_bytes));
        if (status == PXA_STATUS_OK) {
            status = pxa_container_header_parse(
                (pxa_bytes_t){header_bytes, sizeof(header_bytes)},
                (uint64_t)source_metadata.st_size, &header);
        }
        if (status == PXA_STATUS_OK &&
            (header.manifest_size == 0 ||
             header.file_count > installer->limits.max_files)) {
            status = PXA_STATUS_DENIED;
        }
        if (status == PXA_STATUS_OK) *size = header.manifest_size;
    } else {
        status = open_relative(source_fd, "manifest.pxm", &manifest_fd,
                               &manifest_metadata);
        if (status == PXA_STATUS_OK &&
            (manifest_metadata.st_size <= 0 ||
             (uint64_t)manifest_metadata.st_size > SIZE_MAX)) {
            status = PXA_STATUS_DENIED;
        }
        if (status == PXA_STATUS_OK) *size = (size_t)manifest_metadata.st_size;
    }
    if (manifest_fd >= 0) close(manifest_fd);
    close(source_fd);
    return status;
}

pxa_status_t pxa_posix_installer_verify_source(
    pxa_posix_installer_t *installer, const char *source_dir,
    pxa_posix_installer_result_t *result) {
    uint8_t publisher_key_id[PXA_PACKAGE_DIGEST_BYTES];
    char app_id[PXA_POSIX_INSTALLER_MAX_APP_ID + 1];
    size_t app_id_size = 0;
    size_t manifest_size = 0;
    size_t manifest_capacity = 0;
    uint8_t *manifest_bytes = NULL;
    pxa_container_header_t container_header;
    struct stat metadata;
    int source_is_container = 0;
    int source_fd = -1;
    pxa_status_t status;
    if (installer == NULL || installer->magic != PXA_POSIX_INSTALLER_MAGIC ||
        source_dir == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    status = open_package_source(source_dir, &source_fd,
                                 &source_is_container, &metadata);
    if (status != PXA_STATUS_OK) {
        PXA_POSIX_INSTALLER_LOG_FAILURE(source_dir, "open-source", status);
        return status;
    }
    if (source_is_container) {
        status = verify_opened_container(
            installer, source_fd, &manifest_bytes, &manifest_capacity,
            &manifest_size,
            publisher_key_id, app_id, &app_id_size, &container_header);
        if (status == PXA_STATUS_OK) {
            status = fill_result_encoded(installer, manifest_bytes,
                                         manifest_size, source_dir, result);
        }
    } else {
        status = verify_opened_directory(
            installer, source_fd, 1, publisher_key_id, app_id, &app_id_size);
        if (status == PXA_STATUS_OK) {
            status = fill_result(installer, source_fd, source_dir, result);
        }
    }
    free(manifest_bytes);
    close(source_fd);
    return status;
}

pxa_status_t pxa_posix_installer_recover(
    pxa_posix_installer_t *installer,
    const pxa_posix_installer_identity_t *identity) {
    pxa_posix_slot_ctx_t ctx;
    int lock_fd = -1;
    pxa_status_t status;
    memset(&ctx, 0, sizeof(ctx));
    if (installer == NULL || installer->magic != PXA_POSIX_INSTALLER_MAGIC) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    status = lock_identity(installer, identity, &lock_fd);
    if (status != PXA_STATUS_OK) return status;
    if (!setup_ctx(installer, identity, &ctx)) {
        status = PXA_STATUS_INTERNAL;
        goto done;
    }
    status = recover_locked(installer, &ctx);
done:
    if (lock_fd >= 0) close(lock_fd);
    free_ctx(&ctx);
    return status;
}

pxa_status_t pxa_posix_installer_load_current(
    pxa_posix_installer_t *installer,
    const pxa_posix_installer_identity_t *identity,
    pxa_posix_installer_result_t *result) {
    pxa_posix_slot_ctx_t ctx;
    uint8_t parsed_key_id[PXA_PACKAGE_DIGEST_BYTES];
    char parsed_app_id[PXA_POSIX_INSTALLER_MAX_APP_ID + 1];
    size_t parsed_app_id_size = 0;
    int lock_fd = -1;
    int root_fd = -1;
    pxa_status_t status;
    memset(&ctx, 0, sizeof(ctx));
    if (installer == NULL || installer->magic != PXA_POSIX_INSTALLER_MAGIC) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    status = lock_identity(installer, identity, &lock_fd);
    if (status != PXA_STATUS_OK) return status;
    if (!setup_ctx(installer, identity, &ctx)) {
        status = PXA_STATUS_INTERNAL;
        goto done;
    }
    root_fd = open(ctx.root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (root_fd < 0) {
        status = PXA_STATUS_NOT_FOUND;
        goto done;
    }
    status = verify_opened_directory(
        installer, root_fd, 1, parsed_key_id, parsed_app_id,
        &parsed_app_id_size);
    if (status != PXA_STATUS_OK) goto done;
    if (memcmp(parsed_key_id, ctx.publisher_key_id,
               PXA_PACKAGE_DIGEST_BYTES) != 0 ||
        parsed_app_id_size != ctx.app_id_size ||
        memcmp(parsed_app_id, ctx.app_id, ctx.app_id_size) != 0) {
        status = PXA_STATUS_DENIED;
        goto done;
    }
    status = fill_result(installer, root_fd, ctx.root, result);
done:
    if (root_fd >= 0) close(root_fd);
    if (lock_fd >= 0) close(lock_fd);
    free_ctx(&ctx);
    return status;
}

pxa_status_t pxa_posix_installer_uninstall(
    pxa_posix_installer_t *installer,
    const pxa_posix_installer_identity_t *identity) {
    pxa_posix_slot_ctx_t ctx;
    int lock_fd = -1;
    pxa_status_t status;
    memset(&ctx, 0, sizeof(ctx));
    if (installer == NULL || installer->magic != PXA_POSIX_INSTALLER_MAGIC) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    status = lock_identity(installer, identity, &lock_fd);
    if (status != PXA_STATUS_OK) return status;
    if (!setup_ctx(installer, identity, &ctx)) {
        status = PXA_STATUS_INTERNAL;
        goto done;
    }
    status = slot_remove(&ctx, PXA_PACKAGE_SLOT_INCOMING);
    if (status == PXA_STATUS_OK) {
        status = slot_remove(&ctx, PXA_PACKAGE_SLOT_PREVIOUS);
    }
    if (status == PXA_STATUS_OK) {
        status = slot_remove(&ctx, PXA_PACKAGE_SLOT_CURRENT);
    }
    if (status == PXA_STATUS_OK) status = remove_tree(ctx.session);
    if (status == PXA_STATUS_OK) status = slot_barrier(&ctx);
done:
    if (lock_fd >= 0) close(lock_fd);
    free_ctx(&ctx);
    return status;
}
