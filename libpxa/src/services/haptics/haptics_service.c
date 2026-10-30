#include "pxa/haptics.h"
#include <string.h>
#define HAPTICS_MAGIC UINT32_C(0x48505431)
struct pxa_haptics_service {
    uint32_t magic;
    pxa_runtime_t *runtime;
    pxa_permission_service_t *permissions;
    pxa_haptics_backend_t backend;
    pxa_component_t foreground, owner;
    pxa_handle64_t handle;
    uint8_t registered;
};
static int valid(const pxa_haptics_service_t *s) { return s && s->magic == HAPTICS_MAGIC; }
size_t pxa_haptics_service_workspace_size(void) { return sizeof(pxa_haptics_service_t); }
pxa_status_t pxa_haptics_service_init(void *workspace, size_t bytes, pxa_runtime_t *runtime,
    pxa_permission_service_t *permissions, const pxa_haptics_backend_t *backend,
    pxa_haptics_service_t **output) {
    pxa_haptics_service_t *s = workspace;
    if (!output) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (!s || bytes < sizeof(*s) || (uintptr_t)s % sizeof(uint64_t) || !runtime || !permissions)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (backend && (backend->struct_size < sizeof(*backend) ||
        (backend->flags & ~(PXA_HAPTICS_CAP_VIBRATE | PXA_HAPTICS_CAP_AMPLITUDE)) ||
        (!backend->flags && backend->max_duration_ms) ||
        ((backend->flags & PXA_HAPTICS_CAP_AMPLITUDE) && !(backend->flags & PXA_HAPTICS_CAP_VIBRATE)) ||
        ((backend->flags & PXA_HAPTICS_CAP_VIBRATE) &&
            (!backend->play || !backend->stop || !backend->max_duration_ms)))) return PXA_STATUS_INVALID_ARGUMENT;
    memset(s, 0, sizeof(*s));
    s->magic = HAPTICS_MAGIC; s->runtime = runtime; s->permissions = permissions;
    if (backend) s->backend = *backend;
    *output = s;
    return PXA_STATUS_OK;
}
static void close_actuator(void *context) {
    pxa_haptics_service_t *s = context;
    if (!valid(s)) return;
    if (s->handle && s->backend.stop) s->backend.stop(s->backend.context);
    s->handle = 0; s->owner = PXA_COMPONENT_INVALID;
}
static int32_t no_io(void *context, uint32_t operation, uint8_t *data, size_t size) {
    (void)context; (void)operation; (void)data; (void)size;
    return PXA_STATUS_UNSUPPORTED;
}
static const pxa_resource_ops_t resource_ops = {sizeof(pxa_resource_ops_t), no_io};
static pxa_status_t open_actuator(pxa_haptics_service_t *s, pxa_component_t component, pxa_bytes_t payload) {
    static const uint8_t permission[] = PXA_HAPTICS_PERMISSION, scope[] = PXA_HAPTICS_SCOPE;
    pxa_authority_t authority;
    pxa_status_t status;
    pxa_resource_t resource = {s, &resource_ops, close_actuator};
    if (payload.size != 8 || !(pxa_read_u64(payload.data) >> 32)) return PXA_STATUS_INVALID_ARGUMENT;
    if (!(s->backend.flags & PXA_HAPTICS_CAP_VIBRATE)) return PXA_STATUS_UNSUPPORTED;
    if (!s->foreground || component != s->foreground) return PXA_STATUS_DENIED;
    status = pxa_permission_resolve64(s->permissions, component, pxa_read_u64(payload.data),
        (pxa_bytes_t){permission, sizeof(permission)-1}, (pxa_bytes_t){scope, sizeof(scope)-1}, &authority);
    if (status != PXA_STATUS_OK) return status;
    if (s->handle) return PXA_STATUS_BUSY;
    status = pxa_handle_open64(s->runtime, component, PXA_RESOURCE_HAPTICS, authority, &resource, &s->handle);
    if (status == PXA_STATUS_OK) s->owner = component;
    return status;
}
static pxa_status_t control_actuator(pxa_haptics_service_t *s, pxa_component_t component,
    const pxa_message_view_t *message) {
    pxa_resource_t resource;
    pxa_status_t status;
    const size_t bytes = message->opcode == PXA_HAPTICS_STOP ? 8 : 16;
    if (message->payload.size != bytes) return PXA_STATUS_INVALID_ARGUMENT;
    const pxa_handle64_t handle = pxa_read_u64(message->payload.data);
    if (!(handle >> 32)) return PXA_STATUS_INVALID_ARGUMENT;
    status = pxa_handle_get64(s->runtime, component, handle, PXA_RESOURCE_HAPTICS, &resource);
    if (status != PXA_STATUS_OK) return status;
    if (resource.context != s || handle != s->handle || component != s->owner) return PXA_STATUS_DENIED;
    if (message->opcode == PXA_HAPTICS_STOP) { s->backend.stop(s->backend.context); return PXA_STATUS_OK; }
    if (component != s->foreground) return PXA_STATUS_DENIED;
    const uint32_t duration = pxa_read_u32(message->payload.data + 8);
    const uint16_t amplitude = pxa_read_u16(message->payload.data + 12);
    if (!duration || duration > s->backend.max_duration_ms || !amplitude || amplitude > 1000 ||
        pxa_read_u16(message->payload.data + 14)) return PXA_STATUS_INVALID_ARGUMENT;
    if (amplitude != 1000 && !(s->backend.flags & PXA_HAPTICS_CAP_AMPLITUDE)) return PXA_STATUS_UNSUPPORTED;
    status = s->backend.play(s->backend.context, duration, amplitude);
    if (!pxa_status_is_known(status)) status = PXA_STATUS_IO_ERROR;
    if (status != PXA_STATUS_OK) s->backend.stop(s->backend.context);
    return status;
}
static pxa_status_t control(void *context, pxa_runtime_t *runtime, pxa_component_t component,
    const pxa_message_view_t *message) {
    pxa_haptics_service_t *s = context;
    uint8_t result[8]; size_t result_bytes = 0;
    pxa_status_t status, completion;
    if (!valid(s) || !message || !message->request_token) return PXA_STATUS_INVALID_ARGUMENT;
    if (message->opcode < PXA_HAPTICS_CAPABILITIES || message->opcode > PXA_HAPTICS_STOP)
        return PXA_STATUS_UNSUPPORTED;
    uint16_t profile;
    status = pxa_component_wire_profile(runtime, component, &profile);
    if (status != PXA_STATUS_OK || profile != 1) return PXA_STATUS_UNSUPPORTED;
    status = pxa_request_begin_reserved(runtime, component, message->request_token,
        PXA_HAPTICS_SERVICE_ID, message->opcode, 0, sizeof(result));
    if (status != PXA_STATUS_OK) return status;
    status = pxa_request_commit(runtime, component, message->request_token);
    if (status != PXA_STATUS_OK) {
        (void)pxa_request_cancel(runtime, component, message->request_token);
        return status;
    }
    if (message->opcode == PXA_HAPTICS_CAPABILITIES) {
        status = message->payload.size ? PXA_STATUS_INVALID_ARGUMENT : PXA_STATUS_OK;
        pxa_write_u32(result, s->backend.flags); pxa_write_u32(result + 4, s->backend.max_duration_ms);
        result_bytes = sizeof(result);
    } else if (message->opcode == PXA_HAPTICS_OPEN) {
        status = open_actuator(s, component, message->payload);
        if (status == PXA_STATUS_OK) { pxa_write_u64(result, s->handle); result_bytes = 8; }
    } else status = control_actuator(s, component, message);
    completion = pxa_request_complete(runtime, component, message->request_token, status,
        status == PXA_STATUS_OK ? result : NULL, status == PXA_STATUS_OK ? result_bytes : 0);
    if (completion != PXA_STATUS_OK) {
        if (status == PXA_STATUS_OK && message->opcode == PXA_HAPTICS_OPEN)
            (void)pxa_handle_close64(runtime, component, s->handle);
        if (status == PXA_STATUS_OK && message->opcode == PXA_HAPTICS_VIBRATE) s->backend.stop(s->backend.context);
        (void)pxa_request_cancel(runtime, component, message->request_token);
    }
    return PXA_STATUS_OK;
}
pxa_status_t pxa_haptics_service_register(pxa_haptics_service_t *s) {
    pxa_service_ops_t ops = {0}; pxa_status_t status;
    if (!valid(s)) return PXA_STATUS_INVALID_ARGUMENT;
    if (s->registered) return PXA_STATUS_BAD_STATE;
    ops.struct_size = sizeof(ops); ops.service_id = PXA_HAPTICS_SERVICE_ID;
    ops.major = PXA_HAPTICS_SERVICE_MAJOR; ops.minor = PXA_HAPTICS_SERVICE_MINOR;
    ops.context = s; ops.control = control;
    status = pxa_service_register(s->runtime, &ops);
    if (status == PXA_STATUS_OK) s->registered = 1;
    return status;
}
void pxa_haptics_service_set_foreground(pxa_haptics_service_t *s, pxa_component_t component) {
    if (!valid(s)) return;
    if (s->handle && s->owner != component) (void)pxa_handle_close64(s->runtime, s->owner, s->handle);
    s->foreground = component;
}
void pxa_haptics_service_deinit(pxa_haptics_service_t *s) {
    if (!valid(s)) return;
    if (s->handle) (void)pxa_handle_close64(s->runtime, s->owner, s->handle);
}
