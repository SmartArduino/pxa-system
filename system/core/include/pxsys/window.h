#ifndef PXSYS_WINDOW_H
#define PXSYS_WINDOW_H

#include <stddef.h>
#include <stdint.h>

#include "pxsys/status.h"
#include "pxsys/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PXSYS_WINDOW_BAR_VISIBLE = 0,
    PXSYS_WINDOW_BAR_HIDDEN,
    PXSYS_WINDOW_BAR_TRANSIENT,
} pxsys_window_bar_mode_t;

typedef enum {
    PXSYS_WINDOW_ICON_AUTO = 0,
    PXSYS_WINDOW_ICON_LIGHT,
    PXSYS_WINDOW_ICON_DARK,
} pxsys_window_icon_style_t;

/* Shared window policy for native and managed runtimes. Colors are RGBA8888. */
typedef struct {
    uint32_t struct_size;
    uint64_t generation;
    uint8_t edge_to_edge;
    pxsys_window_bar_mode_t status_bar_mode;
    pxsys_window_bar_mode_t navigation_bar_mode;
    pxsys_window_icon_style_t status_bar_icons;
    pxsys_window_icon_style_t navigation_bar_icons;
    uint32_t status_bar_color;
    uint32_t navigation_bar_color;
} pxsys_window_snapshot_t;

typedef void (*pxsys_window_changed_fn)(
    void* context, const pxsys_window_snapshot_t* snapshot);

typedef struct {
    uint32_t struct_size;
    size_t max_observers;
    pxsys_allocator_t allocator;
} pxsys_window_service_config_t;

typedef struct pxsys_window_service pxsys_window_service_t;

void pxsys_window_snapshot_init(pxsys_window_snapshot_t* snapshot);
void pxsys_window_service_config_init(pxsys_window_service_config_t* config);
pxsys_status_t pxsys_window_service_create(
    const pxsys_window_service_config_t* config,
    const pxsys_window_snapshot_t* initial, pxsys_window_service_t** output);
pxsys_status_t pxsys_window_service_destroy(pxsys_window_service_t* service);
pxsys_status_t pxsys_window_service_update(
    pxsys_window_service_t* service, const pxsys_window_snapshot_t* snapshot);
pxsys_status_t pxsys_window_service_get(
    const pxsys_window_service_t* service, pxsys_window_snapshot_t* snapshot);
pxsys_status_t pxsys_window_service_subscribe(
    pxsys_window_service_t* service, void* context,
    pxsys_window_changed_fn callback);
pxsys_status_t pxsys_window_service_unsubscribe(
    pxsys_window_service_t* service, void* context,
    pxsys_window_changed_fn callback);

#ifdef __cplusplus
}
#endif

#endif
