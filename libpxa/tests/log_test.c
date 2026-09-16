#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pxa/log.h"
#include "pxa/service.h"
#include "pxa/wire.h"

typedef struct {
    unsigned writes;
    pxa_component_t component;
    pxa_log_level_t level;
    uint8_t app_id[64];
    size_t app_id_size;
    uint8_t message[PXA_LOG_MAX_MESSAGE_BYTES];
    size_t message_size;
} backend_t;

static pxa_status_t backend_write(
    void *context, pxa_component_t component, pxa_bytes_t app_id,
    pxa_log_level_t level, pxa_bytes_t message) {
    backend_t *backend = context;
    assert(app_id.size <= sizeof(backend->app_id));
    assert(message.size <= sizeof(backend->message));
    ++backend->writes;
    backend->component = component;
    backend->level = level;
    backend->app_id_size = app_id.size;
    backend->message_size = message.size;
    memcpy(backend->app_id, app_id.data, app_id.size);
    memcpy(backend->message, message.data, message.size);
    return PXA_STATUS_OK;
}

static size_t make_log(uint8_t *packet, size_t capacity, uint16_t opcode,
                       uint32_t request_id, uint8_t level,
                       const uint8_t *message, size_t message_size) {
    uint8_t payload[1u + PXA_LOG_MAX_MESSAGE_BYTES];
    pxa_writer_t writer;
    assert(message_size <= PXA_LOG_MAX_MESSAGE_BYTES);
    payload[0] = level;
    memcpy(payload + 1, message, message_size);
    pxa_writer_init(&writer, packet, capacity);
    assert(pxa_writer_message(&writer, PXA_LOG_SERVICE_ID, opcode, request_id,
                              payload, message_size + 1u) == PXA_STATUS_OK);
    return writer.size;
}

int main(void) {
    static uint8_t app_id[] = "log-test";
    static const uint8_t message[] = "frame complete";
    static const uint8_t injected[] = "bad\nline";
    static const uint8_t invalid_utf8[] = {UINT8_C(0xc0), UINT8_C(0xaf)};
    pxa_runtime_limits_t limits;
    pxa_runtime_t *runtime = NULL;
    pxa_component_t component = PXA_COMPONENT_INVALID;
    pxa_log_config_t config = {0};
    pxa_log_service_t *service = NULL;
    backend_t backend = {0};
    void *runtime_workspace;
    void *service_workspace;
    uint8_t packet[320];

    pxa_runtime_limits_init(&limits);
    limits.max_components = 1;
    limits.max_services = 1;
    runtime_workspace = malloc(pxa_runtime_workspace_size(&limits));
    assert(runtime_workspace != NULL);
    assert(pxa_runtime_init(runtime_workspace,
                            pxa_runtime_workspace_size(&limits), &limits,
                            &runtime) == PXA_STATUS_OK);

    config.struct_size = sizeof(config);
    config.context = &backend;
    config.write = backend_write;
    config.app_id =
        (pxa_bytes_t){app_id, sizeof(app_id) - 1u};
    config.max_message_bytes = PXA_LOG_MAX_MESSAGE_BYTES;
    service_workspace = malloc(pxa_log_service_workspace_size(&config));
    assert(service_workspace != NULL);
    assert(pxa_log_service_init(
               service_workspace, pxa_log_service_workspace_size(&config),
               runtime, &config, &service) == PXA_STATUS_OK);
    assert(pxa_log_service_register(service) == PXA_STATUS_OK);
    app_id[0] = 'X';

    assert(pxa_component_create(runtime, 1, &component) == PXA_STATUS_OK);
    assert(pxa_component_begin_start(runtime, component) == PXA_STATUS_OK);
    assert(pxa_component_finish_start(runtime, component, PXA_STATUS_OK) ==
           PXA_STATUS_OK);
    assert(pxa_component_begin_event(runtime, component) == PXA_STATUS_OK);
    assert(pxa_runtime_control(
               runtime, component, packet,
               make_log(packet, sizeof(packet), PXA_LOG_WRITE, 0,
                        PXA_LOG_LEVEL_INFO, message, sizeof(message) - 1u)) ==
           PXA_STATUS_OK);
    assert(backend.writes == 1 && backend.component == component &&
           backend.level == PXA_LOG_LEVEL_INFO &&
           backend.app_id_size == sizeof(app_id) - 1u &&
           memcmp(backend.app_id, "log-test", sizeof(app_id) - 1u) == 0 &&
           backend.message_size == sizeof(message) - 1u &&
           memcmp(backend.message, message, sizeof(message) - 1u) == 0);

    assert(pxa_runtime_control(
               runtime, component, packet,
               make_log(packet, sizeof(packet), PXA_LOG_WRITE, 0,
                        PXA_LOG_LEVEL_WARN, injected,
                        sizeof(injected) - 1u)) ==
           PXA_STATUS_INVALID_ARGUMENT);
    assert(pxa_runtime_control(
               runtime, component, packet,
               make_log(packet, sizeof(packet), PXA_LOG_WRITE, 0,
                        PXA_LOG_LEVEL_INFO, invalid_utf8,
                        sizeof(invalid_utf8))) == PXA_STATUS_INVALID_ARGUMENT);
    assert(pxa_runtime_control(
               runtime, component, packet,
               make_log(packet, sizeof(packet), PXA_LOG_WRITE, 7,
                        PXA_LOG_LEVEL_INFO, message,
                        sizeof(message) - 1u)) ==
           PXA_STATUS_INVALID_ARGUMENT);
    assert(pxa_runtime_control(
               runtime, component, packet,
               make_log(packet, sizeof(packet), UINT16_C(99), 0,
                        PXA_LOG_LEVEL_INFO, message,
                        sizeof(message) - 1u)) == PXA_STATUS_UNSUPPORTED);
    assert(pxa_runtime_control(
               runtime, component, packet,
               make_log(packet, sizeof(packet), PXA_LOG_WRITE, 0, 9,
                        message, sizeof(message) - 1u)) ==
           PXA_STATUS_INVALID_ARGUMENT);
    assert(backend.writes == 1);
    assert(pxa_component_finish_event(runtime, component, 1) == PXA_STATUS_OK);

    pxa_runtime_deinit(runtime);
    free(service_workspace);
    free(runtime_workspace);
    return 0;
}
