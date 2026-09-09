#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxsys/native_runtime.h"

typedef struct {
    size_t create_calls;
    size_t start_calls;
    size_t foreground_calls;
    size_t background_calls;
    size_t event_calls;
    size_t back_calls;
    size_t stop_calls;
    size_t destroy_calls;
} native_app_state_t;

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

static pxsys_app_identity_t identity(const char* app_id) {
    pxsys_app_identity_t value;
    memset(&value, 0x5a, sizeof(value.publisher_root));
    value.app_id = pxsys_string_from_cstr(app_id);
    return value;
}

static pxsys_status_t app_create(void* context, const pxsys_app_descriptor_t* app,
                                 uint64_t instance_id, void** app_instance) {
    native_app_state_t* state = (native_app_state_t*)context;
    assert(app != NULL && instance_id != 0 && app_instance != NULL);
    state->create_calls++;
    *app_instance = state;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t app_create_display(
    void* context, const pxsys_app_descriptor_t* app, uint64_t instance_id,
    const pxsys_display_profile_t* display, void** app_instance) {
    assert(display == NULL);
    return app_create(context, app, instance_id, app_instance);
}

static pxsys_status_t app_start(void* context, void* app_instance, const pxsys_message_t* launch) {
    native_app_state_t* state = (native_app_state_t*)context;
    assert(app_instance == state && launch != NULL && launch->operation == 42);
    state->start_calls++;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t app_foreground(void* context, void* app_instance) {
    native_app_state_t* state = (native_app_state_t*)context;
    assert(app_instance == state);
    state->foreground_calls++;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t app_background(void* context, void* app_instance) {
    native_app_state_t* state = (native_app_state_t*)context;
    assert(app_instance == state);
    state->background_calls++;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t app_event(void* context, void* app_instance, const pxsys_message_t* event) {
    native_app_state_t* state = (native_app_state_t*)context;
    assert(app_instance == state && event != NULL && event->operation == 7);
    state->event_calls++;
    return PXSYS_STATUS_OK;
}

static pxsys_back_result_t app_back(void* context, void* app_instance) {
    native_app_state_t* state = (native_app_state_t*)context;
    assert(app_instance == state);
    state->back_calls++;
    return PXSYS_BACK_HANDLED;
}

static void app_stop(void* context, void* app_instance, pxsys_stop_reason_t reason) {
    native_app_state_t* state = (native_app_state_t*)context;
    assert(app_instance == state && reason == PXSYS_STOP_NORMAL);
    state->stop_calls++;
}

static void app_destroy(void* context, void* app_instance) {
    native_app_state_t* state = (native_app_state_t*)context;
    assert(app_instance == state);
    state->destroy_calls++;
}

static pxsys_native_app_t native_app(native_app_state_t* state) {
    pxsys_native_app_t app;
    memset(&app, 0, sizeof(app));
    app.struct_size = sizeof(app);
    app.identity = identity("settings");
    app.context = state;
    app.create = app_create;
    app.start = app_start;
    app.foreground = app_foreground;
    app.background = app_background;
    app.event = app_event;
    app.back = app_back;
    app.stop = app_stop;
    app.destroy = app_destroy;
    return app;
}

static pxsys_app_descriptor_t app_descriptor(void) {
    pxsys_app_descriptor_t app;
    memset(&app, 0, sizeof(app));
    app.struct_size = sizeof(app);
    app.identity = identity("settings");
    app.display_name = pxsys_string_from_cstr("Settings");
    app.version = pxsys_string_from_cstr("1.0.0");
    app.runtime_id = pxsys_string_from_cstr(PXSYS_NATIVE_RUNTIME_ID);
    app.flags = PXSYS_APP_FLAG_SYSTEM | PXSYS_APP_FLAG_ENABLED;
    return app;
}

int main(void) {
    size_t allocations = 0;
    native_app_state_t state = {0};
    pxsys_app_registry_config_t app_config;
    pxsys_native_runtime_config_t native_config;
    pxsys_runtime_config_t runtime_config;
    pxsys_app_registry_t* apps = NULL;
    pxsys_native_runtime_t* native = NULL;
    pxsys_runtime_t* runtime = NULL;
    pxsys_native_app_t implementation = native_app(&state);
    pxsys_app_descriptor_t descriptor = app_descriptor();
    pxsys_runtime_provider_t provider;
    pxsys_message_t launch;
    pxsys_message_t event;
    pxsys_instance_ref_t instance;
    pxsys_back_result_t back;

    pxsys_app_registry_config_init(&app_config);
    app_config.allocator = allocator(&allocations);
    assert(pxsys_app_registry_create(&app_config, &apps) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_register(apps, &descriptor) == PXSYS_STATUS_OK);

    pxsys_native_runtime_config_init(&native_config);
    native_config.allocator = allocator(&allocations);
    assert(pxsys_native_runtime_create(&native_config, &native) == PXSYS_STATUS_OK);
    assert(pxsys_native_runtime_register_app(native, &implementation) == PXSYS_STATUS_OK);
    assert(pxsys_native_runtime_provider(native, &provider) == PXSYS_STATUS_OK);

    pxsys_runtime_config_init(&runtime_config);
    runtime_config.apps = apps;
    runtime_config.allocator = allocator(&allocations);
    assert(pxsys_runtime_create(&runtime_config, &runtime) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_register_provider(runtime, &provider) == PXSYS_STATUS_OK);

    memset(&launch, 0, sizeof(launch));
    launch.struct_size = sizeof(launch);
    launch.interface_id = pxsys_string_from_cstr("system.intent");
    launch.operation = 42;
    assert(pxsys_runtime_launch(runtime, &descriptor.identity, &launch, 1, &instance) ==
           PXSYS_STATUS_OK);
    assert(pxsys_native_runtime_unregister_app(native, &descriptor.identity) == PXSYS_STATUS_BUSY);

    memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.interface_id = pxsys_string_from_cstr("system.test");
    event.operation = 7;
    assert(pxsys_runtime_deliver(runtime, instance, &event) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_back(runtime, instance, &back) == PXSYS_STATUS_OK &&
           back == PXSYS_BACK_HANDLED);
    assert(pxsys_runtime_background(runtime, instance) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_foreground(runtime, instance) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_stop(runtime, instance, PXSYS_STOP_NORMAL) == PXSYS_STATUS_OK);

    implementation.create = NULL;
    implementation.create_display = app_create_display;
    assert(pxsys_native_runtime_unregister_app(native, &descriptor.identity) ==
           PXSYS_STATUS_OK);
    implementation.create = app_create;
    implementation.struct_size = offsetof(pxsys_native_app_t, display);
    assert(pxsys_native_runtime_register_app(native, &implementation) ==
           PXSYS_STATUS_OK);
    assert(pxsys_native_runtime_unregister_app(native, &descriptor.identity) ==
           PXSYS_STATUS_OK);
    implementation.struct_size = sizeof(implementation);
    implementation.create = NULL;
    assert(pxsys_native_runtime_register_app(native, &implementation) ==
           PXSYS_STATUS_OK);
    assert(pxsys_runtime_launch(runtime, &descriptor.identity, &launch, 2,
                                &instance) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_stop(runtime, instance, PXSYS_STOP_NORMAL) ==
           PXSYS_STATUS_OK);

    assert(state.create_calls == 2 && state.start_calls == 2 && state.foreground_calls == 3 &&
           state.background_calls == 1 && state.event_calls == 1 && state.back_calls == 1 &&
           state.stop_calls == 2 && state.destroy_calls == 2);
    assert(pxsys_native_runtime_unregister_app(native, &descriptor.identity) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_unregister_provider(
               runtime, pxsys_string_from_cstr(PXSYS_NATIVE_RUNTIME_ID)) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_destroy(runtime) == PXSYS_STATUS_OK);
    assert(pxsys_native_runtime_destroy(native) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_destroy(apps) == PXSYS_STATUS_OK);
    assert(allocations == 0);
    puts("pxa_system_native runtime tests passed");
    return 0;
}
