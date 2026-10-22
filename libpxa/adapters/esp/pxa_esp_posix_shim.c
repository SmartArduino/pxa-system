/* ESP-IDF link-free filesystem adapter. Directory handles are owned by the
 * Host and work without LittleFS-specific fcntl support, including on FAT. */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#if defined(ESP_PLATFORM)
#include "sdkconfig.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pxa/esp/pxa_esp_posix_shim.h"
#include "esp_heap_caps.h"
#if !defined(__linux__) && !defined(__APPLE__)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

void pxa_esp_posix_io_checkpoint(uint32_t *last_yield_tick) {
#if !defined(__linux__) && !defined(__APPLE__)
    const uint32_t now = (uint32_t)xTaskGetTickCount();
    const uint32_t interval = (uint32_t)pdMS_TO_TICKS(8);
    if ((uint32_t)(now - *last_yield_tick) >= (interval ? interval : 1u)) {
        vTaskDelay(1);
        *last_yield_tick = (uint32_t)xTaskGetTickCount();
    }
#else
    (void)last_yield_tick;
#endif
}

int pxa_esp_wasi_null_fd(void) {
    /* IDF 6.2 nullfs close uses SET_FLAGS(..., 0), whose OR leaves the
     * descriptor's occupied bits set. Reopening per Component exhausts its
     * 16 slots. Own one sink for the process instead; WAMR's stdio entries
     * borrow it, including when a Guest closes its local stdio handle.
     * Calls are serialized by the PXA Host. Retry a failed initial open. */
    static int descriptor = -1;
    if (descriptor < 0) descriptor = open("/dev/null", O_RDWR);
    return descriptor;
}

#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif

#ifndef AT_REMOVEDIR
#define AT_REMOVEDIR 0x200
#endif

#ifndef PXA_ESP_POSIX_PATH_MAX
#define PXA_ESP_POSIX_PATH_MAX 512
#endif

#ifndef PXA_ESP_POSIX_DIRECTORY_HANDLES
#define PXA_ESP_POSIX_DIRECTORY_HANDLES 16u
#endif
#define DIRECTORY_FD_BASE 0x40000000
#define DIRECTORY_FD_SPAN (65536 * 32)
typedef char directory_handle_capacity_must_fit[
    PXA_ESP_POSIX_DIRECTORY_HANDLES > 0 && PXA_ESP_POSIX_DIRECTORY_HANDLES <= 32 ? 1 : -1];
typedef struct {
    char *path;
    uint64_t epoch;
    uint16_t generation;
    uint8_t invalid;
} directory_handle_t;
static directory_handle_t g_directories[PXA_ESP_POSIX_DIRECTORY_HANDLES];
static pthread_mutex_t g_directory_lock = PTHREAD_MUTEX_INITIALIZER;
static pxa_esp_posix_directory_hooks_t g_directory_hooks;

static void *directory_allocate(size_t bytes) {
    void *memory = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return memory ? memory : heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
}

static int synthetic_directory(int fd) {
    return fd >= DIRECTORY_FD_BASE && fd < DIRECTORY_FD_BASE + DIRECTORY_FD_SPAN;
}

static directory_handle_t *directory_slot(int fd) {
    unsigned value = (unsigned)(fd - DIRECTORY_FD_BASE), slot = value & 31u;
    if (slot >= PXA_ESP_POSIX_DIRECTORY_HANDLES || !g_directories[slot].path ||
        g_directories[slot].generation != value >> 5) return NULL;
    return &g_directories[slot];
}

static uint64_t path_epoch(const char *path) {
    return g_directory_hooks.mount_epoch
        ? g_directory_hooks.mount_epoch(g_directory_hooks.context, path) : 0;
}

/* FAT has no inode-backed directory descriptors. Keep canonical paths so
 * renames can update every borrowed directory, and never resolve a parent
 * component through a different volume. Caller paths are Host-owned. */
static int canonical_path(const char *path, char *output, size_t capacity) {
    size_t used = 0;
    const char *cursor;
    if (!path || path[0] != '/' || capacity < 2) { errno = EINVAL; return -1; }
    output[used++] = '/';
    cursor = path + 1;
    while (*cursor) {
        const char *start;
        size_t length;
        while (*cursor == '/') ++cursor;
        start = cursor;
        while (*cursor && *cursor != '/') ++cursor;
        length = (size_t)(cursor - start);
        if (!length || (length == 1 && start[0] == '.')) continue;
        if (length == 2 && start[0] == '.' && start[1] == '.') { errno = EINVAL; return -1; }
        if (used + (used > 1) + length >= capacity) { errno = ENAMETOOLONG; return -1; }
        if (used > 1) output[used++] = '/';
        memmove(output + used, start, length); used += length;
    }
    output[used] = 0;
    return 0;
}

static int path_contains(const char *parent, const char *path) {
    size_t length = strlen(parent);
    return strncmp(parent, path, length) == 0 &&
        (length == 1 || path[length] == 0 || path[length] == '/');
}

int pxa_esp_posix_set_directory_hooks(const pxa_esp_posix_directory_hooks_t *hooks) {
    unsigned i;
    pxa_esp_posix_directory_hooks_t next = {0};
    if (hooks) next = *hooks;
    pthread_mutex_lock(&g_directory_lock);
    for (i = 0; i < PXA_ESP_POSIX_DIRECTORY_HANDLES; ++i) {
        if (g_directories[i].path) {
            pthread_mutex_unlock(&g_directory_lock); errno = EBUSY; return -1;
        }
    }
    g_directory_hooks = next;
    pthread_mutex_unlock(&g_directory_lock);
    return 0;
}

static int directory_path(int fd, char *path, size_t capacity, uint64_t *epoch) {
    directory_handle_t *handle;
    size_t size;
    uint64_t current;
    pthread_mutex_lock(&g_directory_lock);
    handle = directory_slot(fd);
    if (!handle || handle->invalid) {
        pthread_mutex_unlock(&g_directory_lock); errno = EBADF; return -1;
    }
    size = strlen(handle->path);
    if (size >= capacity) {
        pthread_mutex_unlock(&g_directory_lock); errno = ENAMETOOLONG; return -1;
    }
    memcpy(path, handle->path, size + 1); *epoch = handle->epoch;
    pthread_mutex_unlock(&g_directory_lock);
    current = path_epoch(path);
    if (current == UINT64_MAX || current != *epoch) { errno = EIO; return -1; }
    return 0;
}

int pxa_esp_open(const char *path, int flags, ...) {
    mode_t mode = 0;
    char *owned;
    uint64_t epoch;
    unsigned i;
    DIR *check;
    size_t length;
    char normalized[PXA_ESP_POSIX_PATH_MAX];
    if ((flags & O_CREAT) != 0) {
        va_list arguments; va_start(arguments, flags);
        mode = (mode_t)va_arg(arguments, int); va_end(arguments);
    }
    if (!(flags & O_DIRECTORY)) return (flags & O_CREAT) ? open(path, flags, mode) : open(path, flags);
    if (!path || path[0] != '/' || (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC))) {
        errno = EINVAL; return -1;
    }
    if (canonical_path(path, normalized, sizeof(normalized)) != 0) return -1;
    path = normalized;
    length = strlen(path);
    epoch = path_epoch(path);
    if (epoch == UINT64_MAX) { errno = EIO; return -1; }
#if defined(__linux__) || defined(__APPLE__)
    /* Host tests can contain links even though the deployed FAT/LittleFS
     * backends are link-free. Preserve the caller's no-follow contract. */
    if(flags&O_NOFOLLOW) {
        struct stat metadata;
        if(lstat(path,&metadata)!=0)return -1;
        if(S_ISLNK(metadata.st_mode)) {errno=ELOOP;return -1;}
    }
#endif
    check = opendir(path);
    if (!check) return -1;
    if (closedir(check) != 0) return -1;
    if (epoch != path_epoch(path)) { errno = EIO; return -1; }
    owned = directory_allocate(length + 1);
    if (!owned) { errno = ENOMEM; return -1; }
    memcpy(owned, path, length + 1);
    while (length > 1 && owned[length - 1] == '/') owned[--length] = 0;
    pthread_mutex_lock(&g_directory_lock);
    for (i = 0; i < PXA_ESP_POSIX_DIRECTORY_HANDLES; ++i) {
        directory_handle_t *handle = &g_directories[i];
        if (handle->path) continue;
        if (!++handle->generation) ++handle->generation;
        handle->path = owned; handle->epoch = epoch; handle->invalid = 0;
        int fd = DIRECTORY_FD_BASE + ((int)handle->generation << 5) + (int)i;
        pthread_mutex_unlock(&g_directory_lock);
        return fd;
    }
    pthread_mutex_unlock(&g_directory_lock);
    heap_caps_free(owned); errno = EMFILE; return -1;
}

static __attribute__((noinline)) int rename_canonical(const char *source,const char *target) {
    char *updated[PXA_ESP_POSIX_DIRECTORY_HANDLES] = {0};
    uint64_t source_epoch, target_epoch;
    size_t source_size, target_size;
    unsigned i;
    int result = -1, saved_errno;
    source_epoch = path_epoch(source); target_epoch = path_epoch(target);
    if (source_epoch == UINT64_MAX || target_epoch == UINT64_MAX) { errno = EIO; return -1; }
    if (source_epoch != target_epoch) { errno = EXDEV; return -1; }
    source_size = strlen(source); target_size = strlen(target);
    pthread_mutex_lock(&g_directory_lock);
    for (i = 0; i < PXA_ESP_POSIX_DIRECTORY_HANDLES; ++i) {
        directory_handle_t *handle = &g_directories[i];
        size_t length;
        if (!handle->path || handle->invalid || handle->epoch != source_epoch) continue;
        if (!path_contains(source, handle->path)) {
            /* A pathname-backed reference must not silently start pointing
             * at a replacement directory. Retry after its owner closes it. */
            if (strcmp(source, target) && path_contains(target, handle->path)) { errno = EBUSY; goto done; }
            continue;
        }
        length = target_size + strlen(handle->path) - source_size;
        if (length >= PXA_ESP_POSIX_PATH_MAX) { errno = ENAMETOOLONG; goto done; }
        updated[i] = directory_allocate(length + 1);
        if (!updated[i]) { errno = ENOMEM; goto done; }
        memcpy(updated[i], target, target_size);
        strcpy(updated[i] + target_size, handle->path + source_size);
    }
    result = rename(source, target);
    if (result == 0) {
        for (i = 0; i < PXA_ESP_POSIX_DIRECTORY_HANDLES; ++i) {
            if (!updated[i]) continue;
            heap_caps_free(g_directories[i].path);
            g_directories[i].path = updated[i]; updated[i] = NULL;
        }
    }
done:
    saved_errno = errno;
    for (i = 0; i < PXA_ESP_POSIX_DIRECTORY_HANDLES; ++i) heap_caps_free(updated[i]);
    pthread_mutex_unlock(&g_directory_lock);
    errno = saved_errno;
    return result;
}

int pxa_esp_rename(const char *oldpath,const char *newpath) {
    char source[PXA_ESP_POSIX_PATH_MAX],target[PXA_ESP_POSIX_PATH_MAX];
    if(canonical_path(oldpath,source,sizeof(source)) || canonical_path(newpath,target,sizeof(target)))return -1;
    return rename_canonical(source,target);
}

int pxa_esp_rmdir(const char *path) {
    char normalized[PXA_ESP_POSIX_PATH_MAX];
    unsigned i;
    int result;
    if (canonical_path(path, normalized, sizeof(normalized)) != 0) return -1;
    if (path_epoch(normalized) == UINT64_MAX) { errno = EIO; return -1; }
    pthread_mutex_lock(&g_directory_lock);
    result = rmdir(normalized);
    if (result == 0) {
        for (i = 0; i < PXA_ESP_POSIX_DIRECTORY_HANDLES; ++i) {
            if (g_directories[i].path && path_contains(normalized, g_directories[i].path))
                g_directories[i].invalid = 1;
        }
    }
    pthread_mutex_unlock(&g_directory_lock);
    return result;
}

int pxa_esp_close(int fd) {
    directory_handle_t *handle;
    char *path;
    if (!synthetic_directory(fd)) return close(fd);
    pthread_mutex_lock(&g_directory_lock);
    handle = directory_slot(fd);
    if (!handle) { pthread_mutex_unlock(&g_directory_lock); errno = EBADF; return -1; }
    path = handle->path; handle->path = NULL;
    pthread_mutex_unlock(&g_directory_lock);
    heap_caps_free(path);
    return 0;
}

#if CONFIG_LITTLEFS_FCNTL_GET_PATH
#ifndef F_GETPATH
#define F_GETPATH CONFIG_LITTLEFS_FCNTL_F_GETPATH_VALUE
#endif
#endif

static int resolve_path(int dirfd, const char *path, char *output,
                        size_t capacity) {
    char base[PXA_ESP_POSIX_PATH_MAX];
    uint64_t epoch = 0;
    int result;
    int scoped = 0;

    if (path == NULL || output == NULL || capacity == 0) {
        errno = EINVAL;
        return -1;
    }
    if (path[0] == '/') {
        if (canonical_path(path, output, capacity) != 0) return -1;
        if (path_epoch(output) == UINT64_MAX) { errno = EIO; return -1; }
        return 0;
    } else if (dirfd == AT_FDCWD) {
        result = snprintf(output, capacity, "%s", path);
    } else {
        size_t base_length;
        const char *component = path;
        /* Refuse parent components even when they resolve back into this
         * volume: path-based VFS adapters cannot prove mount containment. */
        while (*component) {
            const char *start;
            while (*component == '/') ++component;
            start = component;
            while (*component && *component != '/') ++component;
            if (component - start == 2 && start[0] == '.' && start[1] == '.') {
                errno = EINVAL; return -1;
            }
        }
        if (synthetic_directory(dirfd)) {
            if (directory_path(dirfd, base, sizeof(base), &epoch) != 0) return -1;
        } else {
#if CONFIG_LITTLEFS_FCNTL_GET_PATH
            if (fcntl(dirfd, F_GETPATH, base) != 0) return -1;
            epoch = path_epoch(base);
#else
            errno = ENOSYS;
            return -1;
#endif
        }
        base_length = strlen(base);
        if (strcmp(path, ".") == 0 || path[0] == '\0') {
            result = snprintf(output, capacity, "%s", base);
        } else if (base_length != 0 && base[base_length - 1] == '/') {
            result = snprintf(output, capacity, "%s%s", base, path);
        } else {
            result = snprintf(output, capacity, "%s/%s", base, path);
        }
        scoped = 1;
    }
    if (result < 0 || (size_t)result >= capacity) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (scoped) {
        if (canonical_path(output, output, capacity) != 0) return -1;
        if (path_epoch(output) != epoch) { errno = EXDEV; return -1; }
    }
    return 0;
}

DIR *pxa_esp_opendirat(int fd, const char *path) {
    char resolved[PXA_ESP_POSIX_PATH_MAX];
    if (path == NULL || *path == '/') { errno = EINVAL; return NULL; }
    if (resolve_path(fd, path, resolved, sizeof(resolved)) != 0) return NULL;
    const uint64_t epoch = path_epoch(resolved);
    if (epoch == UINT64_MAX) { errno = EIO; return NULL; }
    DIR *stream = opendir(resolved);
    if (stream != NULL && path_epoch(resolved) != epoch) {
        closedir(stream); errno = EIO; return NULL;
    }
    return stream;
}

int flock(int fd, int operation) {
    (void)fd;
    (void)operation;
    return 0;
}

int pxa_esp_dup(int fd) {
    char path[PXA_ESP_POSIX_PATH_MAX];
    struct stat metadata;
    if (pxa_esp_fstat(fd, &metadata) != 0) return -1;
    if (!S_ISDIR(metadata.st_mode)) {
        errno = ENOTSUP;
        return -1;
    }
    if (resolve_path(fd, ".", path, sizeof(path)) != 0) return -1;
    /* PXA duplicates only directory handles. LittleFS does not implement
     * F_DUPFD, so reopen the resolved directory as an independent handle. */
    return pxa_esp_open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
}

int pxa_esp_openat(int dirfd, const char *path, int flags, ...) {
    char full_path[PXA_ESP_POSIX_PATH_MAX];
    mode_t mode = 0;
    int result;
    if (resolve_path(dirfd, path, full_path, sizeof(full_path)) != 0) {
        return -1;
    }
    if ((flags & O_CREAT) != 0) {
        va_list arguments;
        va_start(arguments, flags);
        mode = (mode_t)va_arg(arguments, int);
        va_end(arguments);
        result = pxa_esp_open(full_path, flags, mode);
    } else {
        result = pxa_esp_open(full_path, flags);
    }
    return result;
}

static void normalize_metadata(struct stat *metadata) {
    if (S_ISREG(metadata->st_mode)) {
        /* LittleFS has no hard links but leaves st_nlink zero-initialized.
         * Present the POSIX invariant used by PXA's regular-file checks. */
        metadata->st_nlink = 1;
    }
}

int pxa_esp_fstat(int fd, struct stat *buffer) {
    int result;
    if (buffer == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (synthetic_directory(fd)) {
        char path[PXA_ESP_POSIX_PATH_MAX]; uint64_t epoch;
        if (directory_path(fd, path, sizeof(path), &epoch) != 0) return -1;
        result = stat(path, buffer);
    } else result = fstat(fd, buffer);
    if (result == 0) normalize_metadata(buffer);
    return result;
}

int pxa_esp_fstatat(int dirfd, const char *path, struct stat *buffer,
                    int flags) {
    char full_path[PXA_ESP_POSIX_PATH_MAX];
    int result;
    (void)flags;
    if (buffer == NULL || resolve_path(dirfd, path, full_path,
                                       sizeof(full_path)) != 0) {
        if (buffer == NULL) errno = EINVAL;
        return -1;
    }
    /* Native ESP filesystems are link-free. POSIX-hosted tests must still
     * distinguish links when AT_SYMLINK_NOFOLLOW was requested. */
#if defined(__linux__) || defined(__APPLE__)
    result = fstatat(AT_FDCWD, full_path, buffer, flags);
#else
    result = stat(full_path, buffer);
#endif
    if (result == 0) normalize_metadata(buffer);
    return result;
}

int pxa_esp_mkdirat(int dirfd, const char *path, mode_t mode) {
    char full_path[PXA_ESP_POSIX_PATH_MAX];
    if (resolve_path(dirfd, path, full_path, sizeof(full_path)) != 0) {
        return -1;
    }
    return mkdir(full_path, mode);
}

int pxa_esp_renameat(int olddirfd, const char *oldpath, int newdirfd,
                     const char *newpath) {
    char old_full_path[PXA_ESP_POSIX_PATH_MAX];
    char new_full_path[PXA_ESP_POSIX_PATH_MAX];
    if (resolve_path(olddirfd, oldpath, old_full_path,
                     sizeof(old_full_path)) != 0 ||
        resolve_path(newdirfd, newpath, new_full_path,
                     sizeof(new_full_path)) != 0) {
        return -1;
    }
    return rename_canonical(old_full_path,new_full_path);
}

int pxa_esp_unlinkat(int dirfd, const char *path, int flags) {
    char full_path[PXA_ESP_POSIX_PATH_MAX];
    if (resolve_path(dirfd, path, full_path, sizeof(full_path)) != 0) {
        return -1;
    }
    return (flags & AT_REMOVEDIR) != 0 ? pxa_esp_rmdir(full_path) : unlink(full_path);
}

int pxa_esp_fsync(int fd) {
    struct stat metadata;
    int saved_errno = errno;
    int result;
    /* LittleFS fstat resolves the handle's pathname. Once its empty parent
     * was pruned by SPIFFS compatibility mode, that lookup can fail. Never
     * pass such a directory handle to LittleFS's file-only fsync callback. */
    if (pxa_esp_fstat(fd, &metadata) != 0) return -1;
    if (S_ISDIR(metadata.st_mode)) {
        if (g_directory_hooks.sync_directory) {
            char path[PXA_ESP_POSIX_PATH_MAX]; uint64_t epoch;
            if (resolve_path(fd, ".", path, sizeof(path)) != 0) return -1;
            epoch = path_epoch(path);
            return g_directory_hooks.sync_directory(g_directory_hooks.context, path, epoch);
        }
        /* LittleFS persists directory metadata during its file/rename
         * operations but does not implement fsync for directory handles. */
        return 0;
    }
    result = fsync(fd);
    if (result != 0 && (errno == ENOTSUP || errno == EOPNOTSUPP)) {
        /* Some ESP-IDF VFS paths do not expose fsync even though LittleFS
         * commits the pending file state when the descriptor is closed. */
        errno = saved_errno;
        return 0;
    }
    return result;
}

DIR *pxa_esp_fdopendir(int fd) {
    char path[PXA_ESP_POSIX_PATH_MAX];
    DIR *stream;
    if (resolve_path(fd, ".", path, sizeof(path)) != 0) return NULL;
    stream = opendir(path);
    if (stream == NULL) return NULL;
    (void)pxa_esp_close(fd);
    return stream;
}

#endif /* ESP_PLATFORM */
