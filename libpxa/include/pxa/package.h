#ifndef PXA_PACKAGE_H
#define PXA_PACKAGE_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The encoded length is a uint32_t in both the manifest and container
 * formats. Runtime code allocates the actual length; this is only the format
 * representability boundary. */
#define PXA_PACKAGE_MAX_MANIFEST_BYTES ((size_t)UINT32_MAX)
/* Keep stack-based test fixtures small without imposing a product limit. */
#define PXA_PACKAGE_TEST_MANIFEST_BYTES ((size_t)16384)
#define PXA_PACKAGE_DIGEST_BYTES ((size_t)32)
#define PXA_PACKAGE_SIGNATURE_BYTES ((size_t)64)
/* These counts are represented as uint16_t in the package format. Memory for
 * their decoded entries is measured and allocated per manifest. */
#define PXA_PACKAGE_MAX_COMPONENTS UINT16_MAX
#define PXA_PACKAGE_MAX_ARTIFACTS_PER_COMPONENT UINT16_MAX
#define PXA_PACKAGE_MAX_SERVICES_PER_COMPONENT UINT16_MAX
#define PXA_PACKAGE_MAX_FILES UINT16_MAX
#define PXA_PACKAGE_MAX_PERMISSIONS UINT16_MAX
#define PXA_PACKAGE_MAX_IPC_ENDPOINTS UINT16_MAX
#define PXA_PACKAGE_MAX_LINEAGE_LINKS UINT16_C(8)
#define PXA_PACKAGE_MAX_LINEAGE_SPKI_BYTES UINT16_C(160)
#define PXA_PACKAGE_MAX_PUBLISHER_SPKI_BYTES UINT16_C(160)

#define PXA_PACKAGE_MANIFEST_FORMAT_MAJOR UINT16_C(0)
#define PXA_PACKAGE_MANIFEST_FORMAT_MINOR UINT16_C(5)
#define PXA_PACKAGE_MANIFEST_FORMAT_PATCH UINT16_C(0)
#define PXA_PACKAGE_SIGNATURE_FORMAT_MAJOR UINT16_C(0)
#define PXA_PACKAGE_SIGNATURE_FORMAT_MINOR UINT16_C(1)
#define PXA_PACKAGE_SIGNATURE_FORMAT_PATCH UINT16_C(0)
#define PXA_PACKAGE_SIGNATURE_FORMAT_VERSION UINT16_C(0x0001)
#define PXA_PUBLISHER_LINEAGE_FORMAT_MAJOR UINT16_C(0)
#define PXA_PUBLISHER_LINEAGE_FORMAT_MINOR UINT16_C(1)
#define PXA_PUBLISHER_LINEAGE_FORMAT_PATCH UINT16_C(0)

typedef struct {
    uint16_t major;
    uint16_t minor;
} pxa_package_version_t;

typedef uint8_t pxa_component_kind_t;
#define PXA_COMPONENT_KIND_UI ((pxa_component_kind_t)1)
#define PXA_COMPONENT_KIND_SERVICE ((pxa_component_kind_t)2)
#define PXA_COMPONENT_KIND_JOB ((pxa_component_kind_t)3)

typedef uint8_t pxa_artifact_kind_t;
#define PXA_ARTIFACT_WASM ((pxa_artifact_kind_t)1)
#define PXA_ARTIFACT_AOT ((pxa_artifact_kind_t)2)

typedef uint8_t pxa_memory_model_t;
#define PXA_MEMORY_WASM32 ((pxa_memory_model_t)1)
#define PXA_MEMORY_WASM32_SHARED ((pxa_memory_model_t)2)

typedef struct {
    pxa_artifact_kind_t kind;
    pxa_bytes_t path;
    pxa_bytes_t target;
    pxa_bytes_t engine;
    pxa_bytes_t engine_abi;
    uint64_t required_features;
    pxa_memory_model_t memory_model;
} pxa_package_artifact_t;

typedef struct {
    uint16_t service;
    pxa_package_version_t min_version;
    pxa_package_version_t max_version;
    uint64_t required_features;
} pxa_package_service_requirement_t;

typedef struct {
    pxa_bytes_t id;
    pxa_component_kind_t kind;
    pxa_package_artifact_t *artifacts;
    pxa_package_service_requirement_t *services;
    uint16_t artifact_count;
    uint16_t service_count;
} pxa_package_component_t;

typedef struct {
    pxa_bytes_t path;
    uint64_t size;
    const uint8_t *sha256;
} pxa_package_file_t;

typedef struct {
    pxa_bytes_t name;
    pxa_bytes_t scope;
    uint8_t required;
} pxa_package_permission_t;

typedef struct {
    pxa_bytes_t name;
    pxa_bytes_t component_id;
} pxa_package_ipc_endpoint_t;

typedef struct {
    uint32_t generation;
    uint32_t flags;
    const uint8_t *old_key_id;
    const uint8_t *new_key_id;
    pxa_bytes_t new_spki;
    const uint8_t *signature;
} pxa_package_lineage_link_t;

typedef struct {
    const uint8_t *root_key_id;
    const uint8_t *current_key_id;
    pxa_package_lineage_link_t links[PXA_PACKAGE_MAX_LINEAGE_LINKS];
    uint16_t link_count;
} pxa_package_publisher_lineage_t;

typedef struct {
    uint32_t struct_size;
    uint16_t max_components;
    uint16_t max_artifacts;
    uint16_t max_services;
    uint16_t max_files;
    uint16_t max_permissions;
    uint16_t max_ipc_endpoints;
} pxa_package_limits_t;

typedef struct pxa_package_manifest {
    pxa_bytes_t encoded;
    uint16_t format_minor;
    const uint8_t *publisher_key_id;
    const uint8_t *management_key_id;
    pxa_bytes_t app_id;
    pxa_bytes_t version;
    pxa_bytes_t name;
    pxa_bytes_t description;
    pxa_bytes_t icon_path;
    uint64_t release_sequence;
    uint8_t has_release_sequence;
    pxa_bytes_t publisher_lineage;
    pxa_bytes_t publisher_spki;
    /* Manifest 0.5 compatibility declarations. min_sdk decides whether a
     * Host can run the package; target_sdk selects behavior policy. */
    pxa_package_version_t min_sdk;
    pxa_package_version_t target_sdk;
    pxa_package_component_t *components;
    pxa_package_artifact_t *artifacts;
    pxa_package_service_requirement_t *services;
    pxa_package_file_t *files;
    pxa_package_permission_t *permissions;
    pxa_package_ipc_endpoint_t *ipc_endpoints;
    uint16_t component_count;
    uint16_t artifact_count;
    uint16_t service_count;
    uint16_t file_count;
    uint16_t permission_count;
    uint16_t ipc_endpoint_count;
} pxa_package_manifest_t;

typedef struct {
    uint16_t algorithm;
    const uint8_t *publisher_key_id;
    const uint8_t *signature;
} pxa_package_signature_t;

typedef struct {
    pxa_bytes_t path;
    uint64_t size;
    const uint8_t *sha256;
} pxa_package_inventory_entry_t;

typedef struct {
    pxa_bytes_t target;
    pxa_bytes_t engine;
    pxa_bytes_t engine_abi;
    uint64_t supported_features;
    pxa_memory_model_t memory_model;
} pxa_package_host_profile_t;

typedef struct {
    uint16_t service;
    pxa_package_version_t version;
    uint64_t features;
} pxa_package_service_capability_t;

typedef struct {
    pxa_package_version_t core_version;
    const pxa_package_service_capability_t *services;
    size_t service_count;
} pxa_package_activation_profile_t;

typedef pxa_status_t (*pxa_package_signature_verify_fn)(
    void *context, pxa_bytes_t publisher_key_id, pxa_bytes_t domain,
    pxa_bytes_t manifest, pxa_bytes_t signature);

void pxa_package_limits_init(pxa_package_limits_t *limits);
size_t pxa_package_manifest_workspace_size(
    const pxa_package_limits_t *limits);
/* Reads the manifest record layout without allocating parser output. The
 * supplied limits remain the accepted protocol bounds; required_limits is
 * populated with the capacities needed by this specific manifest. */
pxa_status_t pxa_package_manifest_measure(
    pxa_bytes_t encoded, const pxa_package_limits_t *limits,
    pxa_package_limits_t *required_limits);
pxa_status_t pxa_package_manifest_parse(
    void *workspace, size_t workspace_size, pxa_bytes_t encoded,
    const pxa_package_limits_t *limits, pxa_package_manifest_t **output);
pxa_status_t pxa_package_signature_parse(
    pxa_bytes_t encoded, pxa_package_signature_t *output);
pxa_status_t pxa_package_publisher_lineage_parse(
    const pxa_package_manifest_t *manifest,
    pxa_package_publisher_lineage_t *output);
pxa_status_t pxa_package_signature_identity_validate(
    const pxa_package_manifest_t *manifest,
    const pxa_package_signature_t *signature);
pxa_status_t pxa_package_signature_verify(
    const pxa_package_manifest_t *manifest,
    const pxa_package_signature_t *signature, void *context,
    pxa_package_signature_verify_fn verify);
pxa_status_t pxa_package_inventory_validate(
    const pxa_package_manifest_t *manifest,
    const pxa_package_inventory_entry_t *inventory, size_t count);
const pxa_package_file_t *pxa_package_file_find(
    const pxa_package_manifest_t *manifest, pxa_bytes_t path);
pxa_status_t pxa_package_requirements_validate(
    const pxa_package_manifest_t *manifest,
    const pxa_package_component_t *component,
    const pxa_package_activation_profile_t *host);
pxa_status_t pxa_package_artifact_select(
    const pxa_package_component_t *component,
    const pxa_package_host_profile_t *host,
    const pxa_package_artifact_t **output);
int pxa_package_same_identity(const pxa_package_manifest_t *left,
                              const pxa_package_manifest_t *right);

#ifdef __cplusplus
}
#endif

#endif
