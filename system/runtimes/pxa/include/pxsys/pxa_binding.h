#ifndef PXSYS_PXA_BINDING_H
#define PXSYS_PXA_BINDING_H

#include <stdint.h>

#include "pxa/package.h"
#include "pxa/status.h"
#include "pxsys/app_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXSYS_PXA_RUNTIME_WASM "wamr-wasm"
#define PXSYS_PXA_RUNTIME_AOT "wamr-aot"

/* The manifest must already have passed signature, lineage and package-policy
 * verification. Returned strings are views into manifest/runtime_id storage. */
pxsys_status_t pxsys_pxa_manifest_identity(const pxa_package_manifest_t* manifest,
                                           pxsys_app_identity_t* identity);
pxsys_status_t pxsys_pxa_manifest_descriptor(const pxa_package_manifest_t* manifest,
                                             pxsys_string_t runtime_id, uint32_t app_flags,
                                             pxsys_app_descriptor_t* descriptor);
/* component_id must identify a component declared by the verified manifest. */
pxsys_status_t pxsys_pxa_manifest_caller(const pxa_package_manifest_t* manifest,
                                         pxa_bytes_t component_id, pxsys_caller_t* caller);
pxsys_status_t pxsys_status_from_pxa(pxa_status_t status);
pxa_status_t pxsys_status_to_pxa(pxsys_status_t status);

#ifdef __cplusplus
}
#endif

#endif
