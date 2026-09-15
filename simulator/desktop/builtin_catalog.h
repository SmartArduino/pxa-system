#ifndef PXSYS_DESKTOP_BUILTIN_CATALOG_H
#define PXSYS_DESKTOP_BUILTIN_CATALOG_H

#include <stddef.h>
#include <stdint.h>

#include "pxsys/app_registry.h"

typedef struct {
    const char* id;
    const char* name;
    const char* version;
    const char* description;
    const char* icon_reference;
} pxsys_desktop_builtin_app_t;

size_t pxsys_desktop_builtin_app_count(void);
const pxsys_desktop_builtin_app_t* pxsys_desktop_builtin_app(size_t index);
pxsys_status_t pxsys_desktop_register_builtin_apps(
    pxsys_app_registry_t* registry,
    const uint8_t publisher_root[PXSYS_PUBLISHER_ROOT_BYTES]);

#endif
