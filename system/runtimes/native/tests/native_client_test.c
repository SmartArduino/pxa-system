#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxsys/native_client.h"

typedef struct {
    size_t outstanding;
    int caller_matches;
    int event_publisher_matches;
} test_context_t;

static void* allocate(void* context, size_t size) {
    test_context_t* test = (test_context_t*)context;
    void* memory = malloc(size);
    if (memory != NULL)
        test->outstanding++;
    return memory;
}

static void release(void* context, void* memory) {
    test_context_t* test = (test_context_t*)context;
    if (memory != NULL)
        test->outstanding--;
    free(memory);
}

static pxsys_allocator_t allocator(test_context_t* test) {
    pxsys_allocator_t value = {0};
    value.struct_size = sizeof(value);
    value.context = test;
    value.allocate = allocate;
    value.release = release;
    return value;
}

static pxsys_status_t invoke(void* context, const pxsys_service_request_t* request,
                             void* completion_context, pxsys_service_complete_fn complete) {
    test_context_t* test = (test_context_t*)context;
    test->caller_matches = request->caller != NULL && request->caller->app.app_id.size == 6 &&
                           memcmp(request->caller->app.app_id.data, "camera", 6) == 0 &&
                           request->caller->component_id.size == 4 &&
                           memcmp(request->caller->component_id.data, "main", 4) == 0;
    complete(completion_context, request->request_id, PXSYS_STATUS_OK, pxsys_bytes(NULL, 0));
    return PXSYS_STATUS_OK;
}

static void complete(void* context, uint64_t request_id, pxsys_status_t status,
                     pxsys_bytes_t payload) {
    int* called = (int*)context;
    assert(request_id == 7 && status == PXSYS_STATUS_OK && payload.size == 0);
    *called = 1;
}

static void receive_event(void* context, const pxsys_topic_event_t* event) {
    test_context_t* test = (test_context_t*)context;
    test->event_publisher_matches = event->publisher != NULL &&
                                    event->publisher->app.app_id.size == 6 &&
                                    memcmp(event->publisher->app.app_id.data, "camera", 6) == 0 &&
                                    event->publisher->component_id.size == 4 &&
                                    memcmp(event->publisher->component_id.data, "main", 4) == 0;
}

int main(void) {
    test_context_t test = {0};
    pxsys_app_registry_config_t app_config;
    pxsys_runtime_config_t runtime_config;
    pxsys_intent_resolver_config_t intent_config;
    pxsys_task_manager_config_t task_config;
    pxsys_service_registry_config_t service_config;
    pxsys_event_broker_config_t event_config;
    pxsys_app_registry_t* apps = NULL;
    pxsys_runtime_t* runtime = NULL;
    pxsys_intent_resolver_t* intents = NULL;
    pxsys_task_manager_t* tasks = NULL;
    pxsys_service_registry_t* services = NULL;
    pxsys_event_broker_t* events = NULL;
    pxsys_native_client_config_t client_config = {0};
    pxsys_native_client_t* client = NULL;
    pxsys_service_provider_t provider = {0};
    pxsys_service_request_t request = {0};
    pxsys_caller_t forged = {0};
    pxsys_app_descriptor_t app = {0};
    pxsys_topic_event_t event = {0};
    size_t delivered = 0;
    int completed = 0;

    pxsys_app_registry_config_init(&app_config);
    app_config.allocator = allocator(&test);
    assert(pxsys_app_registry_create(&app_config, &apps) == PXSYS_STATUS_OK);
    app.struct_size = sizeof(app);
    memset(app.identity.publisher_root, 4, PXSYS_PUBLISHER_ROOT_BYTES);
    app.identity.app_id = pxsys_string_from_cstr("camera");
    app.display_name = pxsys_string_from_cstr("Camera");
    app.version = pxsys_string_from_cstr("1.0.0");
    app.runtime_id = pxsys_string_from_cstr("native-static");
    app.flags = PXSYS_APP_FLAG_ENABLED;
    assert(pxsys_app_registry_register(apps, &app) == PXSYS_STATUS_OK);
    pxsys_runtime_config_init(&runtime_config);
    runtime_config.apps = apps;
    runtime_config.allocator = allocator(&test);
    assert(pxsys_runtime_create(&runtime_config, &runtime) == PXSYS_STATUS_OK);
    pxsys_intent_resolver_config_init(&intent_config);
    intent_config.apps = apps;
    intent_config.allocator = allocator(&test);
    assert(pxsys_intent_resolver_create(&intent_config, &intents) == PXSYS_STATUS_OK);
    pxsys_task_manager_config_init(&task_config);
    task_config.intents = intents;
    task_config.runtime = runtime;
    task_config.allocator = allocator(&test);
    assert(pxsys_task_manager_create(&task_config, &tasks) == PXSYS_STATUS_OK);
    pxsys_service_registry_config_init(&service_config);
    service_config.allocator = allocator(&test);
    assert(pxsys_service_registry_create(&service_config, &services) == PXSYS_STATUS_OK);
    pxsys_event_broker_config_init(&event_config);
    event_config.apps = apps;
    event_config.allocator = allocator(&test);
    assert(pxsys_event_broker_create(&event_config, &events) == PXSYS_STATUS_OK);
    provider.struct_size = sizeof(provider);
    provider.interface_id = pxsys_string_from_cstr("vendor.example.camera");
    provider.version = (pxsys_version_t){1, 0};
    provider.context = &test;
    provider.invoke = invoke;
    assert(pxsys_service_register(services, &provider) == PXSYS_STATUS_OK);
    client_config.struct_size = sizeof(client_config);
    memset(client_config.app.publisher_root, 4, PXSYS_PUBLISHER_ROOT_BYTES);
    client_config.app.app_id = pxsys_string_from_cstr("camera");
    client_config.component_id = pxsys_string_from_cstr("main");
    client_config.tasks = tasks;
    client_config.services = services;
    client_config.events = events;
    client_config.allocator = allocator(&test);
    assert(pxsys_native_client_create(&client_config, &client) == PXSYS_STATUS_OK);
    forged.struct_size = sizeof(forged);
    forged.app.app_id = pxsys_string_from_cstr("forged");
    request.struct_size = sizeof(request);
    request.interface_id = provider.interface_id;
    request.version = (pxsys_version_t){1, 0};
    request.request_id = 7;
    request.caller = &forged;
    assert(pxsys_native_client_invoke(client, &request, &completed, complete) == PXSYS_STATUS_OK);
    assert(completed && test.caller_matches);

    assert(pxsys_native_client_subscribe(client, pxsys_string_from_cstr("camera.capture"),
                                         (pxsys_version_t){1, 0}, &test,
                                         receive_event) == PXSYS_STATUS_OK);
    event.struct_size = sizeof(event);
    event.topic = pxsys_string_from_cstr("camera.capture");
    event.version = (pxsys_version_t){1, 0};
    event.publisher = &forged;
    assert(pxsys_native_client_publish(client, &event, &delivered) == PXSYS_STATUS_OK);
    assert(delivered == 1 && test.event_publisher_matches);
    assert(pxsys_native_client_unsubscribe(client, event.topic, &test, receive_event) ==
           PXSYS_STATUS_OK);

    pxsys_native_client_destroy(client);
    assert(pxsys_event_broker_destroy(events) == PXSYS_STATUS_OK);
    assert(pxsys_service_unregister(services, provider.interface_id, 1) == PXSYS_STATUS_OK);
    assert(pxsys_service_registry_destroy(services) == PXSYS_STATUS_OK);
    assert(pxsys_task_manager_destroy(tasks) == PXSYS_STATUS_OK);
    assert(pxsys_intent_resolver_destroy(intents) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_destroy(runtime) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_unregister(apps, &app.identity) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_destroy(apps) == PXSYS_STATUS_OK);
    assert(test.outstanding == 0);
    puts("native client tests passed");
    return 0;
}
