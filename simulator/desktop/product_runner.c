#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <SDL2/SDL.h>

#include "lvgl.h"
#include "pxa/activation.h"
#include "pxa/audio.h"
#include "pxa/lvgl/pxa_lvgl_ui.h"
#include "pxa/openssl/pxa_openssl.h"
#include "pxa/package.h"
#include "pxa/permission.h"
#include "pxa/posix/pxa_posix_installer.h"
#include "pxa/runtime.h"
#include "pxa/service.h"
#include "pxa/ui.h"
#include "pxa/wamr/pxa_wamr_engine.h"
#include "pxa/window.h"

#define PRODUCT_CLOCK_SERVICE UINT16_C(4)
#define PRODUCT_CLOCK_TICK UINT16_C(0x8001)
#define PRODUCT_COMPONENTS UINT16_C(8)

typedef struct {
    const char *package_path;
    const char *publisher_key;
    uint32_t width;
    uint32_t height;
} options_t;

typedef struct {
    pxa_runtime_t *runtime;
    pxa_window_service_t *window;
    pxa_ui_service_t *ui;
    pxa_ui_backend_t ui_backend;
    pxa_audio_service_t *audio;
    pxa_permission_service_t *permissions;
    pxa_wamr_engine_t *engine;
    pxa_component_engine_t engine_ops;
    pxa_activation_coordinator_t *coordinator;
    pxa_component_t active_component;
    char package_root[1024];
    uint32_t width;
    uint32_t height;
    uint16_t clock_period_ms;
    uint64_t next_clock_tick_us;
} product_host_t;

static uint64_t now_us(void *context) {
    struct timespec now;
    (void)context;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000);
}

static void *allocate_memory(void *context, size_t size) {
    (void)context;
    return malloc(size);
}

static void release_memory(void *context, void *memory) {
    (void)context;
    free(memory);
}

static void *reallocate_memory(void *context, void *memory, size_t size) {
    (void)context;
    return realloc(memory, size);
}

static pxa_status_t execute_inline(pxa_lvgl_ui_execute_callback_fn callback,
                                   void *data, void *context) {
    (void)context;
    callback(data);
    return PXA_STATUS_OK;
}

static const void *resolve_asset(const uint8_t *path, size_t path_size,
                                 void *context) {
    product_host_t *host = context;
    char *full_path;
    if (host == NULL || path == NULL || path_size == 0 ||
        path_size > 512 || strlen(host->package_root) + path_size + 2 >= 1024)
        return NULL;
    full_path = malloc(strlen(host->package_root) + path_size + 2);
    if (full_path == NULL) return NULL;
    sprintf(full_path, "%s/%.*s", host->package_root, (int)path_size, path);
    return full_path;
}

static void release_asset(const void *asset, void *context) {
    (void)context;
    free((void *)asset);
}

static void dispatch_component_events(product_host_t *host) {
    pxa_wamr_event_result_t result;
    if (host == NULL || host->engine == NULL || host->runtime == NULL ||
        host->active_component == PXA_COMPONENT_INVALID)
        return;
    while (pxa_wamr_engine_deliver_event_result(host->engine, host->runtime,
                                                 host->active_component,
                                                 &result) == PXA_STATUS_OK) {
    }
}

static void dispatch_clock_tick(product_host_t *host) {
    uint64_t now;
    if (host == NULL || host->runtime == NULL ||
        host->active_component == PXA_COMPONENT_INVALID ||
        host->clock_period_ms == 0)
        return;
    now = now_us(NULL);
    if (now < host->next_clock_tick_us) return;
    host->next_clock_tick_us =
        now + (uint64_t)host->clock_period_ms * UINT64_C(1000);
    if (pxa_event_post_message(host->runtime, host->active_component,
                               PRODUCT_CLOCK_SERVICE, PRODUCT_CLOCK_TICK, 0,
                               (pxa_bytes_t){NULL, 0}, 0, 0) == PXA_STATUS_OK)
        dispatch_component_events(host);
}

static void ui_event(uint32_t surface, uint32_t node, pxa_ui_event_kind_t kind,
                     uint16_t flags, const void *value, size_t value_size,
                     void *context) {
    product_host_t *host = context;
    if (host == NULL || host->ui == NULL ||
        host->active_component == PXA_COMPONENT_INVALID)
        return;
    if (pxa_ui_queue_event(host->ui, host->active_component, surface, node,
                           kind, flags, now_us(NULL), value, value_size) ==
        PXA_STATUS_OK)
        dispatch_component_events(host);
}

static pxa_status_t window_apply(void *context,
                                 const pxa_window_configuration_t *config) {
    (void)context;
    (void)config;
    return PXA_STATUS_OK;
}

static pxa_status_t prepare_start(void *context, pxa_component_t component,
                                  uint64_t instance_id, uint8_t kind) {
    product_host_t *host = context;
    pxa_window_backend_t window_backend = {0};
    pxa_window_snapshot_t snapshot = {0};
    pxa_status_t status;
    (void)instance_id;
    if (host == NULL || kind != PXA_COMPONENT_KIND_UI) return PXA_STATUS_UNSUPPORTED;
    window_backend.struct_size = sizeof(window_backend);
    window_backend.context = host;
    window_backend.apply = window_apply;
    status = pxa_window_bind(host->window, component, &window_backend);
    if (status != PXA_STATUS_OK) return status;
    status = pxa_ui_bind(host->ui, component, &host->ui_backend);
    if (status != PXA_STATUS_OK) return status;
    snapshot.logical_width = host->width;
    snapshot.logical_height = host->height;
    snapshot.pixel_width = host->width;
    snapshot.pixel_height = host->height;
    snapshot.density_numerator = 1;
    snapshot.density_denominator = 1;
    snapshot.focused = 1;
    status = pxa_window_update_snapshot(host->window, component, &snapshot);
    if (status != PXA_STATUS_OK) return status;
    host->active_component = component;
    return PXA_STATUS_OK;
}

static pxa_status_t read_artifact(void *context, pxa_bytes_t path,
                                  uint8_t *output, size_t capacity,
                                  size_t *size) {
    FILE *file;
    long file_size;
    char filename[1536];
    product_host_t *host = context;
    if (host == NULL || path.data == NULL || size == NULL ||
        path.size == 0 || path.size >= sizeof(filename))
        return PXA_STATUS_INVALID_ARGUMENT;
    /* The WAMR adapter already joins the package root and artifact path. */
    memcpy(filename, path.data, path.size);
    filename[path.size] = '\0';
    file = fopen(filename, "rb");
    if (file == NULL) return PXA_STATUS_NOT_FOUND;
    if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) <= 0) {
        fclose(file);
        return PXA_STATUS_IO_ERROR;
    }
    rewind(file);
    if (output == NULL) {
        *size = (size_t)file_size;
        fclose(file);
        return PXA_STATUS_OK;
    }
    if ((size_t)file_size > capacity ||
        fread(output, 1, (size_t)file_size, file) != (size_t)file_size) {
        fclose(file);
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    fclose(file);
    *size = (size_t)file_size;
    return PXA_STATUS_OK;
}

static pxa_status_t clock_control(void *context, pxa_runtime_t *runtime,
                                  pxa_component_t component,
                                  const pxa_message_view_t *message) {
    product_host_t *host = context;
    (void)runtime;
    if (host == NULL || message == NULL || component != host->active_component)
        return PXA_STATUS_BAD_STATE;
    if (message->opcode != 1 || message->request_id != 0 ||
        message->payload.size != 2)
        return PXA_STATUS_UNSUPPORTED;
    host->clock_period_ms = (uint16_t)message->payload.data[0] |
                            (uint16_t)((uint16_t)message->payload.data[1] << 8);
    if (host->clock_period_ms != 0 &&
        (host->clock_period_ms < 16 || host->clock_period_ms > 1000))
        return PXA_STATUS_INVALID_ARGUMENT;
    host->next_clock_tick_us =
        now_us(NULL) + (uint64_t)host->clock_period_ms * UINT64_C(1000);
    return PXA_STATUS_OK;
}

static pxa_status_t audio_open(void *context, uint16_t usage,
                               pxa_audio_format_t *format, uint64_t *session) {
    (void)context;
    if (usage != PXA_AUDIO_USAGE_MEDIA || format == NULL || session == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    format->sample_rate = 16000;
    format->channels = 1;
    format->frame_ms = 20;
    *session = 1;
    return PXA_STATUS_OK;
}

static pxa_status_t audio_commit(void *context, uint64_t session,
                                 const pxa_audio_graph_t *graph) {
    (void)context;
    (void)graph;
    return session == 1 ? PXA_STATUS_OK : PXA_STATUS_NOT_FOUND;
}

static pxa_status_t audio_submit(void *context, uint64_t session,
                                 const uint8_t *pcm, size_t size) {
    (void)context;
    (void)pcm;
    (void)size;
    return session == 1 ? PXA_STATUS_OK : PXA_STATUS_NOT_FOUND;
}

static void audio_close(void *context, uint64_t session) {
    (void)context;
    (void)session;
}

static pxa_status_t audio_query(void *context, uint64_t session,
                                pxa_audio_state_t *state) {
    (void)context;
    if (session != 1 || state == NULL) return PXA_STATUS_NOT_FOUND;
    memset(state, 0, sizeof(*state));
    state->flags = PXA_AUDIO_STATE_ACCEPTED_IS_SINK_SUBMITTED;
    return PXA_STATUS_OK;
}

static pxa_status_t audio_flush(void *context, uint64_t session) {
    (void)context;
    return session == 1 ? PXA_STATUS_OK : PXA_STATUS_NOT_FOUND;
}

static pxa_status_t permission_load(void *context, pxa_bytes_t identity,
                                    pxa_bytes_t name, pxa_bytes_t scope,
                                    pxa_permission_decision_t *decision) {
    (void)context;
    (void)identity;
    (void)name;
    (void)scope;
    (void)decision;
    return PXA_STATUS_NOT_FOUND;
}

static pxa_status_t permission_save(void *context, pxa_bytes_t identity,
                                    pxa_bytes_t name, pxa_bytes_t scope,
                                    pxa_permission_decision_t decision) {
    (void)context;
    (void)identity;
    (void)name;
    (void)scope;
    (void)decision;
    return PXA_STATUS_OK;
}

static void print_usage(const char *program) {
    fprintf(stderr, "Usage: %s --package DIR --publisher-key DER [--width PX --height PX]\n",
            program);
}

static int parse_options(int argc, char **argv, options_t *options) {
    int index;
    memset(options, 0, sizeof(*options));
    options->width = 296;
    options->height = 240;
    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--package") == 0 && index + 1 < argc)
            options->package_path = argv[++index];
        else if (strcmp(argv[index], "--publisher-key") == 0 && index + 1 < argc)
            options->publisher_key = argv[++index];
        else if (strcmp(argv[index], "--width") == 0 && index + 1 < argc)
            options->width = (uint32_t)strtoul(argv[++index], NULL, 10);
        else if (strcmp(argv[index], "--height") == 0 && index + 1 < argc)
            options->height = (uint32_t)strtoul(argv[++index], NULL, 10);
        else return 0;
    }
    return options->package_path != NULL && options->publisher_key != NULL &&
           options->width > 0 && options->height > 0;
}

int main(int argc, char **argv) {
    options_t options;
    product_host_t host = {0};
    pxa_package_limits_t limits;
    pxa_posix_installer_config_t installer_config = {0};
    pxa_posix_installer_t *installer = NULL;
    pxa_posix_installer_result_t package_result = {0};
    pxa_package_manifest_t *manifest = NULL;
    pxa_openssl_publisher_key_t key = {0};
    pxa_runtime_limits_t runtime_limits;
    pxa_ui_config_t ui_config;
    pxa_lvgl_ui_config_t lvgl_config = {0};
    pxa_lvgl_ui_t *lvgl_ui = NULL;
    pxa_ui_backend_t ui_backend;
    pxa_audio_config_t audio_config = {0};
    pxa_audio_backend_t audio_backend = {0};
    pxa_permission_config_t permission_config = {0};
    pxa_service_ops_t clock_service = {0};
    pxa_wamr_engine_config_t engine_config = {0};
    pxa_package_service_capability_t capabilities[6] = {0};
    pxa_package_activation_profile_t activation = {0};
    pxa_package_host_profile_t profile = {0};
    pxa_activation_plan_t *plan = NULL;
    void *installer_workspace = NULL, *manifest_workspace = NULL;
    void *runtime_workspace = NULL, *window_workspace = NULL, *ui_workspace = NULL;
    void *permission_workspace = NULL, *audio_workspace = NULL, *lvgl_workspace = NULL, *engine_workspace = NULL;
    void *plan_workspace = NULL, *coordinator_workspace = NULL;
    uint8_t *encoded = NULL, *public_key = NULL;
    size_t manifest_size = 0, public_key_size = 0;
    char root[1024] = {0};
    lv_display_t *display = NULL;
    lv_indev_t *mouse = NULL;
    pxa_component_t component;
    pxa_status_t status;
    const char *stage = "arguments";
    int result = 1;

    {
        struct stat metadata;
        if (!parse_options(argc, argv, &options)) {
            print_usage(argv[0]);
            return 2;
        }
        if (stat(options.package_path, &metadata) != 0 ||
            !S_ISDIR(metadata.st_mode)) {
            fprintf(stderr, "PXA product simulator requires an unpacked package directory: %s\n",
                    options.package_path);
            return 2;
        }
    }
    stage = "publisher key";
    {
        FILE *file = fopen(options.publisher_key, "rb");
        long size;
        if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
            (size = ftell(file)) <= 0 || size > 4096)
            goto done;
        rewind(file);
        public_key = malloc((size_t)size);
        if (public_key == NULL || fread(public_key, 1, (size_t)size, file) != (size_t)size) {
            fclose(file); goto done;
        }
        fclose(file);
        public_key_size = (size_t)size;
    }
    key.spki = public_key; key.spki_size = public_key_size;
    stage = "package installer";
    pxa_package_limits_init(&limits);
    installer_config.struct_size = sizeof(installer_config);
    installer_config.storage_root = ".";
    installer_config.trust.struct_size = sizeof(installer_config.trust);
    installer_config.trust.keys = &key; installer_config.trust.key_count = 1;
    installer_config.limits = limits;
    installer_workspace = malloc(pxa_posix_installer_workspace_size(&installer_config));
    if (installer_workspace == NULL ||
        pxa_posix_installer_init(installer_workspace,
            pxa_posix_installer_workspace_size(&installer_config), &installer_config,
            &installer) != PXA_STATUS_OK ||
        pxa_posix_installer_source_manifest_size(installer, options.package_path,
                                                  &manifest_size) != PXA_STATUS_OK)
        goto done;
    encoded = malloc(manifest_size);
    manifest_workspace = malloc(pxa_package_manifest_workspace_size(&limits));
    if (encoded == NULL || manifest_workspace == NULL) goto done;
    package_result.struct_size = sizeof(package_result);
    package_result.manifest_workspace = manifest_workspace;
    package_result.manifest_workspace_size = pxa_package_manifest_workspace_size(&limits);
    package_result.encoded = encoded; package_result.encoded_capacity = manifest_size;
    package_result.manifest = &manifest; package_result.root = root;
    package_result.root_capacity = sizeof(root);
    stage = "package verification";
    if (pxa_posix_installer_verify_source(installer, options.package_path,
                                          &package_result) != PXA_STATUS_OK)
        goto done;
    stage = "display";
    if (strlen(root) >= sizeof(host.package_root)) goto done;
    strcpy(host.package_root, root); host.width = options.width; host.height = options.height;
    lv_init();
    display = lv_sdl_window_create((int32_t)options.width, (int32_t)options.height);
    mouse = lv_sdl_mouse_create();
    if (display == NULL || mouse == NULL) goto done;
    lv_indev_set_display(mouse, display);
    lv_sdl_window_set_title(display, "PXA Product Simulator");
    pxa_runtime_limits_init(&runtime_limits); runtime_limits.max_components = PRODUCT_COMPONENTS;
    stage = "runtime";
    runtime_workspace = malloc(pxa_runtime_workspace_size(&runtime_limits));
    if (runtime_workspace == NULL || pxa_runtime_init(runtime_workspace,
        pxa_runtime_workspace_size(&runtime_limits), &runtime_limits, &host.runtime) != PXA_STATUS_OK)
        goto done;
    window_workspace = malloc(pxa_window_service_workspace_size(PRODUCT_COMPONENTS));
    stage = "window service";
    if (window_workspace == NULL || pxa_window_service_init(window_workspace,
        pxa_window_service_workspace_size(PRODUCT_COMPONENTS), host.runtime,
        PRODUCT_COMPONENTS, &host.window) != PXA_STATUS_OK ||
        pxa_window_service_register(host.window) != PXA_STATUS_OK) goto done;
    pxa_ui_config_init(&ui_config);
    stage = "ui service";
    ui_config.allocate = allocate_memory; ui_config.release = release_memory;
    ui_config.now_us = now_us; ui_config.features = PXA_UI_FEATURE_CANVAS |
        PXA_UI_FEATURE_VIRTUAL_LIST | PXA_UI_FEATURE_RGB565_BITMAP;
    ui_config.primary_width = options.width; ui_config.primary_height = options.height;
    ui_workspace = malloc(pxa_ui_service_workspace_size());
    if (ui_workspace == NULL || pxa_ui_service_init(ui_workspace,
        pxa_ui_service_workspace_size(), host.runtime, &ui_config, &host.ui) != PXA_STATUS_OK ||
        pxa_ui_service_register(host.ui) != PXA_STATUS_OK) goto done;
    stage = "permission service";
    permission_config.struct_size = sizeof(permission_config);
    permission_config.app_identity = manifest->app_id;
    permission_config.declarations =
        (const pxa_permission_declaration_t *)manifest->permissions;
    permission_config.declaration_count = manifest->permission_count;
    permission_config.max_authorities = 8;
    permission_config.max_pending_prompts = 0;
    permission_config.store.struct_size = sizeof(permission_config.store);
    permission_config.store.load = permission_load;
    permission_config.store.save = permission_save;
    permission_workspace = malloc(pxa_permission_service_workspace_size(&permission_config));
    if (permission_workspace == NULL || pxa_permission_service_init(permission_workspace,
        pxa_permission_service_workspace_size(&permission_config), host.runtime,
        &permission_config, &host.permissions) != PXA_STATUS_OK ||
        pxa_permission_policy_load(host.permissions) != PXA_STATUS_OK ||
        pxa_permission_service_register(host.permissions) != PXA_STATUS_OK) goto done;
    audio_backend.struct_size = sizeof(audio_backend);
    stage = "audio service";
    audio_backend.open = audio_open; audio_backend.commit = audio_commit;
    audio_backend.submit = audio_submit; audio_backend.close = audio_close;
    audio_backend.query = audio_query; audio_backend.flush = audio_flush;
    audio_config.struct_size = sizeof(audio_config);
    audio_config.max_sessions = 2; audio_config.max_sessions_per_component = 2;
    audio_config.max_eq_bands = PXA_AUDIO_MAX_EQ_BANDS;
    audio_config.backend = audio_backend;
    audio_config.permissions = host.permissions;
    audio_workspace = malloc(pxa_audio_service_workspace_size(&audio_config));
    if (audio_workspace == NULL || pxa_audio_service_init(audio_workspace,
        pxa_audio_service_workspace_size(&audio_config), host.runtime,
        &audio_config, &host.audio) != PXA_STATUS_OK ||
        pxa_audio_service_register(host.audio) != PXA_STATUS_OK) goto done;
    clock_service.struct_size = sizeof(clock_service);
    clock_service.service_id = PRODUCT_CLOCK_SERVICE;
    clock_service.major = 0; clock_service.minor = 1;
    clock_service.context = &host;
    clock_service.control = clock_control;
    if (pxa_service_register(host.runtime, &clock_service) != PXA_STATUS_OK) goto done;
    lvgl_config.struct_size = sizeof(lvgl_config);
    stage = "LVGL adapter";
    lvgl_config.allocate = allocate_memory; lvgl_config.release = release_memory;
    lvgl_config.execute = execute_inline; lvgl_config.resolve_asset = resolve_asset;
    lvgl_config.release_asset = release_asset; lvgl_config.event_callback = ui_event;
    lvgl_config.now_us = now_us; lvgl_config.callback_user_data = &host;
    lvgl_config.asset_user_data = &host;
    lvgl_config.primary_environment.surface = PXA_UI_PRIMARY_SURFACE;
    lvgl_config.primary_environment.width = options.width;
    lvgl_config.primary_environment.height = options.height;
    lvgl_config.primary_environment.density_q16 = UINT32_C(1) << 16;
    lvgl_config.primary_environment.font_scale_q16 = UINT32_C(1) << 16;
    pxa_lvgl_ui_theme_init(&lvgl_config.theme);
    lvgl_config.theme.body_font = &lv_font_montserrat_14;
    lvgl_config.theme.title_font = &lv_font_montserrat_20;
    lvgl_workspace = malloc(pxa_lvgl_ui_workspace_size());
    if (lvgl_workspace == NULL || pxa_lvgl_ui_init(lvgl_workspace,
        pxa_lvgl_ui_workspace_size(), &lvgl_config, &lvgl_ui, &ui_backend) != PXA_STATUS_OK)
        goto done;
    /* The UI backend is bound during prepare_start. */
    host.ui_backend = ui_backend;
    engine_config.struct_size = sizeof(engine_config); engine_config.host_context = &host;
    stage = "WAMR engine";
    engine_config.read_artifact = read_artifact; engine_config.now_us = now_us;
    engine_config.prepare_start = prepare_start; engine_config.max_components = PRODUCT_COMPONENTS;
    engine_config.wasi_enabled = 1; engine_config.allocate_artifact = allocate_memory;
    engine_config.release_artifact = release_memory; engine_config.allocate_runtime = allocate_memory;
    engine_config.reallocate_runtime = reallocate_memory;
    engine_config.release_runtime = release_memory;
    engine_workspace = malloc(pxa_wamr_engine_workspace_size(&engine_config));
    if (engine_workspace == NULL || pxa_wamr_engine_init(engine_workspace,
        pxa_wamr_engine_workspace_size(&engine_config), &engine_config, &host.engine,
        &host.engine_ops) != PXA_STATUS_OK) goto done;
    pxa_wamr_engine_set_runtime(host.engine, host.runtime);
    capabilities[0].service = PXA_SERVICE_CORE; capabilities[0].version.major = 0; capabilities[0].version.minor = 1;
    capabilities[1].service = PXA_WINDOW_SERVICE_ID; capabilities[1].version.major = 0; capabilities[1].version.minor = 1;
    capabilities[2].service = PXA_UI_SERVICE_ID; capabilities[2].version.major = 0; capabilities[2].version.minor = 3;
    capabilities[3].service = PRODUCT_CLOCK_SERVICE; capabilities[3].version.major = 0; capabilities[3].version.minor = 1;
    capabilities[4].service = PXA_AUDIO_SERVICE_ID; capabilities[4].version.major = 0; capabilities[4].version.minor = 5;
    capabilities[5].service = PXA_PERMISSION_SERVICE_ID; capabilities[5].version.major = 0; capabilities[5].version.minor = 1;
    activation.core_version.major = 0; activation.core_version.minor = 1;
    activation.services = capabilities; activation.service_count = 6;
    profile.target = (pxa_bytes_t){(const uint8_t *)"linux-x86_64", 13};
    profile.engine = (pxa_bytes_t){(const uint8_t *)"wamr", 4};
    profile.engine_abi = (pxa_bytes_t){(const uint8_t *)"wasm32", 6};
    profile.memory_model = PXA_MEMORY_WASM32;
    plan_workspace = malloc(pxa_activation_plan_workspace_size(manifest->component_count));
    stage = "activation plan";
    status = plan_workspace == NULL ? PXA_STATUS_RESOURCE_LIMIT :
        pxa_activation_plan_prepare(plan_workspace,
            pxa_activation_plan_workspace_size(manifest->component_count), manifest, &activation,
            &profile, (pxa_bytes_t){(const uint8_t *)host.package_root, strlen(host.package_root)}, &plan);
    if (status != PXA_STATUS_OK) {
        fprintf(stderr, "PXA activation plan status=%d\n", (int)status);
        goto done;
    }
    coordinator_workspace = malloc(pxa_activation_coordinator_workspace_size(plan));
    stage = "activation coordinator";
    if (coordinator_workspace == NULL || pxa_activation_coordinator_init(coordinator_workspace,
        pxa_activation_coordinator_workspace_size(plan), host.runtime, plan, &host.engine_ops,
        &host.coordinator) != PXA_STATUS_OK) goto done;
    stage = "main component start";
    status = pxa_activation_activate(host.coordinator,
        (pxa_bytes_t){(const uint8_t *)"main", 4}, 1, &component);
    if (status != PXA_STATUS_OK) {
        fprintf(stderr, "PXA main component start status=%d\n", (int)status);
        goto done;
    }
    if (pxa_window_flush_metrics(host.window, component) == PXA_STATUS_OK)
        dispatch_component_events(&host);
    while (lv_display_get_default() != NULL) {
        uint32_t delay = lv_timer_handler();
        dispatch_clock_tick(&host);
        if (delay < 1) delay = 1;
        if (delay > 16) delay = 16;
        SDL_Delay(delay);
    }
    result = 0;
done:
    if (result != 0) fprintf(stderr, "PXA product simulator failed at %s\n", stage);
    if (host.coordinator != NULL && lv_display_get_default() != NULL)
        pxa_activation_deactivate_all(host.coordinator, PXA_STOP_SHUTDOWN);
    if (host.engine != NULL) pxa_wamr_engine_deinit(host.engine);
    if (lv_display_get_default() != NULL) {
        if (lvgl_ui != NULL) pxa_lvgl_ui_deinit(lvgl_ui);
        if (host.ui != NULL) pxa_ui_service_deinit(host.ui);
    }
    if (host.runtime != NULL) pxa_runtime_deinit(host.runtime);
    if (installer != NULL) pxa_posix_installer_deinit(installer);
    free(coordinator_workspace); free(plan_workspace); free(engine_workspace); free(lvgl_workspace);
    free(ui_workspace); free(window_workspace); free(permission_workspace); free(runtime_workspace); free(manifest_workspace);
    free(encoded); free(installer_workspace); free(public_key);
    return result;
}
