#include "pxsys/pxa_catalog.h"

pxsys_status_t pxsys_pxa_catalog_publish(pxsys_app_registry_t* apps,
                                         const pxa_package_manifest_t* manifest,
                                         pxsys_string_t runtime_id, uint32_t app_flags,
                                         pxsys_pxa_catalog_change_t* change) {
    pxsys_app_descriptor_t descriptor;
    pxsys_status_t status;
    if (apps == NULL || change == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *change = 0;
    status = pxsys_pxa_manifest_descriptor(manifest, runtime_id, app_flags, &descriptor);
    if (status != PXSYS_STATUS_OK)
        return status;
    if (pxsys_app_registry_find(apps, &descriptor.identity) == NULL) {
        status = pxsys_app_registry_register(apps, &descriptor);
        if (status == PXSYS_STATUS_OK)
            *change = PXSYS_PXA_CATALOG_ADDED;
        return status;
    }
    status = pxsys_app_registry_update(apps, &descriptor);
    if (status == PXSYS_STATUS_OK)
        *change = PXSYS_PXA_CATALOG_UPDATED;
    return status;
}

pxsys_status_t pxsys_pxa_catalog_unpublish(pxsys_app_registry_t* apps,
                                           const pxa_package_manifest_t* manifest) {
    pxsys_app_identity_t identity;
    pxsys_status_t status;
    if (apps == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    status = pxsys_pxa_manifest_identity(manifest, &identity);
    if (status != PXSYS_STATUS_OK)
        return status;
    return pxsys_app_registry_unregister(apps, &identity);
}
