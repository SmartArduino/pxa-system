#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxsys/native_client.h"
#include "pxsys/pxa_client.h"
#include "pxsys/role_registry.h"

typedef struct {
    size_t allocations;
    int service_called;
    int event_received;
} test_context_t;

static void* allocate(void* context, size_t size) {
    test_context_t* test = (test_context_t*)context;
    void* memory = malloc(size);
    if (memory != NULL)
        test->allocations++;
    return memory;
}

static void release(void* context, void* memory) {
    test_context_t* test = (test_context_t*)context;
    if (memory != NULL)
        test->allocations--;
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

static int string_equals(pxsys_string_t value, const char* expected) {
    size_t size = strlen(expected);
    return value.size == size && memcmp(value.data, expected, size) == 0;
}

static pxsys_status_t invoke_native_service(void* context, const pxsys_service_request_t* request,
                                            void* completion_context,
                                            pxsys_service_complete_fn complete) {
    test_context_t* test = (test_context_t*)context;
    assert(request->caller != NULL);
    assert(string_equals(request->caller->app.app_id, "weather"));
    assert(string_equals(request->caller->component_id, "main"));
    test->service_called = 1;
    complete(completion_context, request->request_id, PXSYS_STATUS_OK, pxsys_bytes(NULL, 0));
    return PXSYS_STATUS_OK;
}

static void complete_call(void* context, uint64_t request_id, pxsys_status_t status,
                          pxsys_bytes_t payload) {
    int* completed = (int*)context;
    assert(request_id == 42);
    assert(status == PXSYS_STATUS_OK);
    assert(payload.size == 0);
    *completed = 1;
}

static void receive_pxa_event(void* context, const pxsys_topic_event_t* event) {
    test_context_t* test = (test_context_t*)context;
    assert(event->publisher != NULL);
    assert(string_equals(event->publisher->app.app_id, "weather"));
    assert(string_equals(event->publisher->component_id, "main"));
    test->event_received = 1;
}

int main(void) {
    static const uint8_t weather_id[] = "weather";
    static const uint8_t weather_name[] = "Weather";
    static const uint8_t weather_version[] = "1.0.0";
    static const uint8_t main_component[] = "main";
    uint8_t weather_key[PXSYS_PUBLISHER_ROOT_BYTES];
    test_context_t test = {0};
    pxsys_app_registry_config_t app_config;
    pxsys_runtime_config_t runtime_config;
    pxsys_intent_resolver_config_t intent_config;
    pxsys_task_manager_config_t task_config;
    pxsys_service_registry_config_t service_config;
    pxsys_event_broker_config_t event_config;
    pxsys_role_registry_config_t role_config;
    pxsys_app_registry_t* apps = NULL;
    pxsys_runtime_t* runtime = NULL;
    pxsys_intent_resolver_t* intents = NULL;
    pxsys_task_manager_t* tasks = NULL;
    pxsys_service_registry_t* services = NULL;
    pxsys_event_broker_t* events = NULL;
    pxsys_role_registry_t* roles = NULL;
    pxsys_app_descriptor_t native_app = {0};
    pxsys_app_descriptor_t pxa_app = {0};
    pxsys_native_client_config_t native_config = {0};
    pxsys_pxa_client_config_t pxa_config = {0};
    pxsys_native_client_t* native_client = NULL;
    pxsys_pxa_client_t* pxa_client = NULL;
    pxa_package_manifest_t manifest = {0};
    pxa_package_component_t component = {0};
    pxsys_service_provider_t provider = {0};
    pxsys_service_request_t request = {0};
    pxsys_topic_event_t event = {0};
    pxsys_role_candidate_t role = {0};
    const pxsys_app_descriptor_t* role_app = NULL;
    pxsys_caller_t forged = {0};
    size_t delivered = 0;
    int completed = 0;

    memset(weather_key, 0x5a, sizeof(weather_key));
    pxsys_app_registry_config_init(&app_config);
    app_config.allocator = allocator(&test);
    assert(pxsys_app_registry_create(&app_config, &apps) == PXSYS_STATUS_OK);

    native_app.struct_size = sizeof(native_app);
    memset(native_app.identity.publisher_root, 0x31, PXSYS_PUBLISHER_ROOT_BYTES);
    native_app.identity.app_id = pxsys_string_from_cstr("system.settings");
    native_app.display_name = pxsys_string_from_cstr("Settings");
    native_app.version = pxsys_string_from_cstr("1.0.0");
    native_app.runtime_id = pxsys_string_from_cstr("native-static");
    native_app.flags = PXSYS_APP_FLAG_ENABLED | PXSYS_APP_FLAG_SYSTEM;
    assert(pxsys_app_registry_register(apps, &native_app) == PXSYS_STATUS_OK);

    component.id = (pxa_bytes_t){main_component, sizeof(main_component) - 1u};
    manifest.management_key_id = weather_key;
    manifest.app_id = (pxa_bytes_t){weather_id, sizeof(weather_id) - 1u};
    manifest.name = (pxa_bytes_t){weather_name, sizeof(weather_name) - 1u};
    manifest.version = (pxa_bytes_t){weather_version, sizeof(weather_version) - 1u};
    manifest.components = &component;
    manifest.component_count = 1;
    assert(pxsys_pxa_manifest_descriptor(&manifest, pxsys_string_from_cstr(PXSYS_PXA_RUNTIME_AOT),
                                         PXSYS_APP_FLAG_ENABLED | PXSYS_APP_FLAG_REMOVABLE,
                                         &pxa_app) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_register(apps, &pxa_app) == PXSYS_STATUS_OK);

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
    pxsys_role_registry_config_init(&role_config);
    role_config.apps = apps;
    role_config.allocator = allocator(&test);
    assert(pxsys_role_registry_create(&role_config, &roles) == PXSYS_STATUS_OK);

    role.struct_size = sizeof(role);
    role.role_id = pxsys_string_from_cstr(PXSYS_ROLE_SETTINGS);
    role.app = pxa_app.identity;
    role.priority = 100;
    assert(pxsys_role_candidate_register(roles, &role) == PXSYS_STATUS_OK);
    assert(pxsys_role_resolve(roles, role.role_id, &role_app) == PXSYS_STATUS_OK);
    assert(pxsys_app_identity_equal(&role_app->identity, &pxa_app.identity));

    native_config.struct_size = sizeof(native_config);
    native_config.app = native_app.identity;
    native_config.component_id = pxsys_string_from_cstr("main");
    native_config.tasks = tasks;
    native_config.services = services;
    native_config.events = events;
    native_config.allocator = allocator(&test);
    assert(pxsys_native_client_create(&native_config, &native_client) == PXSYS_STATUS_OK);
    pxa_config.struct_size = sizeof(pxa_config);
    pxa_config.manifest = &manifest;
    pxa_config.component_id = component.id;
    pxa_config.tasks = tasks;
    pxa_config.services = services;
    pxa_config.events = events;
    pxa_config.allocator = allocator(&test);
    assert(pxsys_pxa_client_create(&pxa_config, &pxa_client) == PXSYS_STATUS_OK);

    provider.struct_size = sizeof(provider);
    provider.interface_id = pxsys_string_from_cstr("system.settings");
    provider.version = (pxsys_version_t){1, 0};
    provider.context = &test;
    provider.invoke = invoke_native_service;
    assert(pxsys_service_register(services, &provider) == PXSYS_STATUS_OK);
    forged.struct_size = sizeof(forged);
    forged.app.app_id = pxsys_string_from_cstr("forged");
    request.struct_size = sizeof(request);
    request.interface_id = provider.interface_id;
    request.version = provider.version;
    request.request_id = 42;
    request.caller = &forged;
    assert(pxsys_pxa_client_invoke(pxa_client, &request, &completed, complete_call) ==
           PXSYS_STATUS_OK);
    assert(completed && test.service_called);

    assert(pxsys_native_client_subscribe(native_client, pxsys_string_from_cstr("weather.changed"),
                                         (pxsys_version_t){1, 0}, &test,
                                         receive_pxa_event) == PXSYS_STATUS_OK);
    event.struct_size = sizeof(event);
    event.topic = pxsys_string_from_cstr("weather.changed");
    event.version = (pxsys_version_t){1, 0};
    event.publisher = &forged;
    assert(pxsys_pxa_client_publish(pxa_client, &event, &delivered) == PXSYS_STATUS_OK);
    assert(delivered == 1 && test.event_received);
    assert(pxsys_native_client_unsubscribe(native_client, event.topic, &test, receive_pxa_event) ==
           PXSYS_STATUS_OK);

    assert(pxsys_service_unregister(services, provider.interface_id, 1) == PXSYS_STATUS_OK);
    pxsys_pxa_client_destroy(pxa_client);
    pxsys_native_client_destroy(native_client);
    assert(pxsys_role_candidates_unregister(roles, &pxa_app.identity) == PXSYS_STATUS_OK);
    assert(pxsys_role_registry_destroy(roles) == PXSYS_STATUS_OK);
    assert(pxsys_event_broker_destroy(events) == PXSYS_STATUS_OK);
    assert(pxsys_service_registry_destroy(services) == PXSYS_STATUS_OK);
    assert(pxsys_task_manager_destroy(tasks) == PXSYS_STATUS_OK);
    assert(pxsys_intent_resolver_destroy(intents) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_destroy(runtime) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_unregister(apps, &pxa_app.identity) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_unregister(apps, &native_app.identity) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_destroy(apps) == PXSYS_STATUS_OK);
    assert(test.allocations == 0);
    puts("PXA/native interoperability tests passed");
    return 0;
}
