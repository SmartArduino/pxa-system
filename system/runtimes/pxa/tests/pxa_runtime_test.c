#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxsys/pxa_runtime.h"

typedef struct {
    size_t starts;
    size_t destroys;
    pxsys_instance_ref_t reference;
} backend_t;

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

static pxsys_status_t instantiate(void* context, const pxsys_app_descriptor_t* app,
                                  uint64_t instance_id, void** output) {
    (void)context;
    assert(app != NULL && instance_id != 0);
    *output = (void*)app;
    return PXSYS_STATUS_OK;
}

static pxsys_status_t start(void* context, void* instance, const pxsys_message_t* message) {
    backend_t* backend = (backend_t*)context;
    assert(instance != NULL);
    (void)message;
    backend->starts++;
    return PXSYS_STATUS_OK;
}

static void bound(void* context, void* instance, pxsys_instance_ref_t reference) {
    backend_t* backend = (backend_t*)context;
    assert(instance != NULL);
    backend->reference = reference;
}

static pxsys_status_t state(void* context, void* instance) {
    (void)context;
    assert(instance != NULL);
    return PXSYS_STATUS_OK;
}

static pxsys_status_t deliver(void* context, void* instance, const pxsys_message_t* message) {
    (void)context;
    assert(instance != NULL && message != NULL);
    return PXSYS_STATUS_OK;
}

static pxsys_back_result_t back(void* context, void* instance) {
    (void)context;
    assert(instance != NULL);
    return PXSYS_BACK_UNHANDLED;
}

static void stop(void* context, void* instance, pxsys_stop_reason_t reason) {
    (void)context;
    (void)reason;
    assert(instance != NULL);
}

static void destroy(void* context, void* instance) {
    backend_t* backend = (backend_t*)context;
    assert(instance != NULL);
    backend->destroys++;
}

int main(void) {
    size_t allocations = 0;
    backend_t backend = {0};
    pxsys_pxa_runtime_config_t config = {0};
    pxsys_pxa_runtime_t* runtime = NULL;
    pxsys_runtime_provider_t provider;
    pxsys_app_descriptor_t app = {0};
    void* instance = NULL;

    config.struct_size = sizeof(config);
    config.runtime_id = pxsys_string_from_cstr(PXSYS_PXA_RUNTIME_AOT);
    config.version = (pxsys_version_t){1, 0};
    config.allocator.struct_size = sizeof(config.allocator);
    config.allocator.context = &allocations;
    config.allocator.allocate = allocate;
    config.allocator.release = release;
    config.backend.struct_size = sizeof(config.backend);
    config.backend.context = &backend;
    config.backend.instantiate = instantiate;
    config.backend.bound = bound;
    config.backend.start = start;
    config.backend.foreground = state;
    config.backend.background = state;
    config.backend.deliver = deliver;
    config.backend.back = back;
    config.backend.stop = stop;
    config.backend.destroy = destroy;
    assert(pxsys_pxa_runtime_create(&config, &runtime) == PXSYS_STATUS_OK);
    assert(pxsys_pxa_runtime_provider(runtime, &provider) == PXSYS_STATUS_OK);
    app.struct_size = sizeof(app);
    assert(provider.instantiate(provider.context, &app, 1, &instance) == PXSYS_STATUS_OK);
    provider.bound(provider.context, instance, (pxsys_instance_ref_t){3, 7});
    assert(backend.reference.slot == 3 && backend.reference.generation == 7);
    assert(provider.start(provider.context, instance, NULL) == PXSYS_STATUS_OK);
    assert(pxsys_pxa_runtime_destroy(runtime) == PXSYS_STATUS_BUSY);
    assert(provider.request_stop(provider.context, instance, PXSYS_STOP_NORMAL) == PXSYS_STATUS_OK);
    provider.destroy(provider.context, instance);
    assert(backend.starts == 1 && backend.destroys == 1);
    assert(pxsys_pxa_runtime_destroy(runtime) == PXSYS_STATUS_OK);
    assert(allocations == 0);
    puts("pxa runtime tests passed");
    return 0;
}
