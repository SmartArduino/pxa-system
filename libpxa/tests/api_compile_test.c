#include "pxa/pxa.h"

int main(void) {
    pxa_runtime_limits_t limits;
    pxa_runtime_usage_t usage;
    pxa_status_t (*post_message)(
        pxa_runtime_t *, pxa_component_t, uint16_t, uint16_t, uint32_t,
        pxa_bytes_t, uint8_t, uint64_t) = pxa_event_post_message;
    pxa_status_t (*post_messagev)(
        pxa_runtime_t *, pxa_component_t, uint16_t, uint16_t, uint32_t,
        const pxa_bytes_t *, size_t, uint8_t,
        uint64_t) = pxa_event_post_messagev;
    size_t (*device_workspace_size)(const pxa_device_config_t *) =
        pxa_device_service_workspace_size;
    pxa_runtime_limits_init(&limits);
    (void)usage;
    (void)post_message;
    (void)post_messagev;
    (void)device_workspace_size;
    return PXA_VERSION_MAJOR == 0 && limits.struct_size == sizeof(limits)
               ? 0
               : 1;
}
