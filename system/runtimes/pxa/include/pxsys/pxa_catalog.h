#ifndef PXSYS_PXA_CATALOG_H
#define PXSYS_PXA_CATALOG_H

#include "pxsys/pxa_binding.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PXSYS_PXA_CATALOG_ADDED = 1,
    PXSYS_PXA_CATALOG_UPDATED = 2,
} pxsys_pxa_catalog_change_t;

/* Manifests passed here must already have passed package verification and
 * product install policy. The registry deep-copies all descriptor fields. */
pxsys_status_t pxsys_pxa_catalog_publish(pxsys_app_registry_t* apps,
                                         const pxa_package_manifest_t* manifest,
                                         pxsys_string_t runtime_id, uint32_t app_flags,
                                         pxsys_pxa_catalog_change_t* change);
pxsys_status_t pxsys_pxa_catalog_unpublish(pxsys_app_registry_t* apps,
                                           const pxa_package_manifest_t* manifest);

#ifdef __cplusplus
}
#endif

#endif
