#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>

#include "pxa/activation.h"
#include "pxa/fs.h"
#include "pxa/openssl/pxa_openssl.h"
#include "pxa/package.h"
#include "pxa/posix/pxa_posix_fs.h"
#include "pxa/posix/pxa_posix_storage.h"
#include "pxa/runtime.h"
#include "pxa/storage.h"
#include "pxa/wamr/pxa_wamr_engine.h"
#include "pxa/wasi.h"

#include "test_manifest.h"

static int failures = 0;

typedef struct {
    unsigned enter_count;
    unsigned leave_count;
    unsigned depth;
} synchronization_probe_t;

typedef struct {
    size_t current_bytes;
    size_t peak_bytes;
    unsigned allocate_count;
    unsigned release_count;
} artifact_allocation_probe_t;

typedef struct {
    unsigned allocate_count;
    unsigned reallocate_count;
    unsigned release_count;
} runtime_allocation_probe_t;

typedef struct {
    size_t size;
} artifact_allocation_header_t;

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

static void enter_critical(void *context) {
    synchronization_probe_t *probe = (synchronization_probe_t *)context;
    CHECK(probe != NULL);
    if (probe == NULL) return;
    CHECK(probe->depth == 0);
    probe->depth++;
    probe->enter_count++;
}

static void leave_critical(void *context) {
    synchronization_probe_t *probe = (synchronization_probe_t *)context;
    CHECK(probe != NULL);
    if (probe == NULL) return;
    CHECK(probe->depth == 1);
    if (probe->depth != 0) probe->depth--;
    probe->leave_count++;
}

static void *allocate_artifact(void *context, size_t size) {
    artifact_allocation_probe_t *probe = (artifact_allocation_probe_t *)context;
    artifact_allocation_header_t *header;
    if (probe == NULL || size == 0 || size > SIZE_MAX - sizeof(*header))
        return NULL;
    header = (artifact_allocation_header_t *)malloc(sizeof(*header) + size);
    if (header == NULL) return NULL;
    header->size = size;
    probe->current_bytes += size;
    if (probe->current_bytes > probe->peak_bytes)
        probe->peak_bytes = probe->current_bytes;
    probe->allocate_count++;
    return header + 1;
}

static void release_artifact(void *context, void *memory) {
    artifact_allocation_probe_t *probe = (artifact_allocation_probe_t *)context;
    artifact_allocation_header_t *header;
    CHECK(probe != NULL);
    CHECK(memory != NULL);
    if (probe == NULL || memory == NULL) return;
    header = (artifact_allocation_header_t *)memory - 1;
    CHECK(header->size <= probe->current_bytes);
    if (header->size <= probe->current_bytes)
        probe->current_bytes -= header->size;
    probe->release_count++;
    memset(memory, 0xa5, header->size);
    free(header);
}

static void *allocate_runtime(void *context, size_t size) {
    runtime_allocation_probe_t *probe =
        (runtime_allocation_probe_t *)context;
    void *memory;
    CHECK(probe != NULL);
    if (probe == NULL) return NULL;
    memory = malloc(size);
    if (memory != NULL) probe->allocate_count++;
    return memory;
}

static void *reallocate_runtime(void *context, void *memory, size_t size) {
    runtime_allocation_probe_t *probe =
        (runtime_allocation_probe_t *)context;
    void *resized;
    CHECK(probe != NULL);
    if (probe == NULL) return NULL;
    resized = realloc(memory, size);
    if (resized != NULL) probe->reallocate_count++;
    return resized;
}

static void release_runtime(void *context, void *memory) {
    runtime_allocation_probe_t *probe =
        (runtime_allocation_probe_t *)context;
    CHECK(probe != NULL);
    CHECK(memory != NULL);
    if (probe == NULL || memory == NULL) return;
    probe->release_count++;
    free(memory);
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

static void copy_file(const char *source, const char *destination) {
    struct stat metadata;
    uint8_t *buffer;
    size_t size = 0;
    int input = open(source, O_RDONLY);
    CHECK(input >= 0);
    if (input < 0) exit(1);
    CHECK(fstat(input, &metadata) == 0);
    CHECK(metadata.st_size > 0 && metadata.st_size <= 262144);
    if (metadata.st_size <= 0 || metadata.st_size > 262144) exit(1);
    buffer = (uint8_t *)malloc((size_t)metadata.st_size);
    CHECK(buffer != NULL);
    if (buffer == NULL) exit(1);
    while (size < (size_t)metadata.st_size) {
        ssize_t count =
            read(input, buffer + size, (size_t)metadata.st_size - size);
        if (count == 0) break;
        if (count < 0) exit(1);
        size += (size_t)count;
    }
    CHECK(size == (size_t)metadata.st_size);
    close(input);
    write_file(destination, buffer, size);
    free(buffer);
}

static void
check_rejected_import(const char *label, const char *guest_path,
                      const char *package_dir, const char *artifact_name,
                      pxa_status_t expected, pxa_component_engine_t *engine_ops,
                      pxa_package_service_requirement_t *requirements,
                      uint16_t requirement_count) {
    char destination[512];
    char artifact_path[96];
    pxa_package_artifact_t artifact;
    pxa_package_component_t package_component;
    pxa_activation_entry_t activation_entry;
    snprintf(destination, sizeof(destination), "%s/artifacts/%s", package_dir,
             artifact_name);
    copy_file(guest_path, destination);
    snprintf(artifact_path, sizeof(artifact_path), "artifacts/%s",
             artifact_name);
    memset(&artifact, 0, sizeof(artifact));
    artifact.kind = PXA_ARTIFACT_WASM;
    artifact.path =
        (pxa_bytes_t){(const uint8_t *)artifact_path, strlen(artifact_path)};
    memset(&package_component, 0, sizeof(package_component));
    package_component.id = (pxa_bytes_t){(const uint8_t *)"policy", 6};
    package_component.kind = PXA_COMPONENT_KIND_SERVICE;
    package_component.artifacts = &artifact;
    package_component.artifact_count = 1;
    package_component.services = requirements;
    package_component.service_count = requirement_count;
    activation_entry.component = &package_component;
    activation_entry.artifact = &artifact;
    check_status(
        label,
        engine_ops->instantiate(
            engine_ops->context,
            (pxa_bytes_t){(const uint8_t *)package_dir, strlen(package_dir)},
            &activation_entry, UINT32_C(0x7ffffffe), UINT64_C(99)),
        expected);
}

static void check_wasi_guest(const char *label, const char *guest_path,
                             const char *artifact_name,
                             pxa_component_t component, uint64_t features,
                             const char *package_dir,
                             pxa_component_engine_t *engine_ops,
                             artifact_allocation_probe_t *artifact_probe) {
    char destination[512];
    char artifact_path[96];
    pxa_package_artifact_t artifact;
    pxa_package_service_requirement_t requirement;
    pxa_package_component_t package_component;
    pxa_activation_entry_t activation_entry;
    const size_t current_bytes = artifact_probe->current_bytes;
    const unsigned allocate_count = artifact_probe->allocate_count;
    const unsigned release_count = artifact_probe->release_count;
    snprintf(artifact_path, sizeof(artifact_path), "artifacts/%s",
             artifact_name);
    snprintf(destination, sizeof(destination), "%s/%s", package_dir,
             artifact_path);
    copy_file(guest_path, destination);
    memset(&artifact, 0, sizeof(artifact));
    artifact.kind = strstr(artifact_name, ".aot") == NULL ? PXA_ARTIFACT_WASM
                                                          : PXA_ARTIFACT_AOT;
    artifact.path =
        (pxa_bytes_t){(const uint8_t *)artifact_path, strlen(artifact_path)};
    memset(&requirement, 0, sizeof(requirement));
    requirement.service = PXA_WASI_SERVICE_ID;
    requirement.min_version.major = PXA_WASI_SERVICE_MAJOR;
    requirement.max_version.major = PXA_WASI_SERVICE_MAJOR;
    requirement.required_features = features;
    memset(&package_component, 0, sizeof(package_component));
    package_component.id = (pxa_bytes_t){(const uint8_t *)"libc", 4};
    package_component.kind = PXA_COMPONENT_KIND_SERVICE;
    package_component.artifacts = &artifact;
    package_component.artifact_count = 1;
    package_component.services = &requirement;
    package_component.service_count = 1;
    activation_entry.component = &package_component;
    activation_entry.artifact = &artifact;
    check_status(
        label,
        engine_ops->instantiate(
            engine_ops->context,
            (pxa_bytes_t){(const uint8_t *)package_dir, strlen(package_dir)},
            &activation_entry, component, UINT64_C(98)),
        PXA_STATUS_OK);
    CHECK(artifact_probe->current_bytes == current_bytes);
    CHECK(artifact_probe->allocate_count == allocate_count + 1);
    CHECK(artifact_probe->release_count == release_count + 1);
    check_status(label, engine_ops->start(engine_ops->context, component),
                 PXA_STATUS_OK);
    engine_ops->stop(engine_ops->context, component, PXA_STOP_NORMAL);
    engine_ops->destroy(engine_ops->context, component);
}

static pxa_status_t read_artifact(void *context, pxa_bytes_t path,
                                  uint8_t *output, size_t capacity,
                                  size_t *size) {
    char *copy;
    struct stat metadata;
    int fd;
    ssize_t count;
    (void)context;
    if (size == NULL || path.data == NULL || path.size == 0 ||
        (output == NULL && capacity != 0)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *size = 0;
    copy = (char *)malloc(path.size + 1);
    if (copy == NULL) return PXA_STATUS_INTERNAL;
    memcpy(copy, path.data, path.size);
    copy[path.size] = '\0';
    if (stat(copy, &metadata) != 0 || !S_ISREG(metadata.st_mode)) {
        free(copy);
        return PXA_STATUS_NOT_FOUND;
    }
    if (metadata.st_size <= 0) {
        free(copy);
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    if (output == NULL) {
        free(copy);
        *size = (size_t)metadata.st_size;
        return PXA_STATUS_OK;
    }
    if ((uint64_t)metadata.st_size > capacity) {
        free(copy);
        return PXA_STATUS_QUOTA_EXCEEDED;
    }
    fd = open(copy, O_RDONLY);
    free(copy);
    if (fd < 0) return PXA_STATUS_INTERNAL;
    {
        size_t offset = 0;
        while (offset < (size_t)metadata.st_size) {
            do {
                count = read(fd, output + offset,
                             (size_t)metadata.st_size - offset);
            } while (count < 0 && errno == EINTR);
            if (count <= 0) {
                close(fd);
                return PXA_STATUS_INTERNAL;
            }
            offset += (size_t)count;
        }
    }
    close(fd);
    *size = (size_t)metadata.st_size;
    return PXA_STATUS_OK;
}

static uint64_t now_us(void *context) {
    struct timespec value;
    (void)context;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (uint64_t)value.tv_sec * 1000000ull +
           (uint64_t)value.tv_nsec / 1000ull;
}

static pxa_status_t storage_get_value(const pxa_storage_backend_t *backend,
                                      const char *key, uint8_t *output,
                                      size_t capacity, size_t *size) {
    return backend->get(backend->context,
                        (pxa_bytes_t){(const uint8_t *)key, strlen(key)},
                        output, capacity, size);
}

int main(void) {
    char *package_dir = make_temp_dir("pxa-wamr-package");
    char *storage_root = make_temp_dir("pxa-wamr-storage");
    char *fs_root = make_temp_dir("pxa-wamr-fs");
    pxa_runtime_limits_t limits;
    pxa_runtime_t *runtime = NULL;
    void *runtime_workspace;
    pxa_fs_backend_t fs_backend;
    pxa_fs_config_t fs_config;
    pxa_fs_service_t *fs_service = NULL;
    void *fs_workspace;
    void *fs_service_workspace;
    pxa_posix_fs_t *posix_fs = NULL;
    pxa_storage_backend_t storage_backend;
    pxa_storage_config_t storage_config;
    pxa_storage_service_t *storage_service = NULL;
    void *storage_workspace;
    void *storage_service_workspace;
    pxa_posix_storage_t *posix_storage = NULL;
    pxa_wamr_engine_config_t engine_config;
    pxa_wamr_engine_t *engine = NULL;
    pxa_component_engine_t engine_ops;
    void *engine_workspace;
    pxa_package_limits_t package_limits;
    pxa_package_manifest_t *manifest = NULL;
    void *manifest_workspace;
    pxa_activation_plan_t *plan = NULL;
    void *plan_workspace;
    pxa_activation_coordinator_t *coordinator = NULL;
    void *coordinator_workspace;
    pxa_component_t component = PXA_COMPONENT_INVALID;
    uint8_t manifest_bytes[PXA_PACKAGE_TEST_MANIFEST_BYTES];
    uint8_t wasm_bytes[262144];
    uint8_t sha[32];
    uint8_t value[32];
    size_t value_size = 0;
    size_t encoded_size;
    size_t wasm_size = 0;
    char path[512];
    int delivered = 0;
    int fd;
    pxa_status_t status;
    pxa_package_host_profile_t host_profile;
    pxa_package_service_capability_t service_capabilities[2];
    pxa_package_activation_profile_t capabilities;
    synchronization_probe_t synchronization_probe = {0, 0, 0};
    artifact_allocation_probe_t artifact_probe = {0, 0, 0, 0};
    runtime_allocation_probe_t runtime_probe = {0, 0, 0};

    /* 1. Runtime. */
    pxa_runtime_limits_init(&limits);
    limits.max_components = 2;
    runtime_workspace = malloc(pxa_runtime_workspace_size(&limits));
    CHECK(runtime_workspace != NULL);
    check_status("runtime init",
                 pxa_runtime_init(runtime_workspace,
                                  pxa_runtime_workspace_size(&limits), &limits,
                                  &runtime),
                 PXA_STATUS_OK);
    CHECK(runtime != NULL);

    /* 2. FS service on the POSIX backend. */
    {
        pxa_posix_fs_config_t config;
        size_t backend_size;
        size_t service_size;
        memset(&config, 0, sizeof(config));
        config.struct_size = sizeof(config);
        config.root_path = fs_root;
        config.quota_bytes = 1024 * 1024;
        config.max_open_resources = 8;
        backend_size = pxa_posix_fs_workspace_size(&config);
        fs_workspace = malloc(backend_size);
        CHECK(fs_workspace != NULL);
        check_status("posix fs init",
                     pxa_posix_fs_init(fs_workspace, backend_size, &config,
                                       &posix_fs, &fs_backend),
                     PXA_STATUS_OK);
        memset(&fs_config, 0, sizeof(fs_config));
        fs_config.struct_size = sizeof(fs_config);
        fs_config.max_open_resources = 8;
        fs_config.backend = fs_backend;
        service_size = pxa_fs_service_workspace_size(&fs_config);
        CHECK(service_size != 0);
        fs_service_workspace = malloc(service_size);
        CHECK(fs_service_workspace != NULL);
        check_status("fs service init",
                     pxa_fs_service_init(fs_service_workspace, service_size,
                                         runtime, &fs_config, &fs_service),
                     PXA_STATUS_OK);
        check_status("fs service register", pxa_fs_service_register(fs_service),
                     PXA_STATUS_OK);
    }

    /* 3. Storage service on the POSIX backend. */
    {
        pxa_posix_storage_config_t config;
        memset(&config, 0, sizeof(config));
        config.struct_size = sizeof(config);
        config.root_path = storage_root;
        config.max_keys = 16;
        config.max_value_bytes = 2048;
        config.quota_bytes = 8192;
        storage_workspace = malloc(pxa_posix_storage_workspace_size(&config));
        CHECK(storage_workspace != NULL);
        check_status(
            "posix storage init",
            pxa_posix_storage_init(storage_workspace,
                                   pxa_posix_storage_workspace_size(&config),
                                   &config, &posix_storage, &storage_backend),
            PXA_STATUS_OK);
        memset(&storage_config, 0, sizeof(storage_config));
        storage_config.struct_size = sizeof(storage_config);
        storage_config.max_value_bytes = 64;
        storage_config.backend = storage_backend;
        {
            const size_t service_workspace_size =
                pxa_storage_service_workspace_size(&storage_config);
            CHECK(service_workspace_size != 0);
            storage_service_workspace = malloc(service_workspace_size);
            CHECK(storage_service_workspace != NULL);
            check_status("storage service init",
                         pxa_storage_service_init(
                             storage_service_workspace, service_workspace_size,
                             runtime, &storage_config, &storage_service),
                         PXA_STATUS_OK);
            check_status("storage service register",
                         pxa_storage_service_register(storage_service),
                         PXA_STATUS_OK);
        }
    }

    /* 4. Engine. */
    memset(&engine_config, 0, sizeof(engine_config));
    engine_config.struct_size = sizeof(engine_config);
    engine_config.read_artifact = read_artifact;
    engine_config.now_us = now_us;
    engine_config.call_timeout_us = 0;
    engine_config.guest_stack_size = 128 * 1024;
    engine_config.host_managed_heap_size = 16 * 1024;
    engine_config.max_components = 2;
    engine_config.wasi_enabled = 1;
    engine_config.pool_bytes = 2 * 1024 * 1024;
    engine_config.max_module_bytes = sizeof(wasm_bytes);
    engine_config.synchronization_context = &synchronization_probe;
    engine_config.enter_critical = enter_critical;
    CHECK(pxa_wamr_engine_workspace_size(&engine_config) == 0);
    engine_config.leave_critical = leave_critical;
    {
        const size_t static_workspace_size =
            pxa_wamr_engine_workspace_size(&engine_config);
        size_t engine_workspace_size;
        pxa_wamr_memory_snapshot_t usage;
        CHECK(static_workspace_size != 0);
        engine_config.artifact_allocator_context = &artifact_probe;
        engine_config.allocate_artifact = allocate_artifact;
        CHECK(pxa_wamr_engine_workspace_size(&engine_config) == 0);
        engine_config.release_artifact = release_artifact;
        /* Dynamic Artifact storage has no artificial size ceiling. */
        engine_config.max_module_bytes = 0;
        engine_config.runtime_allocator_context = &runtime_probe;
        engine_config.allocate_runtime = allocate_runtime;
        engine_config.reallocate_runtime = reallocate_runtime;
        engine_config.release_runtime = release_runtime;
        engine_workspace_size = pxa_wamr_engine_workspace_size(&engine_config);
        CHECK(engine_workspace_size != 0);
        CHECK(engine_workspace_size < static_workspace_size);
        engine_workspace = malloc(engine_workspace_size);
        CHECK(engine_workspace != NULL);
        check_status("engine init",
                     pxa_wamr_engine_init(engine_workspace,
                                          engine_workspace_size, &engine_config,
                                          &engine, &engine_ops),
                     PXA_STATUS_OK);
        CHECK(engine != NULL);
        check_status("engine memory snapshot",
                     pxa_wamr_engine_memory_snapshot(engine, &usage),
                     PXA_STATUS_OK);
        CHECK(usage.total_bytes == 0);
        CHECK(usage.current_bytes != 0);
        CHECK(usage.peak_bytes >= usage.current_bytes);
        CHECK(runtime_probe.allocate_count != 0);
    }
    pxa_wamr_engine_set_runtime(engine, runtime);

    /* 5. Package: manifest + guest artifact. */
    fd = open(PXA_WAMR_TEST_GUEST_PATH, O_RDONLY);
    CHECK(fd >= 0);
    {
        ssize_t count;
        do {
            count = read(fd, wasm_bytes + wasm_size,
                         sizeof(wasm_bytes) - wasm_size);
            if (count > 0) wasm_size += (size_t)count;
        } while (count > 0);
        close(fd);
    }
    CHECK(wasm_size != 0);
    CHECK(pxa_openssl_sha256(wasm_bytes, wasm_size, sha) == PXA_STATUS_OK);
    encoded_size = pxa_test_encode_manifest(
        manifest_bytes, sizeof(manifest_bytes), sha, "com.example.wamrtest",
        "0.1.0", "artifacts/main.wasm", (uint64_t)wasm_size, sha);
    CHECK(encoded_size != 0);
    make_dirs(package_dir);
    snprintf(path, sizeof(path), "%s/artifacts", package_dir);
    make_dirs(path);
    snprintf(path, sizeof(path), "%s/manifest.pxm", package_dir);
    write_file(path, manifest_bytes, encoded_size);
    snprintf(path, sizeof(path), "%s/artifacts/main.wasm", package_dir);
    write_file(path, wasm_bytes, wasm_size);

    pxa_package_limits_init(&package_limits);
    manifest_workspace =
        malloc(pxa_package_manifest_workspace_size(&package_limits));
    CHECK(manifest_workspace != NULL);
    check_status("manifest parse",
                 pxa_package_manifest_parse(
                     manifest_workspace,
                     pxa_package_manifest_workspace_size(&package_limits),
                     (pxa_bytes_t){manifest_bytes, encoded_size},
                     &package_limits, &manifest),
                 PXA_STATUS_OK);
    CHECK(manifest != NULL);

    memset(&host_profile, 0, sizeof(host_profile));
    host_profile.target = (pxa_bytes_t){(const uint8_t *)"linux-x86_64", 13};
    host_profile.engine = (pxa_bytes_t){(const uint8_t *)"wamr", 4};
    host_profile.engine_abi = (pxa_bytes_t){(const uint8_t *)"wasm32", 6};
    host_profile.supported_features = 0;
    host_profile.memory_model = PXA_MEMORY_WASM32;
    memset(&service_capabilities, 0, sizeof(service_capabilities));
    service_capabilities[0].service = PXA_STORAGE_SERVICE_ID;
    service_capabilities[0].version.major = PXA_CORE_SERVICE_MAJOR;
    service_capabilities[0].version.minor = PXA_CORE_SERVICE_MINOR;
    service_capabilities[1].service = PXA_FS_SERVICE_ID;
    service_capabilities[1].version.major = PXA_CORE_SERVICE_MAJOR;
    service_capabilities[1].version.minor = PXA_CORE_SERVICE_MINOR;
    memset(&capabilities, 0, sizeof(capabilities));
    capabilities.core_version.major = PXA_CORE_VERSION_MAJOR;
    capabilities.core_version.minor = PXA_CORE_VERSION_MINOR;
    capabilities.services = service_capabilities;
    capabilities.service_count = 2;
    {
        const size_t plan_size =
            pxa_activation_plan_workspace_size(package_limits.max_components);
        plan_workspace = malloc(plan_size);
        CHECK(plan_workspace != NULL);
        check_status("plan prepare",
                     pxa_activation_plan_prepare(
                         plan_workspace, plan_size, manifest, &capabilities,
                         &host_profile,
                         (pxa_bytes_t){(const uint8_t *)package_dir,
                                       strlen(package_dir)},
                         &plan),
                     PXA_STATUS_OK);
        CHECK(plan != NULL);
    }
    {
        const size_t coordinator_size =
            pxa_activation_coordinator_workspace_size(plan);
        CHECK(coordinator_size != 0);
        coordinator_workspace = malloc(coordinator_size);
        CHECK(coordinator_workspace != NULL);
        check_status("coordinator init",
                     pxa_activation_coordinator_init(
                         coordinator_workspace, coordinator_size, runtime, plan,
                         &engine_ops, &coordinator),
                     PXA_STATUS_OK);
        CHECK(coordinator != NULL);
    }

    /* 6. Activate and drain. */
    check_status("activate",
                 pxa_activation_activate(
                     coordinator, (pxa_bytes_t){(const uint8_t *)"main", 4}, 1,
                     &component),
                 PXA_STATUS_OK);
    CHECK(component != PXA_COMPONENT_INVALID);
    CHECK(artifact_probe.allocate_count == 1);
    CHECK(artifact_probe.release_count == 1);
    CHECK(artifact_probe.current_bytes == 0);
    CHECK(pxa_wamr_engine_busy(engine) == 0);
    CHECK(synchronization_probe.depth == 0);
    CHECK(synchronization_probe.enter_count != 0);
    CHECK(synchronization_probe.enter_count ==
          synchronization_probe.leave_count);
    while (delivered < 64) {
        pxa_wamr_event_result_t event_result;
        status = pxa_wamr_engine_deliver_event_result(
            engine, runtime, component, &event_result);
        if (status == PXA_STATUS_WOULD_BLOCK) {
            CHECK(event_result.event_consumed == 0);
            break;
        }
        if (status != PXA_STATUS_OK) {
            check_status("deliver event", status, PXA_STATUS_OK);
            break;
        }
        CHECK(event_result.event_consumed != 0);
        ++delivered;
    }
    CHECK(delivered >= 4);
    CHECK(synchronization_probe.depth == 0);
    CHECK(synchronization_probe.enter_count ==
          synchronization_probe.leave_count);
    {
        uint8_t payload[PXA_MAX_CONTROL_MESSAGE - PXA_ENVELOPE_SIZE];
        pxa_wamr_event_result_t event_result;
        memset(payload, 0x5a, sizeof(payload));
        check_status(
            "post maximum event",
            pxa_event_post_message(
                runtime, component, UINT16_C(0x7fff), UINT16_C(1), 0,
                (pxa_bytes_t){payload, sizeof(payload)}, 1, 0),
            PXA_STATUS_OK);
        check_status("deliver maximum event",
                     pxa_wamr_engine_deliver_event_result(
                         engine, runtime, component, &event_result),
                     PXA_STATUS_OK);
        CHECK(event_result.event_consumed != 0);
        CHECK(event_result.guest_result == 0);
    }

    {
        uint8_t payload[4];
        pxa_wamr_event_result_t result;
        pxa_write_u32(payload, UINT32_C(0x12345678));
        for (unsigned index = 0; index < 32; ++index) {
            check_status("post event buffer reuse probe",
                pxa_event_post_message(runtime, component, UINT16_C(0x7ffe),
                    index == 0 ? 1 : 2, 0, (pxa_bytes_t){payload, sizeof(payload)},
                    1, 0), PXA_STATUS_OK);
            check_status("reuse event buffer after memory.grow",
                pxa_wamr_engine_deliver_event_result(engine, runtime, component,
                    &result), PXA_STATUS_OK);
            CHECK(result.event_consumed && result.guest_result == 1);
        }
    }

    /* 7. Verify observable state. */
    value_size = 0;
    check_status("storage hits",
                 storage_get_value(&storage_backend, "hits", value,
                                   sizeof(value), &value_size),
                 PXA_STATUS_OK);
    CHECK(value_size == 1);
    CHECK(value[0] == '1');
    value_size = 0;
    check_status("storage result",
                 storage_get_value(&storage_backend, "result", value,
                                   sizeof(value), &value_size),
                 PXA_STATUS_OK);
    CHECK(value_size == 1);
    CHECK(value[0] == '1');
    value_size = 0;
    check_status("storage done",
                 storage_get_value(&storage_backend, "done", value,
                                   sizeof(value), &value_size),
                 PXA_STATUS_OK);
    CHECK(value_size == 1);
    CHECK(value[0] == '1');
    {
        char log_path[512];
        uint8_t log[8];
        ssize_t count;
        snprintf(log_path, sizeof(log_path), "%s/log.txt", fs_root);
        fd = open(log_path, O_RDONLY);
        CHECK(fd >= 0);
        count = read(fd, log, sizeof(log));
        close(fd);
        CHECK(count == 2);
        CHECK(memcmp(log, "hi", 2) == 0);
    }

    /* 8. Signed feature claims and actual imports must agree. */
    {
        pxa_package_service_requirement_t wasi_requirement;
        memset(&wasi_requirement, 0, sizeof(wasi_requirement));
        wasi_requirement.service = PXA_WASI_SERVICE_ID;
        wasi_requirement.min_version.major = PXA_WASI_SERVICE_MAJOR;
        wasi_requirement.max_version.major = PXA_WASI_SERVICE_MAJOR;
        check_rejected_import("undeclared WASI random import",
                              PXA_WAMR_TEST_WASI_IMPORT_PATH, package_dir,
                              "wasi-import.wasm", PXA_STATUS_DENIED,
                              &engine_ops, &wasi_requirement, 1);
        check_rejected_import("env import", PXA_WAMR_TEST_ENV_IMPORT_PATH,
                              package_dir, "env-import.wasm",
                              PXA_STATUS_UNSUPPORTED, &engine_ops, NULL, 0);
        wasi_requirement.required_features =
            PXA_WASI_FEATURE_MONOTONIC_CLOCK | PXA_WASI_FEATURE_RANDOM;
        check_rejected_import("single-clock WASI declaration",
                              PXA_WAMR_TEST_WASI_SYSTEM_PATH, package_dir,
                              "wasi-system-incomplete.wasm", PXA_STATUS_DENIED,
                              &engine_ops, &wasi_requirement, 1);
        check_wasi_guest("clock and random WASI imports",
                         PXA_WAMR_TEST_WASI_SYSTEM_PATH, "wasi-system.wasm",
                         UINT32_C(0x7ffffffb),
                         PXA_WASI_FEATURE_CLOCKS | PXA_WASI_FEATURE_RANDOM,
                         package_dir, &engine_ops, &artifact_probe);
#ifdef PXA_WAMR_TEST_WASI_LIBC_PATH
        check_wasi_guest("wasi-libc Wasm", PXA_WAMR_TEST_WASI_LIBC_PATH,
                         "wasi-libc.wasm", UINT32_C(0x7ffffffd), 0, package_dir,
                         &engine_ops, &artifact_probe);
#ifdef PXA_WAMR_TEST_WASI_LIBC_AOT_PATH
        check_wasi_guest("wasi-libc AOT", PXA_WAMR_TEST_WASI_LIBC_AOT_PATH,
                         "wasi-libc.aot", UINT32_C(0x7ffffffc), 0, package_dir,
                         &engine_ops, &artifact_probe);
#endif
#endif
    }

    /* 9. Pending startup configs reserve distinct instance slots. */
    pxa_activation_deactivate_all(coordinator, PXA_STOP_NORMAL);
    CHECK(artifact_probe.current_bytes == 0);
    {
        static const uint8_t failed_config[] = {0x01};
        static const uint8_t first_config[] = {0x11};
        static const uint8_t first_update[] = {0x12};
        static const uint8_t second_config[] = {0x21};
        static const uint8_t third_config[] = {0x31};
        static const uint8_t missing_path[] = "artifacts/missing.wasm";
        const pxa_component_t second_component = UINT32_C(0x7ffffffa);
        pxa_package_artifact_t missing_artifact;
        pxa_activation_entry_t missing_entry;

        memset(&missing_artifact, 0, sizeof(missing_artifact));
        missing_artifact.kind = PXA_ARTIFACT_WASM;
        missing_artifact.path = (pxa_bytes_t){missing_path,
                                              sizeof(missing_path) - 1u};
        missing_entry.component = plan->entries[0].component;
        missing_entry.artifact = &missing_artifact;
        check_status(
            "reserve failed instance config",
            pxa_wamr_engine_set_config(
                engine, UINT64_C(100),
                (pxa_bytes_t){failed_config, sizeof(failed_config)}),
            PXA_STATUS_OK);
        check_status(
            "failed reserved instance",
            engine_ops.instantiate(
                engine_ops.context,
                (pxa_bytes_t){(const uint8_t *)package_dir,
                              strlen(package_dir)},
                &missing_entry, UINT32_C(0x7ffffff9), UINT64_C(100)),
            PXA_STATUS_NOT_FOUND);

        check_status("reserve first pending config",
                     pxa_wamr_engine_set_config(
                         engine, UINT64_C(101),
                         (pxa_bytes_t){first_config, sizeof(first_config)}),
                     PXA_STATUS_OK);
        check_status("reserve second pending config",
                     pxa_wamr_engine_set_config(
                         engine, UINT64_C(102),
                         (pxa_bytes_t){second_config, sizeof(second_config)}),
                     PXA_STATUS_OK);
        check_status("update first pending config",
                     pxa_wamr_engine_set_config(
                         engine, UINT64_C(101),
                         (pxa_bytes_t){first_update, sizeof(first_update)}),
                     PXA_STATUS_OK);
        check_status("pending config capacity",
                     pxa_wamr_engine_set_config(
                         engine, UINT64_C(103),
                         (pxa_bytes_t){third_config, sizeof(third_config)}),
                     PXA_STATUS_RESOURCE_LIMIT);
        check_status("instantiate second reserved instance",
                     engine_ops.instantiate(
                         engine_ops.context,
                         (pxa_bytes_t){(const uint8_t *)package_dir,
                                       strlen(package_dir)},
                         &plan->entries[0], second_component, UINT64_C(102)),
                     PXA_STATUS_OK);
        CHECK(artifact_probe.current_bytes == 0);
        check_status("occupied plus pending capacity",
                     pxa_wamr_engine_set_config(
                         engine, UINT64_C(103),
                         (pxa_bytes_t){third_config, sizeof(third_config)}),
                     PXA_STATUS_RESOURCE_LIMIT);
        engine_ops.destroy(engine_ops.context, second_component);
        CHECK(artifact_probe.current_bytes == 0);
        check_status("reuse released instance slot",
                     pxa_wamr_engine_set_config(
                         engine, UINT64_C(103),
                         (pxa_bytes_t){third_config, sizeof(third_config)}),
                     PXA_STATUS_OK);
    }

    /* 10. Teardown. */
    pxa_wamr_engine_deinit(engine);
    CHECK(runtime_probe.allocate_count == runtime_probe.release_count);
    CHECK(artifact_probe.current_bytes == 0);
    CHECK(artifact_probe.allocate_count == artifact_probe.release_count);
    CHECK(artifact_probe.peak_bytes >= wasm_size);
    free(coordinator_workspace);
    free(plan_workspace);
    free(manifest_workspace);
    free(engine_workspace);
    free(storage_workspace);
    free(storage_service_workspace);
    free(fs_workspace);
    free(fs_service_workspace);
    free(runtime_workspace);
    free(package_dir);
    free(storage_root);
    free(fs_root);

    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("test_wamr_engine OK\n");
    return 0;
}
