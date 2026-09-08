#ifndef PXA_SENSOR_H
#define PXA_SENSOR_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/permission.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_SENSOR_SERVICE_ID UINT16_C(8)
#define PXA_SENSOR_SERVICE_MAJOR UINT16_C(0)
#define PXA_SENSOR_SERVICE_MINOR UINT16_C(1)
#define PXA_SENSOR_SERVICE_PATCH UINT16_C(0)
#define PXA_SENSOR_LIST UINT16_C(1)
#define PXA_SENSOR_SUBSCRIBE UINT16_C(2)
#define PXA_SENSOR_SAMPLE UINT16_C(0x8001)

#define PXA_SENSOR_UNIT_MILLI_CELSIUS UINT16_C(1)
#define PXA_SENSOR_UNIT_MICRO_METERS_PER_SECOND_SQUARED UINT16_C(2)
#define PXA_SENSOR_UNIT_MICRO_RADIANS_PER_SECOND UINT16_C(3)
#define PXA_SENSOR_UNIT_MILLI_LUX UINT16_C(4)

#define PXA_SENSOR_MAX_SEMANTIC_BYTES ((size_t)64)
#define PXA_SENSOR_MAX_DIMENSIONS UINT8_C(3)
#define PXA_SENSOR_MAX_DESCRIPTORS UINT16_C(32)

typedef struct {
    uint16_t id;
    pxa_bytes_t semantic;
    uint16_t unit;
    uint8_t dimensions;
    uint32_t min_period_ms;
    uint32_t max_period_ms;
} pxa_sensor_descriptor_t;

typedef pxa_status_t (*pxa_sensor_subscribe_fn)(
    void *context, uint16_t descriptor_id, uint32_t period_ms,
    void **subscription);
typedef pxa_status_t (*pxa_sensor_read_fn)(
    void *context, void *subscription, uint16_t descriptor_id,
    int32_t values[PXA_SENSOR_MAX_DIMENSIONS]);
typedef void (*pxa_sensor_unsubscribe_fn)(
    void *context, void *subscription, uint16_t descriptor_id);

typedef struct {
    uint32_t struct_size;
    /* NULL is valid only when descriptor_count is zero, for a host that
     * deliberately exposes no physical sensors. */
    const pxa_sensor_descriptor_t *descriptors;
    uint16_t descriptor_count;
    uint16_t max_subscriptions;
    uint16_t max_subscriptions_per_component;
    uint16_t reserved;
    void *provider_context;
    pxa_sensor_subscribe_fn subscribe;
    pxa_sensor_read_fn read;
    pxa_sensor_unsubscribe_fn unsubscribe;
    pxa_permission_service_t *permissions;
} pxa_sensor_config_t;

typedef struct pxa_sensor_service pxa_sensor_service_t;

size_t pxa_sensor_service_workspace_size(const pxa_sensor_config_t *config);
pxa_status_t pxa_sensor_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_sensor_config_t *config, pxa_sensor_service_t **output);
pxa_status_t pxa_sensor_service_register(pxa_sensor_service_t *service);
int pxa_sensor_has_active_subscriptions(const pxa_sensor_service_t *service);
pxa_status_t pxa_sensor_poll(
    pxa_sensor_service_t *service, uint64_t timestamp_us,
    pxa_component_t *affected, size_t capacity, size_t *count);

#ifdef __cplusplus
}
#endif

#endif
