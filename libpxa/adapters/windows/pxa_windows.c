/* SPDX-License-Identifier: MIT */
#define PXA_WINDOWS_IMPLEMENTATION
#include "pxa_windows.h"
#include <stddef.h>
#include <string.h>
#include <winternl.h>

/* CRT descriptor numbers are recycled; clear flags on every close and set
 * them only after _open_osfhandle succeeds. Access is protected for workers. */
static SRWLOCK fd_lock = SRWLOCK_INIT;
static int fd_flags[8192];

static int win_error(DWORD code) {
    switch (code) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        errno = ENOENT;
        break;
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS:
        errno = EEXIST;
        break;
    case ERROR_DIR_NOT_EMPTY:
        errno = ENOTEMPTY;
        break;
    case ERROR_DISK_FULL:
        errno = ENOSPC;
        break;
    case ERROR_NOT_ENOUGH_MEMORY:
        errno = ENOMEM;
        break;
    case ERROR_TOO_MANY_OPEN_FILES:
        errno = EMFILE;
        break;
    case ERROR_NOT_SAME_DEVICE:
        errno = EXDEV;
        break;
    case ERROR_INVALID_HANDLE:
        errno = EBADF;
        break;
    case ERROR_LOCK_VIOLATION:
    case ERROR_SHARING_VIOLATION:
        errno = EAGAIN;
        break;
    case ERROR_DIRECTORY:
        errno = ENOTDIR;
        break;
    default:
        errno = EACCES;
        break;
    }
    return -1;
}
static wchar_t *wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (!n) {
        errno = EINVAL;
        return NULL;
    }
    wchar_t *p = malloc((size_t)n * sizeof(*p));
    if (!p) {
        errno = ENOMEM;
        return NULL;
    }
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, p, n);
    return p;
}
static wchar_t *wide_path(const char *path) {
    wchar_t *input = wide(path);
    if (!input)
        return NULL;
    if (!wcsncmp(input, L"\\\\?\\", 4))
        return input;
    DWORD length = GetFullPathNameW(input, 0, NULL, NULL);
    if (!length) {
        DWORD error = GetLastError();
        free(input);
        win_error(error);
        return NULL;
    }
    wchar_t *full = malloc(((size_t)length + 8) * sizeof(*full));
    if (!full) {
        free(input);
        errno = ENOMEM;
        return NULL;
    }
    if (!GetFullPathNameW(input, length, full + 8, NULL)) {
        DWORD error = GetLastError();
        free(input);
        free(full);
        win_error(error);
        return NULL;
    }
    free(input);
    for (wchar_t *p = full + 8; *p; ++p)
        if (*p == L'/')
            *p = L'\\';
    if (full[8] == L'\\' && full[9] == L'\\') {
        memmove(full + 8, full + 10, (wcslen(full + 10) + 1) * sizeof(*full));
        memcpy(full, L"\\\\?\\UNC\\", 8 * sizeof(*full));
    } else {
        memmove(full + 4, full + 8, (wcslen(full + 8) + 1) * sizeof(*full));
        memcpy(full, L"\\\\?\\", 4 * sizeof(*full));
    }
    return full;
}
static HANDLE handle(int fd) { return (HANDLE)_get_osfhandle(fd); }
FILE *pxa_win_fopen(const char *path, const char *mode) {
    wchar_t *p = wide_path(path), *m = wide(mode);
    FILE *file = NULL;
    if (p && m)
        file = _wfopen(p, m);
    free(p);
    free(m);
    return file;
}
int pxa_win_rename(const char *from, const char *to) {
    wchar_t *a = wide_path(from), *b = wide_path(to);
    if (!a || !b) {
        free(a);
        free(b);
        return -1;
    }
    BOOL ok = MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    DWORD error = GetLastError();
    free(a);
    free(b);
    return ok ? 0 : win_error(error);
}
int pxa_win_unlink(const char *path) {
    wchar_t *p = wide_path(path);
    if (!p)
        return -1;
    BOOL ok = DeleteFileW(p);
    DWORD error = GetLastError();
    free(p);
    return ok ? 0 : win_error(error);
}
static int descriptor(HANDLE h, int flags) {
    if (h == INVALID_HANDLE_VALUE)
        return win_error(GetLastError());
    int fd = _open_osfhandle((intptr_t)h, (flags & (_O_APPEND | _O_RDWR | _O_WRONLY)) |
                                              _O_BINARY | _O_NOINHERIT);
    if (fd < 0)
        CloseHandle(h);
    else if (fd < (int)(sizeof(fd_flags) / sizeof(*fd_flags))) {
        AcquireSRWLockExclusive(&fd_lock);
        fd_flags[fd] = flags;
        ReleaseSRWLockExclusive(&fd_lock);
    }
    return fd;
}
int pxa_win_close(int fd) {
    AcquireSRWLockExclusive(&fd_lock);
    if (fd >= 0 && fd < (int)(sizeof(fd_flags) / sizeof(*fd_flags)))
        fd_flags[fd] = 0;
    int result = close(fd);
    ReleaseSRWLockExclusive(&fd_lock);
    return result;
}
static int no_reparse(HANDLE h) {
    FILE_ATTRIBUTE_TAG_INFO info;
    if (!GetFileInformationByHandleEx(h, FileAttributeTagInfo, &info, sizeof(info)))
        return win_error(GetLastError());
    if (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        errno = ELOOP;
        return -1;
    }
    return 0;
}
int pxa_win_open(const char *path, int flags, ...) {
    if (!_stricmp(path, "NUL"))
        return _wopen(L"NUL",
                      (flags & (_O_RDWR | _O_WRONLY)) | _O_BINARY | _O_NOINHERIT);
    wchar_t *p = wide_path(path);
    if (!p)
        return -1;
    DWORD access = GENERIC_READ;
    if (flags & O_RDWR)
        access |= GENERIC_WRITE;
    else if (flags & O_WRONLY)
        access = GENERIC_WRITE;
    DWORD disposition = (flags & O_CREAT)
                            ? ((flags & O_EXCL) ? CREATE_NEW : OPEN_ALWAYS)
                            : OPEN_EXISTING;
    /* Truncation happens only after rejecting a reparse point. */
    HANDLE h =
        CreateFileW(p, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, disposition,
                    FILE_FLAG_BACKUP_SEMANTICS |
                        ((flags & O_NOFOLLOW) ? FILE_FLAG_OPEN_REPARSE_POINT : 0),
                    NULL);
    free(p);
    if (h == INVALID_HANDLE_VALUE)
        return win_error(GetLastError());
    if ((flags & O_NOFOLLOW) && no_reparse(h)) {
        CloseHandle(h);
        return -1;
    }
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(h, &info)) {
        DWORD e = GetLastError();
        CloseHandle(h);
        return win_error(e);
    }
    if ((flags & O_DIRECTORY) && !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        CloseHandle(h);
        errno = ENOTDIR;
        return -1;
    }
    if (flags & O_TRUNC) {
        LARGE_INTEGER zero;
        zero.QuadPart = 0;
        if (!SetFilePointerEx(h, zero, NULL, FILE_BEGIN) || !SetEndOfFile(h)) {
            DWORD e = GetLastError();
            CloseHandle(h);
            return win_error(e);
        }
    }
    return descriptor(h, flags);
}
/* Open one literal child using a live parent handle. Reject Windows path
 * aliases (ADS, DOS separators, trailing spaces/dots), including in archives.
 * Multi-segment callers must walk through directory handles themselves. */
static HANDLE child(int parent, const char *name, ACCESS_MASK access, ULONG disposition,
                    ULONG options) {
    if (!name || !*name || (!strcmp(name, "..")) || strpbrk(name, "/\\:")) {
        errno = EACCES;
        return INVALID_HANDLE_VALUE;
    }
    size_t n = strlen(name);
    if (strcmp(name, ".") && (name[n - 1] == '.' || name[n - 1] == ' ')) {
        errno = EACCES;
        return INVALID_HANDLE_VALUE;
    }
    /* NT paths do not normalize POSIX dot segments. An empty relative name
     * reopens RootDirectory as a fresh file object with its own cursor. */
    wchar_t *w = wide(!strcmp(name, ".") ? "" : name);
    if (!w)
        return INVALID_HANDLE_VALUE;
    UNICODE_STRING s;
    s.Buffer = w;
    s.Length = (USHORT)(wcslen(w) * sizeof(*w));
    s.MaximumLength = s.Length;
    OBJECT_ATTRIBUTES a;
    InitializeObjectAttributes(&a, &s, OBJ_CASE_INSENSITIVE, handle(parent), NULL);
    IO_STATUS_BLOCK status;
    HANDLE result = INVALID_HANDLE_VALUE;
    NTSTATUS code = NtCreateFile(
        &result, access | SYNCHRONIZE | FILE_READ_ATTRIBUTES, &a, &status, NULL,
        FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        disposition, options | FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT,
        NULL, 0);
    free(w);
    if (code < 0) {
        win_error(RtlNtStatusToDosError(code));
        return INVALID_HANDLE_VALUE;
    }
    return result;
}
int pxa_win_openat(int parent, const char *name, int flags, ...) {
    ACCESS_MASK access = GENERIC_READ;
    if (flags & O_RDWR)
        access |= GENERIC_WRITE;
    else if (flags & O_WRONLY)
        access = GENERIC_WRITE;
    ULONG disposition =
        (flags & O_CREAT) ? ((flags & O_EXCL) ? FILE_CREATE : FILE_OPEN_IF) : FILE_OPEN;
    HANDLE h = child(parent, name, access, disposition,
                     (flags & O_DIRECTORY) ? FILE_DIRECTORY_FILE : 0);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    if (no_reparse(h)) {
        CloseHandle(h);
        return -1;
    }
    if (flags & O_TRUNC) {
        LARGE_INTEGER zero;
        zero.QuadPart = 0;
        if (!SetFilePointerEx(h, zero, NULL, FILE_BEGIN) || !SetEndOfFile(h)) {
            DWORD e = GetLastError();
            CloseHandle(h);
            return win_error(e);
        }
    }
    return descriptor(h, flags);
}
static int metadata(HANDLE h, struct pxa_win_stat *out) {
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(h, &info))
        return win_error(GetLastError());
    memset(out, 0, sizeof(*out));
    out->st_dev = info.dwVolumeSerialNumber;
    out->st_ino = ((uint64_t)info.nFileIndexHigh << 32) | info.nFileIndexLow;
    out->st_nlink = info.nNumberOfLinks;
    out->st_size = ((int64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    out->st_mode = (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? S_IFLNK
                   : (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)   ? S_IFDIR
                                                                          : S_IFREG;
    uint64_t t = ((uint64_t)info.ftLastWriteTime.dwHighDateTime << 32) |
                 info.ftLastWriteTime.dwLowDateTime;
    out->st_mtime = (time_t)(t / 10000000 - 11644473600ULL);
    out->st_mtim.tv_sec = out->st_mtime;
    out->st_mtim.tv_nsec = (long)((t % 10000000) * 100);
    FILE_BASIC_INFO basic;
    if (!GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof(basic)))
        return win_error(GetLastError());
    t = (uint64_t)basic.ChangeTime.QuadPart;
    out->st_ctim.tv_sec = (time_t)(t / 10000000 - 11644473600ULL);
    out->st_ctim.tv_nsec = (long)((t % 10000000) * 100);
    return 0;
}
int pxa_win_fstat(int fd, struct pxa_win_stat *out) {
    return metadata(handle(fd), out);
}
int pxa_win_fstatat(int fd, const char *name, struct pxa_win_stat *out, int flags) {
    (void)flags;
    HANDLE h = child(fd, name, 0, FILE_OPEN, 0);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    int r = metadata(h, out);
    CloseHandle(h);
    return r;
}
static int path_stat(const char *path, struct pxa_win_stat *out, int follow) {
    wchar_t *w = wide_path(path);
    if (!w)
        return -1;
    HANDLE h = CreateFileW(
        w, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | (follow ? 0 : FILE_FLAG_OPEN_REPARSE_POINT), NULL);
    free(w);
    if (h == INVALID_HANDLE_VALUE)
        return win_error(GetLastError());
    int r = metadata(h, out);
    CloseHandle(h);
    return r;
}
int pxa_win_stat(const char *p, struct pxa_win_stat *s) { return path_stat(p, s, 1); }
int pxa_win_lstat(const char *p, struct pxa_win_stat *s) { return path_stat(p, s, 0); }
int pxa_win_mkdir(const char *p, int mode) {
    (void)mode;
    wchar_t *w = wide_path(p);
    if (!w)
        return -1;
    BOOL ok = CreateDirectoryW(w, NULL);
    DWORD e = GetLastError();
    free(w);
    return ok ? 0 : win_error(e);
}
int pxa_win_mkdirat(int fd, const char *p, int mode) {
    (void)mode;
    HANDLE h =
        child(fd, p, GENERIC_READ | GENERIC_WRITE, FILE_CREATE, FILE_DIRECTORY_FILE);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    CloseHandle(h);
    return 0;
}
int pxa_win_unlinkat(int fd, const char *p, int flags) {
    HANDLE h = child(fd, p, DELETE, FILE_OPEN, 0);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    struct pxa_win_stat s;
    if (metadata(h, &s)) {
        CloseHandle(h);
        return -1;
    }
    if (!(s.st_mode == S_IFLNK) &&
        ((s.st_mode == S_IFDIR) != !!(flags & AT_REMOVEDIR))) {
        CloseHandle(h);
        errno = EISDIR;
        return -1;
    }
    FILE_DISPOSITION_INFORMATION d = {TRUE};
    IO_STATUS_BLOCK status;
    NTSTATUS code =
        NtSetInformationFile(h, &status, &d, sizeof(d), FileDispositionInformation);
    CloseHandle(h);
    return code >= 0 ? 0 : win_error(RtlNtStatusToDosError(code));
}
static int rename_child(int from, const char *a, int to, const char *b, int replace) {
    /* Validate the destination with the same literal-child grammar. */
    size_t n = strlen(b);
    if (!n || !strcmp(b, ".") || !strcmp(b, "..") || strpbrk(b, "/\\:") ||
        b[n - 1] == '.' || b[n - 1] == ' ') {
        errno = EACCES;
        return -1;
    }
    HANDLE h = child(from, a, DELETE, FILE_OPEN, 0);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    wchar_t *w = wide(b);
    if (!w) {
        CloseHandle(h);
        return -1;
    }
    size_t bytes = wcslen(w) * sizeof(*w),
           size = offsetof(FILE_RENAME_INFO, FileName) + bytes;
    FILE_RENAME_INFO *info = calloc(1, size);
    if (!info) {
        free(w);
        CloseHandle(h);
        errno = ENOMEM;
        return -1;
    }
    info->ReplaceIfExists = (BOOLEAN)replace;
    info->RootDirectory = handle(to);
    info->FileNameLength = (DWORD)bytes;
    memcpy(info->FileName, w, bytes);
    IO_STATUS_BLOCK status;
    NTSTATUS code =
        NtSetInformationFile(h, &status, info, (ULONG)size, FileRenameInformation);
    free(info);
    free(w);
    CloseHandle(h);
    return code >= 0 ? 0 : win_error(RtlNtStatusToDosError(code));
}
int pxa_win_renameat(int a, const char *b, int c, const char *d) {
    return rename_child(a, b, c, d, 1);
}
int pxa_win_rename_noreplace(int a, const char *b, int c, const char *d) {
    return rename_child(a, b, c, d, 0);
}
int pxa_win_fsync(int fd) {
    struct pxa_win_stat info;
    if (pxa_win_fstat(fd, &info))
        return -1;
    /* Windows has no user-mode directory fsync. File data is flushed before
     * publication; directory durability across power loss is OS-managed. */
    if (info.st_mode == S_IFDIR)
        return 0;
    if (FlushFileBuffers(handle(fd)))
        return 0;
    if (GetLastError() != ERROR_ACCESS_DENIED)
        return win_error(GetLastError());
    /* sync_tree opens regular files read-only. Windows requires write access
     * for FlushFileBuffers, so obtain it through the same file object. */
    HANDLE writable =
        ReOpenFile(handle(fd), GENERIC_WRITE,
                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);
    if (writable == INVALID_HANDLE_VALUE)
        return win_error(GetLastError());
    BOOL ok = FlushFileBuffers(writable);
    DWORD error = GetLastError();
    CloseHandle(writable);
    return ok ? 0 : win_error(error);
}
int pxa_win_flock(int fd, int operation) {
    OVERLAPPED where;
    memset(&where, 0, sizeof(where));
    BOOL ok;
    if (operation & LOCK_UN)
        ok = UnlockFileEx(handle(fd), 0, 1, 0, &where);
    else
        ok = LockFileEx(handle(fd),
                        ((operation & LOCK_EX) ? LOCKFILE_EXCLUSIVE_LOCK : 0) |
                            ((operation & LOCK_NB) ? LOCKFILE_FAIL_IMMEDIATELY : 0),
                        0, 1, 0, &where);
    return ok ? 0 : win_error(GetLastError());
}
int pxa_win_fcntl(int fd, int operation, ...) {
    if (operation != F_GETFL) {
        errno = EINVAL;
        return -1;
    }
    if (fd < 0 || fd >= (int)(sizeof(fd_flags) / sizeof(*fd_flags))) {
        errno = EBADF;
        return -1;
    }
    AcquireSRWLockShared(&fd_lock);
    int flags = fd_flags[fd];
    ReleaseSRWLockShared(&fd_lock);
    return flags;
}
ssize_t pxa_win_pread(int fd, void *buffer, size_t size, off_t offset) {
    if (offset < 0) {
        errno = EINVAL;
        return -1;
    }
    /* A synchronous Win32 handle advances its cursor even with OVERLAPPED.
     * ReOpenFile gives this positional read an independent file object. */
    HANDLE file = ReOpenFile(handle(fd), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             FILE_FLAG_OVERLAPPED);
    if (file == INVALID_HANDLE_VALUE)
        return win_error(GetLastError());
    OVERLAPPED where;
    memset(&where, 0, sizeof(where));
    where.Offset = (DWORD)offset;
    where.OffsetHigh = (DWORD)((uint64_t)offset >> 32);
    where.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!where.hEvent) {
        DWORD error = GetLastError();
        CloseHandle(file);
        return win_error(error);
    }
    DWORD count = 0;
    BOOL ok = ReadFile(file, buffer, (DWORD)(size > MAXDWORD ? MAXDWORD : size), &count,
                       &where);
    if (!ok && GetLastError() == ERROR_IO_PENDING)
        ok = GetOverlappedResult(file, &where, &count, TRUE);
    DWORD error = GetLastError();
    CloseHandle(where.hEvent);
    CloseHandle(file);
    return ok ? (ssize_t)count : error == ERROR_HANDLE_EOF ? 0 : win_error(error);
}
char *pxa_win_realpath(const char *path, char *output) {
    int fd = pxa_win_open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    wchar_t w[4096];
    DWORD n = GetFinalPathNameByHandleW(handle(fd), w, 4096,
                                        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    close(fd);
    if (!n || n >= 4096) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    const wchar_t *p = w;
    int unc = 0;
    if (!wcsncmp(w, L"\\\\?\\UNC\\", 8)) {
        p += 8;
        unc = 2;
    } else if (!wcsncmp(w, L"\\\\?\\", 4))
        p += 4;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, p, -1, NULL, 0, NULL, NULL);
    if (bytes + unc > 4096) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    char *result = output ? output : malloc((size_t)bytes + unc);
    if (!result) {
        errno = ENOMEM;
        return NULL;
    }
    if (unc) {
        result[0] = '/';
        result[1] = '/';
    }
    WideCharToMultiByte(CP_UTF8, 0, p, -1, result + unc, bytes, NULL, NULL);
    for (char *c = result; *c; ++c)
        if (*c == '\\')
            *c = '/';
    return result;
}
struct pxa_win_dir {
    int fd;
    long position;
    int restart;
    struct dirent entry;
};
PXA_WIN_DIR *pxa_win_fdopendir(int fd) {
    struct pxa_win_stat info;
    if (pxa_win_fstat(fd, &info) || info.st_mode != S_IFDIR) {
        errno = ENOTDIR;
        return NULL;
    }
    PXA_WIN_DIR *d = calloc(1, sizeof(*d));
    if (!d) {
        errno = ENOMEM;
        return NULL;
    }
    d->fd = fd;
    d->restart = 1;
    return d;
}
PXA_WIN_DIR *pxa_win_opendir(const char *path) {
    int fd = pxa_win_open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0)
        return NULL;
    PXA_WIN_DIR *d = pxa_win_fdopendir(fd);
    if (!d)
        close(fd);
    return d;
}
struct dirent *pxa_win_readdir(PXA_WIN_DIR *d) {
    /* NtQueryDirectoryFile supports returning one entry and works with an
     * opened directory even when another process renames its parent. */
    unsigned char buffer[8192];
    IO_STATUS_BLOCK status;
    typedef NTSTATUS(NTAPI * query_fn)(HANDLE, HANDLE, PVOID, PVOID, PIO_STATUS_BLOCK,
                                       PVOID, ULONG, FILE_INFORMATION_CLASS, BOOLEAN,
                                       PUNICODE_STRING, BOOLEAN);
    query_fn query = (query_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                                      "NtQueryDirectoryFile");
    if (!query) {
        errno = ENOSYS;
        return NULL;
    }
    NTSTATUS code =
        query(handle(d->fd), NULL, NULL, NULL, &status, buffer, sizeof(buffer),
              FileDirectoryInformation, TRUE, NULL, (BOOLEAN)d->restart);
    d->restart = 0;
    if ((ULONG)code == 0x80000006UL) {
        errno = 0;
        return NULL;
    }
    if (code < 0) {
        win_error(RtlNtStatusToDosError(code));
        return NULL;
    }
    FILE_DIRECTORY_INFORMATION *item = (FILE_DIRECTORY_INFORMATION *)buffer;
    int n = WideCharToMultiByte(
        CP_UTF8, 0, item->FileName, (int)(item->FileNameLength / sizeof(wchar_t)),
        d->entry.d_name, sizeof(d->entry.d_name) - 1, NULL, NULL);
    if (!n) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    d->entry.d_name[n] = 0;
    d->position++;
    return &d->entry;
}
int pxa_win_closedir(PXA_WIN_DIR *d) {
    int r = close(d->fd);
    free(d);
    return r;
}
int pxa_win_dirfd(PXA_WIN_DIR *d) { return d->fd; }
long pxa_win_telldir(PXA_WIN_DIR *d) { return d->position; }
void pxa_win_rewinddir(PXA_WIN_DIR *d) {
    d->restart = 1;
    d->position = 0;
}
void pxa_win_seekdir(PXA_WIN_DIR *d, long pos) {
    pxa_win_rewinddir(d);
    while (d->position < pos && pxa_win_readdir(d)) {
    }
}
