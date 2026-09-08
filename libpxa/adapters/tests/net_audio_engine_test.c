#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include "pxa/activation.h"
#include "pxa/audio.h"
#include "pxa/net.h"
#include "pxa/openssl/pxa_openssl.h"
#include "pxa/package.h"
#include "pxa/permission.h"
#include "pxa/posix/pxa_posix_storage.h"
#include "pxa/runtime.h"
#include "pxa/storage.h"
#include "pxa/wamr/pxa_wamr_engine.h"

#include "test_manifest.h"

/* Net + audio integration: the guest acquires net.client and audio.playback
 * permissions, sends a v1.1 HTTPS request whose body is read through pxa_io, and
 * opens an audio session plus speaker graph. A simulated net backend and a
 * counting audio backend back the services. */

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
    (void)context;
    return 1000000;
}

/* --- permission store --------------------------------------------------- */

typedef struct {
    uint8_t allow_net;
    uint8_t allow_audio;
} permission_store_t;

static pxa_status_t permission_load(void *context, pxa_bytes_t app_identity,
                                    pxa_bytes_t name, pxa_bytes_t scope,
                                    pxa_permission_decision_t *decision) {
    permission_store_t *store = (permission_store_t *)context;
    (void)app_identity;
    (void)scope;
    if (name.size == 10 && memcmp(name.data, "net.client", 10) == 0) {
        *decision = store->allow_net ? PXA_PERMISSION_ALLOW
                                     : PXA_PERMISSION_DENY;
        return PXA_STATUS_OK;
    }
    if (name.size == 14 && memcmp(name.data, "audio.playback", 14) == 0) {
        *decision = store->allow_audio ? PXA_PERMISSION_ALLOW
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
    if (name.size == 10 && memcmp(name.data, "net.client", 10) == 0) {
        store->allow_net = decision == PXA_PERMISSION_ALLOW;
    }
    if (name.size == 14 && memcmp(name.data, "audio.playback", 14) == 0) {
        store->allow_audio = decision == PXA_PERMISSION_ALLOW;
    }
    return PXA_STATUS_OK;
}

/* --- net backend: one immediate 200 response with body "hi" ------------- */

static uint32_t g_net_start_count;
static uint8_t g_net_request_valid;

static pxa_status_t net_start(void *context, const pxa_net_request_t *request,
                              uint64_t *operation) {
    static const uint8_t expected_body[] = "{\"hello\":true}";
    (void)context;
    g_net_request_valid =
        request != NULL && request->struct_size >= sizeof(*request) &&
        request->abi_minor == 1 && request->method == PXA_NET_METHOD_POST &&
        request->timeout_ms == 2500 && request->header_count == 1 &&
        request->wanted_response_header_count == 1 &&
        request->body.size == sizeof(expected_body) - 1 &&
        memcmp(request->body.data, expected_body, sizeof(expected_body) - 1) == 0 &&
        request->headers[0].name.size == sizeof("content-type") - 1 &&
        memcmp(request->headers[0].name.data, "content-type",
               sizeof("content-type") - 1) == 0 &&
        request->headers[0].value.size == sizeof("application/json") - 1 &&
        memcmp(request->headers[0].value.data, "application/json",
               sizeof("application/json") - 1) == 0 &&
        request->wanted_response_headers[0].size == sizeof("etag") - 1 &&
        memcmp(request->wanted_response_headers[0].data, "etag",
               sizeof("etag") - 1) == 0;
    if (!g_net_request_valid) return PXA_STATUS_INVALID_ARGUMENT;
    ++g_net_start_count;
    *operation = 1;
    return PXA_STATUS_OK;
}

static pxa_status_t net_poll(void *context, uint64_t operation,
                             pxa_net_response_t *response) {
    static const uint8_t content_type[] = "text/html";
    static const uint8_t etag_name[] = "etag";
    static const uint8_t etag_value[] = "\"test-v1\"";
    static const pxa_net_header_t headers[] = {{
        {etag_name, sizeof(etag_name) - 1},
        {etag_value, sizeof(etag_value) - 1}}};
    (void)context;
    (void)operation;
    response->struct_size = sizeof(*response);
    response->status_code = 200;
    response->content_type.data = content_type;
    response->content_type.size = sizeof(content_type) - 1;
    response->body_stream = (void *)0x1;
    response->headers = headers;
    response->header_count = 1;
    response->body_length = 2;
    response->flags = PXA_NET_RESPONSE_BODY_PRESENT |
                      PXA_NET_RESPONSE_BODY_LENGTH_KNOWN;
    return PXA_STATUS_OK;
}

static void net_cancel(void *context, uint64_t operation) {
    (void)context;
    (void)operation;
}

static pxa_status_t net_read_body(void *context, void *body_stream,
                                  uint8_t *output, size_t capacity,
                                  size_t *size) {
    static const uint8_t body[] = {'h', 'i'};
    (void)context;
    (void)body_stream;
    if (capacity < sizeof(body)) return PXA_STATUS_RESOURCE_LIMIT;
    memcpy(output, body, sizeof(body));
    *size = sizeof(body);
    return PXA_STATUS_OK;
}

static void net_close_body(void *context, void *body_stream) {
    (void)context;
    (void)body_stream;
}

/* --- audio backend: counting sessions ----------------------------------- */

static uint32_t g_audio_open_count;
static uint32_t g_audio_commit_count;
static uint32_t g_audio_submit_count;

static pxa_status_t audio_open(void *context, uint16_t usage,
                               pxa_audio_format_t *format,
                               uint64_t *provider_session) {
    (void)context;
    (void)usage;
    ++g_audio_open_count;
    format->sample_rate = 16000;
    format->channels = 1;
    format->frame_ms = 20;
    *provider_session = 1;
    return PXA_STATUS_OK;
}

static pxa_status_t audio_commit(void *context, uint64_t provider_session,
                                 const pxa_audio_graph_t *graph) {
    (void)context;
    (void)provider_session;
    (void)graph;
    ++g_audio_commit_count;
    return PXA_STATUS_OK;
}

static pxa_status_t audio_submit(void *context, uint64_t provider_session,
                                 const uint8_t *pcm, size_t size) {
    (void)context;
    if (provider_session != 1 || pcm == NULL || size != 640) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    ++g_audio_submit_count;
    return PXA_STATUS_OK;
}

static void audio_close(void *context, uint64_t provider_session) {
    (void)context;
    (void)provider_session;
}

int main(void) {
    char *package_dir = make_temp_dir("pxa-net-package");
    char *storage_root = make_temp_dir("pxa-net-storage");
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
    pxa_permission_declaration_t declarations[2];
    pxa_permission_config_t permission_config;
    pxa_permission_service_t *permission_service = NULL;
    void *permission_workspace;
    pxa_net_backend_t net_backend;
    pxa_net_config_t net_config;
    pxa_net_service_t *net_service = NULL;
    void *net_workspace;
    pxa_audio_backend_t audio_backend;
    pxa_audio_config_t audio_config;
    pxa_audio_service_t *audio_service = NULL;
    void *audio_workspace;
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

    /* 3. Permission service, pre-granted for net and audio. */
    memset(&permission_store, 0, sizeof(permission_store));
    permission_store.allow_net = 1;
    permission_store.allow_audio = 1;
    declarations[0].name = (pxa_bytes_t){(const uint8_t *)"net.client", 10};
    declarations[0].scope = (pxa_bytes_t){
        (const uint8_t *)"https://example.test",
        sizeof("https://example.test") - 1};
    declarations[0].required = 0;
    declarations[1].name =
        (pxa_bytes_t){(const uint8_t *)"audio.playback", 14};
    declarations[1].scope = (pxa_bytes_t){(const uint8_t *)"media", 5};
    declarations[1].required = 0;
    memset(&permission_config, 0, sizeof(permission_config));
    permission_config.struct_size = sizeof(permission_config);
    permission_config.app_identity =
        (pxa_bytes_t){(const uint8_t *)"com.example.nettest", 19};
    permission_config.declarations = declarations;
    permission_config.declaration_count = 2;
    permission_config.max_authorities = 8;
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

    /* 4. Net service with the simulated backend. */
    memset(&net_backend, 0, sizeof(net_backend));
    net_backend.struct_size = sizeof(net_backend);
    net_backend.start = net_start;
    net_backend.poll = net_poll;
    net_backend.cancel = net_cancel;
    net_backend.read_body = net_read_body;
    net_backend.close_body = net_close_body;
    memset(&net_config, 0, sizeof(net_config));
    net_config.struct_size = sizeof(net_config);
    net_config.max_pending_requests = 4;
    net_config.max_requests_per_component = 2;
    net_config.max_response_streams = 4;
    net_config.max_response_bytes = 2048;
    net_config.max_headers = 8;
    net_config.max_inline_body_bytes = 2048;
    net_config.max_request_header_bytes = 2048;
    net_config.max_response_header_bytes = 2048;
    net_config.min_timeout_ms = 100;
    net_config.default_timeout_ms = 15000;
    net_config.max_timeout_ms = 60000;
    net_config.backend = net_backend;
    net_config.permissions = permission_service;
    {
        const size_t service_workspace_size =
            pxa_net_service_workspace_size(&net_config);
        CHECK(service_workspace_size != 0);
        net_workspace = malloc(service_workspace_size);
        CHECK(net_workspace != NULL);
        check_status("net service init",
                     pxa_net_service_init(net_workspace, service_workspace_size,
                                          runtime, &net_config, &net_service),
                     PXA_STATUS_OK);
        check_status("net service register",
                     pxa_net_service_register(net_service), PXA_STATUS_OK);
    }

    /* 5. Audio service with the counting backend. */
    memset(&audio_backend, 0, sizeof(audio_backend));
    audio_backend.struct_size = sizeof(audio_backend);
    audio_backend.open = audio_open;
    audio_backend.commit = audio_commit;
    audio_backend.submit = audio_submit;
    audio_backend.close = audio_close;
    memset(&audio_config, 0, sizeof(audio_config));
    audio_config.struct_size = sizeof(audio_config);
    audio_config.max_sessions = 4;
    audio_config.max_sessions_per_component = 2;
    audio_config.max_eq_bands = 4;
    audio_config.backend = audio_backend;
    audio_config.permissions = permission_service;
    {
        const size_t service_workspace_size =
            pxa_audio_service_workspace_size(&audio_config);
        CHECK(service_workspace_size != 0);
        audio_workspace = malloc(service_workspace_size);
        CHECK(audio_workspace != NULL);
        check_status("audio service init",
                     pxa_audio_service_init(audio_workspace,
                                            service_workspace_size, runtime,
                                            &audio_config, &audio_service),
                     PXA_STATUS_OK);
        check_status("audio service register",
                     pxa_audio_service_register(audio_service),
                     PXA_STATUS_OK);
    }

    /* 6. Engine. */
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

    /* 7. Package. */
    fd = open(PXA_NET_AUDIO_TEST_GUEST_PATH, O_RDONLY);
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
        manifest_bytes, sizeof(manifest_bytes), sha, "com.example.nettest",
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

    /* 8. Activate, drain the request chain, then poll net to completion. */
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
    CHECK(delivered >= 3);
    CHECK(g_net_start_count == 1);
    CHECK(g_net_request_valid == 1);
    affected_count = 0;
    check_status("net poll",
                 pxa_net_poll(net_service, affected, 2, &affected_count),
                 PXA_STATUS_OK);
    CHECK(affected_count == 1);
    for (delivered = 0; delivered < 8; ++delivered) {
        status = pxa_wamr_engine_deliver_event(engine, runtime, affected[0]);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) {
            check_status("deliver net response", status, PXA_STATUS_OK);
            break;
        }
    }
    CHECK(delivered >= 1);
    CHECK(g_audio_open_count == 1);
    CHECK(g_audio_commit_count == 1);
    CHECK(g_audio_submit_count == 1);

    /* 9. Verify the observable state. */
    value_size = 0;
    check_status("storage net_ok",
                 storage_backend.get(
                     storage_backend.context,
                     (pxa_bytes_t){(const uint8_t *)"net_ok",
                                   sizeof("net_ok") - 1},
                     value, sizeof(value), &value_size),
                 PXA_STATUS_OK);
    CHECK(value_size == 1);
    CHECK(value[0] == '1');
    value_size = 0;
    check_status("storage audio_ok",
                 storage_backend.get(
                     storage_backend.context,
                     (pxa_bytes_t){(const uint8_t *)"audio_ok",
                                   sizeof("audio_ok") - 1},
                     value, sizeof(value), &value_size),
                 PXA_STATUS_OK);
    CHECK(value_size == 1);
    CHECK(value[0] == '1');

    /* 10. Teardown. */
    pxa_activation_deactivate_all(coordinator, PXA_STOP_NORMAL);
    pxa_wamr_engine_deinit(engine);
    free(coordinator_workspace);
    free(plan_workspace);
    free(manifest_workspace);
    free(engine_workspace);
    free(audio_workspace);
    free(net_workspace);
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
    printf("test_net_audio_engine OK\n");
    return 0;
}
