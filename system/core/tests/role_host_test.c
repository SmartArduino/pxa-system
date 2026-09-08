#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxsys/intent_wire.h"
#include "pxsys/role_host.h"

typedef struct {
    size_t active;
    size_t starts;
    size_t deliveries;
    size_t stops;
    size_t authorizations;
    int pending_start;
    int pending_stop;
} fake_runtime_t;

typedef struct {
    fake_runtime_t* runtime;
} fake_instance_t;

static void* allocate(void* context, size_t size) {
    size_t* outstanding = (size_t*)context;
    void* memory = malloc(size);
    if (memory != NULL)
        (*outstanding)++;
    return memory;
}

static void release(void* context, void* memory) {
    size_t* outstanding = (size_t*)context;
    if (memory != NULL)
        (*outstanding)--;
    free(memory);
}

static pxsys_allocator_t allocator(size_t* outstanding) {
    pxsys_allocator_t result = {0};
    result.struct_size = sizeof(result);
    result.context = outstanding;
    result.allocate = allocate;
    result.release = release;
    return result;
}

static pxsys_app_identity_t identity(uint8_t publisher, const char* app_id) {
    pxsys_app_identity_t result;
    memset(result.publisher_root, publisher, sizeof(result.publisher_root));
    result.app_id = pxsys_string_from_cstr(app_id);
    return result;
}

static pxsys_app_descriptor_t app(uint8_t publisher, const char* app_id) {
    pxsys_app_descriptor_t result = {0};
    result.struct_size = sizeof(result);
    result.identity = identity(publisher, app_id);
    result.display_name = pxsys_string_from_cstr(app_id);
    result.version = pxsys_string_from_cstr("1.0.0");
    result.runtime_id = pxsys_string_from_cstr("role-test");
    result.flags = PXSYS_APP_FLAG_ENABLED;
    return result;
}

static pxsys_status_t instantiate(void* context, const pxsys_app_descriptor_t* app,
                                  uint64_t instance_id, void** output) {
    fake_runtime_t* runtime = (fake_runtime_t*)context;
    fake_instance_t* instance = (fake_instance_t*)malloc(sizeof(*instance));
    (void)app;
    (void)instance_id;
    if (instance == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    instance->runtime = runtime;
    runtime->active++;
    *output = instance;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t receive(void* context, void* runtime_instance,
                              const pxsys_message_t* message) {
    fake_runtime_t* runtime = ((fake_instance_t*)runtime_instance)->runtime;
    pxsys_intent_t decoded = {0};
    pxsys_app_identity_t target;
    (void)context;
    assert(message->caller != NULL);
    assert(pxsys_intent_wire_decode(message->payload.data, message->payload.size,
                                    &decoded, &target) == PXSYS_STATUS_OK);
    assert(decoded.target != NULL);
    if (message->operation == PXSYS_INTENT_OPERATION_START) {
        runtime->starts++;
        return runtime->pending_start ? PXSYS_STATUS_PENDING : PXSYS_STATUS_OK;
    }
    assert(message->operation == PXSYS_INTENT_OPERATION_DELIVER);
    runtime->deliveries++;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t state(void* context, void* runtime_instance) {
    (void)context;
    assert(runtime_instance != NULL);
    return PXSYS_STATUS_OK;
}

static pxsys_back_result_t back(void* context, void* runtime_instance) {
    (void)context;
    (void)runtime_instance;
    return PXSYS_BACK_UNHANDLED;
}

static void stop(void* context, void* runtime_instance,
                 pxsys_stop_reason_t reason) {
    fake_runtime_t* runtime = ((fake_instance_t*)runtime_instance)->runtime;
    (void)context;
    (void)reason;
    runtime->stops++;
}

static pxsys_status_t request_stop(void* context, void* runtime_instance,
                                   pxsys_stop_reason_t reason) {
    fake_runtime_t* runtime = ((fake_instance_t*)runtime_instance)->runtime;
    (void)context;
    (void)reason;
    runtime->stops++;
    return runtime->pending_stop ? PXSYS_STATUS_PENDING : PXSYS_STATUS_OK;
}

static void destroy(void* context, void* runtime_instance) {
    fake_instance_t* instance = (fake_instance_t*)runtime_instance;
    (void)context;
    instance->runtime->active--;
    free(instance);
}

static pxsys_status_t authorize(void* context, const pxsys_caller_t* caller,
                                uint32_t operation, const pxsys_intent_t* intent,
                                const pxsys_app_descriptor_t* target) {
    fake_runtime_t* runtime = (fake_runtime_t*)context;
    assert(caller != NULL && intent != NULL && target != NULL);
    assert(operation == PXSYS_INTENT_OPERATION_START ||
           operation == PXSYS_INTENT_OPERATION_DELIVER);
    runtime->authorizations++;
    return PXSYS_STATUS_OK;
}

int main(void) {
    size_t outstanding = 0;
    fake_runtime_t fake = {0};
    pxsys_app_registry_config_t apps_config;
    pxsys_runtime_config_t runtime_config;
    pxsys_role_registry_config_t roles_config;
    pxsys_role_host_config_t host_config;
    pxsys_app_registry_t* apps = NULL;
    pxsys_runtime_t* runtime = NULL;
    pxsys_role_registry_t* roles = NULL;
    pxsys_role_host_t* host = NULL;
    pxsys_app_descriptor_t first = app(1, "status.default");
    pxsys_app_descriptor_t replacement = app(2, "status.product");
    pxsys_runtime_provider_t provider = {0};
    pxsys_role_candidate_t candidate = {0};
    pxsys_intent_t intent = {0};
    pxsys_caller_t caller = {0};
    pxsys_instance_ref_t old_instance;
    pxsys_instance_ref_t new_instance;
    pxsys_instance_ref_t current;

    pxsys_app_registry_config_init(&apps_config);
    apps_config.allocator = allocator(&outstanding);
    assert(pxsys_app_registry_create(&apps_config, &apps) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_register(apps, &first) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_register(apps, &replacement) == PXSYS_STATUS_OK);

    pxsys_runtime_config_init(&runtime_config);
    runtime_config.apps = apps;
    runtime_config.allocator = allocator(&outstanding);
    assert(pxsys_runtime_create(&runtime_config, &runtime) == PXSYS_STATUS_OK);
    provider.struct_size = sizeof(provider);
    provider.runtime_id = pxsys_string_from_cstr("role-test");
    provider.context = &fake;
    provider.instantiate = instantiate;
    provider.start = receive;
    provider.foreground = state;
    provider.background = state;
    provider.event = receive;
    provider.back = back;
    provider.stop = stop;
    provider.destroy = destroy;
    provider.request_stop = request_stop;
    assert(pxsys_runtime_register_provider(runtime, &provider) == PXSYS_STATUS_OK);

    pxsys_role_registry_config_init(&roles_config);
    roles_config.apps = apps;
    roles_config.allocator = allocator(&outstanding);
    assert(pxsys_role_registry_create(&roles_config, &roles) == PXSYS_STATUS_OK);
    candidate.struct_size = sizeof(candidate);
    candidate.role_id = pxsys_string_from_cstr(PXSYS_ROLE_STATUS_BAR);
    candidate.app = first.identity;
    candidate.priority = 0;
    assert(pxsys_role_candidate_register(roles, &candidate) == PXSYS_STATUS_OK);

    pxsys_role_host_config_init(&host_config);
    host_config.roles = roles;
    host_config.runtime = runtime;
    host_config.policy_context = &fake;
    host_config.authorize = authorize;
    host_config.allocator = allocator(&outstanding);
    assert(pxsys_role_host_create(&host_config, &host) == PXSYS_STATUS_OK);

    caller.struct_size = sizeof(caller);
    caller.app = identity(3, "system.shell");
    caller.component_id = pxsys_string_from_cstr("role-supervisor");
    intent.struct_size = sizeof(intent);
    intent.action = pxsys_string_from_cstr("system.intent.main");
    assert(pxsys_role_host_start(host, &caller, candidate.role_id, &intent,
                                 &old_instance) == PXSYS_STATUS_OK);
    assert(fake.active == 1 && fake.starts == 1);

    candidate.app = replacement.identity;
    candidate.priority = 10;
    assert(pxsys_role_candidate_register(roles, &candidate) == PXSYS_STATUS_OK);
    fake.pending_start = 1;
    assert(pxsys_role_host_start(host, &caller, candidate.role_id, &intent,
                                 &new_instance) == PXSYS_STATUS_PENDING);
    assert(pxsys_role_host_current(host, candidate.role_id, &current) == PXSYS_STATUS_OK);
    assert(current.slot == old_instance.slot && current.generation == old_instance.generation);
    assert(fake.active == 2);

    fake.pending_start = 0;
    fake.pending_stop = 1;
    assert(pxsys_role_host_complete_start(host, new_instance, PXSYS_STATUS_OK) ==
           PXSYS_STATUS_OK);
    assert(pxsys_role_host_current(host, candidate.role_id, &current) == PXSYS_STATUS_OK);
    assert(current.slot == new_instance.slot && current.generation == new_instance.generation);
    assert(pxsys_role_host_report_stopped(host, old_instance, PXSYS_STOP_REPLACED) ==
           PXSYS_STATUS_OK);
    assert(fake.active == 1);

    assert(pxsys_role_host_start(host, &caller, candidate.role_id, &intent,
                                 &current) == PXSYS_STATUS_OK);
    assert(fake.deliveries == 1 && fake.active == 1);
    assert(pxsys_role_host_stop(host, candidate.role_id, PXSYS_STOP_SHUTDOWN) ==
           PXSYS_STATUS_PENDING);
    assert(pxsys_role_host_report_stopped(host, new_instance, PXSYS_STOP_SHUTDOWN) ==
           PXSYS_STATUS_OK);
    assert(pxsys_role_host_count(host) == 0 && fake.active == 0);

    assert(pxsys_role_host_destroy(host) == PXSYS_STATUS_OK);
    assert(pxsys_role_registry_destroy(roles) == PXSYS_STATUS_OK);
    assert(pxsys_runtime_unregister_provider(runtime,
                                             pxsys_string_from_cstr("role-test")) ==
           PXSYS_STATUS_OK);
    assert(pxsys_runtime_destroy(runtime) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_unregister(apps, &first.identity) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_unregister(apps, &replacement.identity) == PXSYS_STATUS_OK);
    assert(pxsys_app_registry_destroy(apps) == PXSYS_STATUS_OK);
    assert(outstanding == 0);
    assert(fake.authorizations == 3);
    puts("role host tests passed");
    return 0;
}
