#ifndef PXA_HAPTICS_H
#define PXA_HAPTICS_H
#include "pxa/permission.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PXA_HAPTICS_SERVICE_ID UINT16_C(23)
#define PXA_HAPTICS_SERVICE_MAJOR UINT16_C(0)
#define PXA_HAPTICS_SERVICE_MINOR UINT16_C(1)
#define PXA_HAPTICS_SERVICE_PATCH UINT16_C(0)
#define PXA_HAPTICS_CAPABILITIES UINT16_C(1)
#define PXA_HAPTICS_OPEN UINT16_C(2)
#define PXA_HAPTICS_VIBRATE UINT16_C(3)
#define PXA_HAPTICS_STOP UINT16_C(4)
#define PXA_HAPTICS_CAP_VIBRATE UINT32_C(1)
#define PXA_HAPTICS_CAP_AMPLITUDE UINT32_C(2)
#define PXA_HAPTICS_PERMISSION "haptics.play"
#define PXA_HAPTICS_SCOPE "device.default"
typedef struct {
    uint32_t struct_size;
    void *context;
    uint32_t flags, max_duration_ms;
    /* Runtime-thread callbacks. play replaces the previous pulse and must
     * enforce its duration in the device, independently of Guest progress. */
    pxa_status_t (*play)(void *, uint32_t duration_ms, uint16_t amplitude);
    void (*stop)(void *);
} pxa_haptics_backend_t;
typedef struct pxa_haptics_service pxa_haptics_service_t;
size_t pxa_haptics_service_workspace_size(void);
pxa_status_t pxa_haptics_service_init(void *, size_t, pxa_runtime_t *,
    pxa_permission_service_t *, const pxa_haptics_backend_t *, pxa_haptics_service_t **);
pxa_status_t pxa_haptics_service_register(pxa_haptics_service_t *);
void pxa_haptics_service_set_foreground(pxa_haptics_service_t *, pxa_component_t);
/* Before freeing the service workspace, while runtime still exists. */
void pxa_haptics_service_deinit(pxa_haptics_service_t *);
#ifdef __cplusplus
}
#endif
#endif
