#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include "pxa/activation.h"
#include "pxa/lease.h"
#include "pxa/openssl/pxa_openssl.h"
#include "pxa/package.h"
#include "pxa/posix/pxa_posix_storage.h"
#include "pxa/runtime.h"
#include "pxa/scheduler.h"
#include "pxa/storage.h"
#include "pxa/wamr/pxa_wamr_engine.h"

#include "test_manifest.h"

/* Lease + Work integration: the UI guest acquires a foreground lease and
 * enqueues work; the host advances a fake clock, takes the due item and
 * activates the worker with its Work context; later revocation of
 * the expired lease is observed by the guest. All outcomes are persisted
 * through Storage. */

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

static uint64_t fake_now_us;

static uint64_t fake_clock_us(void *context) {
    (void)context;
    return fake_now_us;
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

/* --- scheduler store: in-memory entries -------------------------------- */

typedef struct {
    pxa_scheduler_entry_t entries[8];
    size_t count;
} scheduler_store_t;

static pxa_status_t scheduler_load(void *context, pxa_scheduler_entry_t *entries,
                                   size_t capacity, size_t *count) {
    scheduler_store_t *store = (scheduler_store_t *)context;
    size_t index;
    *count = 0;
    for (index = 0; index < store->count && index < capacity; ++index) {
        entries[index] = store->entries[index];
        ++*count;
    }
    return PXA_STATUS_OK;
}

static pxa_status_t scheduler_save(void *context,
                                   const pxa_scheduler_entry_t *entries,
                                   size_t count) {
    scheduler_store_t *store = (scheduler_store_t *)context;
    size_t index;
    store->count = 0;
    for (index = 0; index < count && index < 8; ++index) {
        store->entries[index] = entries[index];
        ++store->count;
    }
    return PXA_STATUS_OK;
}

int main(void) {
    char *package_dir = make_temp_dir("pxa-sched-package");
    char *storage_root = make_temp_dir("pxa-sched-storage");
    pxa_runtime_limits_t limits;
    pxa_runtime_t *runtime = NULL;
    void *runtime_workspace;
    pxa_storage_backend_t storage_backend;
    pxa_storage_config_t storage_config;
    pxa_storage_service_t *storage_service = NULL;
    void *storage_workspace;
    void *storage_service_workspace;
    pxa_posix_storage_t *posix_storage = NULL;
    pxa_lease_limits_t lease_limits;
    pxa_lease_service_t *lease_service = NULL;
    void *lease_workspace;
    scheduler_store_t scheduler_store;
    pxa_scheduler_config_t scheduler_config;
    pxa_scheduler_service_t *scheduler_service = NULL;
    void *scheduler_workspace;
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
    pxa_component_t job_component = PXA_COMPONENT_INVALID;
    uint8_t manifest_bytes[PXA_PACKAGE_TEST_MANIFEST_BYTES];
    uint8_t wasm_bytes[65536];
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
    pxa_package_activation_profile_t capabilities;
    pxa_test_component_t components[2];
    pxa_test_file_t files[1];
    pxa_scheduler_entry_t due[2];
    size_t due_count = 0;
    pxa_component_t affected[2];
    size_t affected_count = 0;
    uint8_t job_config[64];
    size_t job_config_size = 0;

    fake_now_us = 1000000;

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

    /* 2. Storage service (observability sink). */
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

    /* 3. Lease service (foreground kind only) on the fake clock. */
    pxa_lease_limits_init(&lease_limits);
    lease_limits.allowed_kinds = 1u; /* PXA_LEASE_FOREGROUND (kind 1) */
    lease_limits.max_leases_per_component = 2;
    lease_limits.max_leases = 4;
    lease_limits.default_duration_ms = 5000;
    lease_limits.max_duration_ms = 60000;
    lease_limits.clock_context = NULL;
    lease_limits.clock = fake_clock_us;
    lease_workspace = malloc(pxa_lease_service_workspace_size(&lease_limits));
    CHECK(lease_workspace != NULL);
    check_status("lease service init",
                 pxa_lease_service_init(lease_workspace,
                                        pxa_lease_service_workspace_size(
                                            &lease_limits),
                                        runtime, &lease_limits,
                                        &lease_service),
                 PXA_STATUS_OK);
    check_status("lease service register",
                 pxa_lease_service_register(lease_service), PXA_STATUS_OK);

    /* 4. Scheduler service with the in-memory store. */
    memset(&scheduler_store, 0, sizeof(scheduler_store));
    pxa_scheduler_config_init(&scheduler_config);
    {
        static pxa_bytes_t job_components_storage[1];
        job_components_storage[0] =
            (pxa_bytes_t){(const uint8_t *)"job", 3};
        scheduler_config.job_components = job_components_storage;
        scheduler_config.job_component_count = 1;
    }
    scheduler_config.clock_context = NULL;
    scheduler_config.clock = fake_clock_us;
    scheduler_config.store.context = &scheduler_store;
    scheduler_config.store.load = scheduler_load;
    scheduler_config.store.save = scheduler_save;
    scheduler_workspace =
        malloc(pxa_scheduler_service_workspace_size(&scheduler_config));
    CHECK(scheduler_workspace != NULL);
    check_status("scheduler service init",
                 pxa_scheduler_service_init(scheduler_workspace,
                                            pxa_scheduler_service_workspace_size(
                                                &scheduler_config),
                                            runtime, &scheduler_config,
                                            &scheduler_service),
                 PXA_STATUS_OK);
    check_status("scheduler load",
                 pxa_scheduler_load(scheduler_service), PXA_STATUS_OK);
    check_status("scheduler service register",
                 pxa_scheduler_service_register(scheduler_service),
                 PXA_STATUS_OK);

    /* 5. Engine. */
    memset(&engine_config, 0, sizeof(engine_config));
    engine_config.struct_size = sizeof(engine_config);
    engine_config.read_artifact = read_artifact;
    engine_config.now_us = fake_clock_us;
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

    /* 6. Package: main + job components sharing one artifact. */
    fd = open(PXA_SCHEDULER_TEST_GUEST_PATH, O_RDONLY);
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
    make_dirs(package_dir);
    snprintf(path, sizeof(path), "%s/artifacts", package_dir);
    make_dirs(path);
    snprintf(path, sizeof(path), "%s/artifacts/main.wasm", package_dir);
    write_file(path, wasm_bytes, wasm_size);
    components[0].id = "job";
    components[0].kind = PXA_COMPONENT_KIND_SERVICE;
    components[0].artifact_path = "artifacts/main.wasm";
    components[1].id = "main";
    components[1].kind = PXA_COMPONENT_KIND_UI;
    components[1].artifact_path = "artifacts/main.wasm";
    files[0].path = "artifacts/main.wasm";
    files[0].size = (uint64_t)wasm_size;
    files[0].sha256 = sha;
    encoded_size = pxa_test_encode_manifest_multi(
        manifest_bytes, sizeof(manifest_bytes), sha, "com.example.schedtest",
        "0.1.0", components, 2, files, 1);
    CHECK(encoded_size != 0);
    snprintf(path, sizeof(path), "%s/manifest.pxm", package_dir);
    write_file(path, manifest_bytes, encoded_size);

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

    /* 7. Activate main, drain (acquire lease, enqueue Work). */
    check_status("activate main",
                 pxa_activation_activate(
                     coordinator,
                     (pxa_bytes_t){(const uint8_t *)"main", 4}, 1,
                     &component),
                 PXA_STATUS_OK);
    CHECK(component != PXA_COMPONENT_INVALID);
    for (delivered = 0; delivered < 32; ++delivered) {
        status = pxa_wamr_engine_deliver_event(engine, runtime, component);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) {
            check_status("deliver main", status, PXA_STATUS_OK);
            break;
        }
    }
    CHECK(delivered >= 2);

    /* 8. Advance the clock, take the due Work, activate it with the Work
     * config, then deactivate. */
    fake_now_us += 3000000;
    check_status("scheduler take due",
                 pxa_scheduler_take_due(scheduler_service, due, 2,
                                        &due_count),
                 PXA_STATUS_OK);
    CHECK(due_count == 1);
    check_status("encode Work context",
                 pxa_scheduler_encode_start_config(
                     &due[0], fake_now_us / 1000u + due[0].max_execution_ms,
                     job_config, sizeof(job_config), &job_config_size),
                 PXA_STATUS_OK);
    check_status("engine set job config",
                 pxa_wamr_engine_set_config(
                     engine, 2,
                     (pxa_bytes_t){job_config, job_config_size}),
                 PXA_STATUS_OK);
    check_status("activate job",
                 pxa_activation_activate(
                     coordinator, (pxa_bytes_t){(const uint8_t *)"job", 3}, 2,
                     &job_component),
                 PXA_STATUS_OK);
    CHECK(job_component != PXA_COMPONENT_INVALID);
    check_status("deactivate job",
                 pxa_activation_deactivate(
                     coordinator, (pxa_bytes_t){(const uint8_t *)"job", 3},
                     PXA_STOP_NORMAL),
                 PXA_STATUS_OK);

    /* 9. Advance the clock past the lease, revoke, deliver the revocation. */
    fake_now_us += 6000000;
    affected_count = 0;
    check_status("lease revoke expired",
                 pxa_lease_revoke_expired(lease_service, affected, 2,
                                          &affected_count),
                 PXA_STATUS_OK);
    CHECK(affected_count == 1);
    for (delivered = 0; delivered < 8; ++delivered) {
        status = pxa_wamr_engine_deliver_event(engine, runtime, affected[0]);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) {
            check_status("deliver revocation", status, PXA_STATUS_OK);
            break;
        }
    }
    CHECK(delivered >= 1);

    /* 10. Verify the observable state. */
    value_size = 0;
    check_status("storage job_done",
                 storage_backend.get(
                     storage_backend.context,
                     (pxa_bytes_t){(const uint8_t *)"job_done",
                                   sizeof("job_done") - 1},
                     value, sizeof(value), &value_size),
                 PXA_STATUS_OK);
    CHECK(value_size == 1);
    CHECK(value[0] == '1');
    value_size = 0;
    check_status("storage lease_revoked",
                 storage_backend.get(
                     storage_backend.context,
                     (pxa_bytes_t){(const uint8_t *)"lease_revoked",
                                   sizeof("lease_revoked") - 1},
                     value, sizeof(value), &value_size),
                 PXA_STATUS_OK);
    CHECK(value_size == 1);
    CHECK(value[0] == '1');

    /* 11. Teardown. */
    pxa_wamr_engine_deinit(engine);
    free(coordinator_workspace);
    free(plan_workspace);
    free(manifest_workspace);
    free(engine_workspace);
    free(scheduler_workspace);
    free(lease_workspace);
    free(storage_workspace);
    free(storage_service_workspace);
    free(runtime_workspace);
    free(package_dir);
    free(storage_root);

    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("test_scheduler_engine OK\n");
    return 0;
}
