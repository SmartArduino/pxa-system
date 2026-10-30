// SPDX-License-Identifier: Apache-2.0
#include <pxa/haptics.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static pxa_runtime_t *runtime;
static pxa_component_t component, other;
static pxa_permission_service_t *permissions;
static pxa_request_token_t token = 1;
static uint8_t response[2048];
static pxa_message_view_t reply;
static pxa_status_t load(void *ctx, pxa_bytes_t app, pxa_bytes_t name,
                         pxa_bytes_t scope, pxa_permission_decision_t *decision) {
    (void)ctx; (void)app; (void)name; (void)scope;
    *decision = PXA_PERMISSION_ALLOW; return PXA_STATUS_OK;
}
static pxa_status_t save(void *ctx, pxa_bytes_t app, pxa_bytes_t name,
                         pxa_bytes_t scope, pxa_permission_decision_t decision) {
    (void)ctx; (void)app; (void)name; (void)scope; (void)decision;
    return PXA_STATUS_OK;
}
static pxa_component_t create(uint64_t id) {
    pxa_component_t c;
    assert(pxa_component_create(runtime, id, &c) == PXA_STATUS_OK);
    assert(pxa_component_begin_start(runtime, c) == PXA_STATUS_OK);
    assert(pxa_component_set_wire_profile(runtime, c, 1) == PXA_STATUS_OK);
    assert(pxa_component_finish_start(runtime, c, PXA_STATUS_OK) == PXA_STATUS_OK);
    return c;
}
static void pop(pxa_component_t c) {
    size_t size;
    assert(pxa_event_pop(runtime, c, response, sizeof(response), &size) == PXA_STATUS_OK);
    assert(pxa_message_decode(response, size, sizeof(response), &reply) == PXA_STATUS_OK);
}
static pxa_status_t dispatch(pxa_component_t c, uint16_t sid, uint16_t op,
                             const void *data, size_t size) {
    pxa_status_t status;
    pxa_message_view_t message = {sid, op, token++, {data, size}};
    assert(pxa_component_begin_event(runtime, c) == PXA_STATUS_OK);
    status = pxa_runtime_control_view(runtime, c, &message);
    assert(pxa_component_finish_event(runtime, c, 1) == PXA_STATUS_OK);
    return status;
}
static pxa_status_t call(pxa_component_t c, uint16_t sid, uint16_t op,
                         const void *data, size_t size) {
    assert(dispatch(c, sid, op, data, size) == PXA_STATUS_OK);
    pop(c);
    assert(reply.service == sid && reply.opcode == op && reply.request_token == token-1);
    assert(reply.payload.size >= 4);
    return (pxa_status_t)pxa_read_u32(reply.payload.data);
}
static pxa_handle64_t acquire_named(pxa_component_t c, const char *name, const char *scope) {
    uint8_t packet[128];
    pxa_writer_t writer;
    pxa_writer_init(&writer, packet, sizeof(packet));
    assert(pxa_writer_record(&writer, 1, name, strlen(name)) == PXA_STATUS_OK);
    assert(pxa_writer_record(&writer, 2, scope, strlen(scope)) == PXA_STATUS_OK);
    assert(call(c, 11, PXA_PERMISSION_ACQUIRE, packet, writer.size) == PXA_STATUS_OK);
    assert(reply.payload.size == 12);
    return pxa_read_u64(reply.payload.data + 4);
}
static unsigned plays, stops;
static pxa_status_t backend_status;
static pxa_status_t play(void *ctx, uint32_t duration, uint16_t amplitude) {
    (void)ctx; assert(duration == 100 && amplitude == 1000); ++plays; return backend_status;
}
static void stop(void *ctx) { (void)ctx; ++stops; }
int main(void) {
    pxa_runtime_limits_t limits; pxa_permission_config_t config = {0}; size_t size;
    pxa_haptics_service_t *haptics;
    const pxa_permission_declaration_t declarations[] = {
        {{(const uint8_t *)PXA_HAPTICS_PERMISSION, sizeof(PXA_HAPTICS_PERMISSION)-1},
         {(const uint8_t *)PXA_HAPTICS_SCOPE, sizeof(PXA_HAPTICS_SCOPE)-1}, 0}};
    const pxa_haptics_backend_t motor = {sizeof(motor), NULL, 1, 1000, play, stop};
    pxa_runtime_limits_init(&limits); limits.max_services = 4; limits.max_components = 2;
    limits.max_requests = 8; limits.max_requests_per_component = 4;
    limits.max_handles = 12; limits.max_events = 10; limits.mailbox_capacity = 6;
    limits.event_block_count = 128; limits.event_block_size = 64;
    size = pxa_runtime_workspace_size(&limits); void *workspace = malloc(size); assert(workspace);
    assert(pxa_runtime_init(workspace, size, &limits, &runtime) == PXA_STATUS_OK);
    component = create(1); other = create(2);
    config.struct_size = sizeof(config); config.app_identity = (pxa_bytes_t){(const uint8_t *)"test", 4};
    config.declarations = declarations; config.declaration_count = 1; config.max_authorities = 1;
    config.store = (pxa_permission_store_t){sizeof(config.store), NULL, load, save};
    size = pxa_permission_service_workspace_size(&config); void *pw = malloc(size); assert(pw);
    assert(pxa_permission_service_init(pw, size, runtime, &config, &permissions) == PXA_STATUS_OK);
    assert(pxa_permission_policy_load(permissions) == PXA_STATUS_OK);
    assert(pxa_permission_service_register(permissions) == PXA_STATUS_OK);
    size = pxa_haptics_service_workspace_size(); void *hw = malloc(size); assert(hw);
    // Unsupported hardware is a standard, truthful capability result.
    assert(pxa_haptics_service_init(hw, size, runtime, permissions, NULL, &haptics) == PXA_STATUS_OK);
    assert(pxa_haptics_service_register(haptics) == PXA_STATUS_OK);
    assert(call(component, 23, 1, NULL, 0) == PXA_STATUS_OK && pxa_read_u32(reply.payload.data+4) == 0);
    uint8_t packet[16] = {0}; pxa_write_u64(packet, UINT64_C(0x100000001));
    assert(call(component, 23, 2, packet, 8) == PXA_STATUS_UNSUPPORTED);
    // Reinitialize in place before opening any resource; runtime retains this context.
    assert(pxa_haptics_service_init(hw, size, runtime, permissions, &motor, &haptics) == PXA_STATUS_OK);
    assert(call(component, 23, 1, NULL, 0) == PXA_STATUS_OK);
    assert(pxa_read_u32(reply.payload.data+4) == 1 && pxa_read_u32(reply.payload.data+8) == 1000);
    const pxa_handle64_t permission = acquire_named(component, PXA_HAPTICS_PERMISSION, PXA_HAPTICS_SCOPE);
    pxa_write_u64(packet, permission);
    assert(call(component, 23, 2, packet, 8) == PXA_STATUS_DENIED);
    pxa_haptics_service_set_foreground(haptics, component);
    assert(call(component, 23, 2, packet, 8) == PXA_STATUS_OK);
    const pxa_handle64_t actuator = pxa_read_u64(reply.payload.data+4);
    assert(call(component, 23, 2, packet, 8) == PXA_STATUS_BUSY);
    pxa_write_u64(packet, actuator); pxa_write_u32(packet+8, 100); pxa_write_u16(packet+12, 500);
    assert(call(component, 23, 3, packet, 16) == PXA_STATUS_UNSUPPORTED && !plays);
    pxa_write_u16(packet+12, 1000);
    assert(call(other, 23, 3, packet, 16) != PXA_STATUS_OK && !plays);
    assert(call(component, 23, 3, packet, 16) == PXA_STATUS_OK && plays == 1);
    // Refuse new side effects if a reliable completion cannot be reserved.
    for (unsigned i=0; i<4; ++i)
        assert(pxa_request_begin_reserved(runtime, component, 1001+i, 23, 3, 0, 8) == PXA_STATUS_OK);
    assert(dispatch(component, 23, 3, packet, 16) == PXA_STATUS_RESOURCE_LIMIT && plays == 1);
    for (unsigned i=0; i<4; ++i) assert(pxa_request_cancel(runtime, component, 1001+i) == PXA_STATUS_OK);
    for (unsigned i=0; i<4; ++i) pop(component);
    pxa_write_u32(packet+8, 1001);
    assert(call(component, 23, 3, packet, 16) == PXA_STATUS_INVALID_ARGUMENT && plays == 1);
    pxa_write_u32(packet+8, 100); backend_status = (pxa_status_t)-12345;
    assert(call(component, 23, 3, packet, 16) == PXA_STATUS_IO_ERROR && plays == 2 && stops == 1);
    backend_status = PXA_STATUS_OK;
    pxa_haptics_service_set_foreground(haptics, other);
    assert(stops == 2 && call(component, 23, 4, packet, 8) == PXA_STATUS_NOT_FOUND);
    pxa_haptics_service_set_foreground(haptics, component); pxa_write_u64(packet, permission);
    assert(call(component, 23, 2, packet, 8) == PXA_STATUS_OK);
    assert(pxa_handle_close64(runtime, component, permission) == PXA_STATUS_OK && stops == 3);
    pxa_haptics_service_deinit(haptics); pxa_runtime_deinit(runtime);
    free(hw); free(pw); free(workspace);
    puts("Haptics standard: capabilities, authority, ownership, bounds, backend errors and revocation passed");
}
