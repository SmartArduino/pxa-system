#ifndef PXSYS_PXADB_CONTROL_H
#define PXSYS_PXADB_CONTROL_H

#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

enum {
    PXSYS_PXADB_PACKAGE_RUN = 4,
    PXSYS_PXADB_PACKAGE_STOP = 5,
    PXSYS_PXADB_PACKAGE_ACTIVE = 6
};

typedef int (*pxsys_pxadb_package_action_fn)(void *context, const char *id, unsigned action);

typedef int (*pxsys_pxadb_catalog_refresh_fn)(void *context);

typedef struct {
    lv_indev_t *indev;
    lv_point_t point;
    uint8_t pressed;
} pxsys_pxadb_touch_t;

typedef struct {
    int listener;
    lv_display_t *display;
    uint32_t window_id;
    uint8_t tap_pending;
    int16_t tap_x;
    int16_t tap_y;
    uint32_t tap_release_at;
    /* Debug-only contacts, allocated lazily; no firmware or Guest storage. */
    pxsys_pxadb_touch_t touches[2];
    void *catalog_context;
    pxsys_pxadb_catalog_refresh_fn refresh_catalog;
    pxsys_pxadb_package_action_fn package_action;
    char path[108];
} pxsys_pxadb_control_t;

int pxsys_pxadb_control_start(pxsys_pxadb_control_t *control,
                              const char *socket_path,
                              lv_display_t *display, void *catalog_context,
                              pxsys_pxadb_catalog_refresh_fn refresh_catalog);
void pxsys_pxadb_control_poll(pxsys_pxadb_control_t *control);
void pxsys_pxadb_control_stop(pxsys_pxadb_control_t *control);

#endif
