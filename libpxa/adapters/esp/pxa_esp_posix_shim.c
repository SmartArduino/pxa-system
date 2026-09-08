/* ESP-IDF shims for POSIX calls missing from newlib. LittleFS has no
 * symlinks and the host is single-owner, so directory reopen and a no-op
 * flock are sufficient. */

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
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pxa/esp/pxa_esp_posix_shim.h"

#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif

#ifndef AT_REMOVEDIR
#define AT_REMOVEDIR 0x200
#endif

#ifndef PXA_ESP_POSIX_PATH_MAX
#define PXA_ESP_POSIX_PATH_MAX 512
#endif

#if CONFIG_LITTLEFS_FCNTL_GET_PATH
#ifndef F_GETPATH
#define F_GETPATH CONFIG_LITTLEFS_FCNTL_F_GETPATH_VALUE
#endif
#endif

static int resolve_path(int dirfd, const char *path, char *output,
                        size_t capacity) {
    char base[PXA_ESP_POSIX_PATH_MAX];
    int result;

    if (path == NULL || output == NULL || capacity == 0) {
        errno = EINVAL;
        return -1;
    }
    if (path[0] == '/') {
        result = snprintf(output, capacity, "%s", path);
    } else if (dirfd == AT_FDCWD) {
        result = snprintf(output, capacity, "%s", path);
    } else {
#if CONFIG_LITTLEFS_FCNTL_GET_PATH
        size_t base_length;
        if (fcntl(dirfd, F_GETPATH, base) != 0) return -1;
        base_length = strlen(base);
        if (strcmp(path, ".") == 0 || path[0] == '\0') {
            result = snprintf(output, capacity, "%s", base);
        } else if (base_length != 0 && base[base_length - 1] == '/') {
            result = snprintf(output, capacity, "%s%s", base, path);
        } else {
            result = snprintf(output, capacity, "%s/%s", base, path);
        }
#else
        (void)base;
        errno = ENOSYS;
        return -1;
#endif
    }
    if (result < 0 || (size_t)result >= capacity) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

int flock(int fd, int operation) {
    (void)fd;
    (void)operation;
    return 0;
}

int pxa_esp_dup(int fd) {
    char path[PXA_ESP_POSIX_PATH_MAX];
    struct stat metadata;
    if (fstat(fd, &metadata) != 0) return -1;
    if (!S_ISDIR(metadata.st_mode)) {
        errno = ENOTSUP;
        return -1;
    }
    if (resolve_path(fd, ".", path, sizeof(path)) != 0) return -1;
    /* PXA duplicates only directory handles. LittleFS does not implement
     * F_DUPFD, so reopen the resolved directory as an independent handle. */
    return open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
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
        result = open(full_path, flags, mode);
    } else {
        result = open(full_path, flags);
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
    result = fstat(fd, buffer);
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
    /* LittleFS does not support symlinks, so stat is equivalent to fstatat
     * with AT_SYMLINK_NOFOLLOW for this adapter. */
    result = stat(full_path, buffer);
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
    return rename(old_full_path, new_full_path);
}

int pxa_esp_unlinkat(int dirfd, const char *path, int flags) {
    char full_path[PXA_ESP_POSIX_PATH_MAX];
    if (resolve_path(dirfd, path, full_path, sizeof(full_path)) != 0) {
        return -1;
    }
    return (flags & AT_REMOVEDIR) != 0 ? rmdir(full_path) : unlink(full_path);
}

int pxa_esp_fsync(int fd) {
    struct stat metadata;
    int saved_errno = errno;
    int result;
    if (fstat(fd, &metadata) == 0 && S_ISDIR(metadata.st_mode)) {
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
    (void)close(fd);
    return stream;
}

#endif /* ESP_PLATFORM */
