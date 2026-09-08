#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxsys/intent_wire.h"
#include "pxsys/task_manager.h"

typedef struct {
    size_t starts;
    size_t deliveries;
    size_t stops;
    size_t active;
    size_t authorizations;
    int deny_settings;
    int pending_start;
    int pending_stop;
} fake_runtime_t;

typedef struct {
    fake_runtime_t* owner;
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
    pxsys_allocator_t value = {0};
    value.struct_size = sizeof(value);
    value.context = allocations;
    value.allocate = test_allocate;
    value.release = test_release;
    return value;
}

static pxsys_status_t instantiate(void* context, const pxsys_app_descriptor_t* app,
                                  uint64_t instance_id, void** output) {
    fake_runtime_t* fake = (fake_runtime_t*)context;
    fake_instance_t* instance = (fake_instance_t*)malloc(sizeof(*instance));
    (void)app;
    (void)instance_id;
    if (instance == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    instance->owner = fake;
    fake->active++;
    *output = instance;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t check_intent(void* context, void* runtime_instance,
                                   const pxsys_message_t* message) {
    fake_instance_t* instance = (fake_instance_t*)runtime_instance;
    pxsys_intent_t decoded;
    pxsys_app_identity_t target;
    (void)context;
    assert(message != NULL);
    assert(pxsys_intent_wire_decode(message->payload.data, message->payload.size, &decoded,
                                    &target) == PXSYS_STATUS_OK);
    assert(decoded.action.size != 0);
    if (message->operation == PXSYS_INTENT_OPERATION_START)
        instance->owner->starts++;
    else if (message->operation == PXSYS_INTENT_OPERATION_DELIVER)
        instance->owner->deliveries++;
    else
        assert(0);
    return message->operation == PXSYS_INTENT_OPERATION_START && instance->owner->pending_start
               ? PXSYS_STATUS_PENDING
               : PXSYS_STATUS_OK;
}

static pxsys_status_t state(void* context, void* runtime_instance) {
    (void)context;
    assert(runtime_instance != NULL);
    return PXSYS_STATUS_OK;
}

static pxsys_back_result_t back(void* context, void* runtime_instance) {
    (void)context;
    assert(runtime_instance != NULL);
    return PXSYS_BACK_UNHANDLED;
}

static void stop(void* context, void* runtime_instance, pxsys_stop_reason_t reason) {
    fake_instance_t* instance = (fake_instance_t*)runtime_instance;
    (void)context;
    (void)reason;
    instance->owner->stops++;
}

static pxsys_status_t request_stop(void* context, void* runtime_instance,
                                   pxsys_stop_reason_t reason) {
    fake_instance_t* instance = (fake_instance_t*)runtime_instance;
    (void)context;
    (void)reason;
    instance->owner->stops++;
    return instance->owner->pending_stop ? PXSYS_STATUS_PENDING : PXSYS_STATUS_OK;
}

static void destroy(void* context, void* runtime_instance) {
    fake_instance_t* instance = (fake_instance_t*)runtime_instance;
    (void)context;
    instance->owner->active--;
    free(instance);
}

static pxsys_app_identity_t identity(uint8_t publisher, const char* app_id) {
    pxsys_app_identity_t value;
    memset(&value, publisher, sizeof(value.publisher_root));
    value.app_id = pxsys_string_from_cstr(app_id);
    return value;
}

static pxsys_app_descriptor_t app(uint8_t publisher, const char* app_id, uint32_t flags) {
    pxsys_app_descriptor_t value = {0};
    value.struct_size = sizeof(value);
    value.identity = identity(publisher, app_id);
    value.display_name = pxsys_string_from_cstr(app_id);
    value.version = pxsys_string_from_cstr("1.0.0");
    value.runtime_id = pxsys_string_from_cstr("test-runtime");
    value.flags = flags | PXSYS_APP_FLAG_ENABLED;
    return value;
}

static pxsys_status_t authorize_navigation(void* context, const pxsys_caller_t* caller,
                                           uint32_t operation, const pxsys_intent_t* intent,
                                           const pxsys_app_descriptor_t* target) {
    fake_runtime_t* fake = (fake_runtime_t*)context;
    assert(operation == PXSYS_INTENT_OPERATION_START ||
           operation == PXSYS_INTENT_OPERATION_DELIVER);
    assert(intent != NULL && target != NULL);
    assert(caller == NULL || caller->struct_size >= sizeof(*caller));
    fake->authorizations++;
    if (fake->deny_settings && target->identity.app_id.size == 8 &&
        memcmp(target->identity.app_id.data, "settings", 8) == 0) {
        return PXSYS_STATUS_DENIED;
    }
    return PXSYS_STATUS_OK;
}

static void test_navigation_and_single_instance(void) {
    size_t allocations = 0;
    fake_runtime_t fake = {0};
    pxsys_app_registry_config_t app_config;
    pxsys_runtime_config_t runtime_config;
    pxsys_intent_resolver_config_t intent_config;
    pxsys_task_manager_config_t task_config;
    pxsys_app_registry_t* apps = NULL;
    pxsys_runtime_t* runtime = NULL;
    pxsys_intent_resolver_t* resolver = NULL;
    pxsys_task_manager_t* tasks = NULL;
    pxsys_app_descriptor_t home = app(1, "home", PXSYS_APP_FLAG_SINGLE_INSTANCE);
    pxsys_app_descriptor_t settings = app(1, "settings", 0);
    pxsys_runtime_provider_t provider = {0};
    pxsys_intent_t intent = {0};
    pxsys_instance_ref_t instance;
    pxsys_instance_ref_t home_instance;
    pxsys_instance_ref_t settings_instance;
    pxsys_instance_ref_t recent_instance;
    pxsys_instance_snapshot_t recent_snapshot = {0};
    pxsys_back_result_t back_result;

    pxsys_app_registry_config_init(&app_config);
    app_config.allocator = allocator(&allocations);
    assert(pxsys_app_registry_create(&app_config, &apps) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_register(apps, &home) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_register(apps, &settings) == PXSYS_STATUS_OK);
    pxsys_runtime_config_init(&runtime_config);
    runtime_config.apps = apps;
    runtime_config.allocator = allocator(&allocations);
    assert(pxsys_runtime_create(&runtime_config, &runtime) == PXSYS_STATUS_OK);
    provider.struct_size = sizeof(provider);
    provider.runtime_id = pxsys_string_from_cstr("test-runtime");
    provider.context = &fake;
    provider.instantiate = instantiate;
    provider.start = check_intent;
    provider.foreground = state;
    provider.background = state;
    provider.event = check_intent;
    provider.back = back;
    provider.stop = stop;
    provider.destroy = destroy;
    provider.request_stop = request_stop;
    assert(pxsys_runtime_register_provider(runtime, &provider) == PXSYS_STATUS_OK);
    pxsys_intent_resolver_config_init(&intent_config);
    intent_config.apps = apps;
    intent_config.allocator = allocator(&allocations);
    assert(pxsys_intent_resolver_create(&intent_config, &resolver) == PXSYS_STATUS_OK);
    pxsys_task_manager_config_init(&task_config);
    task_config.intents = resolver;
    task_config.runtime = runtime;
    task_config.policy_context = &fake;
    task_config.authorize = authorize_navigation;
    task_config.allocator = allocator(&allocations);
    assert(pxsys_task_manager_create(&task_config, &tasks) == PXSYS_STATUS_OK);

    intent.struct_size = sizeof(intent);
    intent.action = pxsys_string_from_cstr("system.intent.main");
    intent.target = &home.identity;
    assert(pxsys_task_manager_start(tasks, &intent, &instance) == PXSYS_STATUS_OK);
    home_instance = instance;
    intent.target = &settings.identity;
    fake.deny_settings = 1;
    assert(pxsys_task_manager_start(tasks, &intent, &instance) == PXSYS_STATUS_DENIED);
    assert(pxsys_task_manager_count(tasks) == 1 && fake.active == 1);
    fake.deny_settings = 0;
    fake.pending_start = 1;
    assert(pxsys_task_manager_start(tasks, &intent, &instance) == PXSYS_STATUS_PENDING);
    assert(pxsys_task_manager_count(tasks) == 2 && fake.active == 2);
    assert(pxsys_task_manager_complete_start(tasks, instance, PXSYS_STATUS_INTERNAL) ==
           PXSYS_STATUS_INTERNAL);
    assert(pxsys_task_manager_count(tasks) == 1 && fake.active == 1 && fake.stops == 1);
    fake.pending_start = 0;
    assert(pxsys_task_manager_start(tasks, &intent, &instance) == PXSYS_STATUS_OK);
    assert(pxsys_task_manager_count(tasks) == 2 && fake.starts == 3 && fake.active == 2);
    assert(pxsys_task_manager_report_stopped(tasks, instance, PXSYS_STOP_NORMAL) ==
           PXSYS_STATUS_OK);
    assert(pxsys_task_manager_count(tasks) == 1 && fake.active == 1 && fake.stops == 1);
    assert(pxsys_task_manager_start(tasks, &intent, &instance) == PXSYS_STATUS_OK);
    settings_instance = instance;
    assert(pxsys_task_manager_count(tasks) == 2 && fake.starts == 4 && fake.active == 2);
    intent.target = &home.identity;
    assert(pxsys_task_manager_start(tasks, &intent, &instance) == PXSYS_STATUS_OK);
    assert(instance.slot == home_instance.slot &&
           instance.generation == home_instance.generation);
    assert(pxsys_task_manager_count(tasks) == 2 && fake.starts == 4 && fake.deliveries == 1);
    assert(fake.authorizations == 6);
    recent_snapshot.struct_size = sizeof(recent_snapshot);
    assert(pxsys_task_manager_task(tasks, 0, &recent_instance,
                                   &recent_snapshot) == PXSYS_STATUS_OK);
    assert(recent_instance.slot == home_instance.slot &&
           recent_instance.generation == home_instance.generation);
    assert(recent_snapshot.app != NULL &&
           pxsys_app_identity_equal(&recent_snapshot.app->identity,
                                    &home.identity));
    recent_snapshot.struct_size = sizeof(recent_snapshot);
    assert(pxsys_task_manager_task(tasks, 1, &recent_instance,
                                   &recent_snapshot) == PXSYS_STATUS_OK);
    assert(recent_instance.slot == settings_instance.slot &&
           recent_instance.generation == settings_instance.generation);
    assert(recent_snapshot.app != NULL &&
           pxsys_app_identity_equal(&recent_snapshot.app->identity,
                                    &settings.identity));
    assert(pxsys_task_manager_task(tasks, 2, &recent_instance,
                                   &recent_snapshot) == PXSYS_STATUS_NOT_FOUND);
    assert(pxsys_task_manager_activate(tasks, settings_instance) ==
           PXSYS_STATUS_OK);
    assert(pxsys_task_manager_current(tasks, &recent_instance) ==
           PXSYS_STATUS_OK);
    assert(recent_instance.slot == settings_instance.slot &&
           recent_instance.generation == settings_instance.generation);
    recent_instance = pxsys_instance_ref_invalid();
    assert(pxsys_task_manager_activate(tasks, recent_instance) ==
           PXSYS_STATUS_NOT_FOUND);
    assert(pxsys_task_manager_back(tasks, &back_result) == PXSYS_STATUS_OK);
    assert(back_result == PXSYS_BACK_HANDLED && pxsys_task_manager_count(tasks) == 1);
    fake.pending_stop = 1;
    assert(pxsys_task_manager_current(tasks, &instance) == PXSYS_STATUS_OK);
    assert(pxsys_task_manager_finish_top(tasks, PXSYS_STOP_SHUTDOWN) == PXSYS_STATUS_PENDING);
    assert(pxsys_task_manager_count(tasks) == 1 && fake.active == 1);
    assert(pxsys_task_manager_report_stopped(tasks, instance, PXSYS_STOP_SHUTDOWN) ==
           PXSYS_STATUS_OK);
    assert(fake.active == 0 && fake.stops == 3);

    assert(pxsys_task_manager_destroy(tasks) == PXSYS_STATUS_OK);
    assert(pxsys_intent_resolver_destroy(resolver) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_unregister_provider(runtime, pxsys_string_from_cstr("test-runtime")) ==
           PXSYS_STATUS_OK);
    assert(pxsys_runtime_destroy(runtime) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_unregister(apps, &home.identity) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_unregister(apps, &settings.identity) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_destroy(apps) == PXSYS_STATUS_OK);
    assert(allocations == 0);
}

int main(void) {
    test_navigation_and_single_instance();
    puts("task manager tests passed");
    return 0;
}
