#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxsys/service_registry.h"

typedef struct {
    void* completion_context;
    pxsys_service_complete_fn complete;
    uint64_t request_id;
} fake_sensor_t;

typedef struct {
    size_t calls;
    pxsys_status_t status;
    uint8_t value;
} result_t;

static void* allocate(void* context, size_t size) {
    size_t* count = (size_t*)context;
    void* memory = malloc(size);
    if (memory != NULL)
        (*count)++;
    return memory;
}

static void release(void* context, void* memory) {
    size_t* count = (size_t*)context;
    if (memory != NULL)
        (*count)--;
    free(memory);
}

static pxsys_status_t authorize(void* context, const pxsys_service_request_t* request) {
    size_t* calls = (size_t*)context;
    (*calls)++;
    return request->operation == 99 ? PXSYS_STATUS_DENIED : PXSYS_STATUS_OK;
}

static pxsys_status_t invoke(void* context, const pxsys_service_request_t* request,
                             void* completion_context, pxsys_service_complete_fn complete) {
    fake_sensor_t* sensor = (fake_sensor_t*)context;
    sensor->completion_context = completion_context;
    sensor->complete = complete;
    sensor->request_id = request->request_id;
    return PXSYS_STATUS_OK;
}

static void completed(void* context, uint64_t request_id, pxsys_status_t status,
                      pxsys_bytes_t payload) {
    result_t* result = (result_t*)context;
    assert(request_id == 42 && payload.size == 1);
    result->calls++;
    result->status = status;
    result->value = payload.data[0];
}

int main(void) {
    size_t allocations = 0;
    size_t policy_calls = 0;
    fake_sensor_t sensor = {0};
    result_t result = {0};
    pxsys_service_registry_config_t config;
    pxsys_service_registry_t* registry = NULL;
    pxsys_service_provider_t provider = {0};
    pxsys_service_request_t request = {0};
    pxsys_service_info_t info = {0};
    pxsys_caller_t caller = {0};
    uint8_t value = 27;

    pxsys_service_registry_config_init(&config);
    config.policy_context = &policy_calls;
    config.authorize = authorize;
    config.allocator.struct_size = sizeof(config.allocator);
    config.allocator.context = &allocations;
    config.allocator.allocate = allocate;
    config.allocator.release = release;
    assert(pxsys_service_registry_create(&config, &registry) == PXSYS_STATUS_OK);
    provider.struct_size = sizeof(provider);
    provider.interface_id = pxsys_string_from_cstr("vendor.example.sensor.soil-moisture");
    provider.version = (pxsys_version_t){1, 2};
    provider.features = UINT64_C(0x5);
    provider.context = &sensor;
    provider.invoke = invoke;
    assert(pxsys_service_register(registry, &provider) == PXSYS_STATUS_OK);
    assert(pxsys_service_count(registry) == 1);
    assert(pxsys_service_at(registry, 0, &info) == PXSYS_STATUS_OK);
    assert(info.features == UINT64_C(0x5) && info.version.minor == 2);
    assert(pxsys_service_resolve(registry, provider.interface_id, (pxsys_version_t){1, 1}, &info) ==
           PXSYS_STATUS_OK);
    assert(pxsys_service_resolve(registry, provider.interface_id, (pxsys_version_t){1, 3}, &info) ==
           PXSYS_STATUS_NOT_FOUND);
    caller.struct_size = sizeof(caller);
    memset(caller.app.publisher_root, 1, PXSYS_PUBLISHER_ROOT_BYTES);
    caller.app.app_id = pxsys_string_from_cstr("garden");
    caller.component_id = pxsys_string_from_cstr("main");
    request.struct_size = sizeof(request);
    request.interface_id = provider.interface_id;
    request.version = (pxsys_version_t){1, 0};
    request.operation = 1;
    request.request_id = 42;
    request.caller = &caller;
    assert(pxsys_service_invoke(registry, &request, &result, completed) == PXSYS_STATUS_OK);
    assert(pxsys_service_unregister(registry, provider.interface_id, 1) == PXSYS_STATUS_BUSY);
    sensor.complete(sensor.completion_context, sensor.request_id, PXSYS_STATUS_OK,
                    pxsys_bytes(&value, 1));
    assert(result.calls == 1 && result.status == PXSYS_STATUS_OK && result.value == 27);
    request.operation = 99;
    assert(pxsys_service_invoke(registry, &request, &result, completed) == PXSYS_STATUS_DENIED);
    assert(policy_calls == 2);
    assert(pxsys_service_unregister(registry, provider.interface_id, 1) == PXSYS_STATUS_OK);
    assert(pxsys_service_registry_destroy(registry) == PXSYS_STATUS_OK);
    assert(allocations == 0);
    puts("service registry tests passed");
    return 0;
}
