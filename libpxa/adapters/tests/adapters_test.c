#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include "lz4.h"

#include "pxa/openssl/pxa_openssl.h"
#include "pxa/container.h"
#include "test_manifest.h"
#include "pxa/package.h"
#include "pxa/posix/pxa_posix_fs.h"
#include "pxa/posix/pxa_posix_installer.h"
#include "pxa/posix/pxa_posix_scheduler_store.h"
#include "pxa/posix/pxa_posix_storage.h"

static int failures = 0;
static size_t read_test_file(const char *path, uint8_t *bytes, size_t capacity);

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,            \
                    #condition);                                               \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static void check_status(const char *label, pxa_status_t actual,
                         pxa_status_t expected) {
    if (actual != expected) {
        fprintf(stderr, "FAIL %s: got %d expected %d\n", label, (int)actual,
                (int)expected);
        ++failures;
    }
}

static char *make_temp_dir(const char *prefix) {
    char pattern[128];
    char *copy;
    snprintf(pattern, sizeof(pattern), "/tmp/%s-XXXXXX", prefix);
    char *path = mkdtemp(pattern);
    if (path == NULL) {
        perror("mkdtemp");
        exit(1);
    }
    copy = strdup(path);
    if (copy == NULL) exit(1);
    return copy;
}

static void write_file(const char *path, const uint8_t *bytes, size_t size) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        perror("open");
        exit(1);
    }
    size_t offset = 0;
    while (offset < size) {
        ssize_t count = write(fd, bytes + offset, size - offset);
        if (count <= 0) {
            perror("write");
            exit(1);
        }
        offset += (size_t)count;
    }
    close(fd);
}

static void make_dirs(const char *path) {
    char buffer[512];
    size_t length = strlen(path);
    size_t index;
    if (length >= sizeof(buffer)) exit(1);
    memcpy(buffer, path, length + 1);
    for (index = 1; index < length; ++index) {
        if (buffer[index] != '/') continue;
        buffer[index] = '\0';
        if (mkdir(buffer, 0700) != 0 && errno != EEXIST) exit(1);
        buffer[index] = '/';
    }
    if (mkdir(buffer, 0700) != 0 && errno != EEXIST) exit(1);
}

static void test_openssl_sha256(void) {
    uint8_t output[32];
    static const uint8_t empty_expected[32] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4,
        0xc8, 0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b,
        0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55};
    static const uint8_t abc_expected[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
        0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
        0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    CHECK(pxa_openssl_sha256(NULL, 0, output) == PXA_STATUS_OK);
    CHECK(memcmp(output, empty_expected, 32) == 0);
    CHECK(pxa_openssl_sha256((const uint8_t *)"abc", 3, output) ==
          PXA_STATUS_OK);
    CHECK(memcmp(output, abc_expected, 32) == 0);
    CHECK(pxa_openssl_sha256(NULL, 1, output) == PXA_STATUS_INVALID_ARGUMENT);
    CHECK(pxa_openssl_sha256((const uint8_t *)"abc", 3, NULL) ==
          PXA_STATUS_INVALID_ARGUMENT);
    printf("test_openssl_sha256 OK\n");
}

static void test_fs_backend(void) {
    char *root = make_temp_dir("pxa-fs-test");
    pxa_posix_fs_config_t config;
    pxa_fs_backend_t backend;
    size_t workspace_size;
    void *workspace;
    pxa_posix_fs_t *fs = NULL;
    void *resource = NULL;
    uint8_t kind = 0;
    uint8_t buffer[64];
    size_t size = 0;
    uint64_t position = 0;
    pxa_fs_entry_t entry;
    uint8_t end = 0;
    memset(&config, 0, sizeof(config));
    config.struct_size = sizeof(config);
    config.root_path = root;
    config.quota_bytes = 100;
    config.max_open_resources = 8;
    workspace_size = pxa_posix_fs_workspace_size(&config);
    CHECK(workspace_size != 0);
    workspace = malloc(workspace_size);
    CHECK(workspace != NULL);
    check_status("fs init", pxa_posix_fs_init(workspace, workspace_size,
                                              &config, &fs, &backend),
                 PXA_STATUS_OK);
    CHECK(fs != NULL);

    check_status("mkdir",
                 backend.make_directory(backend.context,
                                        (pxa_bytes_t){(const uint8_t *)"sub",
                                                      3}),
                 PXA_STATUS_OK);
    check_status(
        "open create",
        backend.open(backend.context,
                     (pxa_bytes_t){(const uint8_t *)"file.txt", 8},
                     PXA_FS_OPEN_WRITE | PXA_FS_OPEN_CREATE |
                         PXA_FS_OPEN_EXCLUSIVE,
                     &resource, &kind),
        PXA_STATUS_OK);
    CHECK(kind == PXA_FS_KIND_REGULAR);
    check_status("write",
                 backend.write(backend.context, resource,
                               (const uint8_t *)"hello", 5, &size),
                 PXA_STATUS_OK);
    CHECK(size == 5);
    backend.close(backend.context, resource, kind);

    check_status(
        "open read",
        backend.open(backend.context,
                     (pxa_bytes_t){(const uint8_t *)"file.txt", 8},
                     PXA_FS_OPEN_READ, &resource, &kind),
        PXA_STATUS_OK);
    size = 0;
    check_status("read", backend.read(backend.context, resource, buffer,
                                      sizeof(buffer), &size),
                 PXA_STATUS_OK);
    CHECK(size == 5);
    CHECK(memcmp(buffer, "hello", 5) == 0);
    position = 0;
    check_status("seek", backend.seek(backend.context, resource, 2,
                                      PXA_FS_SEEK_START, &position),
                 PXA_STATUS_OK);
    CHECK(position == 2);
    size = 0;
    check_status("read after seek",
                 backend.read(backend.context, resource, buffer, sizeof(buffer),
                              &size),
                 PXA_STATUS_OK);
    CHECK(size == 3);
    CHECK(memcmp(buffer, "llo", 3) == 0);
    backend.close(backend.context, resource, kind);

    check_status("stat",
                 backend.stat(backend.context,
                              (pxa_bytes_t){(const uint8_t *)"file.txt", 8},
                              &entry),
                 PXA_STATUS_OK);
    CHECK(entry.kind == PXA_FS_KIND_REGULAR);
    CHECK(entry.size == 5);

    check_status("open dir",
                 backend.open(backend.context,
                              (pxa_bytes_t){(const uint8_t *)"sub", 3},
                              PXA_FS_OPEN_READ | PXA_FS_OPEN_DIRECTORY,
                              &resource, &kind),
                 PXA_STATUS_OK);
    CHECK(kind == PXA_FS_KIND_DIRECTORY);
    backend.close(backend.context, resource, kind);

    check_status(
        "open root dir",
        backend.open(backend.context,
                     (pxa_bytes_t){(const uint8_t *)"sub", 3},
                     PXA_FS_OPEN_READ | PXA_FS_OPEN_DIRECTORY, &resource,
                     &kind),
        PXA_STATUS_OK);
    end = 0;
    check_status("read_directory empty",
                 backend.read_directory(backend.context, resource, &entry,
                                        &end),
                 PXA_STATUS_OK);
    CHECK(end == 1);
    backend.close(backend.context, resource, kind);

    check_status("rename",
                 backend.rename(backend.context,
                                (pxa_bytes_t){(const uint8_t *)"file.txt", 8},
                                (pxa_bytes_t){(const uint8_t *)"moved.txt",
                                              8}),
                 PXA_STATUS_OK);
    check_status(
        "stat moved",
        backend.stat(backend.context,
                     (pxa_bytes_t){(const uint8_t *)"moved.txt", 8}, &entry),
        PXA_STATUS_OK);
    check_status("remove moved",
                 backend.remove(backend.context,
                                (pxa_bytes_t){(const uint8_t *)"moved.txt",
                                              8}),
                 PXA_STATUS_OK);
    check_status(
        "stat removed",
        backend.stat(backend.context,
                     (pxa_bytes_t){(const uint8_t *)"moved.txt", 8}, &entry),
        PXA_STATUS_NOT_FOUND);

    check_status(
        "open keep",
        backend.open(backend.context,
                     (pxa_bytes_t){(const uint8_t *)"keep.txt", 8},
                     PXA_FS_OPEN_WRITE | PXA_FS_OPEN_CREATE, &resource, &kind),
        PXA_STATUS_OK);
    check_status(
        "remove busy",
        backend.remove(backend.context,
                       (pxa_bytes_t){(const uint8_t *)"keep.txt", 8}),
        PXA_STATUS_BUSY);
    backend.close(backend.context, resource, kind);
    check_status(
        "remove after close",
        backend.remove(backend.context,
                       (pxa_bytes_t){(const uint8_t *)"keep.txt", 8}),
        PXA_STATUS_OK);

    check_status(
        "reject pxa name",
        backend.make_directory(backend.context,
                               (pxa_bytes_t){(const uint8_t *)".pxa-x", 6}),
        PXA_STATUS_INVALID_ARGUMENT);
    check_status(
        "reject dotdot",
        backend.stat(backend.context,
                     (pxa_bytes_t){(const uint8_t *)"../etc", 6}, &entry),
        PXA_STATUS_INVALID_ARGUMENT);

    check_status(
        "open big",
        backend.open(backend.context,
                     (pxa_bytes_t){(const uint8_t *)"big.bin", 7},
                     PXA_FS_OPEN_WRITE | PXA_FS_OPEN_CREATE, &resource, &kind),
        PXA_STATUS_OK);
    {
        uint8_t big[512];
        memset(big, 'x', sizeof(big));
        size = 0;
        check_status("quota write",
                     backend.write(backend.context, resource, big,
                                   sizeof(big), &size),
                     PXA_STATUS_QUOTA_EXCEEDED);
    }
    backend.close(backend.context, resource, kind);

    pxa_posix_fs_deinit(fs);
    free(workspace);
    free(root);
    printf("test_fs_backend OK\n");
}

static void test_fs_remove_tree(void) {
    char *root = make_temp_dir("pxa-remove-tree-test");
    char *outside = make_temp_dir("pxa-remove-tree-outside");
    char nested[512];
    char file[512];
    char link[512];
    char outside_file[512];
    CHECK(snprintf(nested, sizeof(nested), "%s/one/two", root) > 0);
    make_dirs(nested);
    CHECK(snprintf(file, sizeof(file), "%s/value", nested) > 0);
    write_file(file, (const uint8_t *)"inside", 6);
    CHECK(snprintf(outside_file, sizeof(outside_file), "%s/keep", outside) >
          0);
    write_file(outside_file, (const uint8_t *)"outside", 7);
    CHECK(snprintf(link, sizeof(link), "%s/link", root) > 0);
    CHECK(symlink(outside_file, link) == 0);
    check_status("remove nested tree", pxa_posix_fs_remove_tree(root),
                 PXA_STATUS_OK);
    CHECK(access(root, F_OK) != 0 && errno == ENOENT);
    CHECK(access(outside_file, F_OK) == 0);
    check_status("remove missing tree", pxa_posix_fs_remove_tree(root),
                 PXA_STATUS_NOT_FOUND);
    CHECK(unlink(outside_file) == 0);
    CHECK(rmdir(outside) == 0);
    free(root);
    free(outside);
    printf("test_fs_remove_tree OK\n");
}

typedef struct {
    char keys[8][32];
    size_t sizes[8];
    size_t count;
} storage_collector_t;

static pxa_status_t collect_key(void *context, pxa_bytes_t key) {
    storage_collector_t *collector = (storage_collector_t *)context;
    if (collector->count >= 8 || key.size >= 32) {
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    memcpy(collector->keys[collector->count], key.data, key.size);
    collector->sizes[collector->count] = key.size;
    ++collector->count;
    return PXA_STATUS_OK;
}

static void test_storage_backend(void) {
    char *root = make_temp_dir("pxa-storage-test");
    pxa_posix_storage_config_t config;
    pxa_storage_backend_t backend;
    size_t workspace_size;
    void *workspace;
    pxa_posix_storage_t *storage = NULL;
    uint8_t buffer[64];
    size_t size = 0;
    storage_collector_t collector;
    char slot_a[512];
    char slot_b[512];
    static const uint8_t corrupt[] = {0};
    static const uint8_t oversized[] = "012345678";
    memset(&config, 0, sizeof(config));
    config.struct_size = sizeof(config);
    config.root_path = root;
    config.max_keys = 16;
    config.max_value_bytes = 16;
    config.quota_bytes = 16;
    workspace_size = pxa_posix_storage_workspace_size(&config);
    CHECK(workspace_size != 0);
    workspace = malloc(workspace_size);
    CHECK(workspace != NULL);
    check_status("storage init",
                 pxa_posix_storage_init(workspace, workspace_size, &config,
                                        &storage, &backend),
                 PXA_STATUS_OK);
    CHECK(storage != NULL);

    check_status("set alpha",
                 backend.set(backend.context,
                             (pxa_bytes_t){(const uint8_t *)"alpha", 5},
                             (pxa_bytes_t){(const uint8_t *)"one", 3}),
                 PXA_STATUS_OK);
    check_status("set beta",
                 backend.set(backend.context,
                             (pxa_bytes_t){(const uint8_t *)"beta", 4},
                             (pxa_bytes_t){(const uint8_t *)"two", 3}),
                 PXA_STATUS_OK);
    check_status("set gamma",
                 backend.set(backend.context,
                             (pxa_bytes_t){(const uint8_t *)"gamma", 5},
                             (pxa_bytes_t){(const uint8_t *)"three", 5}),
                 PXA_STATUS_OK);
    size = 0;
    check_status("get alpha",
                 backend.get(backend.context,
                             (pxa_bytes_t){(const uint8_t *)"alpha", 5},
                             buffer, sizeof(buffer), &size),
                 PXA_STATUS_OK);
    CHECK(size == 3);
    CHECK(memcmp(buffer, "one", 3) == 0);
    check_status(
        "get missing",
        backend.get(backend.context,
                    (pxa_bytes_t){(const uint8_t *)"missing", 7}, buffer,
                    sizeof(buffer), &size),
        PXA_STATUS_NOT_FOUND);

    memset(&collector, 0, sizeof(collector));
    check_status("list all",
                 backend.list(backend.context, (pxa_bytes_t){NULL, 0},
                              collect_key, &collector),
                 PXA_STATUS_OK);
    CHECK(collector.count == 3);
    CHECK(memcmp(collector.keys[0], "alpha", 5) == 0);
    CHECK(memcmp(collector.keys[1], "beta", 4) == 0);
    CHECK(memcmp(collector.keys[2], "gamma", 5) == 0);

    memset(&collector, 0, sizeof(collector));
    check_status("list after cursor",
                 backend.list(backend.context,
                              (pxa_bytes_t){(const uint8_t *)"beta", 4},
                              collect_key, &collector),
                 PXA_STATUS_OK);
    CHECK(collector.count == 1);
    CHECK(memcmp(collector.keys[0], "gamma", 5) == 0);

    CHECK(snprintf(slot_a, sizeof(slot_a), "%s/.pxa-kv-a", root) > 0);
    CHECK(snprintf(slot_b, sizeof(slot_b), "%s/.pxa-kv-b", root) > 0);
    {
        uint8_t before_a[256], before_b[256], after[256];
        size_t a_size = read_test_file(slot_a, before_a, sizeof(before_a));
        size_t b_size = read_test_file(slot_b, before_b, sizeof(before_b));
        check_status("unchanged value does not persist a new generation",
            backend.set(backend.context, (pxa_bytes_t){(const uint8_t *)"alpha", 5},
                        (pxa_bytes_t){(const uint8_t *)"one", 3}), PXA_STATUS_OK);
        CHECK(read_test_file(slot_a, after, sizeof(after)) == a_size);
        CHECK(memcmp(after, before_a, a_size) == 0);
        CHECK(read_test_file(slot_b, after, sizeof(after)) == b_size);
        CHECK(memcmp(after, before_b, b_size) == 0);
    }
    pxa_posix_storage_deinit(storage);
    storage = NULL;
    write_file(slot_b, corrupt, sizeof(corrupt));
    check_status("storage recovers previous slot",
                 pxa_posix_storage_init(workspace, workspace_size, &config,
                                        &storage, &backend),
                 PXA_STATUS_OK);
    check_status("recovered slot omits newest key",
                 backend.get(backend.context,
                             (pxa_bytes_t){(const uint8_t *)"gamma", 5},
                             buffer, sizeof(buffer), &size),
                 PXA_STATUS_NOT_FOUND);
    check_status("restore gamma",
                 backend.set(backend.context,
                             (pxa_bytes_t){(const uint8_t *)"gamma", 5},
                             (pxa_bytes_t){(const uint8_t *)"three", 5}),
                 PXA_STATUS_OK);

    check_status(
        "remove alpha",
        backend.remove(backend.context,
                       (pxa_bytes_t){(const uint8_t *)"alpha", 5}),
        PXA_STATUS_OK);
    check_status(
        "get removed",
        backend.get(backend.context,
                    (pxa_bytes_t){(const uint8_t *)"alpha", 5}, buffer,
                    sizeof(buffer), &size),
        PXA_STATUS_NOT_FOUND);

    check_status("storage enforces aggregate quota",
                 backend.set(backend.context,
                             (pxa_bytes_t){(const uint8_t *)"delta", 5},
                             (pxa_bytes_t){oversized,
                                           sizeof(oversized) - 1u}),
                 PXA_STATUS_QUOTA_EXCEEDED);
    check_status("quota failure preserves snapshot",
                 backend.get(backend.context,
                             (pxa_bytes_t){(const uint8_t *)"beta", 4},
                             buffer, sizeof(buffer), &size),
                 PXA_STATUS_OK);
    CHECK(size == 3 && memcmp(buffer, "two", 3) == 0);

    pxa_posix_storage_deinit(storage);
    storage = NULL;
    check_status("storage reopen",
                 pxa_posix_storage_init(workspace, workspace_size, &config,
                                        &storage, &backend),
                 PXA_STATUS_OK);
    check_status("storage reopen value",
                 backend.get(backend.context,
                             (pxa_bytes_t){(const uint8_t *)"gamma", 5},
                             buffer, sizeof(buffer), &size),
                 PXA_STATUS_OK);
    CHECK(size == 5 && memcmp(buffer, "three", 5) == 0);
    pxa_posix_storage_deinit(storage);
    storage = NULL;
    write_file(slot_a, corrupt, sizeof(corrupt));
    write_file(slot_b, corrupt, sizeof(corrupt));
    check_status("storage rejects two corrupt slots",
                 pxa_posix_storage_init(workspace, workspace_size, &config,
                                        &storage, &backend),
                 PXA_STATUS_INVALID_ARGUMENT);
    CHECK(storage == NULL);
    check_status("remove storage test root", pxa_posix_fs_remove_tree(root),
                 PXA_STATUS_OK);
    free(workspace);
    free(root);
    printf("test_storage_backend OK\n");
}

static void test_storage_rejects_hard_link_snapshot(void) {
    char* root = make_temp_dir("pxa-storage-link-test");
    char* outside = make_temp_dir("pxa-storage-link-outside");
    char slot[512];
    char outside_file[512];
    pxa_posix_storage_config_t config;
    pxa_storage_backend_t backend;
    pxa_posix_storage_t* storage = NULL;
    void* workspace;
    size_t workspace_size;
    uint8_t contents[16] = {0};
    int descriptor;
    ssize_t count;
    CHECK(snprintf(slot, sizeof(slot), "%s/.pxa-kv-b", root) > 0);
    CHECK(snprintf(outside_file, sizeof(outside_file), "%s/value", outside) > 0);
    write_file(outside_file, (const uint8_t*)"preserve", 8);
    memset(&config, 0, sizeof(config));
    config.struct_size = sizeof(config);
    config.root_path = root;
    config.max_keys = 4;
    config.max_value_bytes = 16;
    config.quota_bytes = 32;
    workspace_size = pxa_posix_storage_workspace_size(&config);
    workspace = malloc(workspace_size);
    CHECK(workspace != NULL);
    check_status("hard-link storage init",
                 pxa_posix_storage_init(workspace, workspace_size, &config, &storage, &backend),
                 PXA_STATUS_OK);
    CHECK(link(outside_file, slot) == 0);
    check_status("hard-link snapshot denied",
                 backend.set(backend.context, (pxa_bytes_t){(const uint8_t*)"alpha", 5},
                             (pxa_bytes_t){(const uint8_t*)"changed", 7}),
                 PXA_STATUS_DENIED);
    descriptor = open(outside_file, O_RDONLY | O_CLOEXEC);
    CHECK(descriptor >= 0);
    count = descriptor < 0 ? -1 : read(descriptor, contents, sizeof(contents));
    if (descriptor >= 0)
        close(descriptor);
    CHECK(count == 8 && memcmp(contents, "preserve", 8) == 0);
    pxa_posix_storage_deinit(storage);
    check_status("remove hard-link storage root", pxa_posix_fs_remove_tree(root), PXA_STATUS_OK);
    check_status("remove hard-link outside root", pxa_posix_fs_remove_tree(outside), PXA_STATUS_OK);
    free(workspace);
    free(root);
    free(outside);
    printf("test_storage_rejects_hard_link_snapshot OK\n");
}

static void test_scheduler_store(void) {
    static const uint8_t key[] = "work.v1";
    char* root = make_temp_dir("pxa-scheduler-store-test");
    pxa_posix_storage_config_t storage_config;
    pxa_posix_scheduler_store_config_t config;
    pxa_storage_backend_t backend;
    pxa_posix_storage_t* storage = NULL;
    pxa_posix_scheduler_store_t* store = NULL;
    pxa_scheduler_store_t scheduler_store;
    pxa_scheduler_entry_t saved[2];
    pxa_scheduler_entry_t loaded[2];
    void* storage_workspace;
    void* scheduler_workspace;
    size_t scheduler_workspace_size;
    size_t count = 0;
    static const uint8_t corrupt[] = {0x50, 0x58, 0x53};

    memset(&storage_config, 0, sizeof(storage_config));
    storage_config.struct_size = sizeof(storage_config);
    storage_config.root_path = root;
    storage_config.max_keys = 4;
    storage_config.max_value_bytes = 2048;
    storage_config.quota_bytes = 8192;
    storage_workspace = malloc(
        pxa_posix_storage_workspace_size(&storage_config));
    CHECK(storage_workspace != NULL);
    check_status("scheduler store storage init",
                 pxa_posix_storage_init(
                     storage_workspace,
                     pxa_posix_storage_workspace_size(&storage_config),
                     &storage_config, &storage, &backend),
                 PXA_STATUS_OK);

    memset(&config, 0, sizeof(config));
    config.struct_size = sizeof(config);
    config.backend = backend;
    config.key = (pxa_bytes_t){key, sizeof(key) - 1};
    config.max_entries = 2;
    config.epoch = UINT64_C(0x1020304050607080);
    scheduler_workspace_size = pxa_posix_scheduler_store_workspace_size(&config);
    CHECK(scheduler_workspace_size != 0);
    scheduler_workspace = malloc(scheduler_workspace_size);
    CHECK(scheduler_workspace != NULL);
    check_status("scheduler store init",
                 pxa_posix_scheduler_store_init(
                     scheduler_workspace, scheduler_workspace_size, &config,
                     &store, &scheduler_store),
                 PXA_STATUS_OK);

    memset(saved, 0, sizeof(saved));
    saved[0].id = 7;
    saved[0].due_at_ms = UINT64_C(1234567);
    saved[0].max_execution_ms = 4000;
    saved[0].component_id_size = sizeof("job.alpha") - 1;
    memcpy(saved[0].component_id, "job.alpha", saved[0].component_id_size);
    saved[0].attempt = 1;
    saved[0].max_attempts = 1;
    saved[1].id = 9;
    saved[1].due_at_ms = UINT64_C(7654321);
    saved[1].max_execution_ms = 5000;
    saved[1].component_id_size = sizeof("job.beta") - 1;
    memcpy(saved[1].component_id, "job.beta", saved[1].component_id_size);
    saved[1].retry_delay_ms = 2000;
    saved[1].attempt = 2;
    saved[1].max_attempts = 3;
    saved[1].input_size = 3;
    memcpy(saved[1].input, "abc", saved[1].input_size);
    check_status("scheduler store save",
                 scheduler_store.save(scheduler_store.context, saved, 2),
                 PXA_STATUS_OK);

    pxa_posix_scheduler_store_deinit(store);
    free(scheduler_workspace);
    scheduler_workspace = malloc(scheduler_workspace_size);
    CHECK(scheduler_workspace != NULL);
    store = NULL;
    memset(&scheduler_store, 0, sizeof(scheduler_store));
    check_status("scheduler store reinit",
                 pxa_posix_scheduler_store_init(
                     scheduler_workspace, scheduler_workspace_size, &config,
                     &store, &scheduler_store),
                 PXA_STATUS_OK);
    check_status("scheduler store small load",
                 scheduler_store.load(scheduler_store.context, loaded, 1,
                                      &count),
                 PXA_STATUS_RESOURCE_LIMIT);
    CHECK(count == 0);
    check_status("scheduler store load",
                 scheduler_store.load(scheduler_store.context, loaded, 2,
                                      &count),
                 PXA_STATUS_OK);
    CHECK(count == 2 && loaded[0].id == saved[0].id &&
          loaded[0].due_at_ms == saved[0].due_at_ms &&
          loaded[0].max_execution_ms == saved[0].max_execution_ms &&
          loaded[0].component_id_size == saved[0].component_id_size &&
          memcmp(loaded[0].component_id, saved[0].component_id,
                 saved[0].component_id_size) == 0 &&
          loaded[1].id == saved[1].id &&
          loaded[1].due_at_ms == saved[1].due_at_ms &&
          loaded[1].max_execution_ms == saved[1].max_execution_ms &&
          loaded[1].component_id_size == saved[1].component_id_size &&
          memcmp(loaded[1].component_id, saved[1].component_id,
                 saved[1].component_id_size) == 0 &&
          loaded[1].retry_delay_ms == saved[1].retry_delay_ms &&
          loaded[1].attempt == saved[1].attempt &&
          loaded[1].max_attempts == saved[1].max_attempts &&
          loaded[1].input_size == saved[1].input_size &&
          memcmp(loaded[1].input, saved[1].input,
                 saved[1].input_size) == 0);

    check_status("scheduler store clears",
                 scheduler_store.save(scheduler_store.context, NULL, 0),
                 PXA_STATUS_OK);
    check_status("scheduler store load clear",
                 scheduler_store.load(scheduler_store.context, loaded, 2,
                                      &count),
                 PXA_STATUS_OK);
    CHECK(count == 0);

    check_status("scheduler store writes corrupt value",
                 backend.set(backend.context, config.key,
                             (pxa_bytes_t){corrupt, sizeof(corrupt)}),
                 PXA_STATUS_OK);
    check_status("scheduler store rejects corruption",
                 scheduler_store.load(scheduler_store.context, loaded, 2,
                                      &count),
                 PXA_STATUS_DENIED);

    pxa_posix_scheduler_store_deinit(store);
    pxa_posix_storage_deinit(storage);
    free(scheduler_workspace);
    free(storage_workspace);
    free(root);
    printf("test_scheduler_store OK\n");
}

typedef struct {
    uint8_t key_id[32];
    EVP_PKEY *key;
} test_signer_t;

static void generate_signer(test_signer_t *signer) {
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    CHECK(ctx != NULL);
    CHECK(EVP_PKEY_keygen_init(ctx) == 1);
    CHECK(EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1) ==
          1);
    CHECK(EVP_PKEY_keygen(ctx, &signer->key) == 1);
    EVP_PKEY_CTX_free(ctx);
    CHECK(signer->key != NULL);
    {
        int length = i2d_PUBKEY(signer->key, NULL);
        uint8_t *der = malloc((size_t)length);
        uint8_t *cursor = der;
        CHECK(der != NULL);
        CHECK(i2d_PUBKEY(signer->key, &cursor) == length);
        SHA256(der, (size_t)length, signer->key_id);
        free(der);
    }
}

static void sign_message(EVP_PKEY *key, const uint8_t *domain,
                         size_t domain_size, const uint8_t *data,
                         size_t data_size, uint8_t signature[64]) {
    uint8_t message[PXA_PACKAGE_TEST_MANIFEST_BYTES + 32];
    size_t message_size = 0;
    EVP_MD_CTX *md;
    uint8_t *der = NULL;
    size_t der_size = 0;
    ECDSA_SIG *ecsig;
    const unsigned char *cursor;
    BIGNUM *r = NULL;
    BIGNUM *s = NULL;
    BIGNUM *order = NULL;
    BIGNUM *half = NULL;
    BN_CTX *bn_ctx;
    CHECK(domain_size + 1 + data_size <= sizeof(message));
    memcpy(message, domain, domain_size);
    message_size += domain_size;
    message[message_size++] = 0;
    memcpy(message + message_size, data, data_size);
    message_size += data_size;
    md = EVP_MD_CTX_new();
    CHECK(md != NULL);
    CHECK(EVP_DigestSignInit(md, NULL, EVP_sha256(), NULL, key) == 1);
    CHECK(EVP_DigestSign(md, NULL, &der_size, message, message_size) == 1);
    der = malloc(der_size);
    CHECK(der != NULL);
    CHECK(EVP_DigestSign(md, der, &der_size, message, message_size) == 1);
    cursor = der;
    ecsig = d2i_ECDSA_SIG(NULL, &cursor, (long)der_size);
    CHECK(ecsig != NULL);
    r = BN_dup(ECDSA_SIG_get0_r(ecsig));
    s = BN_dup(ECDSA_SIG_get0_s(ecsig));
    bn_ctx = BN_CTX_new();
    order = BN_new();
    half = BN_new();
    CHECK(bn_ctx != NULL && order != NULL && half != NULL);
    CHECK(EVP_PKEY_get_bn_param(key, OSSL_PKEY_PARAM_EC_ORDER, &order) == 1);
    CHECK(BN_rshift1(half, order) == 1);
    if (BN_cmp(s, half) > 0) {
        CHECK(BN_sub(s, order, s) == 1);
    }
    CHECK(BN_bn2binpad(r, signature, 32) == 32);
    CHECK(BN_bn2binpad(s, signature + 32, 32) == 32);
    BN_free(half);
    BN_free(order);
    BN_CTX_free(bn_ctx);
    BN_free(r);
    BN_free(s);
    ECDSA_SIG_free(ecsig);
    free(der);
    EVP_MD_CTX_free(md);
}

static void sign_manifest(EVP_PKEY *key, const uint8_t *manifest,
                          size_t manifest_size, uint8_t signature[64]) {
    static const uint8_t domain[] = "PXA-PACKAGE-MANIFEST";
    sign_message(key, domain, sizeof(domain) - 1, manifest, manifest_size,
                 signature);
}

static void build_package(const char *package_dir, const test_signer_t *signer,
                          const char *app_id, const char *version,
                          const char *artifact_name,
                          const uint8_t *artifact_bytes,
                          size_t artifact_size) {
    uint8_t manifest[PXA_PACKAGE_TEST_MANIFEST_BYTES];
    uint8_t signature_file[108];
    uint8_t artifact_sha[32];
    size_t manifest_size;
    char path[512];
    make_dirs(package_dir);
    SHA256(artifact_bytes, artifact_size, artifact_sha);
    manifest_size = pxa_test_encode_manifest(manifest, sizeof(manifest), signer->key_id,
                                    app_id, version, artifact_name,
                                    (uint64_t)artifact_size, artifact_sha);
    CHECK(manifest_size != 0);
    sign_manifest(signer->key, manifest, manifest_size, signature_file + 44);
    memcpy(signature_file, "PXAS", 4);
    pxa_write_u16(signature_file + 4,
                  PXA_PACKAGE_SIGNATURE_FORMAT_VERSION);
    pxa_write_u16(signature_file + 6, 1);
    memcpy(signature_file + 8, signer->key_id, 32);
    pxa_write_u16(signature_file + 40, 64);
    pxa_write_u16(signature_file + 42, 0);
    snprintf(path, sizeof(path), "%s/manifest.pxm", package_dir);
    write_file(path, manifest, manifest_size);
    snprintf(path, sizeof(path), "%s/signature.pxs", package_dir);
    write_file(path, signature_file, sizeof(signature_file));
    snprintf(path, sizeof(path), "%s/artifacts", package_dir);
    make_dirs(path);
    snprintf(path, sizeof(path), "%s/%s", package_dir, artifact_name);
    write_file(path, artifact_bytes, artifact_size);
}

static size_t read_test_file(const char *path, uint8_t *bytes,
                             size_t capacity) {
    int file = open(path, O_RDONLY);
    ssize_t count;
    if (file < 0) exit(1);
    count = read(file, bytes, capacity);
    if (count < 0 || read(file, bytes, 1) != 0) exit(1);
    close(file);
    return (size_t)count;
}

static void build_test_container(const char *container_path,
                                 const char *package_dir,
                                 const test_signer_t *signer,
                                 const uint8_t *artifact,
                                 size_t artifact_size, uint16_t codec) {
    static const uint8_t domain[] = "PXA-PACKAGE-CONTAINER-DIGEST";
    uint8_t manifest[PXA_PACKAGE_TEST_MANIFEST_BYTES];
    uint8_t package_signature[108];
    uint8_t container_signature[108];
    uint8_t header[64];
    uint8_t file_record[16];
    uint8_t chunk_header[4];
    uint8_t compressed[PXA_CONTAINER_CHUNK_BYTES];
    uint8_t digest[32];
    char path[512];
    size_t manifest_size;
    const uint8_t *stored = artifact;
    size_t stored_size = artifact_size;
    size_t payload_size;
    pxa_openssl_sha256_stream_t sha;

    CHECK(artifact_size <= PXA_CONTAINER_CHUNK_BYTES);
    if (codec == PXA_CONTAINER_CODEC_LZ4 && artifact_size != 0) {
        int compressed_size = LZ4_compress_default(
            (const char *)artifact, (char *)compressed, (int)artifact_size,
            (int)sizeof(compressed));
        CHECK(compressed_size > 0);
        if ((size_t)compressed_size < artifact_size) {
            stored = compressed;
            stored_size = (size_t)compressed_size;
        }
    }
    payload_size = 16 + (artifact_size == 0 ? 0 : 4 + stored_size);
    snprintf(path, sizeof(path), "%s/manifest.pxm", package_dir);
    manifest_size = read_test_file(path, manifest, sizeof(manifest));
    snprintf(path, sizeof(path), "%s/signature.pxs", package_dir);
    CHECK(read_test_file(path, package_signature,
                         sizeof(package_signature)) ==
          sizeof(package_signature));
    memset(header, 0, sizeof(header));
    memcpy(header, "PXAC", 4);
    pxa_write_u16(header + 4, PXA_CONTAINER_FORMAT_MAJOR);
    pxa_write_u16(header + 6, PXA_CONTAINER_FORMAT_MINOR);
    pxa_write_u16(header + 8, 64);
    pxa_write_u16(header + 12, codec);
    pxa_write_u16(header + 14, 12);
    pxa_write_u32(header + 16, (uint32_t)manifest_size);
    pxa_write_u32(header + 20, 108);
    pxa_write_u32(header + 24, 108);
    pxa_write_u32(header + 28, 1);
    pxa_write_u64(header + 32, payload_size);
    pxa_write_u64(header + 40, artifact_size);
    pxa_write_u64(header + 48,
                  64 + manifest_size + 108 + 108 + payload_size);
    pxa_write_u32(header + 60, pxa_container_crc32(header, 60));
    memset(file_record, 0, sizeof(file_record));
    pxa_write_u32(file_record + 4, artifact_size == 0 ? 0 : 1);
    pxa_write_u64(file_record + 8, artifact_size == 0 ? 0 : 4 + stored_size);
    pxa_write_u16(chunk_header, (uint16_t)stored_size);
    pxa_write_u16(chunk_header + 2, (uint16_t)artifact_size);
    memset(&sha, 0, sizeof(sha));
    CHECK(pxa_openssl_sha256_stream_begin(&sha) == PXA_STATUS_OK);
    CHECK(pxa_openssl_sha256_stream_update(&sha, header, sizeof(header)) ==
          PXA_STATUS_OK);
    CHECK(pxa_openssl_sha256_stream_update(&sha, manifest, manifest_size) ==
          PXA_STATUS_OK);
    CHECK(pxa_openssl_sha256_stream_update(
              &sha, package_signature, sizeof(package_signature)) ==
          PXA_STATUS_OK);
    CHECK(pxa_openssl_sha256_stream_update(
              &sha, file_record, sizeof(file_record)) == PXA_STATUS_OK);
    if (artifact_size != 0) {
        CHECK(pxa_openssl_sha256_stream_update(
                  &sha, chunk_header, sizeof(chunk_header)) == PXA_STATUS_OK);
        CHECK(pxa_openssl_sha256_stream_update(&sha, stored,
                                               stored_size) ==
              PXA_STATUS_OK);
    }
    CHECK(pxa_openssl_sha256_stream_finish(&sha, digest) == PXA_STATUS_OK);
    memset(container_signature, 0, sizeof(container_signature));
    memcpy(container_signature, "PXCS", 4);
    pxa_write_u16(container_signature + 4,
                  PXA_CONTAINER_SIGNATURE_FORMAT_VERSION);
    pxa_write_u16(container_signature + 6, 1);
    memcpy(container_signature + 8, signer->key_id, 32);
    pxa_write_u16(container_signature + 40, 64);
    sign_message(signer->key, domain, sizeof(domain) - 1, digest,
                 sizeof(digest), container_signature + 44);
    {
        int output = open(container_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        CHECK(output >= 0);
        CHECK(write(output, header, sizeof(header)) == (ssize_t)sizeof(header));
        CHECK(write(output, manifest, manifest_size) == (ssize_t)manifest_size);
        CHECK(write(output, package_signature, sizeof(package_signature)) ==
              (ssize_t)sizeof(package_signature));
        CHECK(write(output, container_signature, sizeof(container_signature)) ==
              (ssize_t)sizeof(container_signature));
        CHECK(write(output, file_record, sizeof(file_record)) ==
              (ssize_t)sizeof(file_record));
        if (artifact_size != 0) {
            CHECK(write(output, chunk_header, sizeof(chunk_header)) ==
                  (ssize_t)sizeof(chunk_header));
            CHECK(write(output, stored, stored_size) ==
                  (ssize_t)stored_size);
        }
        close(output);
    }
}

typedef struct {
    uint8_t fail_checkpoint;
    uint8_t enabled;
} checkpoint_state_t;

static pxa_status_t checkpoint_fail(void *context,
                                    pxa_transaction_checkpoint_t checkpoint) {
    checkpoint_state_t *state = (checkpoint_state_t *)context;
    if (state->enabled && checkpoint == state->fail_checkpoint) {
        return PXA_STATUS_CANCELLED;
    }
    return PXA_STATUS_OK;
}

static void test_installer(void) {
    test_signer_t signer;
    char *storage_root = make_temp_dir("pxa-installer-storage");
    char *package_v1 = make_temp_dir("pxa-installer-src-v1");
    char *package_v2 = make_temp_dir("pxa-installer-src-v2");
    char *package_v3 = make_temp_dir("pxa-installer-src-v3");
    char *package_lz4 = make_temp_dir("pxa-installer-src-lz4");
    static const uint8_t artifact_v1[] = "portable wasm content v1";
    static const uint8_t artifact_v2[] = "portable wasm content v2";
    static const uint8_t artifact_v3[] = "portable wasm content v3";
    pxa_openssl_publisher_key_t publisher_key;
    pxa_openssl_trust_t trust;
    pxa_posix_installer_config_t config;
    pxa_posix_installer_t *installer = NULL;
    size_t workspace_size;
    void *workspace;
    void *manifest_workspace;
    uint8_t encoded[PXA_PACKAGE_TEST_MANIFEST_BYTES];
    char root_path[512];
    pxa_posix_installer_result_t result;
    pxa_package_manifest_t *manifest = NULL;
    pxa_posix_install_disposition_t disposition =
        PXA_POSIX_INSTALL_INSTALLED;
    pxa_posix_installer_identity_t identity;
    checkpoint_state_t checkpoint_state;
    pxa_slot_faults_t faults;
    uint8_t key_der[256];
    int key_length;
    uint8_t *cursor;
    size_t manifest_size = 0;
    size_t queried_manifest_size = 0;
    uint8_t buffer[64];
    char file_path[600];
    struct stat manifest_metadata;
    char container_v1[] = "/tmp/pxa-installer-v1-XXXXXX";
    char container_lz4[] = "/tmp/pxa-installer-lz4-XXXXXX";
    uint8_t artifact_lz4[1024];
    size_t read_size = 0;
    int fd;

    generate_signer(&signer);
    memset(artifact_lz4, 'A', sizeof(artifact_lz4));
    build_package(package_v1, &signer, "com.example.testapp", "0.1.0",
                  "artifacts/Main.wasm", artifact_v1, sizeof(artifact_v1) - 1);
    build_package(package_v2, &signer, "com.example.testapp", "0.2.0",
                  "artifacts/main.wasm", artifact_v2, sizeof(artifact_v2) - 1);
    build_package(package_v3, &signer, "com.example.testapp", "0.3.0",
                  "artifacts/main.wasm", artifact_v3, sizeof(artifact_v3) - 1);
    build_package(package_lz4, &signer, "com.example.testapp", "0.4.0",
                  "artifacts/main.wasm", artifact_lz4, sizeof(artifact_lz4));
    fd = mkstemp(container_v1);
    CHECK(fd >= 0);
    close(fd);
    build_test_container(container_v1, package_v1, &signer, artifact_v1,
                         sizeof(artifact_v1) - 1,
                         PXA_CONTAINER_CODEC_STORE);
    fd = mkstemp(container_lz4);
    CHECK(fd >= 0);
    close(fd);
    build_test_container(container_lz4, package_lz4, &signer, artifact_lz4,
                         sizeof(artifact_lz4), PXA_CONTAINER_CODEC_LZ4);

    key_length = i2d_PUBKEY(signer.key, NULL);
    CHECK(key_length > 0 && key_length <= (int)sizeof(key_der));
    cursor = key_der;
    CHECK(i2d_PUBKEY(signer.key, &cursor) == key_length);
    publisher_key.spki = key_der;
    publisher_key.spki_size = (size_t)key_length;
    memset(&trust, 0, sizeof(trust));
    trust.struct_size = sizeof(trust);
    trust.keys = &publisher_key;
    trust.key_count = 1;

    memset(&config, 0, sizeof(config));
    config.struct_size = sizeof(config);
    config.storage_root = storage_root;
    config.trust = trust;
    pxa_package_limits_init(&config.limits);
    workspace_size = pxa_posix_installer_workspace_size(&config);
    CHECK(workspace_size != 0);
    {
        pxa_posix_installer_config_t smaller_limits = config;
        smaller_limits.limits.max_components = 1;
        smaller_limits.limits.max_artifacts = 1;
        smaller_limits.limits.max_services = 0;
        smaller_limits.limits.max_files = 1;
        smaller_limits.limits.max_permissions = 0;
        smaller_limits.limits.max_ipc_endpoints = 0;
        CHECK(pxa_posix_installer_workspace_size(&smaller_limits) ==
              workspace_size);
    }
    {
        uint8_t *alignment_workspace =
            (uint8_t *)malloc(workspace_size + 16u);
        size_t offset;
        CHECK(alignment_workspace != NULL);
        for (offset = 0; offset < 16u; ++offset) {
            pxa_posix_installer_t *alignment_installer = NULL;
            check_status("installer init at arbitrary alignment",
                         pxa_posix_installer_init(
                             alignment_workspace + offset, workspace_size,
                             &config, &alignment_installer),
                         PXA_STATUS_OK);
            CHECK(alignment_installer != NULL);
            pxa_posix_installer_deinit(alignment_installer);
        }
        free(alignment_workspace);
    }
    workspace = malloc(workspace_size);
    CHECK(workspace != NULL);
    check_status("installer init",
                 pxa_posix_installer_init(workspace, workspace_size, &config,
                                          &installer),
                 PXA_STATUS_OK);
    CHECK(installer != NULL);
    manifest_workspace =
        malloc(pxa_package_manifest_workspace_size(&config.limits));
    CHECK(manifest_workspace != NULL);

    memset(&result, 0, sizeof(result));
    result.struct_size = sizeof(result);
    result.manifest_workspace = manifest_workspace;
    result.manifest_workspace_size =
        pxa_package_manifest_workspace_size(&config.limits);
    result.encoded = encoded;
    result.encoded_capacity = sizeof(encoded);
    result.manifest = &manifest;
    result.root = root_path;
    result.root_capacity = sizeof(root_path);

    snprintf(file_path, sizeof(file_path), "%s/manifest.pxm", package_v1);
    CHECK(stat(file_path, &manifest_metadata) == 0 &&
          manifest_metadata.st_size > 1 &&
          (uint64_t)manifest_metadata.st_size <=
              PXA_PACKAGE_TEST_MANIFEST_BYTES);
    manifest_size = (size_t)manifest_metadata.st_size;
    check_status("query directory manifest size",
                 pxa_posix_installer_source_manifest_size(
                     installer, package_v1, &queried_manifest_size),
                 PXA_STATUS_OK);
    CHECK(queried_manifest_size == manifest_size);
    check_status("query container manifest size",
                 pxa_posix_installer_source_manifest_size(
                     installer, container_v1, &queried_manifest_size),
                 PXA_STATUS_OK);
    CHECK(queried_manifest_size == manifest_size);
    result.encoded_capacity = manifest_size - 1;
    check_status("reject undersized manifest result buffer",
                 pxa_posix_installer_verify_source(installer, package_v1,
                                                    &result),
                 PXA_STATUS_INVALID_ARGUMENT);
    result.encoded_capacity = manifest_size;
    check_status("verify read-only source",
                 pxa_posix_installer_verify_source(installer, package_v1,
                                                    &result),
                 PXA_STATUS_OK);
    CHECK(strcmp(root_path, package_v1) == 0);
    CHECK(manifest != NULL && manifest->version.size == 5);
    CHECK(memcmp(manifest->version.data, "0.1.0", 5) == 0);

    result.encoded_capacity = manifest_size;
    check_status("verify read-only container",
                 pxa_posix_installer_verify_source(installer, container_v1,
                                                    &result),
                 PXA_STATUS_OK);
    CHECK(strcmp(root_path, container_v1) == 0);

    result.encoded_capacity = sizeof(encoded);
    check_status("install v1 container",
                 pxa_posix_installer_install(installer, container_v1, &result,
                                             &disposition),
                 PXA_STATUS_OK);
    CHECK(disposition == PXA_POSIX_INSTALL_INSTALLED);
    CHECK(manifest != NULL);
    CHECK(manifest->app_id.size == sizeof("com.example.testapp") - 1);
    CHECK(memcmp(manifest->app_id.data, "com.example.testapp",
                 sizeof("com.example.testapp") - 1) == 0);
    CHECK(manifest->version.size == 5);
    CHECK(memcmp(manifest->version.data, "0.1.0", 5) == 0);
    CHECK(strcmp(root_path, "") != 0);
    {
        char expected[512];
        snprintf(expected, sizeof(expected), "%s/packages/com.example.testapp",
                 storage_root);
        CHECK(strcmp(root_path, expected) == 0);
    }
    snprintf(file_path, sizeof(file_path), "%s/artifacts/Main.wasm",
             root_path);
    fd = open(file_path, O_RDONLY);
    CHECK(fd >= 0);
    read_size = 0;
    CHECK(read(fd, buffer, sizeof(buffer)) ==
          (ssize_t)(sizeof(artifact_v1) - 1));
    CHECK(memcmp(buffer, artifact_v1, sizeof(artifact_v1) - 1) == 0);
    close(fd);
    (void)read_size;

    check_status("install v1 again",
                 pxa_posix_installer_install(installer, package_v1, &result,
                                             &disposition),
                 PXA_STATUS_OK);
    CHECK(disposition == PXA_POSIX_INSTALL_ALREADY_CURRENT);

    {
        char tampered[512];
        uint8_t junk[32];
        memset(junk, 0x55, sizeof(junk));
        snprintf(tampered, sizeof(tampered), "%s/artifacts/Main.wasm",
                 package_v1);
        write_file(tampered, junk, sizeof(junk));
        check_status("verify tampered read-only source",
                     pxa_posix_installer_verify_source(installer, package_v1,
                                                        &result),
                     PXA_STATUS_DENIED);
        check_status("install tampered",
                     pxa_posix_installer_install(installer, package_v1,
                                                 &result, &disposition),
                     PXA_STATUS_DENIED);
        build_package(package_v1, &signer, "com.example.testapp", "0.1.0",
                      "artifacts/Main.wasm", artifact_v1,
                      sizeof(artifact_v1) - 1);
    }

    {
        char installed_artifact[600];
        pxa_posix_installer_t *repair_installer = NULL;
        void *repair_workspace;
        snprintf(installed_artifact, sizeof(installed_artifact),
                 "%s/artifacts/Main.wasm", root_path);
        write_file(installed_artifact, artifact_v2, sizeof(artifact_v2) - 1);
        check_status("install over corrupt current",
                     pxa_posix_installer_install(installer, package_v1,
                                                 &result, &disposition),
                     PXA_STATUS_DENIED);

        config.flags = PXA_POSIX_INSTALLER_FLAG_REPAIR_CORRUPT;
        repair_workspace = malloc(pxa_posix_installer_workspace_size(&config));
        CHECK(repair_workspace != NULL);
        check_status("repair installer init",
                     pxa_posix_installer_init(
                         repair_workspace,
                         pxa_posix_installer_workspace_size(&config), &config,
                         &repair_installer),
                     PXA_STATUS_OK);
        check_status("install repairs corrupt current",
                     pxa_posix_installer_install(repair_installer, package_v1,
                                                 &result, &disposition),
                     PXA_STATUS_OK);
        CHECK(disposition == PXA_POSIX_INSTALL_INSTALLED);
        CHECK(manifest != NULL && manifest->version.size == 5);
        CHECK(memcmp(manifest->version.data, "0.1.0", 5) == 0);
        pxa_posix_installer_deinit(repair_installer);
        free(repair_workspace);
        config.flags = 0;
    }

    checkpoint_state.fail_checkpoint = PXA_CHECKPOINT_INCOMING_VERIFIED;
    checkpoint_state.enabled = 1;
    memset(&faults, 0, sizeof(faults));
    faults.context = &checkpoint_state;
    faults.checkpoint = checkpoint_fail;
    config.faults = &faults;
    {
        pxa_posix_installer_t *installer_with_faults = NULL;
        void *fault_workspace =
            malloc(pxa_posix_installer_workspace_size(&config));
        CHECK(fault_workspace != NULL);
        check_status("installer init with faults",
                     pxa_posix_installer_init(fault_workspace,
                                              pxa_posix_installer_workspace_size(
                                                  &config),
                                              &config, &installer_with_faults),
                     PXA_STATUS_OK);
        check_status("install v2 interrupted",
                     pxa_posix_installer_install(installer_with_faults,
                                                 package_v2, &result,
                                                 &disposition),
                     PXA_STATUS_CANCELLED);
        pxa_posix_installer_deinit(installer_with_faults);
        free(fault_workspace);
    }
    checkpoint_state.enabled = 0;
    check_status("install v2 after interruption",
                 pxa_posix_installer_install(installer, package_v2, &result,
                                             &disposition),
                 PXA_STATUS_OK);
    CHECK(disposition == PXA_POSIX_INSTALL_INSTALLED);
    CHECK(manifest != NULL && manifest->version.size == 5);
    CHECK(memcmp(manifest->version.data, "0.2.0", 5) == 0);

    checkpoint_state.fail_checkpoint = PXA_CHECKPOINT_CURRENT_MOVED;
    checkpoint_state.enabled = 1;
    {
        pxa_posix_installer_t *installer_with_faults = NULL;
        void *fault_workspace =
            malloc(pxa_posix_installer_workspace_size(&config));
        CHECK(fault_workspace != NULL);
        check_status("installer init with faults 2",
                     pxa_posix_installer_init(fault_workspace,
                                              pxa_posix_installer_workspace_size(
                                                  &config),
                                              &config, &installer_with_faults),
                     PXA_STATUS_OK);
        check_status("install v3 interrupted",
                     pxa_posix_installer_install(installer_with_faults,
                                                 package_v3, &result,
                                                 &disposition),
                     PXA_STATUS_CANCELLED);
        pxa_posix_installer_deinit(installer_with_faults);
        free(fault_workspace);
    }
    checkpoint_state.enabled = 0;
    check_status("install v3 after interruption",
                 pxa_posix_installer_install(installer, package_v3, &result,
                                             &disposition),
                 PXA_STATUS_OK);
    CHECK(disposition == PXA_POSIX_INSTALL_INSTALLED);
    CHECK(manifest != NULL && manifest->version.size == 5);
    CHECK(memcmp(manifest->version.data, "0.3.0", 5) == 0);

    identity.publisher_key_id =
        (pxa_bytes_t){signer.key_id, sizeof(signer.key_id)};
    identity.app_id = (pxa_bytes_t){
        (const uint8_t *)"com.example.testapp",
        sizeof("com.example.testapp") - 1};
    check_status("load current",
                 pxa_posix_installer_load_current(installer, &identity,
                                                  &result),
                 PXA_STATUS_OK);

    CHECK(manifest != NULL && manifest->version.size == 5);
    CHECK(memcmp(manifest->version.data, "0.3.0", 5) == 0);

    check_status("uninstall",
                 pxa_posix_installer_uninstall(installer, &identity),
                 PXA_STATUS_OK);
    check_status("load after uninstall",
                 pxa_posix_installer_load_current(installer, &identity,
                                                  &result),
                 PXA_STATUS_NOT_FOUND);

    snprintf(file_path, sizeof(file_path),
             "%s/owners/com.example.testapp", storage_root);
    fd = open(file_path, O_RDWR);
    CHECK(fd >= 0 && read(fd, buffer, 40) == 40);
    CHECK(memcmp(buffer, "PXAO\1\0\0\0", 8) == 0);
    CHECK(memcmp(buffer + 8, signer.key_id, sizeof(signer.key_id)) == 0);
    buffer[8] ^= 0x01;
    CHECK(lseek(fd, 0, SEEK_SET) == 0 && write(fd, buffer, 40) == 40);
    close(fd);
    check_status("retained owner rejects different management key",
                 pxa_posix_installer_install(installer, container_lz4,
                                             &result, &disposition),
                 PXA_STATUS_DENIED);
    buffer[8] ^= 0x01;
    fd = open(file_path, O_WRONLY | O_TRUNC);
    CHECK(fd >= 0 && write(fd, buffer, 40) == 40);
    close(fd);

    check_status("verify LZ4 container",
                 pxa_posix_installer_verify_source(installer, container_lz4,
                                                    &result),
                 PXA_STATUS_OK);
    fd = open(container_lz4, O_RDWR);
    CHECK(fd >= 0 && lseek(fd, -1, SEEK_END) >= 0);
    CHECK(read(fd, buffer, 1) == 1);
    buffer[0] ^= 0x01;
    CHECK(lseek(fd, -1, SEEK_END) >= 0 && write(fd, buffer, 1) == 1);
    close(fd);
    check_status("reject tampered LZ4 container",
                 pxa_posix_installer_verify_source(installer, container_lz4,
                                                    &result),
                 PXA_STATUS_DENIED);
    build_test_container(container_lz4, package_lz4, &signer, artifact_lz4,
                         sizeof(artifact_lz4), PXA_CONTAINER_CODEC_LZ4);
    check_status("install LZ4 container",
                 pxa_posix_installer_install(installer, container_lz4,
                                             &result, &disposition),
                 PXA_STATUS_OK);
    snprintf(file_path, sizeof(file_path), "%s/artifacts/main.wasm",
             root_path);
    fd = open(file_path, O_RDONLY);
    CHECK(fd >= 0 && read(fd, buffer, sizeof(buffer)) == (ssize_t)sizeof(buffer));
    CHECK(memcmp(buffer, artifact_lz4, sizeof(buffer)) == 0);
    close(fd);

    pxa_posix_installer_deinit(installer);
    free(manifest_workspace);
    free(workspace);
    free(storage_root);
    free(package_v1);
    free(package_v2);
    free(package_v3);
    free(package_lz4);
    unlink(container_v1);
    unlink(container_lz4);
    EVP_PKEY_free(signer.key);
    printf("test_installer OK\n");
    (void)manifest_size;
}

static void test_composite_installer_identity(void) {
    static const uint8_t artifact_one[] = "publisher one";
    static const uint8_t artifact_two[] = "publisher two";
    test_signer_t signers[2];
    pxa_openssl_publisher_key_t keys[2];
    uint8_t key_der[2][256];
    char *storage_root = make_temp_dir("pxa-composite-storage");
    char *package_one = make_temp_dir("pxa-composite-one");
    char *package_two = make_temp_dir("pxa-composite-two");
    pxa_posix_installer_config_t config;
    pxa_posix_installer_t *installer = NULL;
    pxa_posix_installer_result_t result;
    pxa_posix_installer_identity_t identity[2];
    pxa_package_manifest_t *manifest = NULL;
    pxa_posix_install_disposition_t disposition;
    uint8_t encoded[PXA_PACKAGE_TEST_MANIFEST_BYTES];
    char root[512];
    char roots[2][512];
    void *workspace;
    void *manifest_workspace;
    size_t workspace_size;
    size_t index;

    memset(&config, 0, sizeof(config));
    for (index = 0; index < 2; ++index) {
        uint8_t *cursor;
        int length;
        generate_signer(&signers[index]);
        length = i2d_PUBKEY(signers[index].key, NULL);
        CHECK(length > 0 && length <= (int)sizeof(key_der[index]));
        cursor = key_der[index];
        CHECK(i2d_PUBKEY(signers[index].key, &cursor) == length);
        keys[index].spki = key_der[index];
        keys[index].spki_size = (size_t)length;
        identity[index].publisher_key_id =
            (pxa_bytes_t){signers[index].key_id, sizeof(signers[index].key_id)};
        identity[index].app_id = (pxa_bytes_t){
            (const uint8_t *)"com.example.shared",
            sizeof("com.example.shared") - 1u};
    }
    build_package(package_one, &signers[0], "com.example.shared", "1.0.0",
                  "artifacts/main.wasm", artifact_one,
                  sizeof(artifact_one) - 1u);
    build_package(package_two, &signers[1], "com.example.shared", "2.0.0",
                  "artifacts/main.wasm", artifact_two,
                  sizeof(artifact_two) - 1u);

    config.struct_size = sizeof(config);
    config.storage_root = storage_root;
    config.trust.struct_size = sizeof(config.trust);
    config.trust.keys = keys;
    config.trust.key_count = 2;
    config.flags = PXA_POSIX_INSTALLER_FLAG_COMPOSITE_IDENTITY;
    pxa_package_limits_init(&config.limits);
    workspace_size = pxa_posix_installer_workspace_size(&config);
    workspace = malloc(workspace_size);
    manifest_workspace =
        malloc(pxa_package_manifest_workspace_size(&config.limits));
    CHECK(workspace != NULL && manifest_workspace != NULL);
    check_status("composite installer init",
                 pxa_posix_installer_init(workspace, workspace_size, &config,
                                          &installer),
                 PXA_STATUS_OK);
    memset(&result, 0, sizeof(result));
    result.struct_size = sizeof(result);
    result.manifest_workspace = manifest_workspace;
    result.manifest_workspace_size =
        pxa_package_manifest_workspace_size(&config.limits);
    result.encoded = encoded;
    result.encoded_capacity = sizeof(encoded);
    result.manifest = &manifest;
    result.root = root;
    result.root_capacity = sizeof(root);

    check_status("reject source with unexpected composite identity",
                 pxa_posix_installer_install_for_identity(
                     installer, package_two, &identity[0], &result,
                     &disposition),
                 PXA_STATUS_DENIED);
    check_status("rejected source did not install expected identity",
                 pxa_posix_installer_load_current(installer, &identity[0],
                                                  &result),
                 PXA_STATUS_NOT_FOUND);
    check_status("rejected source did not install actual identity",
                 pxa_posix_installer_load_current(installer, &identity[1],
                                                  &result),
                 PXA_STATUS_NOT_FOUND);

    check_status("install first composite identity",
                 pxa_posix_installer_install(installer, package_one, &result,
                                             &disposition),
                 PXA_STATUS_OK);
    snprintf(roots[0], sizeof(roots[0]), "%s", root);
    check_status("install colliding App ID from second publisher",
                 pxa_posix_installer_install(installer, package_two, &result,
                                             &disposition),
                 PXA_STATUS_OK);
    snprintf(roots[1], sizeof(roots[1]), "%s", root);
    CHECK(strcmp(roots[0], roots[1]) != 0);
    CHECK(strstr(roots[0], "~com.example.shared") != NULL);
    CHECK(strstr(roots[1], "~com.example.shared") != NULL);

    check_status("load first composite identity",
                 pxa_posix_installer_load_current(installer, &identity[0],
                                                  &result),
                 PXA_STATUS_OK);
    CHECK(strcmp(root, roots[0]) == 0);
    check_status("load second composite identity",
                 pxa_posix_installer_load_current(installer, &identity[1],
                                                  &result),
                 PXA_STATUS_OK);
    CHECK(strcmp(root, roots[1]) == 0);
    check_status("uninstall first composite identity",
                 pxa_posix_installer_uninstall(installer, &identity[0]),
                 PXA_STATUS_OK);
    check_status("second identity survives first uninstall",
                 pxa_posix_installer_load_current(installer, &identity[1],
                                                  &result),
                 PXA_STATUS_OK);

    pxa_posix_installer_deinit(installer);
    free(manifest_workspace);
    free(workspace);
    free(storage_root);
    free(package_one);
    free(package_two);
    EVP_PKEY_free(signers[0].key);
    EVP_PKEY_free(signers[1].key);
    printf("test_composite_installer_identity OK\n");
}

int main(void) {
    test_openssl_sha256();
    test_fs_backend();
    test_fs_remove_tree();
    test_storage_backend();
    test_storage_rejects_hard_link_snapshot();
    test_scheduler_store();
    test_installer();
    test_composite_installer_identity();
    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("all adapter tests passed\n");
    return 0;
}
