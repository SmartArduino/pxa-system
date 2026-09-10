#ifndef PXSYS_APP_REGISTRY_H
#define PXSYS_APP_REGISTRY_H

#include <stddef.h>
#include <stdint.h>

#include "pxsys/status.h"
#include "pxsys/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXSYS_APP_FLAG_SYSTEM UINT32_C(1)
#define PXSYS_APP_FLAG_REMOVABLE UINT32_C(2)
#define PXSYS_APP_FLAG_SINGLE_INSTANCE UINT32_C(4)
#define PXSYS_APP_FLAG_ENABLED UINT32_C(8)
#define PXSYS_APP_FLAG_LAUNCHER UINT32_C(16)

typedef struct {
    uint32_t struct_size;
    pxsys_app_identity_t identity;
    /* Required language-independent fallback shown when no catalog matches. */
    pxsys_string_t display_name;
    pxsys_string_t version;
    /* Runtime selection is metadata, never part of application identity. */
    pxsys_string_t runtime_id;
    uint32_t flags;
    /* Optional fields appended in descriptor v2. The resource namespace and
     * keys are interpreted by app_metadata.h for both Native and PXA Apps. */
    pxsys_string_t description;
    pxsys_string_t icon_reference;
    pxsys_string_t resource_namespace;
    pxsys_string_t display_name_resource_key;
    pxsys_string_t description_resource_key;
    pxsys_string_t icon_resource_key;
} pxsys_app_descriptor_t;

#define PXSYS_APP_DESCRIPTOR_V1_SIZE offsetof(pxsys_app_descriptor_t, description)
#define PXSYS_APP_DESCRIPTION_MAX_BYTES ((size_t)512)
#define PXSYS_APP_ICON_REFERENCE_MAX_BYTES ((size_t)255)
#define PXSYS_APP_RESOURCE_NAMESPACE_MAX_BYTES ((size_t)95)
#define PXSYS_APP_RESOURCE_KEY_MAX_BYTES ((size_t)95)

typedef struct {
    uint32_t struct_size;
    size_t max_apps;
    size_t max_app_id_bytes;
    size_t max_display_name_bytes;
    size_t max_version_bytes;
    size_t max_runtime_id_bytes;
    pxsys_allocator_t allocator;
} pxsys_app_registry_config_t;

typedef struct pxsys_app_registry pxsys_app_registry_t;

typedef struct {
    uint32_t slot;
    uint32_t generation;
} pxsys_app_ref_t;

#define PXSYS_APP_REF_INVALID_SLOT UINT32_MAX

void pxsys_app_registry_config_init(pxsys_app_registry_config_t* config);

pxsys_status_t pxsys_app_identity_validate(const pxsys_app_identity_t* identity,
                                           size_t max_app_id_bytes);
int pxsys_app_identity_equal(const pxsys_app_identity_t* left, const pxsys_app_identity_t* right);

pxsys_status_t pxsys_app_registry_create(const pxsys_app_registry_config_t* config,
                                         pxsys_app_registry_t** output);
pxsys_status_t pxsys_app_registry_destroy(pxsys_app_registry_t* registry);

pxsys_status_t pxsys_app_registry_register(pxsys_app_registry_t* registry,
                                           const pxsys_app_descriptor_t* descriptor);
pxsys_status_t pxsys_app_registry_update(pxsys_app_registry_t* registry,
                                         const pxsys_app_descriptor_t* descriptor);
pxsys_status_t pxsys_app_registry_unregister(pxsys_app_registry_t* registry,
                                             const pxsys_app_identity_t* identity);

size_t pxsys_app_registry_count(const pxsys_app_registry_t* registry);
uint64_t pxsys_app_registry_generation(const pxsys_app_registry_t* registry);
const pxsys_app_descriptor_t* pxsys_app_registry_find(const pxsys_app_registry_t* registry,
                                                      const pxsys_app_identity_t* identity);
const pxsys_app_descriptor_t* pxsys_app_registry_at(const pxsys_app_registry_t* registry,
                                                    size_t index);

pxsys_app_ref_t pxsys_app_ref_invalid(void);
pxsys_status_t pxsys_app_registry_acquire(pxsys_app_registry_t* registry,
                                          const pxsys_app_identity_t* identity,
                                          pxsys_app_ref_t* reference,
                                          const pxsys_app_descriptor_t** descriptor);
const pxsys_app_descriptor_t* pxsys_app_registry_get(const pxsys_app_registry_t* registry,
                                                     pxsys_app_ref_t reference);
pxsys_status_t pxsys_app_registry_release(pxsys_app_registry_t* registry,
                                          pxsys_app_ref_t reference);

/* Unacquired descriptor views remain valid until their entry is unregistered.
 * Acquired entries cannot be unregistered until every reference is released. */

#ifdef __cplusplus
}
#endif

#endif
