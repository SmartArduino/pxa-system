#ifndef PXA_ESP_POSIX_SHIM_H
#define PXA_ESP_POSIX_SHIM_H

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ESP-IDF's VFS lacks the *at family and FAT directory file descriptors.
 * Host-owned directory handles work on link-free LittleFS/FAT volumes. Mount
 * epochs prevent a retained handle from following a different mounted card. */
typedef struct {
    void *context;
    /* Zero: unmanaged bootstrap path; UINT64_MAX: unavailable/draining. */
    uint64_t (*mount_epoch)(void *, const char *path);
    int (*sync_directory)(void *, const char *path, uint64_t mount_epoch);
} pxa_esp_posix_directory_hooks_t;
/* Configure before creating handles; changing live hooks returns EBUSY. */
int pxa_esp_posix_set_directory_hooks(const pxa_esp_posix_directory_hooks_t *);
int pxa_esp_open(const char *path, int flags, ...);
int pxa_esp_close(int fd);
int pxa_esp_rename(const char *oldpath, const char *newpath);
int pxa_esp_rmdir(const char *path);
int pxa_esp_dup(int fd);
int pxa_esp_openat(int dirfd, const char *path, int flags, ...);
int pxa_esp_fstat(int fd, struct stat *buffer);
int pxa_esp_fstatat(int dirfd, const char *path, struct stat *buffer,
                    int flags);
int pxa_esp_mkdirat(int dirfd, const char *path, mode_t mode);
int pxa_esp_renameat(int olddirfd, const char *oldpath, int newdirfd,
                     const char *newpath);
int pxa_esp_unlinkat(int dirfd, const char *path, int flags);
int pxa_esp_fsync(int fd);
DIR *pxa_esp_fdopendir(int fd);
/* Open a stream directly on a link-free ESP volume, checking the retained
 * directory's epoch and refusing paths that cross its physical volume. */
DIR *pxa_esp_opendirat(int fd, const char *path);
/* Call between operations in a long I/O traversal, outside filesystem locks.
 * The operation owns a zero-initialized tick stamp. Periodically let the
 * idle task run; no worker task, allocation or permanent timer is needed. */
void pxa_esp_posix_io_checkpoint(uint32_t *last_yield_tick);
/* Process-owned WASI sink, borrowed by serialized PXA Host instances.
 * WAMR marks stdio borrowed; callers must never close this descriptor. */
int pxa_esp_wasi_null_fd(void);

#ifdef __cplusplus
}
#endif

#endif
