#ifndef PXSYS_APP_METADATA_H
#define PXSYS_APP_METADATA_H

#include <stdint.h>

#include "pxsys/app_registry.h"
#include "pxsys/resources.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXSYS_APP_METADATA_CATALOG_DISPLAY_NAME UINT32_C(1)
#define PXSYS_APP_METADATA_CATALOG_DESCRIPTION UINT32_C(2)
#define PXSYS_APP_METADATA_CATALOG_ICON UINT32_C(4)
#define PXSYS_APP_METADATA_NAME_KEY "metadata.name"
#define PXSYS_APP_METADATA_DESCRIPTION_KEY "metadata.description"
#define PXSYS_APP_METADATA_ICON_KEY "metadata.icon"

typedef struct {
    uint32_t struct_size;
    pxsys_string_t display_name;
    pxsys_string_t description;
    pxsys_string_t icon_reference;
    uint32_t catalog_fields;
} pxsys_app_metadata_t;

/* Resolves application-facing metadata for a canonical BCP 47 locale. Every
 * returned string is borrowed from either the descriptor or a mounted catalog.
 * Missing translations fall back independently to descriptor defaults. */
pxsys_status_t pxsys_app_metadata_resolve(
    const pxsys_resource_service_t* resources,
    const pxsys_app_descriptor_t* app, pxsys_string_t locale,
    pxsys_app_metadata_t* metadata);

#ifdef __cplusplus
}
#endif

#endif
