#define _GNU_SOURCE
#include "pxa/posix/pxa_posix_fs.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void write_file(const char *path, size_t bytes) {
    int fd = open(path, O_CREAT | O_WRONLY | O_EXCL, 0600);
    assert(fd >= 0);
    for (size_t index = 0; index < bytes; ++index) assert(write(fd, "x", 1) == 1);
    assert(close(fd) == 0);
}

static void path_join(char *output, size_t capacity, const char *parent, const char *name) {
    int n = snprintf(output, capacity, "%s/%s", parent, name);
    assert(n > 0 && (size_t)n < capacity);
}

static void test_deep_usage(const char *base) {
    char root[512], path[512], file[512], outside[512], link[512];
    const size_t expected = 3u * 80u + 8u;
    path_join(root, sizeof(root), base, "usage"); assert(mkdir(root, 0700) == 0);
    path_join(file, sizeof(file), root, "head"); write_file(file, 3);
    for (unsigned branch = 0; branch < 3; ++branch) {
        int n = snprintf(path, sizeof(path), "%s/%c", root, 'a' + branch);
        assert(n > 0 && (size_t)n < sizeof(path));
        for (unsigned depth = 0; depth < 80; ++depth) {
            assert(mkdir(path, 0700) == 0);
            path_join(file, sizeof(file), path, "v"); write_file(file, 1);
            assert(strlen(path) + 2 < sizeof(path)); strcat(path, "/n");
        }
    }
    path_join(file, sizeof(file), root, "tail"); write_file(file, 5);
    path_join(file, sizeof(file), root, ".pxa-meta"); write_file(file, 31);
    path_join(outside, sizeof(outside), base, "outside"); write_file(outside, 7);
    path_join(path, sizeof(path), root, ".pxa-hidden"); assert(mkdir(path, 0700) == 0);
    path_join(link, sizeof(link), path, "link"); assert(symlink(outside, link) == 0);
    /* The scan must restore evicted ancestor cursors and count all branches
     * exactly once. Its bootstrap scratch cannot survive as occupied handles. */
    for (unsigned handles = 1; handles <= 8; handles += 7) {
        pxa_posix_fs_config_t config = {sizeof(config), root, expected, (uint16_t)handles};
        size_t bytes = pxa_posix_fs_workspace_size(&config);
        void *workspace = malloc(bytes); assert(workspace != NULL);
        for (unsigned retry = 0; retry < 3; ++retry) {
            pxa_posix_fs_t *fs = NULL; pxa_fs_backend_t backend;
            assert(pxa_posix_fs_init(workspace, bytes, &config, &fs, &backend) == PXA_STATUS_OK);
            void *resource = NULL; uint8_t kind = 0; size_t written = 99;
            const pxa_bytes_t name = {(const uint8_t *)"new", 3};
            assert(backend.open(backend.context, name, PXA_FS_OPEN_CREATE | PXA_FS_OPEN_WRITE,
                                &resource, &kind) == PXA_STATUS_OK);
            assert(backend.write(backend.context, resource, (const uint8_t *)"x", 1,
                                 &written) == PXA_STATUS_QUOTA_EXCEEDED);
            assert(written == 0);
            backend.close(backend.context, resource, kind);
            pxa_posix_fs_deinit(fs);
            path_join(file, sizeof(file), root, "new"); assert(unlink(file) == 0);
        }
        free(workspace);
    }
    assert(pxa_posix_fs_remove_tree(root) == PXA_STATUS_OK);
    assert(access(outside, F_OK) == 0);
    assert(pxa_posix_fs_remove_tree(root) == PXA_STATUS_NOT_FOUND);
}

static void test_path_boundary(const char *base) {
    char root[512], path[512], file[512];
    path_join(root, sizeof(root), base, "boundary"); assert(mkdir(root, 0700) == 0);
    strcpy(path, root);
    for (unsigned depth = 0; depth < 128; ++depth) {
        strcat(path, "/n"); assert(mkdir(path, 0700) == 0);
        if (depth == 126) {path_join(file, sizeof(file), path, "x"); write_file(file, 1);}
    }
    pxa_posix_fs_config_t config = {sizeof(config), root, 1, 1};
    size_t bytes = pxa_posix_fs_workspace_size(&config);
    void *workspace = malloc(bytes); pxa_posix_fs_t *fs = NULL; pxa_fs_backend_t backend;
    assert(workspace != NULL);
    assert(pxa_posix_fs_init(workspace, bytes, &config, &fs, &backend) == PXA_STATUS_OK);
    pxa_posix_fs_deinit(fs);
    path_join(file, sizeof(file), path, "x"); write_file(file, 1);
    assert(pxa_posix_fs_init(workspace, bytes, &config, &fs, &backend) == PXA_STATUS_LIMIT_EXCEEDED);
    assert(fs == NULL);
    free(workspace);
    assert(pxa_posix_fs_remove_tree(root) == PXA_STATUS_OK);
}

static void test_deep_link_denied(const char *base) {
    char root[512], path[512], link[512], outside[512];
    path_join(root, sizeof(root), base, "links"); assert(mkdir(root, 0700) == 0);
    strcpy(path, root);
    for (unsigned depth = 0; depth < 80; ++depth) {
        strcat(path, "/n"); assert(mkdir(path, 0700) == 0);
    }
    path_join(outside, sizeof(outside), base, "outside");
    path_join(link, sizeof(link), path, "link"); assert(symlink(outside, link) == 0);
    pxa_posix_fs_config_t config = {sizeof(config), root, UINT64_MAX, 1};
    size_t bytes = pxa_posix_fs_workspace_size(&config);
    void *workspace = malloc(bytes); assert(workspace != NULL);
    for (unsigned retry = 0; retry < 20; ++retry) {
        pxa_posix_fs_t *fs = NULL; pxa_fs_backend_t backend;
        assert(pxa_posix_fs_init(workspace, bytes, &config, &fs, &backend) == PXA_STATUS_DENIED);
        assert(fs == NULL);
    }
    free(workspace);
    assert(pxa_posix_fs_remove_tree(root) == PXA_STATUS_OK);
    assert(access(outside, F_OK) == 0);
}

static void test_entry_limit(const char *base) {
    char root[512], path[512], name[32];
    path_join(root, sizeof(root), base, "limit"); assert(mkdir(root, 0700) == 0);
    for (unsigned index = 0; index <= PXA_POSIX_FS_MAX_TREE_ENTRIES; ++index) {
        snprintf(name, sizeof(name), "f-%u", index);
        path_join(path, sizeof(path), root, name); write_file(path, 0);
    }
    pxa_posix_fs_config_t config = {sizeof(config), root, UINT64_MAX, 1};
    size_t bytes = pxa_posix_fs_workspace_size(&config);
    void *workspace = malloc(bytes); pxa_posix_fs_t *fs = NULL; pxa_fs_backend_t backend;
    assert(workspace != NULL);
    assert(pxa_posix_fs_init(workspace, bytes, &config, &fs, &backend) == PXA_STATUS_RESOURCE_LIMIT);
    assert(fs == NULL); free(workspace);
    assert(pxa_posix_fs_remove_tree(root) == PXA_STATUS_RESOURCE_LIMIT);
    assert(pxa_posix_fs_remove_tree(root) == PXA_STATUS_OK);
}

static void *run_cases(void *base) {
    test_deep_usage(base);
    test_path_boundary(base);
    test_deep_link_denied(base);
    test_entry_limit(base);
    return NULL;
}
static int cleanup(const char *path, const struct stat *st, int kind, struct FTW *state) {
    (void)st; (void)kind; (void)state;
    return remove(path);
}
int main(void) {
    char base[] = "/tmp/pxa-bounded-fs-XXXXXX";
    assert(mkdtemp(base) != NULL);
    fflush(NULL);
    pid_t child = fork(); assert(child >= 0);
    if (child == 0) {
        struct rlimit limit; assert(getrlimit(RLIMIT_NOFILE, &limit) == 0);
        limit.rlim_cur = 12; assert(setrlimit(RLIMIT_NOFILE, &limit) == 0);
        pthread_attr_t attr; pthread_t thread;
        assert(pthread_attr_init(&attr) == 0);
        assert(pthread_attr_setstacksize(&attr, 64u * 1024u) == 0);
        assert(pthread_create(&thread, &attr, run_cases, base) == 0);
        assert(pthread_join(thread, NULL) == 0);
        assert(pthread_attr_destroy(&attr) == 0);
        _exit(0);
    }
    int status; assert(waitpid(child, &status, 0) == child);
    assert(nftw(base, cleanup, 16, FTW_DEPTH | FTW_PHYS) == 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return 1;
    puts("Bounded private FS: 128 directory levels, 12-FD limit, 64-KiB stack, exact quota, cursor restoration, symlink denial, retry cleanup and entry/path bounds passed");
    return 0;
}
