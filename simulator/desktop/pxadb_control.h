#ifndef PXSYS_PXADB_CONTROL_H
#define PXSYS_PXADB_CONTROL_H

#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

typedef int (*pxsys_pxadb_catalog_refresh_fn)(void *context);

typedef struct {
    int listener;
    lv_display_t *display;
    uint32_t window_id;
    uint8_t tap_pending;
    int16_t tap_x;
    int16_t tap_y;
    uint32_t tap_release_at;
    void *catalog_context;
    pxsys_pxadb_catalog_refresh_fn refresh_catalog;
    char path[108];
} pxsys_pxadb_control_t;

int pxsys_pxadb_control_start(pxsys_pxadb_control_t *control,
                              const char *socket_path,
                              lv_display_t *display, void *catalog_context,
                              pxsys_pxadb_catalog_refresh_fn refresh_catalog);
void pxsys_pxadb_control_poll(pxsys_pxadb_control_t *control);
void pxsys_pxadb_control_stop(pxsys_pxadb_control_t *control);

#endif
