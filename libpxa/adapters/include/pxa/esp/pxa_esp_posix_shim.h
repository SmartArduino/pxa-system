#ifndef PXA_ESP_POSIX_SHIM_H
#define PXA_ESP_POSIX_SHIM_H

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ESP-IDF's VFS does not provide the POSIX *at family used by the portable
 * adapters. Resolve the directory fd through LittleFS F_GETPATH and dispatch
 * to the ordinary path-based VFS calls. */
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

#ifdef __cplusplus
}
#endif

#endif
