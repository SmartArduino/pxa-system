#ifndef PXA_SERVICE_H
#define PXA_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif
#define PXA_SERVICE_CORE UINT16_C(1)
#define PXA_CORE_SERVICE_MAJOR UINT16_C(0)
#define PXA_CORE_SERVICE_MINOR UINT16_C(1)
#define PXA_CORE_SERVICE_PATCH UINT16_C(0)
#define PXA_CORE_CANCEL_REQUEST UINT16_C(1)
#define PXA_CORE_CLOSE_HANDLE UINT16_C(2)

typedef pxa_status_t (*pxa_service_control_fn)(
    void *context, pxa_runtime_t *runtime, pxa_component_t component,
    const pxa_message_view_t *message);

typedef void (*pxa_service_component_stopped_fn)(
    void *context, pxa_runtime_t *runtime, pxa_component_t component);

typedef struct {
    uint32_t struct_size;
    uint16_t service_id;
    uint16_t major;
    uint16_t minor;
    uint16_t reserved;
    uint64_t features;
    void *context;
    pxa_service_control_fn control;
    pxa_service_component_stopped_fn component_stopped;
} pxa_service_ops_t;

pxa_status_t pxa_service_register(pxa_runtime_t *runtime,
                                  const pxa_service_ops_t *service);

#ifdef __cplusplus
}
#endif

#endif
