#ifndef PXA_WINDOW_H
#define PXA_WINDOW_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/service.h"

#ifdef __cplusplus
extern "C" {
#endif
#define PXA_WINDOW_SERVICE_ID UINT16_C(2)
#define PXA_WINDOW_SERVICE_MAJOR UINT16_C(0)
#define PXA_WINDOW_SERVICE_MINOR UINT16_C(1)
#define PXA_WINDOW_SERVICE_PATCH UINT16_C(0)

#define PXA_WINDOW_CONFIGURE UINT16_C(1)
#define PXA_WINDOW_GET_SNAPSHOT UINT16_C(2)
#define PXA_WINDOW_METRICS_CHANGED UINT16_C(0x8001)
#define PXA_WINDOW_BACK_REQUESTED UINT16_C(0x8002)

typedef uint8_t pxa_window_bar_mode_t;
#define PXA_WINDOW_BAR_VISIBLE ((pxa_window_bar_mode_t)0)
#define PXA_WINDOW_BAR_HIDDEN ((pxa_window_bar_mode_t)1)
#define PXA_WINDOW_BAR_TRANSIENT ((pxa_window_bar_mode_t)2)

typedef uint8_t pxa_window_icon_style_t;
#define PXA_WINDOW_ICON_AUTO ((pxa_window_icon_style_t)0)
#define PXA_WINDOW_ICON_LIGHT ((pxa_window_icon_style_t)1)
#define PXA_WINDOW_ICON_DARK ((pxa_window_icon_style_t)2)

typedef uint8_t pxa_window_orientation_t;
#define PXA_WINDOW_ORIENTATION_UNSPECIFIED ((pxa_window_orientation_t)0)
#define PXA_WINDOW_ORIENTATION_PORTRAIT ((pxa_window_orientation_t)1)
#define PXA_WINDOW_ORIENTATION_LANDSCAPE ((pxa_window_orientation_t)2)

typedef struct {
    uint16_t min_major;
    uint16_t min_minor;
    uint16_t max_major;
    uint16_t max_minor;
} pxa_version_range_t;

typedef struct {
    uint16_t major;
    uint16_t minor;
} pxa_version_t;

typedef struct {
    uint32_t left;
    uint32_t top;
    uint32_t right;
    uint32_t bottom;
} pxa_window_insets_t;

typedef struct {
    uint8_t edge_to_edge;
    pxa_window_bar_mode_t status_bar_mode;
    pxa_window_bar_mode_t navigation_bar_mode;
    pxa_window_icon_style_t status_bar_icons;
    pxa_window_icon_style_t navigation_bar_icons;
    uint32_t status_bar_color;
    uint32_t navigation_bar_color;
} pxa_window_configuration_t;

typedef struct {
    uint64_t revision;
    uint32_t logical_width;
    uint32_t logical_height;
    uint32_t pixel_width;
    uint32_t pixel_height;
    uint32_t density_numerator;
    uint32_t density_denominator;
    pxa_window_insets_t safe_insets;
    pxa_window_insets_t system_bar_insets;
    pxa_window_orientation_t orientation;
    uint8_t focused;
} pxa_window_snapshot_t;

typedef pxa_status_t (*pxa_window_apply_fn)(
    void *context, const pxa_window_configuration_t *configuration);

typedef struct {
    uint32_t struct_size;
    void *context;
    pxa_window_apply_fn apply;
} pxa_window_backend_t;

typedef struct pxa_window_service pxa_window_service_t;

size_t pxa_window_service_workspace_size(uint16_t max_windows);
pxa_status_t pxa_window_service_init(void *workspace, size_t workspace_size,
                                     pxa_runtime_t *runtime,
                                     uint16_t max_windows,
                                     pxa_window_service_t **output);
pxa_status_t pxa_window_service_register(pxa_window_service_t *service);
pxa_status_t pxa_window_bind(pxa_window_service_t *service,
                             pxa_component_t component,
                             const pxa_window_backend_t *backend);
pxa_status_t pxa_window_unbind(pxa_window_service_t *service,
                               pxa_component_t component);

pxa_status_t pxa_window_negotiate_version(pxa_version_range_t requested,
                                          pxa_version_t *selected);
pxa_status_t pxa_window_update_snapshot(pxa_window_service_t *service,
                                        pxa_component_t component,
                                        const pxa_window_snapshot_t *snapshot);
pxa_status_t pxa_window_flush_metrics(pxa_window_service_t *service,
                                      pxa_component_t component);
pxa_status_t pxa_window_queue_back(pxa_window_service_t *service,
                                   pxa_component_t component);
pxa_status_t pxa_window_get_configuration(
    const pxa_window_service_t *service, pxa_component_t component,
    pxa_window_configuration_t *output);
pxa_status_t pxa_window_get_snapshot(const pxa_window_service_t *service,
                                     pxa_component_t component,
                                     pxa_window_snapshot_t *output);
pxa_status_t pxa_window_resolve_event_result(
    const pxa_message_view_t *event, int32_t guest_result,
    uint8_t *close_requested);

#ifdef __cplusplus
}
#endif

#endif
