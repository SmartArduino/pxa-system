#ifndef PXA_LOG_H
#define PXA_LOG_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_LOG_SERVICE_ID UINT16_C(19)
#define PXA_LOG_SERVICE_MAJOR UINT16_C(0)
#define PXA_LOG_SERVICE_MINOR UINT16_C(1)
#define PXA_LOG_SERVICE_PATCH UINT16_C(0)

#define PXA_LOG_WRITE UINT16_C(1)
#define PXA_LOG_MAX_MESSAGE_BYTES UINT16_C(256)

typedef uint8_t pxa_log_level_t;
#define PXA_LOG_LEVEL_TRACE ((pxa_log_level_t)0)
#define PXA_LOG_LEVEL_DEBUG ((pxa_log_level_t)1)
#define PXA_LOG_LEVEL_INFO ((pxa_log_level_t)2)
#define PXA_LOG_LEVEL_WARN ((pxa_log_level_t)3)
#define PXA_LOG_LEVEL_ERROR ((pxa_log_level_t)4)

typedef pxa_status_t (*pxa_log_write_fn)(
    void *context, pxa_component_t component, pxa_bytes_t app_id,
    pxa_log_level_t level, pxa_bytes_t message);

typedef struct {
    uint32_t struct_size;
    void *context;
    pxa_log_write_fn write;
    pxa_bytes_t app_id;
    uint16_t max_message_bytes;
} pxa_log_config_t;

typedef struct pxa_log_service pxa_log_service_t;

size_t pxa_log_service_workspace_size(const pxa_log_config_t *config);
pxa_status_t pxa_log_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_log_config_t *config, pxa_log_service_t **output);
pxa_status_t pxa_log_service_register(pxa_log_service_t *service);

#ifdef __cplusplus
}
#endif

#endif
