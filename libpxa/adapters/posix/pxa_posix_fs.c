#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "pxa/posix/pxa_posix_fs.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <dirent.h>

#ifdef ESP_PLATFORM
#include "pxa/esp/pxa_esp_posix_shim.h"
#define dup pxa_esp_dup
#define fdopendir pxa_esp_fdopendir
#define fstat pxa_esp_fstat
#define fstatat pxa_esp_fstatat
#define mkdirat pxa_esp_mkdirat
#define openat pxa_esp_openat
#define renameat pxa_esp_renameat
#define unlinkat pxa_esp_unlinkat
#endif

#define PXA_POSIX_FS_MAGIC UINT32_C(0x50504e46)
#define PXA_POSIX_FS_ALIGNMENT ((size_t)16)

typedef struct {
    char scratch[PXA_FS_MAX_PATH_BYTES + 1];
    uint16_t offset[PXA_FS_MAX_PATH_BYTES];
    uint16_t length[PXA_FS_MAX_PATH_BYTES];
    uint16_t count;
} pxa_posix_path_split_t;

typedef struct {
    int fd;
    DIR *stream;
    uint16_t path_size;
    uint8_t kind;
    uint8_t occupied;
    char path[PXA_FS_MAX_PATH_BYTES];
} pxa_posix_fs_entry_t;

struct pxa_posix_fs {
    uint32_t magic;
    uint64_t usage;
    uint64_t quota;
    uint16_t max_open_resources;
    uint16_t open_count;
    int root_fd;
    pxa_posix_fs_entry_t entries[];
};

static uintptr_t align_up(uintptr_t value, size_t alignment) {
    uintptr_t mask = (uintptr_t)alignment - 1u;
    return (value + mask) & ~mask;
}

static pxa_status_t status_from_errno(int error) {
    if (error == ENOENT) return PXA_STATUS_NOT_FOUND;
    if (error == EEXIST || error == ENOTEMPTY) return PXA_STATUS_BUSY;
    if (error == ENOSPC) return PXA_STATUS_QUOTA_EXCEEDED;
    return PXA_STATUS_INTERNAL;
}

static int valid_utf8(const uint8_t *data, size_t size) {
    size_t index = 0;
    while (index < size) {
        uint8_t first = data[index++];
        uint32_t codepoint;
        uint32_t minimum;
        size_t continuation;
        size_t count;
        if (first < UINT8_C(0x80)) {
            if (first == 0 || first < 0x20 || first == 0x7f || first == '\\') {
                return 0;
            }
            continue;
        }
        if ((first & UINT8_C(0xe0)) == UINT8_C(0xc0)) {
            codepoint = first & UINT8_C(0x1f);
            minimum = UINT32_C(0x80);
            continuation = 1;
        } else if ((first & UINT8_C(0xf0)) == UINT8_C(0xe0)) {
            codepoint = first & UINT8_C(0x0f);
            minimum = UINT32_C(0x800);
            continuation = 2;
        } else if ((first & UINT8_C(0xf8)) == UINT8_C(0xf0)) {
            codepoint = first & UINT8_C(0x07);
            minimum = UINT32_C(0x10000);
            continuation = 3;
        } else {
            return 0;
        }
        if (continuation > size - index) return 0;
        for (count = 0; count < continuation; ++count) {
            uint8_t next = data[index++];
            if ((next & UINT8_C(0xc0)) != UINT8_C(0x80)) return 0;
            codepoint = (codepoint << 6) | (next & UINT8_C(0x3f));
        }
        if (codepoint < minimum || codepoint > UINT32_C(0x10ffff) ||
            (codepoint >= UINT32_C(0xd800) &&
             codepoint <= UINT32_C(0xdfff))) {
            return 0;
        }
    }
    return 1;
}

static int segment_is_valid(pxa_bytes_t segment) {
    if (segment.data == NULL || segment.size == 0 ||
        segment.size > PXA_FS_MAX_SEGMENT_BYTES ||
        (segment.size == 1 && segment.data[0] == '.') ||
        (segment.size == 2 && segment.data[0] == '.' &&
         segment.data[1] == '.') ||
        (segment.size >= 5 && memcmp(segment.data, ".pxa-", 5) == 0)) {
        return 0;
    }
    return valid_utf8(segment.data, segment.size);
}

static int split_path(pxa_bytes_t path, pxa_posix_path_split_t *split) {
    size_t begin = 0;
    size_t index;
    memset(split, 0, sizeof(*split));
    if (path.data == NULL || path.size == 0 ||
        path.size > PXA_FS_MAX_PATH_BYTES || path.data[0] == '/' ||
        path.data[path.size - 1] == '/') {
        return 0;
    }
    memcpy(split->scratch, path.data, path.size);
    split->scratch[path.size] = '\0';
    for (index = 0; index <= path.size; ++index) {
        if (index != path.size && split->scratch[index] != '/') continue;
        const size_t length = index - begin;
        if (length == 0 || length > PXA_FS_MAX_SEGMENT_BYTES ||
            !segment_is_valid((pxa_bytes_t){(const uint8_t *)split->scratch +
                                                begin,
                                            length})) {
            return 0;
        }
        split->offset[split->count] = (uint16_t)begin;
        split->length[split->count] = (uint16_t)length;
        ++split->count;
        split->scratch[index] = '\0';
        begin = index + 1;
    }
    return split->count != 0;
}

static int entry_is_open_regular(const pxa_posix_fs_t *fs, const char *path,
                                 size_t path_size) {
    uint16_t index;
    for (index = 0; index < fs->max_open_resources; ++index) {
        const pxa_posix_fs_entry_t *entry = &fs->entries[index];
        if (!entry->occupied || entry->kind != PXA_FS_KIND_REGULAR) continue;
        if (entry->path_size == path_size &&
            memcmp(entry->path, path, path_size) == 0) {
            return 1;
        }
    }
    return 0;
}

static pxa_status_t open_parent(pxa_posix_fs_t *fs,
                                const pxa_posix_path_split_t *split,
                                int *parent) {
    int current;
    uint16_t index;
    *parent = -1;
    if (split->count == 0) return PXA_STATUS_INVALID_ARGUMENT;
    current = dup(fs->root_fd);
    if (current < 0) return PXA_STATUS_INTERNAL;
    for (index = 0; index + 1 < split->count; ++index) {
        const char *segment = split->scratch + split->offset[index];
        const int child = openat(current, segment, O_RDONLY | O_DIRECTORY |
                                                       O_NOFOLLOW | O_CLOEXEC);
        const int error = errno;
        struct stat metadata;
        close(current);
        if (child < 0) {
            return error == ENOENT ? PXA_STATUS_NOT_FOUND
                                   : PXA_STATUS_DENIED;
        }
        if (fstat(child, &metadata) != 0 || !S_ISDIR(metadata.st_mode)) {
            close(child);
            return PXA_STATUS_DENIED;
        }
        current = child;
    }
    *parent = current;
    return PXA_STATUS_OK;
}

static int safe_regular(const struct stat *metadata) {
    return S_ISREG(metadata->st_mode) && metadata->st_nlink == 1 &&
           metadata->st_size >= 0;
}

static pxa_status_t count_usage(int directory, uint64_t *usage,
                                size_t *entries) {
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
        if (strncmp(item->d_name, ".pxa-", 5) == 0) continue;
        if (++*entries > PXA_POSIX_FS_MAX_TREE_ENTRIES ||
            !segment_is_valid(
                (pxa_bytes_t){(const uint8_t *)item->d_name,
                              strlen(item->d_name)})) {
            status = PXA_STATUS_RESOURCE_LIMIT;
            break;
        }
        if (fstatat(directory, item->d_name, &metadata,
                    AT_SYMLINK_NOFOLLOW) != 0 ||
            S_ISLNK(metadata.st_mode)) {
            status = PXA_STATUS_DENIED;
            break;
        }
        if (safe_regular(&metadata)) {
            if ((uint64_t)metadata.st_size > UINT64_MAX - *usage) {
                status = PXA_STATUS_RESOURCE_LIMIT;
                break;
            }
            *usage += (uint64_t)metadata.st_size;
        } else if (S_ISDIR(metadata.st_mode)) {
            int child = openat(directory, item->d_name,
                               O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (child < 0) {
                status = PXA_STATUS_DENIED;
                break;
            }
            status = count_usage(child, usage, entries);
            close(child);
            if (status != PXA_STATUS_OK) break;
        } else {
            status = PXA_STATUS_DENIED;
            break;
        }
    }
    if (closedir(stream) != 0 && status == PXA_STATUS_OK) {
        status = PXA_STATUS_INTERNAL;
    }
    return status;
}

static int config_valid(const pxa_posix_fs_config_t *config) {
    return config != NULL && config->struct_size >= sizeof(*config) &&
           config->root_path != NULL && config->root_path[0] != '\0' &&
           config->max_open_resources != 0;
}

size_t pxa_posix_fs_workspace_size(const pxa_posix_fs_config_t *config) {
    size_t entries_size;
    size_t size;
    if (!config_valid(config)) return 0;
    if (sizeof(pxa_posix_fs_entry_t) >
        SIZE_MAX / (size_t)config->max_open_resources) {
        return 0;
    }
    entries_size = (size_t)config->max_open_resources *
                   sizeof(pxa_posix_fs_entry_t);
    size = PXA_POSIX_FS_ALIGNMENT - 1u;
    if (sizeof(pxa_posix_fs_t) > SIZE_MAX - size) return 0;
    size += sizeof(pxa_posix_fs_t);
    if (entries_size > SIZE_MAX - size) return 0;
    return size + entries_size;
}

static pxa_status_t open_backend(void *context, pxa_bytes_t path,
                                 uint32_t flags, void **resource,
                                 uint8_t *kind) {
    pxa_posix_fs_t *fs = (pxa_posix_fs_t *)context;
    pxa_posix_path_split_t split;
    uint16_t entry_index;
    pxa_posix_fs_entry_t *entry;
    const char *name;
    int parent = -1;
    int flags_posix;
    struct stat prior;
    struct stat opened;
    int exists = 0;
    pxa_status_t status;
    *resource = NULL;
    *kind = 0;
    if (!split_path(path, &split)) return PXA_STATUS_INVALID_ARGUMENT;
    if (fs->open_count >= fs->max_open_resources) {
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    status = open_parent(fs, &split, &parent);
    if (status != PXA_STATUS_OK) return status;
    name = split.scratch + split.offset[split.count - 1];
    if ((flags & PXA_FS_OPEN_DIRECTORY) != 0) {
        const int child = openat(parent, name, O_RDONLY | O_DIRECTORY |
                                                   O_NOFOLLOW | O_CLOEXEC);
        const int error = errno;
        DIR *stream;
        close(parent);
        if (child < 0) {
            return error == ENOENT ? PXA_STATUS_NOT_FOUND
                                   : PXA_STATUS_DENIED;
        }
        if (fstat(child, &opened) != 0 || !S_ISDIR(opened.st_mode)) {
            close(child);
            return PXA_STATUS_DENIED;
        }
        stream = fdopendir(child);
        if (stream == NULL) {
            close(child);
            return PXA_STATUS_INTERNAL;
        }
        for (entry_index = 0; entry_index < fs->max_open_resources;
             ++entry_index) {
            if (!fs->entries[entry_index].occupied) break;
        }
        if (entry_index == fs->max_open_resources) {
            closedir(stream);
            return PXA_STATUS_RESOURCE_LIMIT;
        }
        entry = &fs->entries[entry_index];
        memset(entry, 0, sizeof(*entry));
        entry->stream = stream;
        entry->kind = PXA_FS_KIND_DIRECTORY;
        entry->path_size = (uint16_t)path.size;
        memcpy(entry->path, path.data, path.size);
        entry->occupied = 1;
        ++fs->open_count;
        *resource = (void *)(uintptr_t)(entry_index + 1);
        *kind = PXA_FS_KIND_DIRECTORY;
        return PXA_STATUS_OK;
    }
    if (fstatat(parent, name, &prior, AT_SYMLINK_NOFOLLOW) == 0) {
        exists = 1;
        if (S_ISLNK(prior.st_mode) || !safe_regular(&prior)) {
            close(parent);
            return PXA_STATUS_DENIED;
        }
    } else if (errno != ENOENT) {
        close(parent);
        return PXA_STATUS_DENIED;
    }
    flags_posix = ((flags & PXA_FS_OPEN_READ) != 0 &&
                   (flags & PXA_FS_OPEN_WRITE) != 0)
                      ? O_RDWR
                      : ((flags & PXA_FS_OPEN_WRITE) != 0 ? O_WRONLY
                                                          : O_RDONLY);
    flags_posix |= O_NOFOLLOW | O_CLOEXEC;
    if ((flags & PXA_FS_OPEN_CREATE) != 0) flags_posix |= O_CREAT;
    if ((flags & PXA_FS_OPEN_EXCLUSIVE) != 0) flags_posix |= O_EXCL;
    if ((flags & PXA_FS_OPEN_APPEND) != 0) flags_posix |= O_APPEND;
    if ((flags & PXA_FS_OPEN_TRUNCATE) != 0) flags_posix |= O_TRUNC;
    {
        const int file = openat(parent, name, flags_posix, 0600);
        const int error = errno;
        close(parent);
        if (file < 0) return status_from_errno(error);
        if (fstat(file, &opened) != 0 || !safe_regular(&opened)) {
            close(file);
            return PXA_STATUS_DENIED;
        }
        if ((flags & PXA_FS_OPEN_TRUNCATE) != 0 && exists) {
            fs->usage -= (uint64_t)prior.st_size;
        }
        for (entry_index = 0; entry_index < fs->max_open_resources;
             ++entry_index) {
            if (!fs->entries[entry_index].occupied) break;
        }
        if (entry_index == fs->max_open_resources) {
            close(file);
            return PXA_STATUS_RESOURCE_LIMIT;
        }
        entry = &fs->entries[entry_index];
        memset(entry, 0, sizeof(*entry));
        entry->fd = file;
        entry->kind = PXA_FS_KIND_REGULAR;
        entry->path_size = (uint16_t)path.size;
        memcpy(entry->path, path.data, path.size);
        entry->occupied = 1;
        ++fs->open_count;
        *resource = (void *)(uintptr_t)(entry_index + 1);
        *kind = PXA_FS_KIND_REGULAR;
        return PXA_STATUS_OK;
    }
}

static pxa_posix_fs_entry_t *lookup(pxa_posix_fs_t *fs, void *resource) {
    uintptr_t value = (uintptr_t)resource;
    uint16_t index;
    if (value == 0 || value > fs->max_open_resources) return NULL;
    index = (uint16_t)(value - 1);
    return fs->entries[index].occupied ? &fs->entries[index] : NULL;
}

static pxa_status_t read_backend(void *context, void *resource,
                                 uint8_t *output, size_t capacity,
                                 size_t *size) {
    pxa_posix_fs_t *fs = (pxa_posix_fs_t *)context;
    pxa_posix_fs_entry_t *entry;
    ssize_t count;
    *size = 0;
    if (output == NULL && capacity != 0) return PXA_STATUS_INVALID_ARGUMENT;
    entry = lookup(fs, resource);
    if (entry == NULL || entry->kind != PXA_FS_KIND_REGULAR) {
        return PXA_STATUS_NOT_FOUND;
    }
    do {
        count = read(entry->fd, output, capacity);
    } while (count < 0 && errno == EINTR);
    if (count < 0) return status_from_errno(errno);
    *size = (size_t)count;
    return PXA_STATUS_OK;
}

static pxa_status_t write_backend(void *context, void *resource,
                                  const uint8_t *input, size_t size,
                                  size_t *written) {
    pxa_posix_fs_t *fs = (pxa_posix_fs_t *)context;
    pxa_posix_fs_entry_t *entry;
    struct stat before;
    struct stat after;
    off_t position;
    uint64_t end;
    uint64_t old_size;
    uint64_t new_size;
    ssize_t count;
    *written = 0;
    if (input == NULL && size != 0) return PXA_STATUS_INVALID_ARGUMENT;
    entry = lookup(fs, resource);
    if (entry == NULL || entry->kind != PXA_FS_KIND_REGULAR) {
        return PXA_STATUS_NOT_FOUND;
    }
    if (fstat(entry->fd, &before) != 0 || !safe_regular(&before)) {
        return PXA_STATUS_DENIED;
    }
    position = lseek(entry->fd, 0, SEEK_CUR);
    if (position < 0) return PXA_STATUS_INTERNAL;
    {
        const int append = (fcntl(entry->fd, F_GETFL) & O_APPEND) != 0;
        const uint64_t write_position =
            append ? (uint64_t)before.st_size : (uint64_t)position;
        if (write_position > UINT64_MAX - size) {
            return PXA_STATUS_QUOTA_EXCEEDED;
        }
        end = write_position + size;
    }
    old_size = (uint64_t)before.st_size;
    if (end > old_size && end - old_size > fs->quota - fs->usage) {
        return PXA_STATUS_QUOTA_EXCEEDED;
    }
    do {
        count = write(entry->fd, input, size);
    } while (count < 0 && errno == EINTR);
    if (count < 0) return status_from_errno(errno);
    if (fstat(entry->fd, &after) != 0 || !safe_regular(&after)) {
        return PXA_STATUS_INTERNAL;
    }
    new_size = (uint64_t)after.st_size;
    if (new_size >= old_size) fs->usage += new_size - old_size;
    else fs->usage -= old_size - new_size;
    *written = (size_t)count;
    return PXA_STATUS_OK;
}

static pxa_status_t seek_backend(void *context, void *resource,
                                 int64_t offset, uint8_t origin,
                                 uint64_t *position) {
    pxa_posix_fs_t *fs = (pxa_posix_fs_t *)context;
    pxa_posix_fs_entry_t *entry;
    int whence;
    off_t result;
    *position = 0;
    switch (origin) {
        case PXA_FS_SEEK_START: whence = SEEK_SET; break;
        case PXA_FS_SEEK_CURRENT: whence = SEEK_CUR; break;
        case PXA_FS_SEEK_END: whence = SEEK_END; break;
        default: return PXA_STATUS_INVALID_ARGUMENT;
    }
    entry = lookup(fs, resource);
    if (entry == NULL || entry->kind != PXA_FS_KIND_REGULAR) {
        return PXA_STATUS_NOT_FOUND;
    }
    result = lseek(entry->fd, offset, whence);
    if (result < 0) {
        return errno == EINVAL ? PXA_STATUS_INVALID_ARGUMENT
                               : PXA_STATUS_INTERNAL;
    }
    *position = (uint64_t)result;
    return PXA_STATUS_OK;
}

static pxa_status_t read_directory_backend(void* context, void* resource, pxa_fs_entry_t* entry,
                                           uint8_t* end) {
    pxa_posix_fs_t* fs = (pxa_posix_fs_t*)context;
    pxa_posix_fs_entry_t* dir;
    struct dirent* item;
    struct stat metadata;
#ifndef ESP_PLATFORM
    int directory_fd;
#endif
    *end = 0;
    memset(entry, 0, sizeof(*entry));
    dir = lookup(fs, resource);
    if (dir == NULL || dir->kind != PXA_FS_KIND_DIRECTORY) {
        return PXA_STATUS_NOT_FOUND;
    }
    for (;;) {
        errno = 0;
        item = readdir(dir->stream);
        if (item == NULL) {
            if (errno != 0)
                return PXA_STATUS_INTERNAL;
            *end = 1;
            return PXA_STATUS_OK;
        }
        if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0 ||
            strncmp(item->d_name, ".pxa-", 5) == 0) {
            continue;
        }
        break;
    }
    if (!segment_is_valid((pxa_bytes_t){(const uint8_t*)item->d_name, strlen(item->d_name)})) {
        return PXA_STATUS_DENIED;
    }
#ifdef ESP_PLATFORM
    {
        char child_path[PXA_FS_MAX_PATH_BYTES + 1];
        /* ESP VFS directory streams do not expose a usable dirfd(). LittleFS
         * has no links, so resolve the saved sandbox-relative directory path
         * from the already-open private root instead. */
        const int length = snprintf(child_path, sizeof(child_path), "%.*s/%s",
                                    (int)dir->path_size, dir->path,
                                    item->d_name);
        if (length < 0 || (size_t)length >= sizeof(child_path) ||
            fstatat(fs->root_fd, child_path, &metadata,
                    AT_SYMLINK_NOFOLLOW) != 0) {
            return PXA_STATUS_DENIED;
        }
    }
#else
    directory_fd = dirfd(dir->stream);
    if (directory_fd < 0) return PXA_STATUS_INTERNAL;
    if (fstatat(directory_fd, item->d_name, &metadata,
                AT_SYMLINK_NOFOLLOW) != 0) {
        return PXA_STATUS_DENIED;
    }
#endif
    if (S_ISLNK(metadata.st_mode)) {
        return PXA_STATUS_DENIED;
    }
    if (safe_regular(&metadata)) {
        entry->name.data = (const uint8_t*)item->d_name;
        entry->name.size = strlen(item->d_name);
        entry->kind = PXA_FS_KIND_REGULAR;
        entry->size = (uint64_t)metadata.st_size;
        return PXA_STATUS_OK;
    }
    if (S_ISDIR(metadata.st_mode)) {
        entry->name.data = (const uint8_t*)item->d_name;
        entry->name.size = strlen(item->d_name);
        entry->kind = PXA_FS_KIND_DIRECTORY;
        return PXA_STATUS_OK;
    }
    return PXA_STATUS_DENIED;
}

static void close_backend(void *context, void *resource, uint8_t kind) {
    pxa_posix_fs_t *fs = (pxa_posix_fs_t *)context;
    pxa_posix_fs_entry_t *entry;
    uintptr_t value = (uintptr_t)resource;
    uint16_t index;
    (void)kind;
    if (value == 0 || value > fs->max_open_resources) return;
    index = (uint16_t)(value - 1);
    entry = &fs->entries[index];
    if (!entry->occupied) return;
    if (entry->kind == PXA_FS_KIND_REGULAR && entry->fd >= 0) close(entry->fd);
    if (entry->kind == PXA_FS_KIND_DIRECTORY && entry->stream != NULL) {
        closedir(entry->stream);
    }
    memset(entry, 0, sizeof(*entry));
    --fs->open_count;
}

static pxa_status_t make_directory_backend(void *context, pxa_bytes_t path) {
    pxa_posix_fs_t *fs = (pxa_posix_fs_t *)context;
    pxa_posix_path_split_t split;
    const char *name;
    int parent = -1;
    int result;
    pxa_status_t status;
    if (!split_path(path, &split)) return PXA_STATUS_INVALID_ARGUMENT;
    status = open_parent(fs, &split, &parent);
    if (status != PXA_STATUS_OK) return status;
    name = split.scratch + split.offset[split.count - 1];
    result = mkdirat(parent, name, 0700);
    {
        const int error = errno;
        close(parent);
        return result == 0 ? PXA_STATUS_OK : status_from_errno(error);
    }
}

static pxa_status_t remove_backend(void *context, pxa_bytes_t path) {
    pxa_posix_fs_t *fs = (pxa_posix_fs_t *)context;
    pxa_posix_path_split_t split;
    const char *name;
    int parent = -1;
    struct stat metadata;
    int flags = 0;
    uint64_t released = 0;
    int result;
    pxa_status_t status;
    if (!split_path(path, &split)) return PXA_STATUS_INVALID_ARGUMENT;
    status = open_parent(fs, &split, &parent);
    if (status != PXA_STATUS_OK) return status;
    name = split.scratch + split.offset[split.count - 1];
    if (fstatat(parent, name, &metadata, AT_SYMLINK_NOFOLLOW) == 0) {
        if (S_ISLNK(metadata.st_mode)) {
            close(parent);
            return PXA_STATUS_DENIED;
        }
    } else if (errno == ENOENT) {
        close(parent);
        return PXA_STATUS_NOT_FOUND;
    } else {
        close(parent);
        return PXA_STATUS_DENIED;
    }
    if (safe_regular(&metadata)) {
        if (entry_is_open_regular(fs, (const char *)path.data, path.size)) {
            close(parent);
            return PXA_STATUS_BUSY;
        }
        released = (uint64_t)metadata.st_size;
    } else if (S_ISDIR(metadata.st_mode)) {
        flags = AT_REMOVEDIR;
    } else {
        close(parent);
        return PXA_STATUS_DENIED;
    }
    result = unlinkat(parent, name, flags);
    {
        const int error = errno;
        close(parent);
        if (result != 0) return status_from_errno(error);
        if (released != 0) fs->usage -= released;
        return PXA_STATUS_OK;
    }
}

static pxa_status_t rename_backend(void *context, pxa_bytes_t source,
                                   pxa_bytes_t destination) {
    pxa_posix_fs_t *fs = (pxa_posix_fs_t *)context;
    pxa_posix_path_split_t source_split;
    pxa_posix_path_split_t destination_split;
    int source_parent = -1;
    int destination_parent = -1;
    struct stat source_metadata;
    int source_exists = 0;
    int destination_exists = 0;
    int result;
    pxa_status_t status;
    if (source.size == destination.size &&
        memcmp(source.data, destination.data, source.size) == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (!split_path(source, &source_split) ||
        !split_path(destination, &destination_split)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    status = open_parent(fs, &source_split, &source_parent);
    if (status == PXA_STATUS_OK) {
        status = open_parent(fs, &destination_split, &destination_parent);
    }
    if (status != PXA_STATUS_OK) {
        if (source_parent >= 0) close(source_parent);
        if (destination_parent >= 0) close(destination_parent);
        return status;
    }
    if (fstatat(source_parent,
                source_split.scratch + source_split.offset[source_split.count - 1],
                &source_metadata, AT_SYMLINK_NOFOLLOW) == 0) {
        source_exists = 1;
        if (S_ISLNK(source_metadata.st_mode)) {
            close(source_parent);
            close(destination_parent);
            return PXA_STATUS_DENIED;
        }
    } else if (errno != ENOENT) {
        close(source_parent);
        close(destination_parent);
        return PXA_STATUS_DENIED;
    }
    if (!source_exists) {
        close(source_parent);
        close(destination_parent);
        return PXA_STATUS_NOT_FOUND;
    }
    {
        struct stat destination_metadata;
        if (fstatat(destination_parent,
                    destination_split.scratch +
                        destination_split.offset[destination_split.count - 1],
                    &destination_metadata,
                    AT_SYMLINK_NOFOLLOW) == 0) {
            destination_exists = 1;
        } else if (errno != ENOENT) {
            close(source_parent);
            close(destination_parent);
            return PXA_STATUS_DENIED;
        }
    }
    if (destination_exists ||
        (!safe_regular(&source_metadata) &&
         !S_ISDIR(source_metadata.st_mode))) {
        close(source_parent);
        close(destination_parent);
        return destination_exists ? PXA_STATUS_BUSY : PXA_STATUS_DENIED;
    }
    if (S_ISREG(source_metadata.st_mode) &&
        entry_is_open_regular(fs, (const char *)source.data, source.size)) {
        close(source_parent);
        close(destination_parent);
        return PXA_STATUS_BUSY;
    }
    result = renameat(
        source_parent,
        source_split.scratch + source_split.offset[source_split.count - 1],
        destination_parent,
        destination_split.scratch +
            destination_split.offset[destination_split.count - 1]);
    {
        const int error = errno;
        close(source_parent);
        close(destination_parent);
        return result == 0 ? PXA_STATUS_OK : status_from_errno(error);
    }
}

static pxa_status_t stat_backend(void *context, pxa_bytes_t path,
                                 pxa_fs_entry_t *entry) {
    pxa_posix_fs_t *fs = (pxa_posix_fs_t *)context;
    pxa_posix_path_split_t split;
    const char *name;
    int parent = -1;
    struct stat metadata;
    pxa_status_t status;
    memset(entry, 0, sizeof(*entry));
    if (!split_path(path, &split)) return PXA_STATUS_INVALID_ARGUMENT;
    status = open_parent(fs, &split, &parent);
    if (status != PXA_STATUS_OK) return status;
    name = split.scratch + split.offset[split.count - 1];
    if (fstatat(parent, name, &metadata, AT_SYMLINK_NOFOLLOW) != 0) {
        const int error = errno;
        close(parent);
        return error == ENOENT ? PXA_STATUS_NOT_FOUND : PXA_STATUS_DENIED;
    }
    close(parent);
    if (S_ISLNK(metadata.st_mode)) return PXA_STATUS_DENIED;
    if (safe_regular(&metadata)) {
        entry->name = path;
        entry->kind = PXA_FS_KIND_REGULAR;
        entry->size = (uint64_t)metadata.st_size;
        return PXA_STATUS_OK;
    }
    if (S_ISDIR(metadata.st_mode)) {
        entry->name = path;
        entry->kind = PXA_FS_KIND_DIRECTORY;
        return PXA_STATUS_OK;
    }
    return PXA_STATUS_DENIED;
}

pxa_status_t pxa_posix_fs_init(void *workspace, size_t workspace_size,
                               const pxa_posix_fs_config_t *config,
                               pxa_posix_fs_t **output,
                               pxa_fs_backend_t *backend_output) {
    pxa_posix_fs_t *fs;
    pxa_fs_backend_t *backend;
    size_t required;
    uint64_t usage = 0;
    size_t entries = 0;
    struct stat metadata;
    pxa_status_t status;
    if (output == NULL || backend_output == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *output = NULL;
    memset(backend_output, 0, sizeof(*backend_output));
    required = pxa_posix_fs_workspace_size(config);
    if (workspace == NULL || required == 0 || workspace_size < required) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    fs = (pxa_posix_fs_t *)align_up((uintptr_t)workspace,
                                    PXA_POSIX_FS_ALIGNMENT);
    if ((uintptr_t)fs + sizeof(*fs) +
            (size_t)config->max_open_resources *
                sizeof(pxa_posix_fs_entry_t) >
        (uintptr_t)workspace + workspace_size) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    memset(fs, 0,
           sizeof(*fs) +
               (size_t)config->max_open_resources *
                   sizeof(pxa_posix_fs_entry_t));
    fs->magic = PXA_POSIX_FS_MAGIC;
    fs->quota = config->quota_bytes;
    fs->max_open_resources = config->max_open_resources;
    fs->root_fd = -1;
    if (mkdir(config->root_path, 0700) != 0 && errno != EEXIST) {
        return status_from_errno(errno);
    }
    fs->root_fd = open(config->root_path,
                       O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fs->root_fd < 0 ||
        fstat(fs->root_fd, &metadata) != 0 || !S_ISDIR(metadata.st_mode)) {
        if (fs->root_fd >= 0) close(fs->root_fd);
        fs->root_fd = -1;
        return PXA_STATUS_DENIED;
    }
    status = count_usage(fs->root_fd, &usage, &entries);
    if (status != PXA_STATUS_OK || usage > fs->quota) {
        close(fs->root_fd);
        fs->root_fd = -1;
        return status == PXA_STATUS_OK ? PXA_STATUS_QUOTA_EXCEEDED : status;
    }
    fs->usage = usage;
    backend = backend_output;
    backend->struct_size = sizeof(*backend);
    backend->context = fs;
    backend->open = open_backend;
    backend->make_directory = make_directory_backend;
    backend->remove = remove_backend;
    backend->rename = rename_backend;
    backend->stat = stat_backend;
    backend->read = read_backend;
    backend->write = write_backend;
    backend->seek = seek_backend;
    backend->read_directory = read_directory_backend;
    backend->close = close_backend;
    *output = fs;
    return PXA_STATUS_OK;
}

void pxa_posix_fs_deinit(pxa_posix_fs_t *fs) {
    uint16_t index;
    if (fs == NULL || fs->magic != PXA_POSIX_FS_MAGIC) return;
    for (index = 0; index < fs->max_open_resources; ++index) {
        pxa_posix_fs_entry_t *entry = &fs->entries[index];
        if (!entry->occupied) continue;
        if (entry->kind == PXA_FS_KIND_REGULAR && entry->fd >= 0) {
            close(entry->fd);
        }
        if (entry->kind == PXA_FS_KIND_DIRECTORY && entry->stream != NULL) {
            closedir(entry->stream);
        }
        memset(entry, 0, sizeof(*entry));
    }
    if (fs->root_fd >= 0) close(fs->root_fd);
    fs->root_fd = -1;
    fs->magic = 0;
}

static pxa_status_t remove_tree_contents(int directory, size_t *entries) {
    DIR *stream;
    struct dirent *item;
    pxa_status_t status = PXA_STATUS_OK;
    int duplicate = openat(directory, ".", O_RDONLY | O_DIRECTORY |
                                              O_NOFOLLOW | O_CLOEXEC);
    if (duplicate < 0) return PXA_STATUS_INTERNAL;
    stream = fdopendir(duplicate);
    if (stream == NULL) {
        close(duplicate);
        return PXA_STATUS_INTERNAL;
    }
    while ((item = readdir(stream)) != NULL) {
        struct stat metadata;
        if (strcmp(item->d_name, ".") == 0 ||
            strcmp(item->d_name, "..") == 0) {
            continue;
        }
        if (++*entries > PXA_POSIX_FS_MAX_TREE_ENTRIES) {
            status = PXA_STATUS_RESOURCE_LIMIT;
            break;
        }
        if (fstatat(directory, item->d_name, &metadata,
                    AT_SYMLINK_NOFOLLOW) != 0) {
            status = PXA_STATUS_INTERNAL;
            break;
        }
        if (S_ISDIR(metadata.st_mode)) {
            int child = openat(directory, item->d_name,
                               O_RDONLY | O_DIRECTORY | O_NOFOLLOW |
                                   O_CLOEXEC);
            if (child < 0) {
                status = PXA_STATUS_DENIED;
                break;
            }
            status = remove_tree_contents(child, entries);
            close(child);
            if (status != PXA_STATUS_OK) break;
            if (unlinkat(directory, item->d_name, AT_REMOVEDIR) != 0) {
                status = status_from_errno(errno);
                break;
            }
        } else if (unlinkat(directory, item->d_name, 0) != 0) {
            status = status_from_errno(errno);
            break;
        }
    }
    if (closedir(stream) != 0 && status == PXA_STATUS_OK) {
        status = PXA_STATUS_INTERNAL;
    }
    return status;
}

pxa_status_t pxa_posix_fs_remove_tree(const char *root_path) {
    int directory;
    size_t entries = 0;
    pxa_status_t status;
    if (root_path == NULL || root_path[0] == '\0') {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    directory = open(root_path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW |
                                    O_CLOEXEC);
    if (directory < 0) {
        return errno == ENOENT ? PXA_STATUS_NOT_FOUND : PXA_STATUS_DENIED;
    }
    status = remove_tree_contents(directory, &entries);
    close(directory);
    if (status != PXA_STATUS_OK) return status;
    if (rmdir(root_path) != 0) return status_from_errno(errno);
    return PXA_STATUS_OK;
}
