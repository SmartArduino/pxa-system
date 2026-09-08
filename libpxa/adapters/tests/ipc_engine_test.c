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
#include "pxa/ipc.h"
#include "pxa/openssl/pxa_openssl.h"
#include "pxa/package.h"
#include "pxa/posix/pxa_posix_storage.h"
#include "pxa/runtime.h"
#include "pxa/storage.h"
#include "pxa/wamr/pxa_wamr_engine.h"

#include "test_manifest.h"

/* Multi-component integration: an IPC provider (service component) and a
 * caller (UI component) talk through the C broker; the caller persists the
 * verified reply into Storage, which the host checks. */

static int failures = 0;

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

static void *ipc_allocate(void *context, size_t size) {
    (void)context;
    return malloc(size);
}

static void ipc_release(void *context, void *memory) {
    (void)context;
    free(memory);
}

typedef struct {
    pxa_activation_coordinator_t *coordinator;
    pxa_wamr_engine_t *engine;
    pxa_component_t provider;
    unsigned calls;
    int engine_was_busy;
} lazy_endpoint_probe_t;

static pxa_status_t resolve_echo_endpoint(void *context, pxa_bytes_t endpoint,
                                          pxa_component_t *provider) {
    lazy_endpoint_probe_t *probe = (lazy_endpoint_probe_t *)context;
    static const uint8_t expected[] = "com.example.echo";
    pxa_status_t status;
    if (probe == NULL || provider == NULL ||
        endpoint.size != sizeof(expected) - 1u ||
        memcmp(endpoint.data, expected, sizeof(expected) - 1u) != 0) {
        return PXA_STATUS_NOT_FOUND;
    }
    probe->calls++;
    probe->engine_was_busy = pxa_wamr_engine_busy(probe->engine);
    status = pxa_activation_activate(
        probe->coordinator, (pxa_bytes_t){(const uint8_t *)"echo", 4}, 1,
        provider);
    if (status == PXA_STATUS_OK) probe->provider = *provider;
    return status;
}

int main(void) {
    char *package_dir = make_temp_dir("pxa-ipc-package");
    char *storage_root = make_temp_dir("pxa-ipc-storage");
    pxa_runtime_limits_t limits;
    pxa_runtime_t *runtime = NULL;
    void *runtime_workspace;
    pxa_storage_backend_t storage_backend;
    pxa_storage_config_t storage_config;
    pxa_storage_service_t *storage_service = NULL;
    void *storage_workspace;
    void *storage_service_workspace;
    pxa_posix_storage_t *posix_storage = NULL;
    pxa_ipc_limits_t ipc_limits;
    pxa_ipc_broker_t *broker = NULL;
    void *broker_workspace;
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
    pxa_component_t provider = PXA_COMPONENT_INVALID;
    pxa_component_t caller = PXA_COMPONENT_INVALID;
    uint8_t manifest_bytes[PXA_PACKAGE_TEST_MANIFEST_BYTES];
    uint8_t wasm_bytes[65536];
    uint8_t value[32];
    size_t value_size = 0;
    size_t encoded_size;
    char path[512];
    int rounds;
    int fd;
    pxa_package_host_profile_t host_profile;
    pxa_package_activation_profile_t capabilities;
    pxa_test_component_t components[2];
    pxa_test_file_t files[2];
    uint8_t provider_sha[32];
    uint8_t caller_sha[32];
    lazy_endpoint_probe_t lazy_probe;

    /* 1. Runtime. */
    pxa_runtime_limits_init(&limits);
    limits.max_components = 3;
    runtime_workspace = malloc(pxa_runtime_workspace_size(&limits));
    CHECK(runtime_workspace != NULL);
    check_status("runtime init",
                 pxa_runtime_init(runtime_workspace,
                                  pxa_runtime_workspace_size(&limits), &limits,
                                  &runtime),
                 PXA_STATUS_OK);
    CHECK(runtime != NULL);

    /* 2. Storage service (observability sink for the caller guest). */
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
        check_status("posix storage init",
                     pxa_posix_storage_init(
                         storage_workspace,
                         pxa_posix_storage_workspace_size(&config), &config,
                         &posix_storage, &storage_backend),
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
                             storage_service_workspace,
                             service_workspace_size, runtime, &storage_config,
                             &storage_service),
                         PXA_STATUS_OK);
            check_status("storage service register",
                         pxa_storage_service_register(storage_service),
                         PXA_STATUS_OK);
        }
    }

    /* 3. IPC broker. */
    pxa_ipc_limits_init(&ipc_limits);
    ipc_limits.max_endpoints = 4;
    ipc_limits.max_pending_calls = 4;
    broker_workspace = malloc(pxa_ipc_broker_workspace_size(&ipc_limits));
    CHECK(broker_workspace != NULL);
    check_status("broker init",
                 pxa_ipc_broker_init(broker_workspace,
                                     pxa_ipc_broker_workspace_size(&ipc_limits),
                                     runtime, &ipc_limits, &broker),
                 PXA_STATUS_OK);
    check_status("broker register", pxa_ipc_broker_register(broker),
                 PXA_STATUS_OK);
    check_status("broker allocator",
                 pxa_ipc_broker_set_allocator(
                     broker, NULL, ipc_allocate, ipc_release),
                 PXA_STATUS_OK);

    /* 4. Engine. */
    memset(&engine_config, 0, sizeof(engine_config));
    engine_config.struct_size = sizeof(engine_config);
    engine_config.read_artifact = read_artifact;
    engine_config.now_us = now_us;
    engine_config.guest_stack_size = 128 * 1024;
    engine_config.max_components = 3;
    engine_config.pool_bytes = 512 * 1024;
    engine_config.max_module_bytes = sizeof(wasm_bytes);
    {
        const size_t engine_workspace_size =
            pxa_wamr_engine_workspace_size(&engine_config);
        CHECK(engine_workspace_size != 0);
        engine_workspace = malloc(engine_workspace_size);
        CHECK(engine_workspace != NULL);
        check_status("engine init",
                     pxa_wamr_engine_init(engine_workspace,
                                          engine_workspace_size,
                                          &engine_config, &engine,
                                          &engine_ops),
                     PXA_STATUS_OK);
        CHECK(engine != NULL);
    }
    pxa_wamr_engine_set_runtime(engine, runtime);

    /* 5. Package: two components, one artifact file each. */
    {
        uint8_t *provider_wasm = wasm_bytes;
        uint8_t *caller_wasm = wasm_bytes + 32768;
        size_t provider_wasm_size = 0;
        size_t caller_wasm_size = 0;
        fd = open(PXA_IPC_PROVIDER_GUEST_PATH, O_RDONLY);
        CHECK(fd >= 0);
        {
            ssize_t count;
            do {
                count = read(fd, provider_wasm + provider_wasm_size,
                             32768 - provider_wasm_size);
                if (count > 0) provider_wasm_size += (size_t)count;
            } while (count > 0);
        }
        close(fd);
        fd = open(PXA_IPC_CALLER_GUEST_PATH, O_RDONLY);
        CHECK(fd >= 0);
        {
            ssize_t count;
            do {
                count = read(fd, caller_wasm + caller_wasm_size,
                             32768 - caller_wasm_size);
                if (count > 0) caller_wasm_size += (size_t)count;
            } while (count > 0);
        }
        close(fd);
        CHECK(provider_wasm_size != 0);
        CHECK(caller_wasm_size != 0);
        CHECK(pxa_openssl_sha256(provider_wasm, provider_wasm_size,
                                 provider_sha) == PXA_STATUS_OK);
        CHECK(pxa_openssl_sha256(caller_wasm, caller_wasm_size, caller_sha) ==
              PXA_STATUS_OK);
        make_dirs(package_dir);
        snprintf(path, sizeof(path), "%s/artifacts", package_dir);
        make_dirs(path);
        snprintf(path, sizeof(path), "%s/artifacts/provider.wasm",
                 package_dir);
        write_file(path, provider_wasm, provider_wasm_size);
        snprintf(path, sizeof(path), "%s/artifacts/caller.wasm", package_dir);
        write_file(path, caller_wasm, caller_wasm_size);
        components[0].id = "echo";
        components[0].kind = PXA_COMPONENT_KIND_SERVICE;
        components[0].artifact_path = "artifacts/provider.wasm";
        components[1].id = "main";
        components[1].kind = PXA_COMPONENT_KIND_UI;
        components[1].artifact_path = "artifacts/caller.wasm";
        files[0].path = "artifacts/caller.wasm";
        files[0].size = (uint64_t)caller_wasm_size;
        files[0].sha256 = caller_sha;
        files[1].path = "artifacts/provider.wasm";
        files[1].size = (uint64_t)provider_wasm_size;
        files[1].sha256 = provider_sha;
        encoded_size = pxa_test_encode_manifest_multi(
            manifest_bytes, sizeof(manifest_bytes), provider_sha,
            "com.example.ipctest", "0.1.0", components, 2, files, 2);
        CHECK(encoded_size != 0);
        snprintf(path, sizeof(path), "%s/manifest.pxm", package_dir);
        write_file(path, manifest_bytes, encoded_size);
    }

    pxa_package_limits_init(&package_limits);
    manifest_workspace =
        malloc(pxa_package_manifest_workspace_size(&package_limits));
    CHECK(manifest_workspace != NULL);
    check_status(
        "manifest parse",
        pxa_package_manifest_parse(
            manifest_workspace,
            pxa_package_manifest_workspace_size(&package_limits),
            (pxa_bytes_t){manifest_bytes, encoded_size}, &package_limits,
            &manifest),
        PXA_STATUS_OK);
    CHECK(manifest != NULL);

    memset(&host_profile, 0, sizeof(host_profile));
    host_profile.target = (pxa_bytes_t){(const uint8_t *)"linux-x86_64", 13};
    host_profile.engine = (pxa_bytes_t){(const uint8_t *)"wamr", 4};
    host_profile.engine_abi = (pxa_bytes_t){(const uint8_t *)"wasm32", 6};
    host_profile.memory_model = PXA_MEMORY_WASM32;
    memset(&capabilities, 0, sizeof(capabilities));
    capabilities.core_version.major = PXA_CORE_VERSION_MAJOR;
    capabilities.core_version.minor = PXA_CORE_VERSION_MINOR;
    {
        const size_t plan_size =
            pxa_activation_plan_workspace_size(package_limits.max_components);
        plan_workspace = malloc(plan_size);
        CHECK(plan_workspace != NULL);
        check_status(
            "plan prepare",
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
                         coordinator_workspace, coordinator_size, runtime,
                         plan, &engine_ops, &coordinator),
                     PXA_STATUS_OK);
        CHECK(coordinator != NULL);
    }

    /* 6. Declare the endpoint, then let the caller's startup IPC activate the
     * provider on demand. */
    memset(&lazy_probe, 0, sizeof(lazy_probe));
    lazy_probe.coordinator = coordinator;
    lazy_probe.engine = engine;
    check_status("endpoint resolver",
                 pxa_ipc_broker_set_endpoint_resolver(
                     broker, &lazy_probe, resolve_echo_endpoint),
                 PXA_STATUS_OK);
    check_status("endpoint declare",
                 pxa_ipc_endpoint_declare(
                     broker,
                     (pxa_bytes_t){(const uint8_t *)"com.example.echo",
                                   sizeof("com.example.echo") - 1}),
                 PXA_STATUS_OK);
    CHECK(lazy_probe.calls == 0);
    check_status("activate main",
                 pxa_activation_activate(
                     coordinator,
                     (pxa_bytes_t){(const uint8_t *)"main", 4}, 2,
                     &caller),
                 PXA_STATUS_OK);
    CHECK(caller != PXA_COMPONENT_INVALID);
    check_status("resolve pending endpoint", pxa_ipc_flush(broker),
                 PXA_STATUS_OK);
    CHECK(lazy_probe.calls == 1);
    CHECK(!lazy_probe.engine_was_busy);
    provider = lazy_probe.provider;
    CHECK(provider != PXA_COMPONENT_INVALID);

    /* 7. Drain both components until idle. */
    for (rounds = 0; rounds < 32; ++rounds) {
        pxa_status_t provider_status;
        pxa_status_t caller_status;
        provider_status =
            pxa_wamr_engine_deliver_event(engine, runtime, provider);
        caller_status = pxa_wamr_engine_deliver_event(engine, runtime, caller);
        if (provider_status == PXA_STATUS_WOULD_BLOCK &&
            caller_status == PXA_STATUS_WOULD_BLOCK) {
            break;
        }
        if ((provider_status != PXA_STATUS_OK &&
             provider_status != PXA_STATUS_WOULD_BLOCK) ||
            (caller_status != PXA_STATUS_OK &&
             caller_status != PXA_STATUS_WOULD_BLOCK)) {
            check_status("deliver provider", provider_status, PXA_STATUS_OK);
            check_status("deliver caller", caller_status, PXA_STATUS_OK);
            break;
        }
    }
    CHECK(rounds < 32);

    /* 8. Verify the observable state. */
    value_size = 0;
    check_status("storage ipc_verified",
                 storage_backend.get(
                     storage_backend.context,
                     (pxa_bytes_t){(const uint8_t *)"ipc_verified",
                                   sizeof("ipc_verified") - 1},
                     value, sizeof(value), &value_size),
                 PXA_STATUS_OK);
    CHECK(value_size == 1);
    CHECK(value[0] == '1');

    /* 9. Teardown. */
    pxa_activation_deactivate_all(coordinator, PXA_STOP_NORMAL);
    pxa_wamr_engine_deinit(engine);
    free(coordinator_workspace);
    free(plan_workspace);
    free(manifest_workspace);
    free(engine_workspace);
    free(broker_workspace);
    free(storage_workspace);
    free(storage_service_workspace);
    free(runtime_workspace);
    free(package_dir);
    free(storage_root);

    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("test_ipc_engine OK\n");
    return 0;
}
