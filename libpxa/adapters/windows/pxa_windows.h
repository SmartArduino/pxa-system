/* SPDX-License-Identifier: MIT
 * Native MinGW file operations for the desktop Host. Relative operations use
 * NT directory handles; no Guest path is converted to a concatenated pathname.
 */
#ifndef PXA_WINDOWS_H
#define PXA_WINDOWS_H
#ifdef _WIN32
#include <direct.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <winsock2.h>
#include <windows.h>

#define O_DIRECTORY 0x100000
#define O_NOFOLLOW 0x200000
#define O_CLOEXEC 0x400000
#define O_NONBLOCK 0x800000
#define AT_SYMLINK_NOFOLLOW 1
#define AT_REMOVEDIR 2
#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_NB 4
#define LOCK_UN 8
#define F_GETFL 1
#ifndef NAME_MAX
#define NAME_MAX 255
#endif
#ifndef EDQUOT
#define EDQUOT ENOSPC
#endif
#ifndef S_IFLNK
#define S_IFLNK 0120000
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#endif

struct pxa_win_stat {
    uint64_t st_dev, st_ino, st_nlink;
    int64_t st_size;
    unsigned st_mode;
    time_t st_mtime;
    struct timespec st_mtim, st_ctim;
};
typedef struct pxa_win_dir PXA_WIN_DIR;
int pxa_win_open(const char *, int, ...);
int pxa_win_openat(int, const char *, int, ...);
int pxa_win_fstat(int, struct pxa_win_stat *);
int pxa_win_stat(const char *, struct pxa_win_stat *);
int pxa_win_lstat(const char *, struct pxa_win_stat *);
int pxa_win_fstatat(int, const char *, struct pxa_win_stat *, int);
int pxa_win_mkdir(const char *, int);
int pxa_win_mkdirat(int, const char *, int);
int pxa_win_unlinkat(int, const char *, int);
int pxa_win_renameat(int, const char *, int, const char *);
int pxa_win_rename_noreplace(int, const char *, int, const char *);
int pxa_win_fsync(int);
int pxa_win_flock(int, int);
int pxa_win_fcntl(int, int, ...);
int pxa_win_close(int);
FILE *pxa_win_fopen(const char *, const char *);
int pxa_win_rename(const char *, const char *);
int pxa_win_unlink(const char *);
ssize_t pxa_win_pread(int, void *, size_t, off_t);
char *pxa_win_realpath(const char *, char *);
PXA_WIN_DIR *pxa_win_opendir(const char *);
PXA_WIN_DIR *pxa_win_fdopendir(int);
struct dirent *pxa_win_readdir(PXA_WIN_DIR *);
int pxa_win_closedir(PXA_WIN_DIR *);
int pxa_win_dirfd(PXA_WIN_DIR *);
long pxa_win_telldir(PXA_WIN_DIR *);
void pxa_win_seekdir(PXA_WIN_DIR *, long);
void pxa_win_rewinddir(PXA_WIN_DIR *);
static inline struct tm *pxa_win_localtime_r(const time_t *t, struct tm *out) {
    return localtime_s(out, t) == 0 ? out : NULL;
}
static inline struct tm *pxa_win_gmtime_r(const time_t *t, struct tm *out) {
    return gmtime_s(out, t) == 0 ? out : NULL;
}

#ifndef PXA_WINDOWS_IMPLEMENTATION
#define stat pxa_win_stat
#define fstat pxa_win_fstat
#define lstat pxa_win_lstat
#define fstatat pxa_win_fstatat
#define open pxa_win_open
#define openat pxa_win_openat
#define close pxa_win_close
#define fopen pxa_win_fopen
#define rename pxa_win_rename
#define unlink pxa_win_unlink
#define mkdir pxa_win_mkdir
#define mkdirat pxa_win_mkdirat
#define unlinkat pxa_win_unlinkat
#define renameat pxa_win_renameat
#define fsync pxa_win_fsync
#define flock pxa_win_flock
#define fcntl pxa_win_fcntl
#define pread pxa_win_pread
#define realpath pxa_win_realpath
#define DIR PXA_WIN_DIR
#define opendir pxa_win_opendir
#define fdopendir pxa_win_fdopendir
#define readdir pxa_win_readdir
#define closedir pxa_win_closedir
#define dirfd pxa_win_dirfd
#define telldir pxa_win_telldir
#define seekdir pxa_win_seekdir
#define rewinddir pxa_win_rewinddir
#define localtime_r pxa_win_localtime_r
#define gmtime_r pxa_win_gmtime_r
#endif
#endif
#endif
