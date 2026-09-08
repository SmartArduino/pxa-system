#ifndef PXA_DEVICE_H
#define PXA_DEVICE_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/permission.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_DEVICE_SERVICE_ID UINT16_C(15)
#define PXA_DEVICE_SERVICE_MAJOR UINT16_C(0)
#define PXA_DEVICE_SERVICE_MINOR UINT16_C(1)
#define PXA_DEVICE_SERVICE_PATCH UINT16_C(0)

#define PXA_DEVICE_GET_MAC UINT16_C(1)

#define PXA_DEVICE_MAC_KIND_WIFI_STATION_HARDWARE UINT16_C(1)
#define PXA_DEVICE_MAC_KIND_WIFI_SOFTAP_HARDWARE UINT16_C(2)
#define PXA_DEVICE_MAC_KIND_BLUETOOTH_HARDWARE UINT16_C(3)
#define PXA_DEVICE_MAC_KIND_ETHERNET_HARDWARE UINT16_C(4)
#define PXA_DEVICE_MAC_KIND_WIFI_STATION_CURRENT UINT16_C(5)

#define PXA_DEVICE_MAC_FLAG_HARDWARE UINT32_C(1)
#define PXA_DEVICE_MAC_FLAG_CURRENT UINT32_C(2)
#define PXA_DEVICE_MAC_FLAG_LOCALLY_ADMINISTERED UINT32_C(4)

#define PXA_DEVICE_TAG_MAC_KIND UINT16_C(1)
#define PXA_DEVICE_TAG_PERMISSION_HANDLE UINT16_C(2)
#define PXA_DEVICE_TAG_MAC UINT16_C(2)
#define PXA_DEVICE_TAG_FLAGS UINT16_C(3)

typedef pxa_status_t (*pxa_device_get_mac_fn)(
    void *context, uint16_t kind, uint8_t output[6], uint32_t *flags);

typedef struct {
    uint32_t struct_size;
    void *provider_context;
    pxa_device_get_mac_fn get_mac;
    pxa_permission_service_t *permissions;
} pxa_device_config_t;

typedef struct pxa_device_service pxa_device_service_t;

size_t pxa_device_service_workspace_size(const pxa_device_config_t *config);
pxa_status_t pxa_device_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_device_config_t *config, pxa_device_service_t **output);
pxa_status_t pxa_device_service_register(pxa_device_service_t *service);

#ifdef __cplusplus
}
#endif

#endif
