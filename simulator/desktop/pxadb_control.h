#ifndef PXSYS_PXADB_CONTROL_H
#define PXSYS_PXADB_CONTROL_H

#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

/* WinSock SOCKET is pointer-sized, including on 64-bit Windows. */
typedef intptr_t pxsys_control_socket_t;

enum {
    PXSYS_PXADB_MAX_POINTERS = 5,
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
    pxsys_control_socket_t listener;
    lv_display_t *display;
    uint32_t window_id;
    uint8_t tap_pending;
    int16_t tap_x;
    int16_t tap_y;
    uint32_t tap_release_at;
    /* Debug-only contacts, allocated lazily; no firmware or Guest storage. */
    pxsys_pxadb_touch_t touches[PXSYS_PXADB_MAX_POINTERS];
    void *catalog_context;
    pxsys_pxadb_catalog_refresh_fn refresh_catalog;
    pxsys_pxadb_package_action_fn package_action;
    void *inspect_context;
    /* Returns a PXA status (0 = success); NULL/0 queries required size. */
    int (*inspect_ui)(void *context, char *output, size_t capacity, size_t *size);
    int (*runtime_info)(void *context, char *output, size_t capacity, size_t *size);
    char path[108];
} pxsys_pxadb_control_t;

int pxsys_pxadb_control_start(pxsys_pxadb_control_t *control,
                              const char *socket_path,
                              lv_display_t *display, void *catalog_context,
                              pxsys_pxadb_catalog_refresh_fn refresh_catalog);
void pxsys_pxadb_control_poll(pxsys_pxadb_control_t *control);
void pxsys_pxadb_control_cancel_pointers(pxsys_pxadb_control_t *control);
void pxsys_pxadb_control_stop(pxsys_pxadb_control_t *control);

#endif
