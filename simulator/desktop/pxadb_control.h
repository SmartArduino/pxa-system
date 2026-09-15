#ifndef PXSYS_PXADB_CONTROL_H
#define PXSYS_PXADB_CONTROL_H

#include <stddef.h>

#include "lvgl.h"

typedef struct {
    int listener;
    lv_display_t *display;
    char path[108];
} pxsys_pxadb_control_t;

int pxsys_pxadb_control_start(pxsys_pxadb_control_t *control,
                              const char *socket_path,
                              lv_display_t *display);
void pxsys_pxadb_control_poll(pxsys_pxadb_control_t *control);
void pxsys_pxadb_control_stop(pxsys_pxadb_control_t *control);

#endif
