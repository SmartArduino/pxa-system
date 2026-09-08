#ifndef PXA_POSIX_INSTALLER_H
#define PXA_POSIX_INSTALLER_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/openssl/pxa_openssl.h"
#include "pxa/package.h"
#include "pxa/slot_transaction.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Layer 2 reference adapter: POSIX recoverable Package installer. Mirrors the
 * legacy C++ PosixPackageInstaller semantics:
 *  - source manifests, signatures and inventories are checked with no-follow
 *    openat walks before any Package content is written; file hashes are
 *    checked while copying, then the incoming slot is fully verified before
 *    it can be committed;
 *  - installation uses the three-slot transaction from the Core
 *    (slot_transaction.h); the session directory exists only during install
 *    or recovery;
 *  - a <app-id>.lock flock serializes concurrent installers on one root;
 *  - every directory rename and removal is followed by a directory fsync
 *    barrier.
 * The installer depends on the OpenSSL adapter for SHA-256 streaming and
 * signature verification. Transient path strings are heap allocated.
 */

#define PXA_POSIX_INSTALLER_SIGNATURE_ENVELOPE_BYTES ((size_t)108)

typedef struct {
    /* Management/lineage-root key ID. The field name is retained for ABI
     * compatibility with Manifest 0.1 callers. */
    pxa_bytes_t publisher_key_id;
    pxa_bytes_t app_id;
} pxa_posix_installer_identity_t;

typedef enum {
    PXA_POSIX_INSTALL_INSTALLED = 1,
    PXA_POSIX_INSTALL_ALREADY_CURRENT = 2,
} pxa_posix_install_disposition_t;

/* When set, the per-app identity lock is skipped. Hosts without flock
 * support (e.g. ESP-IDF VFS) use this; single-owner hosts do not need the
 * lock. */
#define PXA_POSIX_INSTALLER_FLAG_SKIP_LOCK ((uint32_t)1u << 0)

/* When set, install may replace an unrecoverable slot transaction after the
 * source package has been fully verified. Intended for immutable built-in
 * package sources that must self-heal after an interrupted installation. */
#define PXA_POSIX_INSTALLER_FLAG_REPAIR_CORRUPT ((uint32_t)1u << 1)

/* Store package, lock, owner and private-data names under a filesystem-safe
 * encoding of (publisher lineage root, App ID). Without this flag the legacy
 * App-ID-only layout is retained for compatibility. */
#define PXA_POSIX_INSTALLER_FLAG_COMPOSITE_IDENTITY ((uint32_t)1u << 2)

typedef struct {
    uint32_t struct_size;
    const char *storage_root;   /* directory that contains packages/ */
    pxa_openssl_trust_t trust;  /* trusted publishers (must stay valid) */
    pxa_package_limits_t limits;
    const pxa_slot_faults_t *faults; /* optional checkpoint fault injector */
    uint32_t flags;             /* PXA_POSIX_INSTALLER_FLAG_* */
    /* Optional signature verifier. The trust struct layout is shared with
     * every adapter (pxa_esp_mbedtls_trust_t, ...). NULL selects the
     * OpenSSL reference verifier. */
    pxa_package_signature_verify_fn verify;
} pxa_posix_installer_config_t;

typedef struct pxa_posix_installer pxa_posix_installer_t;

/* Result of install/load: the manifest is parsed into the caller-supplied
 * manifest workspace; its views reference the `encoded` bytes, which the
 * caller must keep alive while the manifest is used. The absolute root path
 * is copied into `root`. */
typedef struct {
    uint32_t struct_size;
    void *manifest_workspace;
    size_t manifest_workspace_size;
    uint8_t *encoded;           /* output: manifest.pxm bytes (caller buffer) */
    size_t encoded_capacity;    /* must hold this Package's bounded manifest */
    pxa_package_manifest_t **manifest; /* output: parsed manifest */
    char *root;                        /* output: absolute package root path */
    size_t root_capacity;
} pxa_posix_installer_result_t;

size_t pxa_posix_installer_workspace_size(
    const pxa_posix_installer_config_t *config);
pxa_status_t pxa_posix_installer_init(void *workspace, size_t workspace_size,
                                      const pxa_posix_installer_config_t *config,
                                      pxa_posix_installer_t **output);
void pxa_posix_installer_deinit(pxa_posix_installer_t *installer);

/* Advisory bounded manifest size for a directory or `.pxa` source. The
 * subsequent verify/install operation reopens and revalidates the source. */
pxa_status_t pxa_posix_installer_source_manifest_size(
    pxa_posix_installer_t *installer, const char *source_path, size_t *size);
pxa_status_t pxa_posix_installer_install(
    pxa_posix_installer_t *installer, const char *source_path,
    pxa_posix_installer_result_t *result,
    pxa_posix_install_disposition_t *disposition);
/* As above, but denies the transaction before any destination mutation unless
 * the verified source has exactly the expected publisher root and App ID. */
pxa_status_t pxa_posix_installer_install_for_identity(
    pxa_posix_installer_t *installer, const char *source_path,
    const pxa_posix_installer_identity_t *expected,
    pxa_posix_installer_result_t *result,
    pxa_posix_install_disposition_t *disposition);
/* Fully authenticate a read-only Package directory or single-file `.pxa`
 * source without copying it into the managed slots. For a container, result
 * root names the authenticated container rather than an extracted directory. */
pxa_status_t pxa_posix_installer_verify_source(
    pxa_posix_installer_t *installer, const char *source_path,
    pxa_posix_installer_result_t *result);
pxa_status_t pxa_posix_installer_recover(
    pxa_posix_installer_t *installer,
    const pxa_posix_installer_identity_t *identity);
pxa_status_t pxa_posix_installer_load_current(
    pxa_posix_installer_t *installer,
    const pxa_posix_installer_identity_t *identity,
    pxa_posix_installer_result_t *result);
pxa_status_t pxa_posix_installer_uninstall(
    pxa_posix_installer_t *installer,
    const pxa_posix_installer_identity_t *identity);

#ifdef __cplusplus
}
#endif

#endif
