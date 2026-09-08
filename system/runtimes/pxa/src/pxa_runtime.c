#include "pxsys/pxa_runtime.h"

#include <stddef.h>
#include <string.h>

#define PXSYS_PXA_RUNTIME_MAGIC UINT32_C(0x50585052)

typedef struct {
    void* backend_instance;
} pxa_instance_t;

struct pxsys_pxa_runtime {
    uint32_t magic;
    uint32_t active_instances;
    char* runtime_id;
    pxsys_version_t version;
    uint64_t features;
    pxsys_pxa_runtime_backend_t backend;
    pxsys_allocator_t allocator;
};

static int runtime_valid(const pxsys_pxa_runtime_t* runtime) {
    return runtime != NULL && runtime->magic == PXSYS_PXA_RUNTIME_MAGIC;
}

static int backend_valid(const pxsys_pxa_runtime_backend_t* backend) {
    return backend->struct_size >= offsetof(pxsys_pxa_runtime_backend_t, bound) &&
           backend->instantiate != NULL && backend->start != NULL && backend->foreground != NULL &&
           backend->background != NULL && backend->deliver != NULL && backend->back != NULL &&
           backend->stop != NULL && backend->destroy != NULL;
}

pxsys_status_t pxsys_pxa_runtime_create(const pxsys_pxa_runtime_config_t* config,
                                        pxsys_pxa_runtime_t** output) {
    pxsys_pxa_runtime_t* runtime;
    if (output == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (config == NULL || config->struct_size < sizeof(*config) ||
        !pxsys_identifier_validate(config->runtime_id, 64) || !backend_valid(&config->backend) ||
        config->allocator.struct_size < sizeof(config->allocator) ||
        config->allocator.allocate == NULL || config->allocator.release == NULL) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    runtime = (pxsys_pxa_runtime_t*)config->allocator.allocate(config->allocator.context,
                                                               sizeof(*runtime));
    if (runtime == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memset(runtime, 0, sizeof(*runtime));
    runtime->runtime_id =
        (char*)config->allocator.allocate(config->allocator.context, config->runtime_id.size + 1u);
    if (runtime->runtime_id == NULL) {
        config->allocator.release(config->allocator.context, runtime);
        return PXSYS_STATUS_NO_MEMORY;
    }
    memcpy(runtime->runtime_id, config->runtime_id.data, config->runtime_id.size);
    runtime->runtime_id[config->runtime_id.size] = '\0';
    runtime->version = config->version;
    runtime->features = config->features;
    memset(&runtime->backend, 0, sizeof(runtime->backend));
    memcpy(&runtime->backend, &config->backend,
           config->backend.struct_size < sizeof(runtime->backend) ? config->backend.struct_size
                                                                  : sizeof(runtime->backend));
    runtime->backend.struct_size = sizeof(runtime->backend);
    runtime->allocator = config->allocator;
    runtime->magic = PXSYS_PXA_RUNTIME_MAGIC;
    *output = runtime;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_pxa_runtime_destroy(pxsys_pxa_runtime_t* runtime) {
    pxsys_allocator_t allocator;
    if (!runtime_valid(runtime))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (runtime->active_instances != 0)
        return PXSYS_STATUS_BUSY;
    allocator = runtime->allocator;
    runtime->magic = 0;
    allocator.release(allocator.context, runtime->runtime_id);
    allocator.release(allocator.context, runtime);
    return PXSYS_STATUS_OK;
}

static pxsys_status_t instantiate(void* context, const pxsys_app_descriptor_t* app,
                                  uint64_t instance_id, void** output) {
    pxsys_pxa_runtime_t* runtime = (pxsys_pxa_runtime_t*)context;
    pxa_instance_t* instance;
    pxsys_status_t status;
    if (!runtime_valid(runtime) || output == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    instance =
        (pxa_instance_t*)runtime->allocator.allocate(runtime->allocator.context, sizeof(*instance));
    if (instance == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    instance->backend_instance = NULL;
    *output = instance;
    runtime->active_instances++;
    status = runtime->backend.instantiate(runtime->backend.context, app, instance_id,
                                          &instance->backend_instance);
    return status;
}

static pxsys_status_t start(void* context, void* runtime_instance, const pxsys_message_t* launch) {
    pxsys_pxa_runtime_t* runtime = (pxsys_pxa_runtime_t*)context;
    pxa_instance_t* instance = (pxa_instance_t*)runtime_instance;
    return runtime->backend.start(runtime->backend.context, instance->backend_instance, launch);
}

static void bound(void* context, void* runtime_instance, pxsys_instance_ref_t reference) {
    pxsys_pxa_runtime_t* runtime = (pxsys_pxa_runtime_t*)context;
    pxa_instance_t* instance = (pxa_instance_t*)runtime_instance;
    if (runtime->backend.bound != NULL)
        runtime->backend.bound(runtime->backend.context, instance->backend_instance, reference);
}

static pxsys_status_t foreground(void* context, void* runtime_instance) {
    pxsys_pxa_runtime_t* runtime = (pxsys_pxa_runtime_t*)context;
    pxa_instance_t* instance = (pxa_instance_t*)runtime_instance;
    return runtime->backend.foreground(runtime->backend.context, instance->backend_instance);
}

static pxsys_status_t background(void* context, void* runtime_instance) {
    pxsys_pxa_runtime_t* runtime = (pxsys_pxa_runtime_t*)context;
    pxa_instance_t* instance = (pxa_instance_t*)runtime_instance;
    return runtime->backend.background(runtime->backend.context, instance->backend_instance);
}

static pxsys_status_t deliver(void* context, void* runtime_instance,
                              const pxsys_message_t* message) {
    pxsys_pxa_runtime_t* runtime = (pxsys_pxa_runtime_t*)context;
    pxa_instance_t* instance = (pxa_instance_t*)runtime_instance;
    return runtime->backend.deliver(runtime->backend.context, instance->backend_instance, message);
}

static pxsys_back_result_t back(void* context, void* runtime_instance) {
    pxsys_pxa_runtime_t* runtime = (pxsys_pxa_runtime_t*)context;
    pxa_instance_t* instance = (pxa_instance_t*)runtime_instance;
    return runtime->backend.back(runtime->backend.context, instance->backend_instance);
}

static void stop(void* context, void* runtime_instance, pxsys_stop_reason_t reason) {
    pxsys_pxa_runtime_t* runtime = (pxsys_pxa_runtime_t*)context;
    pxa_instance_t* instance = (pxa_instance_t*)runtime_instance;
    runtime->backend.stop(runtime->backend.context, instance->backend_instance, reason);
}

static pxsys_status_t request_stop(void* context, void* runtime_instance,
                                   pxsys_stop_reason_t reason) {
    pxsys_pxa_runtime_t* runtime = (pxsys_pxa_runtime_t*)context;
    pxa_instance_t* instance = (pxa_instance_t*)runtime_instance;
    if (runtime->backend.request_stop != NULL)
        return runtime->backend.request_stop(runtime->backend.context, instance->backend_instance,
                                             reason);
    runtime->backend.stop(runtime->backend.context, instance->backend_instance, reason);
    return PXSYS_STATUS_OK;
}

static void destroy(void* context, void* runtime_instance) {
    pxsys_pxa_runtime_t* runtime = (pxsys_pxa_runtime_t*)context;
    pxa_instance_t* instance = (pxa_instance_t*)runtime_instance;
    if (!runtime_valid(runtime) || instance == NULL)
        return;
    runtime->backend.destroy(runtime->backend.context, instance->backend_instance);
    if (runtime->active_instances != 0)
        runtime->active_instances--;
    runtime->allocator.release(runtime->allocator.context, instance);
}

pxsys_status_t pxsys_pxa_runtime_provider(pxsys_pxa_runtime_t* runtime,
                                          pxsys_runtime_provider_t* provider) {
    if (!runtime_valid(runtime) || provider == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    memset(provider, 0, sizeof(*provider));
    provider->struct_size = sizeof(*provider);
    provider->runtime_id = pxsys_string_from_cstr(runtime->runtime_id);
    provider->version = runtime->version;
    provider->features = runtime->features;
    provider->context = runtime;
    provider->instantiate = instantiate;
    provider->bound = bound;
    provider->start = start;
    provider->foreground = foreground;
    provider->background = background;
    provider->event = deliver;
    provider->back = back;
    provider->stop = stop;
    provider->destroy = destroy;
    provider->request_stop = request_stop;
    return PXSYS_STATUS_OK;
}
