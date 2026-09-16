#include "pxa/log.h"

#include "common/bytes_internal.h"
#include "common/checked_math.h"
#include "pxa/service.h"

#include <string.h>

#define PXA_LOG_MAGIC UINT32_C(0x50584c47)
#define PXA_LOG_MAX_APP_ID_BYTES ((size_t)64)

struct pxa_log_service {
    uint32_t magic;
    pxa_runtime_t *runtime;
    void *context;
    pxa_log_write_fn write;
    pxa_bytes_t app_id;
    uint16_t max_message_bytes;
    uint8_t registered;
};

static int config_valid(const pxa_log_config_t *config) {
    return config != NULL && config->struct_size >= sizeof(*config) &&
           config->write != NULL && config->app_id.data != NULL &&
           config->app_id.size != 0 &&
           config->app_id.size <= PXA_LOG_MAX_APP_ID_BYTES &&
           config->max_message_bytes != 0 &&
           config->max_message_bytes <= PXA_LOG_MAX_MESSAGE_BYTES;
}

size_t pxa_log_service_workspace_size(const pxa_log_config_t *config) {
    size_t size = sizeof(pxa_log_service_t);
    if (!config_valid(config) ||
        size > SIZE_MAX - (PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u)) {
        return 0;
    }
    size += PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    if (config->app_id.size > SIZE_MAX - size) return 0;
    size += config->app_id.size;
    return size;
}

pxa_status_t pxa_log_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_log_config_t *config, pxa_log_service_t **output) {
    pxa_log_service_t *service;
    uint8_t *app_id;
    size_t required;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    required = pxa_log_service_workspace_size(config);
    if (workspace == NULL || runtime == NULL || required == 0 ||
        workspace_size < required) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    service = (pxa_log_service_t *)pxa_internal_align_pointer(
        (uintptr_t)workspace, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    memset(service, 0, sizeof(*service));
    app_id = (uint8_t *)(service + 1);
    memcpy(app_id, config->app_id.data, config->app_id.size);
    service->runtime = runtime;
    service->context = config->context;
    service->write = config->write;
    service->app_id = (pxa_bytes_t){app_id, config->app_id.size};
    service->max_message_bytes = config->max_message_bytes;
    service->magic = PXA_LOG_MAGIC;
    *output = service;
    return PXA_STATUS_OK;
}

static int service_valid(const pxa_log_service_t *service) {
    return service != NULL && service->magic == PXA_LOG_MAGIC;
}

static int message_valid(pxa_bytes_t message) {
    size_t index;
    if (!pxa_utf8_validate(message.data, message.size, 0)) return 0;
    for (index = 0; index < message.size; ++index) {
        const uint8_t value = message.data[index];
        if ((value < UINT8_C(0x20) && value != UINT8_C(0x09)) ||
            value == UINT8_C(0x7f)) {
            return 0;
        }
    }
    return 1;
}

static pxa_status_t log_control(void *context, pxa_runtime_t *runtime,
                                pxa_component_t component,
                                const pxa_message_view_t *message) {
    pxa_log_service_t *service = (pxa_log_service_t *)context;
    pxa_bytes_t text;
    pxa_log_level_t level;
    (void)runtime;
    if (!service_valid(service) || message == NULL ||
        message->request_id != 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (message->opcode != PXA_LOG_WRITE) return PXA_STATUS_UNSUPPORTED;
    if (message->payload.data == NULL || message->payload.size < 2u ||
        message->payload.size > (size_t)service->max_message_bytes + 1u) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    level = message->payload.data[0];
    text = (pxa_bytes_t){message->payload.data + 1,
                         message->payload.size - 1u};
    if (level > PXA_LOG_LEVEL_ERROR || !message_valid(text)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    return service->write(service->context, component, service->app_id, level,
                          text);
}

pxa_status_t pxa_log_service_register(pxa_log_service_t *service) {
    pxa_service_ops_t operations;
    pxa_status_t status;
    if (!service_valid(service)) return PXA_STATUS_INVALID_ARGUMENT;
    if (service->registered) return PXA_STATUS_BAD_STATE;
    memset(&operations, 0, sizeof(operations));
    operations.struct_size = sizeof(operations);
    operations.service_id = PXA_LOG_SERVICE_ID;
    operations.major = PXA_LOG_SERVICE_MAJOR;
    operations.minor = PXA_LOG_SERVICE_MINOR;
    operations.context = service;
    operations.control = log_control;
    status = pxa_service_register(service->runtime, &operations);
    if (status == PXA_STATUS_OK) service->registered = 1;
    return status;
}
