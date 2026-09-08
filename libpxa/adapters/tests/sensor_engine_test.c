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
#include "pxa/openssl/pxa_openssl.h"
#include "pxa/package.h"
#include "pxa/permission.h"
#include "pxa/posix/pxa_posix_storage.h"
#include "pxa/runtime.h"
#include "pxa/sensor.h"
#include "pxa/storage.h"
#include "pxa/wamr/pxa_wamr_engine.h"

#include "test_manifest.h"

/* Permission + sensor integration: the guest acquires a sensor.read
 * permission (pre-granted by the host through the permission store), lists
 * sensor descriptors, subscribes to the temperature channel, and a host poll
 * produces a sample the guest verifies and persists through Storage. */

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

/* --- permission store: in-memory decisions ------------------------------ */

typedef struct {
    uint8_t allow_sensor_read;
} permission_store_t;

static pxa_status_t permission_load(void *context, pxa_bytes_t app_identity,
                                    pxa_bytes_t name, pxa_bytes_t scope,
                                    pxa_permission_decision_t *decision) {
    permission_store_t *store = (permission_store_t *)context;
    (void)app_identity;
    (void)scope;
    if (name.size == 11 && memcmp(name.data, "sensor.read", 11) == 0) {
        *decision = store->allow_sensor_read ? PXA_PERMISSION_ALLOW
                                             : PXA_PERMISSION_DENY;
        return PXA_STATUS_OK;
    }
    *decision = PXA_PERMISSION_DENY;
    return PXA_STATUS_OK;
}

static pxa_status_t permission_save(void *context, pxa_bytes_t app_identity,
                                    pxa_bytes_t name, pxa_bytes_t scope,
                                    pxa_permission_decision_t decision) {
    permission_store_t *store = (permission_store_t *)context;
    (void)app_identity;
    (void)scope;
    if (name.size == 11 && memcmp(name.data, "sensor.read", 11) == 0) {
        store->allow_sensor_read = decision == PXA_PERMISSION_ALLOW;
    }
    return PXA_STATUS_OK;
}

/* --- sensor backend: one temperature channel at 25.0 C ------------------ */

static uint32_t g_sensor_subscribe_count;

static pxa_status_t sensor_subscribe(void *context, uint16_t descriptor_id,
                                     uint32_t period_ms,
                                     void **subscription) {
    (void)context;
    (void)descriptor_id;
    (void)period_ms;
    ++g_sensor_subscribe_count;
    *subscription = (void *)0x1;
    return PXA_STATUS_OK;
}

static pxa_status_t sensor_read(void *context, void *subscription,
                                uint16_t descriptor_id,
                                int32_t values[PXA_SENSOR_MAX_DIMENSIONS]) {
    (void)context;
    (void)subscription;
    (void)descriptor_id;
    values[0] = 2500; /* 25.0 C in milli-celsius */
    return PXA_STATUS_OK;
}

static void sensor_unsubscribe(void *context, void *subscription,
                               uint16_t descriptor_id) {
    (void)context;
    (void)subscription;
    (void)descriptor_id;
}

int main(void) {
    char *package_dir = make_temp_dir("pxa-sensor-package");
    char *storage_root = make_temp_dir("pxa-sensor-storage");
    pxa_runtime_limits_t limits;
    pxa_runtime_t *runtime = NULL;
    void *runtime_workspace;
    pxa_storage_backend_t storage_backend;
    pxa_storage_config_t storage_config;
    pxa_storage_service_t *storage_service = NULL;
    void *storage_workspace;
    void *storage_service_workspace;
    pxa_posix_storage_t *posix_storage = NULL;
    permission_store_t permission_store;
    pxa_permission_declaration_t declarations[1];
    pxa_permission_config_t permission_config;
    pxa_permission_service_t *permission_service = NULL;
    void *permission_workspace;
    pxa_sensor_descriptor_t descriptors[1];
    pxa_sensor_config_t sensor_config;
    pxa_sensor_service_t *sensor_service = NULL;
    void *sensor_workspace;
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
    uint8_t wasm_bytes[65536];
    uint8_t sha[32];
    uint8_t value[32];
    size_t value_size = 0;
    size_t encoded_size;
    size_t wasm_size = 0;
    char path[512];
    int delivered = 0;
    int polled = 0;
    int fd;
    pxa_status_t status;
    pxa_package_host_profile_t host_profile;
    pxa_package_activation_profile_t capabilities;
    pxa_component_t affected[2];
    size_t affected_count = 0;

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

    /* 3. Permission service with the in-memory store, pre-granted. */
    memset(&permission_store, 0, sizeof(permission_store));
    permission_store.allow_sensor_read = 1;
    declarations[0].name =
        (pxa_bytes_t){(const uint8_t *)"sensor.read", 11};
    declarations[0].scope =
        (pxa_bytes_t){(const uint8_t *)"ambient.temperature", 18};
    declarations[0].required = 0;
    memset(&permission_config, 0, sizeof(permission_config));
    permission_config.struct_size = sizeof(permission_config);
    permission_config.app_identity =
        (pxa_bytes_t){(const uint8_t *)"com.example.sensortest", sizeof("com.example.sensortest") - 1};
    permission_config.declarations = declarations;
    permission_config.declaration_count = 1;
    permission_config.max_authorities = 4;
    permission_config.store.struct_size = sizeof(permission_config.store);
    permission_config.store.context = &permission_store;
    permission_config.store.load = permission_load;
    permission_config.store.save = permission_save;
    {
        const size_t service_workspace_size =
            pxa_permission_service_workspace_size(&permission_config);
        CHECK(service_workspace_size != 0);
        permission_workspace = malloc(service_workspace_size);
        CHECK(permission_workspace != NULL);
        check_status("permission service init",
                     pxa_permission_service_init(
                         permission_workspace, service_workspace_size, runtime,
                         &permission_config, &permission_service),
                     PXA_STATUS_OK);
        check_status("permission policy load",
                     pxa_permission_policy_load(permission_service),
                     PXA_STATUS_OK);
        check_status("permission service register",
                     pxa_permission_service_register(permission_service),
                     PXA_STATUS_OK);
    }

    /* 4. Sensor service with the simulated temperature channel. */
    descriptors[0].id = 1;
    descriptors[0].semantic =
        (pxa_bytes_t){(const uint8_t *)"ambient.temperature", 18};
    descriptors[0].unit = PXA_SENSOR_UNIT_MILLI_CELSIUS;
    descriptors[0].dimensions = 1;
    descriptors[0].min_period_ms = 1000;
    descriptors[0].max_period_ms = 60000;
    memset(&sensor_config, 0, sizeof(sensor_config));
    sensor_config.struct_size = sizeof(sensor_config);
    sensor_config.descriptors = descriptors;
    sensor_config.descriptor_count = 1;
    sensor_config.max_subscriptions = 4;
    sensor_config.max_subscriptions_per_component = 2;
    sensor_config.provider_context = NULL;
    sensor_config.subscribe = sensor_subscribe;
    sensor_config.read = sensor_read;
    sensor_config.unsubscribe = sensor_unsubscribe;
    sensor_config.permissions = permission_service;
    {
        const size_t service_workspace_size =
            pxa_sensor_service_workspace_size(&sensor_config);
        CHECK(service_workspace_size != 0);
        sensor_workspace = malloc(service_workspace_size);
        CHECK(sensor_workspace != NULL);
        check_status("sensor service init",
                     pxa_sensor_service_init(sensor_workspace,
                                             service_workspace_size, runtime,
                                             &sensor_config, &sensor_service),
                     PXA_STATUS_OK);
        check_status("sensor service register",
                     pxa_sensor_service_register(sensor_service),
                     PXA_STATUS_OK);
    }

    /* 5. Engine. */
    memset(&engine_config, 0, sizeof(engine_config));
    engine_config.struct_size = sizeof(engine_config);
    engine_config.read_artifact = read_artifact;
    engine_config.now_us = now_us;
    engine_config.guest_stack_size = 128 * 1024;
    engine_config.max_components = 2;
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

    /* 6. Package: manifest + guest artifact. */
    fd = open(PXA_SENSOR_TEST_GUEST_PATH, O_RDONLY);
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
        manifest_bytes, sizeof(manifest_bytes), sha, "com.example.sensortest",
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

    /* 7. Activate, drain the request chain, then poll a sample. */
    check_status("activate",
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
            check_status("deliver event", status, PXA_STATUS_OK);
            break;
        }
    }
    CHECK(delivered >= 2);
    CHECK(g_sensor_subscribe_count == 1);
    for (polled = 0; polled < 4; ++polled) {
        status = pxa_sensor_poll(sensor_service, now_us(NULL) + 2000000,
                                 affected, 2, &affected_count);
        if (status != PXA_STATUS_OK) break;
        if (affected_count != 0) {
            size_t index;
            for (index = 0; index < affected_count; ++index) {
                status = pxa_wamr_engine_deliver_event(engine, runtime,
                                                       affected[index]);
                if (status != PXA_STATUS_OK &&
                    status != PXA_STATUS_WOULD_BLOCK) {
                    check_status("deliver sample", status, PXA_STATUS_OK);
                    break;
                }
            }
            break;
        }
    }
    CHECK(polled < 4);

    /* 8. Verify the observable state. */
    value_size = 0;
    check_status("storage sensor_ok",
                 storage_backend.get(
                     storage_backend.context,
                     (pxa_bytes_t){(const uint8_t *)"sensor_ok",
                                   sizeof("sensor_ok") - 1},
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
    free(sensor_workspace);
    free(permission_workspace);
    free(storage_workspace);
    free(storage_service_workspace);
    free(runtime_workspace);
    free(package_dir);
    free(storage_root);

    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("test_sensor_engine OK\n");
    return 0;
}
