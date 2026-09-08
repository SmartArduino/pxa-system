#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxsys/runtime.h"

typedef struct {
    size_t instantiate_calls;
    size_t start_calls;
    size_t foreground_calls;
    size_t background_calls;
    size_t event_calls;
    size_t back_calls;
    size_t stop_calls;
    size_t destroy_calls;
    pxsys_status_t instantiate_result;
    pxsys_status_t start_result;
    pxsys_status_t foreground_result;
    pxsys_status_t stop_result;
    pxsys_back_result_t back_result;
} fake_provider_t;

typedef struct {
    uint64_t id;
} fake_instance_t;

static void* test_allocate(void* context, size_t size) {
    size_t* allocations = (size_t*)context;
    void* memory = malloc(size);
    if (memory != NULL)
        (*allocations)++;
    return memory;
}

static void test_release(void* context, void* memory) {
    size_t* allocations = (size_t*)context;
    if (memory != NULL)
        (*allocations)--;
    free(memory);
}

static pxsys_allocator_t allocator(size_t* allocations) {
    pxsys_allocator_t value;
    memset(&value, 0, sizeof(value));
    value.struct_size = sizeof(value);
    value.context = allocations;
    value.allocate = test_allocate;
    value.release = test_release;
    return value;
}

static pxsys_app_identity_t identity(uint8_t publisher_byte, const char* app_id) {
    pxsys_app_identity_t result;
    memset(&result, publisher_byte, sizeof(result.publisher_root));
    result.app_id = pxsys_string_from_cstr(app_id);
    return result;
}

static pxsys_app_descriptor_t app_descriptor(uint8_t publisher_byte, const char* app_id,
                                             const char* runtime_id) {
    pxsys_app_descriptor_t app;
    memset(&app, 0, sizeof(app));
    app.struct_size = sizeof(app);
    app.identity = identity(publisher_byte, app_id);
    app.display_name = pxsys_string_from_cstr(app_id);
    app.version = pxsys_string_from_cstr("1.0.0");
    app.runtime_id = pxsys_string_from_cstr(runtime_id);
    app.flags = PXSYS_APP_FLAG_ENABLED;
    return app;
}

static pxsys_status_t fake_instantiate(void* context, const pxsys_app_descriptor_t* app,
                                       uint64_t instance_id, void** runtime_instance) {
    fake_provider_t* fake = (fake_provider_t*)context;
    fake_instance_t* instance = NULL;
    assert(app != NULL && runtime_instance != NULL && instance_id != 0);
    fake->instantiate_calls++;
    if (fake->instantiate_result == PXSYS_STATUS_OK) {
        instance = (fake_instance_t*)malloc(sizeof(*instance));
        assert(instance != NULL);
        instance->id = instance_id;
    }
    *runtime_instance = instance;
    return fake->instantiate_result;
}

static pxsys_status_t fake_start(void* context, void* runtime_instance,
                                 const pxsys_message_t* launch) {
    fake_provider_t* fake = (fake_provider_t*)context;
    assert(runtime_instance != NULL);
    (void)launch;
    fake->start_calls++;
    return fake->start_result;
}

static pxsys_status_t fake_foreground(void* context, void* runtime_instance) {
    fake_provider_t* fake = (fake_provider_t*)context;
    assert(runtime_instance != NULL);
    fake->foreground_calls++;
    return fake->foreground_result;
}

static pxsys_status_t fake_background(void* context, void* runtime_instance) {
    fake_provider_t* fake = (fake_provider_t*)context;
    assert(runtime_instance != NULL);
    fake->background_calls++;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t fake_event(void* context, void* runtime_instance,
                                 const pxsys_message_t* event) {
    fake_provider_t* fake = (fake_provider_t*)context;
    assert(runtime_instance != NULL && event != NULL);
    fake->event_calls++;
    return PXSYS_STATUS_OK;
}

static pxsys_back_result_t fake_back(void* context, void* runtime_instance) {
    fake_provider_t* fake = (fake_provider_t*)context;
    assert(runtime_instance != NULL);
    fake->back_calls++;
    return fake->back_result;
}

static void fake_stop(void* context, void* runtime_instance, pxsys_stop_reason_t reason) {
    fake_provider_t* fake = (fake_provider_t*)context;
    assert(runtime_instance != NULL && reason <= PXSYS_STOP_SHUTDOWN);
    fake->stop_calls++;
}

static pxsys_status_t fake_request_stop(void* context, void* runtime_instance,
                                        pxsys_stop_reason_t reason) {
    fake_provider_t* fake = (fake_provider_t*)context;
    assert(runtime_instance != NULL && reason <= PXSYS_STOP_SHUTDOWN);
    fake->stop_calls++;
    return fake->stop_result;
}

static void fake_destroy(void* context, void* runtime_instance) {
    fake_provider_t* fake = (fake_provider_t*)context;
    fake->destroy_calls++;
    free(runtime_instance);
}

static pxsys_runtime_provider_t provider(fake_provider_t* fake, const char* runtime_id) {
    pxsys_runtime_provider_t value;
    memset(&value, 0, sizeof(value));
    value.struct_size = sizeof(value);
    value.runtime_id = pxsys_string_from_cstr(runtime_id);
    value.version = (pxsys_version_t){1, 0};
    value.context = fake;
    value.instantiate = fake_instantiate;
    value.start = fake_start;
    value.foreground = fake_foreground;
    value.background = fake_background;
    value.event = fake_event;
    value.back = fake_back;
    value.stop = fake_stop;
    value.destroy = fake_destroy;
    return value;
}

static void create_system(size_t* allocations, pxsys_app_registry_t** apps,
                          pxsys_runtime_t** runtime) {
    pxsys_app_registry_config_t app_config;
    pxsys_runtime_config_t runtime_config;
    pxsys_app_registry_config_init(&app_config);
    app_config.max_apps = 8;
    app_config.allocator = allocator(allocations);
    assert(pxsys_app_registry_create(&app_config, apps) == PXSYS_STATUS_OK);
    pxsys_runtime_config_init(&runtime_config);
    runtime_config.max_providers = 4;
    runtime_config.max_instances = 4;
    runtime_config.apps = *apps;
    runtime_config.allocator = allocator(allocations);
    assert(pxsys_runtime_create(&runtime_config, runtime) == PXSYS_STATUS_OK);
}

static void test_runtime_lifecycle_and_ownership(void) {
    size_t allocations = 0;
    pxsys_app_registry_t* apps = NULL;
    pxsys_runtime_t* runtime = NULL;
    fake_provider_t fake = {0};
    pxsys_runtime_provider_t native;
    pxsys_app_descriptor_t camera;
    pxsys_instance_ref_t instance;
    pxsys_instance_snapshot_t snapshot;
    pxsys_message_t event;
    pxsys_back_result_t back;

    fake.back_result = PXSYS_BACK_HANDLED;
    create_system(&allocations, &apps, &runtime);
    native = provider(&fake, "native-static");
    camera = app_descriptor(1, "camera", "native-static");
    assert(pxsys_runtime_register_provider(runtime, &native) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_register(apps, &camera) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_launch(runtime, &camera.identity, NULL, 1, &instance) == PXSYS_STATUS_OK);
    assert(fake.instantiate_calls == 1 && fake.start_calls == 1 && fake.foreground_calls == 1);
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.struct_size = sizeof(snapshot);
    assert(pxsys_runtime_snapshot(runtime, instance, &snapshot) == PXSYS_STATUS_OK);
    assert(snapshot.lifecycle.state == PXSYS_APP_FOREGROUND);
    assert(pxsys_app_registry_unregister(apps, &camera.identity) == PXSYS_STATUS_BUSY);
    assert(pxsys_runtime_unregister_provider(runtime, pxsys_string_from_cstr("native-static")) ==
           PXSYS_STATUS_BUSY);
    assert(pxsys_runtime_destroy(runtime) == PXSYS_STATUS_BUSY);

    memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.interface_id = pxsys_string_from_cstr("system.test");
    event.version = (pxsys_version_t){1, 0};
    event.operation = 7;
    assert(pxsys_runtime_deliver(runtime, instance, &event) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_back(runtime, instance, &back) == PXSYS_STATUS_OK);
    assert(back == PXSYS_BACK_HANDLED && fake.event_calls == 1 && fake.back_calls == 1);
    assert(pxsys_runtime_background(runtime, instance) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_foreground(runtime, instance) == PXSYS_STATUS_OK);
    assert(fake.background_calls == 1 && fake.foreground_calls == 2);

    assert(pxsys_runtime_stop(runtime, instance, PXSYS_STOP_NORMAL) == PXSYS_STATUS_OK);
    assert(fake.stop_calls == 1 && fake.destroy_calls == 1);
    assert(pxsys_runtime_snapshot(runtime, instance, &snapshot) == PXSYS_STATUS_NOT_FOUND);
    assert(pxsys_runtime_unregister_provider(runtime, pxsys_string_from_cstr("native-static")) ==
           PXSYS_STATUS_OK);
    assert(pxsys_app_registry_unregister(apps, &camera.identity) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_destroy(runtime) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_destroy(apps) == PXSYS_STATUS_OK);
    assert(allocations == 0);
}

static void test_start_failure_rolls_back(void) {
    size_t allocations = 0;
    pxsys_app_registry_t* apps = NULL;
    pxsys_runtime_t* runtime = NULL;
    fake_provider_t fake = {0};
    pxsys_runtime_provider_t wamr;
    pxsys_app_descriptor_t weather;
    pxsys_instance_ref_t instance;

    fake.start_result = PXSYS_STATUS_INTERNAL;
    create_system(&allocations, &apps, &runtime);
    wamr = provider(&fake, "wamr-aot");
    weather = app_descriptor(2, "weather", "wamr-aot");
    assert(pxsys_runtime_register_provider(runtime, &wamr) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_register(apps, &weather) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_launch(runtime, &weather.identity, NULL, 1, &instance) ==
           PXSYS_STATUS_INTERNAL);
    assert(instance.slot == PXSYS_INSTANCE_REF_INVALID_SLOT);
    assert(fake.instantiate_calls == 1 && fake.start_calls == 1 && fake.stop_calls == 1 &&
           fake.destroy_calls == 1);
    assert(pxsys_app_registry_unregister(apps, &weather.identity) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_destroy(runtime) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_destroy(apps) == PXSYS_STATUS_OK);
    assert(allocations == 0);
}

static void test_instantiate_failure_calls_destroy(void) {
    size_t allocations = 0;
    pxsys_app_registry_t* apps = NULL;
    pxsys_runtime_t* runtime = NULL;
    fake_provider_t fake = {0};
    pxsys_runtime_provider_t provider_value;
    pxsys_app_descriptor_t app;
    pxsys_instance_ref_t instance;

    fake.instantiate_result = PXSYS_STATUS_NO_MEMORY;
    create_system(&allocations, &apps, &runtime);
    provider_value = provider(&fake, "native-static");
    app = app_descriptor(4, "broken", "native-static");
    assert(pxsys_runtime_register_provider(runtime, &provider_value) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_register(apps, &app) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_launch(runtime, &app.identity, NULL, 0, &instance) ==
           PXSYS_STATUS_NO_MEMORY);
    assert(fake.instantiate_calls == 1 && fake.start_calls == 0 && fake.stop_calls == 0 &&
           fake.destroy_calls == 1);
    assert(pxsys_runtime_destroy(runtime) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_destroy(apps) == PXSYS_STATUS_OK);
    assert(allocations == 0);
}

static void test_disabled_and_missing_runtime(void) {
    size_t allocations = 0;
    pxsys_app_registry_t* apps = NULL;
    pxsys_runtime_t* runtime = NULL;
    pxsys_app_descriptor_t disabled;
    pxsys_app_descriptor_t missing;
    pxsys_instance_ref_t instance;

    create_system(&allocations, &apps, &runtime);
    disabled = app_descriptor(5, "disabled", "native-static");
    disabled.flags = 0;
    missing = app_descriptor(5, "missing", "other-engine");
    assert(pxsys_app_registry_register(apps, &disabled) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_register(apps, &missing) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_launch(runtime, &disabled.identity, NULL, 0, &instance) ==
           PXSYS_STATUS_DENIED);
    assert(pxsys_runtime_launch(runtime, &missing.identity, NULL, 0, &instance) ==
           PXSYS_STATUS_UNSUPPORTED);
    assert(pxsys_app_registry_unregister(apps, &disabled.identity) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_unregister(apps, &missing.identity) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_destroy(runtime) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_destroy(apps) == PXSYS_STATUS_OK);
    assert(allocations == 0);
}

static void test_pending_start_completion(void) {
    size_t allocations = 0;
    pxsys_app_registry_t* apps = NULL;
    pxsys_runtime_t* runtime = NULL;
    fake_provider_t fake = {0};
    pxsys_runtime_provider_t async_provider;
    pxsys_app_descriptor_t app;
    pxsys_instance_ref_t instance;
    pxsys_instance_snapshot_t snapshot = {0};

    fake.start_result = PXSYS_STATUS_PENDING;
    create_system(&allocations, &apps, &runtime);
    async_provider = provider(&fake, "async-runtime");
    app = app_descriptor(6, "async-app", "async-runtime");
    assert(pxsys_runtime_register_provider(runtime, &async_provider) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_register(apps, &app) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_launch(runtime, &app.identity, NULL, 1, &instance) ==
           PXSYS_STATUS_PENDING);
    snapshot.struct_size = sizeof(snapshot);
    assert(pxsys_runtime_snapshot(runtime, instance, &snapshot) == PXSYS_STATUS_OK);
    assert(snapshot.lifecycle.state == PXSYS_APP_STARTING && fake.foreground_calls == 0);
    assert(pxsys_runtime_complete_start(runtime, instance, PXSYS_STATUS_OK) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_snapshot(runtime, instance, &snapshot) == PXSYS_STATUS_OK);
    assert(snapshot.lifecycle.state == PXSYS_APP_FOREGROUND && fake.foreground_calls == 1);
    assert(pxsys_runtime_complete_start(runtime, instance, PXSYS_STATUS_OK) ==
           PXSYS_STATUS_BAD_STATE);
    assert(pxsys_runtime_stop(runtime, instance, PXSYS_STOP_NORMAL) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_unregister_provider(runtime, pxsys_string_from_cstr("async-runtime")) ==
           PXSYS_STATUS_OK);
    assert(pxsys_app_registry_unregister(apps, &app.identity) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_destroy(runtime) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_destroy(apps) == PXSYS_STATUS_OK);
    assert(allocations == 0);
}

static void test_provider_stable_prefix(void) {
    size_t allocations = 0;
    pxsys_app_registry_t* apps = NULL;
    pxsys_runtime_t* runtime = NULL;
    fake_provider_t fake = {0};
    pxsys_runtime_provider_t legacy_provider;
    pxsys_app_descriptor_t app;
    pxsys_instance_ref_t instance;

    create_system(&allocations, &apps, &runtime);
    legacy_provider = provider(&fake, "legacy-runtime");
    legacy_provider.struct_size = offsetof(pxsys_runtime_provider_t, bound);
    app = app_descriptor(7, "legacy-app", "legacy-runtime");
    assert(pxsys_runtime_register_provider(runtime, &legacy_provider) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_register(apps, &app) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_launch(runtime, &app.identity, NULL, 1, &instance) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_stop(runtime, instance, PXSYS_STOP_NORMAL) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_unregister_provider(runtime, pxsys_string_from_cstr("legacy-runtime")) ==
           PXSYS_STATUS_OK);
    assert(pxsys_app_registry_unregister(apps, &app.identity) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_destroy(runtime) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_destroy(apps) == PXSYS_STATUS_OK);
    assert(allocations == 0);
}

static void test_pending_stop_completion(void) {
    size_t allocations = 0;
    pxsys_app_registry_t* apps = NULL;
    pxsys_runtime_t* runtime = NULL;
    fake_provider_t fake = {0};
    pxsys_runtime_provider_t async_provider;
    pxsys_app_descriptor_t app;
    pxsys_instance_ref_t instance;
    pxsys_instance_snapshot_t snapshot = {0};

    fake.stop_result = PXSYS_STATUS_PENDING;
    create_system(&allocations, &apps, &runtime);
    async_provider = provider(&fake, "async-stop-runtime");
    async_provider.request_stop = fake_request_stop;
    app = app_descriptor(8, "async-stop-app", "async-stop-runtime");
    assert(pxsys_runtime_register_provider(runtime, &async_provider) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_register(apps, &app) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_launch(runtime, &app.identity, NULL, 1, &instance) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_stop(runtime, instance, PXSYS_STOP_NORMAL) == PXSYS_STATUS_PENDING);
    snapshot.struct_size = sizeof(snapshot);
    assert(pxsys_runtime_snapshot(runtime, instance, &snapshot) == PXSYS_STATUS_OK);
    assert(snapshot.lifecycle.state == PXSYS_APP_STOPPING);
    assert(fake.stop_calls == 1 && fake.destroy_calls == 0);
    assert(pxsys_runtime_report_stopped(runtime, instance, PXSYS_STOP_NORMAL) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_snapshot(runtime, instance, &snapshot) == PXSYS_STATUS_NOT_FOUND);
    assert(fake.destroy_calls == 1);
    assert(pxsys_runtime_unregister_provider(
               runtime, pxsys_string_from_cstr("async-stop-runtime")) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_unregister(apps, &app.identity) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_destroy(runtime) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_destroy(apps) == PXSYS_STATUS_OK);
    assert(allocations == 0);
}

int main(void) {
    test_runtime_lifecycle_and_ownership();
    test_start_failure_rolls_back();
    test_instantiate_failure_calls_destroy();
    test_disabled_and_missing_runtime();
    test_pending_start_completion();
    test_provider_stable_prefix();
    test_pending_stop_completion();
    puts("pxa_system_core runtime tests passed");
    return 0;
}
