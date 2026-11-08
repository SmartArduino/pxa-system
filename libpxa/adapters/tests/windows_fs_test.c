/* SPDX-License-Identifier: MIT */
#include "pxa_windows.h"
#include <assert.h>
#include <string.h>

int main(void) {
    char temporary[MAX_PATH], root[MAX_PATH];
    assert(GetTempPathA(sizeof(temporary), temporary));
    assert(GetTempFileNameA(temporary, "pxa", 0, root));
    assert(DeleteFileA(root));
    assert(mkdir(root, 0700) == 0);
    int parent = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    assert(parent >= 0);
    char long_path[1024];
    strcpy(long_path, root);
    int levels[13];
    levels[0] = parent;
    for (unsigned i = 0; i < 12; ++i) {
        assert(mkdirat(levels[i], "long-path-component-12345", 0700) == 0);
        levels[i + 1] = openat(levels[i], "long-path-component-12345",
                               O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        assert(levels[i + 1] >= 0);
        strcat(long_path, "/long-path-component-12345");
    }
    assert(strlen(long_path) > 260);
    int deep = open(long_path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    assert(deep >= 0);
    close(deep);
    for (unsigned i = 12; i > 0; --i) {
        close(levels[i]);
        assert(unlinkat(levels[i - 1], "long-path-component-12345", AT_REMOVEDIR) == 0);
    }
    assert(mkdirat(parent, "child", 0700) == 0);
    int directory = openat(parent, "child", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    assert(directory >= 0);
    int fresh = openat(directory, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    assert(fresh >= 0);
    close(fresh);
    int fd = openat(directory, "test-中文.bin", O_RDWR | O_CREAT | O_EXCL, 0600);
    assert(fd >= 0 && write(fd, "abc\n", 4) == 4 && fsync(fd) == 0);
    struct stat info;
    assert(fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size == 4 &&
           info.st_nlink == 1);
    char bytes[4];
    assert(pread(fd, bytes, 2, 0) == 2 && !memcmp(bytes, "ab", 2));
    assert(lseek(fd, 0, SEEK_CUR) == 4);
    assert(close(fd) == 0);
    assert(fstatat(directory, "test-中文.bin", &info, AT_SYMLINK_NOFOLLOW) == 0 &&
           info.st_size == 4);
    fd = openat(directory, "test-中文.bin", O_RDWR | O_APPEND);
    assert(fd >= 0 && (fcntl(fd, F_GETFL) & O_APPEND));
    assert(lseek(fd, 0, SEEK_SET) == 0 && write(fd, "d", 1) == 1);
    assert(fstat(fd, &info) == 0 && info.st_size == 5);
    close(fd);
    assert(openat(directory, "../escape", O_WRONLY | O_CREAT, 0600) < 0);
    assert(openat(directory, "test-中文.bin:stream", O_WRONLY | O_CREAT, 0600) < 0);
    assert(openat(directory, "alias.", O_WRONLY | O_CREAT, 0600) < 0);
    fd = openat(directory, "target", O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(fd >= 0);
    close(fd);
    assert(pxa_win_rename_noreplace(directory, "test-中文.bin", directory, "target") <
               0 &&
           errno == EEXIST);
    assert(renameat(directory, "test-中文.bin", directory, "renamed") == 0);
    DIR *stream = fdopendir(dup(directory));
    assert(stream);
    unsigned found = 0;
    struct dirent *entry;
    while ((entry = readdir(stream)))
        if (!strcmp(entry->d_name, "renamed"))
            found++;
    assert(found == 1 && errno == 0);
    closedir(stream);
    /* A directory handle remains usable after its pathname changes. */
    assert(renameat(parent, "child", parent, "moved") == 0);
    assert(fstatat(directory, "renamed", &info, AT_SYMLINK_NOFOLLOW) == 0 &&
           info.st_size == 5);
    close(directory);
    directory = openat(parent, "moved", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    assert(directory >= 0);
    char link[MAX_PATH + 32];
    snprintf(link, sizeof(link), "%s\\moved\\link", root);
    if (CreateSymbolicLinkA(link, "renamed", 2)) {
        assert(openat(directory, "link", O_WRONLY | O_TRUNC | O_NOFOLLOW) < 0 &&
               errno == ELOOP);
        assert(fstatat(directory, "renamed", &info, AT_SYMLINK_NOFOLLOW) == 0 &&
               info.st_size == 5);
        assert(fstatat(directory, "link", &info, AT_SYMLINK_NOFOLLOW) == 0 &&
               S_ISLNK(info.st_mode));
        assert(unlinkat(directory, "link", 0) == 0);
    } else {
        assert(GetLastError() == ERROR_PRIVILEGE_NOT_HELD);
        puts("Symlink creation unavailable: enable Windows Developer Mode for the "
             "reparse-point test");
    }
    assert(unlinkat(directory, "renamed", 0) == 0 &&
           unlinkat(directory, "target", 0) == 0);
    int lock = openat(directory, "lock", O_RDWR | O_CREAT, 0600);
    assert(lock >= 0);
    int other = openat(directory, "lock", O_RDWR);
    assert(other >= 0);
    assert(flock(lock, LOCK_EX | LOCK_NB) == 0);
    assert(flock(other, LOCK_EX | LOCK_NB) < 0 && errno == EAGAIN);
    assert(flock(lock, LOCK_UN) == 0 && flock(other, LOCK_EX | LOCK_NB) == 0);
    close(lock);
    close(other);
    assert(unlinkat(directory, "lock", 0) == 0);
    close(directory);
    assert(unlinkat(parent, "moved", AT_REMOVEDIR) == 0);
    close(parent);
    assert(RemoveDirectoryA(root));
    puts("Windows handle-relative filesystem: passed");
    return 0;
}
