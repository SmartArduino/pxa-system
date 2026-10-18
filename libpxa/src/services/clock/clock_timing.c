#include "pxa/clock.h"
#include "pxa/wire.h"

int pxa_clock_period_supported(const pxa_clock_limits_t *limits,
                                uint32_t period_us) {
    if (limits == NULL || limits->min_period_us == 0 ||
        limits->max_period_us < limits->min_period_us ||
        limits->period_step_us == 0 || limits->dispatch_quantum_us == 0)
        return 0;
    return period_us == 0 || (period_us >= limits->min_period_us &&
        period_us <= limits->max_period_us &&
        period_us % limits->period_step_us == 0);
}

pxa_status_t pxa_clock_timing_control(pxa_runtime_t *runtime,
    pxa_component_t component, const pxa_message_view_t *message,
    const pxa_clock_timing_backend_t *backend) {
    uint8_t payload[PXA_CLOCK_LIMITS_BYTES];
    uint32_t period_us = 0, size;
    uint16_t profile;
    pxa_status_t status;
    if (message == NULL || backend == NULL || backend->configure == NULL ||
        !pxa_clock_period_supported(&backend->limits, 0))
        return PXA_STATUS_INVALID_ARGUMENT;
    if (message->opcode != PXA_CLOCK_LIMITS && message->opcode != PXA_CLOCK_CONFIGURE)
        return PXA_STATUS_UNSUPPORTED;
    size = message->opcode == PXA_CLOCK_LIMITS ? PXA_CLOCK_LIMITS_BYTES : 4u;
    if (message->request_token == 0 || message->payload.size !=
        (message->opcode == PXA_CLOCK_CONFIGURE ? 4u : 0u) ||
        (message->payload.size != 0 && message->payload.data == NULL))
        return PXA_STATUS_INVALID_ARGUMENT;
    status = pxa_component_wire_profile(runtime, component, &profile);
    if (status != PXA_STATUS_OK) return status;
    if (profile != 1) return PXA_STATUS_UNSUPPORTED;
    status = pxa_request_begin_reserved(runtime, component, message->request_token,
        PXA_CLOCK_SERVICE_ID, message->opcode, 0, size);
    if (status != PXA_STATUS_OK) return status;
    status = pxa_request_commit(runtime, component, message->request_token);
    if (status != PXA_STATUS_OK) return status;
    if (message->opcode == PXA_CLOCK_LIMITS) {
        pxa_write_u32(payload, backend->limits.min_period_us);
        pxa_write_u32(payload + 4, backend->limits.max_period_us);
        pxa_write_u32(payload + 8, backend->limits.period_step_us);
        pxa_write_u32(payload + 12, backend->limits.dispatch_quantum_us);
        pxa_write_u32(payload + 16, backend->limits.flags);
    } else {
        period_us = pxa_read_u32(message->payload.data);
        status = pxa_clock_period_supported(&backend->limits, period_us)
            ? backend->configure(backend->context, component, period_us)
            : PXA_STATUS_INVALID_ARGUMENT;
        pxa_write_u32(payload, period_us);
    }
    return pxa_request_complete(runtime, component, message->request_token, status,
        status == PXA_STATUS_OK ? payload : NULL,
        status == PXA_STATUS_OK ? size : 0);
}
