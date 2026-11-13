#include "pxa/permission.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static unsigned closes;
static void closed(void *context) { (void)context; ++closes; }
static pxa_bytes_t text(const char *s) { return (pxa_bytes_t){(const uint8_t *)s, strlen(s)}; }
static pxa_status_t load(void *context, pxa_bytes_t id, pxa_bytes_t name,
    pxa_bytes_t scope, pxa_permission_decision_t *decision) {
    (void)context; (void)id; (void)name; (void)scope; (void)decision;
    assert(!"disabled builds must not read permission decisions");
    return PXA_STATUS_INTERNAL;
}
static pxa_status_t save(void *context, pxa_bytes_t id, pxa_bytes_t name,
    pxa_bytes_t scope, pxa_permission_decision_t decision) {
    (void)context; (void)id; (void)name; (void)scope; (void)decision;
    assert(!"disabled builds must not persist permission decisions");
    return PXA_STATUS_INTERNAL;
}
static pxa_status_t prompt(void *context, pxa_component_t component,
    pxa_request_token_t token, pxa_bytes_t id, pxa_bytes_t name, pxa_bytes_t scope) {
    (void)context; (void)component; (void)token; (void)id; (void)name; (void)scope;
    assert(!"disabled builds must not prompt");
    return PXA_STATUS_INTERNAL;
}

static uint64_t request(pxa_runtime_t *runtime, pxa_component_t component,
    uint16_t opcode, uint64_t token, const uint8_t *payload, size_t size, uint16_t wire) {
    pxa_message_view_t message = {0}, completion;
    uint8_t bytes[96]; size_t result_size;
    message.service = PXA_PERMISSION_SERVICE_ID;
    message.opcode = opcode; message.request_token = token;
    message.payload = (pxa_bytes_t){payload, size};
    assert(!pxa_component_begin_event(runtime, component));
    assert(!pxa_runtime_control_view(runtime, component, &message));
    assert(!pxa_component_finish_event(runtime, component, 0));
    assert(!pxa_event_pop(runtime, component, bytes, sizeof(bytes), &result_size));
    assert(!pxa_message_decode(bytes, result_size, PXA_MAX_CONTROL_MESSAGE, &completion));
    assert(completion.payload.size == (opcode == PXA_PERMISSION_CHECK ? 5u : wire ? 12u : 8u));
    assert(pxa_read_u32(completion.payload.data) == 0);
    return opcode == PXA_PERMISSION_CHECK ? completion.payload.data[4] : wire
        ? pxa_read_u64(completion.payload.data + 4) : pxa_read_u32(completion.payload.data + 4);
}

int main(void) {
    assert(!PXA_ENABLE_PERMISSIONS);
    pxa_runtime_limits_t limits; pxa_runtime_limits_init(&limits);
    size_t runtime_bytes = pxa_runtime_workspace_size(&limits);
    void *memory = malloc(runtime_bytes); pxa_runtime_t *runtime;
    assert(memory && !pxa_runtime_init(memory, runtime_bytes, &limits, &runtime));
    pxa_permission_config_t config = {0}; config.struct_size = sizeof(config);
    pxa_permission_declaration_t required = {text("required.permission"), text("required-scope"), 1};
    config.declarations = &required; config.declaration_count = 1;
    config.max_authorities = 1;
    config.store = (pxa_permission_store_t){sizeof(config.store), NULL, load, save};
    config.prompt = prompt; config.max_pending_prompts = 1;
    size_t permission_bytes = pxa_permission_service_workspace_size(&config);
    void *workspace = malloc(permission_bytes); pxa_permission_service_t *service;
    assert(workspace && !pxa_permission_service_init(workspace, permission_bytes, runtime, &config, &service));
    assert(!pxa_permission_policy_load(service) && pxa_permission_can_activate(service));
    assert(pxa_permission_get(service, required.name, required.scope) == PXA_PERMISSION_ALLOW);
    assert(pxa_permission_get(service, text("undeclared.permission"), text("arbitrary-scope")) == PXA_PERMISSION_ALLOW);
    assert(!pxa_permission_set(service, text("undeclared.permission"), text("arbitrary-scope"), PXA_PERMISSION_DENY));
    assert(pxa_permission_get(service, text("undeclared.permission"), text("arbitrary-scope")) == PXA_PERMISSION_ALLOW);
    size_t affected = 99;
    assert(!pxa_permission_revoke(service, text("undeclared.permission"), text("arbitrary-scope"), NULL, 0, &affected) && !affected);
    assert(!pxa_permission_service_register(service));
    uint8_t payload[96]; pxa_writer_t writer; pxa_writer_init(&writer, payload, sizeof(payload));
    assert(!pxa_writer_record(&writer, 1, "undeclared.permission", 21));
    assert(!pxa_writer_record(&writer, 2, "arbitrary-scope", 15));
    for (uint16_t wire = 0; wire <= 1; ++wire) {
        pxa_component_t component, other;
        assert(!pxa_component_create(runtime, 1 + 2 * wire, &component));
        assert(!pxa_component_begin_start(runtime, component));
        assert(!pxa_component_set_wire_profile(runtime, component, wire));
        assert(!pxa_component_finish_start(runtime, component, PXA_STATUS_OK));
        assert(!pxa_component_create(runtime, 2 + 2 * wire, &other));
        assert(!pxa_component_begin_start(runtime, other));
        assert(!pxa_component_finish_start(runtime, other, PXA_STATUS_OK));
        assert(request(runtime, component, PXA_PERMISSION_CHECK, 1, payload, writer.size, wire) == PXA_PERMISSION_ALLOW);
        for (uint64_t token = 2; token < 100; ++token) {
            uint64_t handle = request(runtime, component, PXA_PERMISSION_ACQUIRE, token, payload, writer.size, wire);
            pxa_authority_t authority = 0; pxa_resource_t resource = {0};
            pxa_status_t status = wire ? pxa_permission_resolve64(service, component, handle,
                text("different.name"), text("different.scope"), &authority) :
                pxa_permission_resolve(service, component, (pxa_handle_t)handle,
                    text("different.name"), text("different.scope"), &authority);
            assert(status == PXA_STATUS_OK && authority != 0);
            assert((wire ? pxa_permission_resolve64(service, other, handle, text("x"), text("y"), &authority) :
                pxa_permission_resolve(service, other, (pxa_handle_t)handle, text("x"), text("y"), &authority)) != PXA_STATUS_OK);
            status = wire ? pxa_permission_resolve64(service, component, handle, text("x"), text("y"), &authority) :
                pxa_permission_resolve(service, component, (pxa_handle_t)handle, text("x"), text("y"), &authority);
            assert(status == PXA_STATUS_OK);
            resource.close = closed;
            pxa_handle64_t child = 0;
            assert(!pxa_handle_open64(runtime, component, PXA_RESOURCE_FILE, authority, &resource, &child));
            unsigned before = closes;
            assert(!(wire ? pxa_handle_close64(runtime, component, handle) : pxa_handle_close(runtime, component, (pxa_handle_t)handle)));
            assert(closes == before + 1 && pxa_handle_get64(runtime, component, child, PXA_RESOURCE_FILE, &resource) != PXA_STATUS_OK);
        }
        assert(!pxa_component_abort(runtime, component, PXA_STOP_NORMAL));
        assert(!pxa_component_abort(runtime, other, PXA_STOP_NORMAL));
    }
    pxa_runtime_deinit(runtime); free(workspace); free(memory);
    return 0;
}
