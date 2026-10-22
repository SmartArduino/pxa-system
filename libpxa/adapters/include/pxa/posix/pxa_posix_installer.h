#ifndef PXA_POSIX_INSTALLER_H
#define PXA_POSIX_INSTALLER_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/openssl/pxa_openssl.h"
#include "pxa/package.h"
#include "pxa/container.h"
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
#define PXA_POSIX_INSTALLER_RECEIVING_PREFIX ".receiving-"

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

/* Permit a host-controlled migration of legacy private-data roots only when
 * they contain no user data. An empty .pxa-storage directory is allowed so
 * hosts can recover from an interrupted first launch. */
#define PXA_POSIX_INSTALLER_FLAG_CLAIM_EMPTY_UNOWNED_DATA ((uint32_t)1u << 3)

/* One self-contained application home on the chosen volume. Current code is
 * home/package, private files are home/private, owner/session/lock records are
 * home-local. No shared locator registry is consulted. Hosts discover homes
 * from volume-local install records. Mutable removable code is reverified on
 * load. The home must already exist and is dedicated to one signed identity. */
#define PXA_POSIX_INSTALLER_FLAG_APP_HOME ((uint32_t)1u << 4)
/* The app-home coordinator commits its volume/media record before retiring
 * previous code. finish() explicitly completes or rolls back that handoff. */
#define PXA_POSIX_INSTALLER_FLAG_RETAIN_PREVIOUS ((uint32_t)1u << 5)

#define PXA_POSIX_INSTALLER_PATH_PACKAGE 1u
#define PXA_POSIX_INSTALLER_PATH_DATA 2u
/* Host control-plane hooks. NOT_FOUND preserves the legacy path; every other
 * failure denies access. A locator contains one or two safe components, and
 * never grants ownership. incoming_ready sees only a fully verified directory.
 * Hosts serialize these hooks with their durable identity registry. */
typedef pxa_status_t (*pxa_posix_installer_locator_fn)(void *,
    const pxa_posix_installer_identity_t *,uint8_t,char *,size_t);
typedef pxa_status_t (*pxa_posix_installer_incoming_fn)(void *,
    const pxa_posix_installer_identity_t *,const char *);
/* Runs after source authentication and before mutation. Host checks the
 * selected filesystem's geometry and admission lease; adopted data needs
 * metadata headroom rather than a second payload. APP_HOME on ESP requires
 * this hook, so an external install never checks the default Flash partition. */
typedef pxa_status_t (*pxa_posix_installer_space_fn)(void *, const char *home,
    const pxa_package_manifest_t *, size_t manifest_bytes, uint8_t already_staged);

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
    /* Optional full adapter context. When omitted, the OpenSSL-compatible
     * trust prefix above is used. ESP supplies this for per-install package
     * state such as Manifest 0.5's transient publisher SPKI. */
    void *verify_context;
    /* Optional tail, guarded by struct_size for existing callers. */
    void *layout_context;
    pxa_posix_installer_locator_fn resolve_locator;
    pxa_posix_installer_incoming_fn incoming_ready;
    void *space_context;
    pxa_posix_installer_space_fn check_space;
    /* Optional immutable-generation policy, checked together with EVERY
     * authenticated manifest, including source inspection and recovery.
     * Digest is copied at init. Zero kind accepts either kind. Dedicated
     * resource homes bind both RESOURCES and the application's signed hash. */
    const uint8_t *expected_manifest_sha256;
    uint8_t expected_package_kind;
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
/* Transfer ownership of storage_root/inbox/<name> into the incoming slot by
 * rename, without duplicating its decoded files. name must be one safe path
 * component; containers and cross-filesystem sources are not accepted. The
 * caller must serialize all writes to this staging directory until return.
 * The normal signature, inventory, file-digest, identity, lineage and recovery
 * checks still apply. The current installation/private data survive rejection.
 * Once adopted, the source may be absent even on failure: retry by restaging
 * or recovering the transaction. ALREADY_CURRENT leaves the source in place.
 * No persistent cache or additional installer workspace is allocated. */
pxa_status_t pxa_posix_installer_adopt_staged_directory(
    pxa_posix_installer_t *installer, const char *name,
    const pxa_posix_installer_identity_t *expected,
    pxa_posix_installer_result_t *result,
    pxa_posix_install_disposition_t *disposition);
/* Upload into inbox/.receiving-<app-id>[.pxa], then fully authenticate and
 * publish by rename. Only the matching App ID is accepted. A failed publish
 * discards its uncommitted source; installed packages/private data are untouched.
 * Serialize all inbox writers with these operations. */
pxa_status_t pxa_posix_installer_publish_transfer(
    pxa_posix_installer_t *, const char *app_id, pxa_posix_installer_result_t *);
/* Remove reserved, unpublished transfer sources. Call after the transport has
 * closed its upload file, or at cold start before accepting transfers. */
pxa_status_t pxa_posix_installer_discard_transfers(pxa_posix_installer_t *);
/* Fully authenticate a read-only Package directory or single-file `.pxa`
 * source without copying it into the managed slots. For a container, result
 * root names the authenticated container rather than an extracted directory. */
pxa_status_t pxa_posix_installer_verify_source(
    pxa_posix_installer_t *installer, const char *source_path,
    pxa_posix_installer_result_t *result);
/* Authenticate only the signed logical manifest, for identity/permission
 * inspection. Does not hash files or authenticate container compression
 * metadata. Raw contents MUST still pass verify_source/install before use.
 * Result buffers have the same lifetime rules as verify_source. */
pxa_status_t pxa_posix_installer_read_signed_manifest(
    pxa_posix_installer_t *installer, const char *source_path,
    pxa_posix_installer_result_t *result);
/* Bounded in-memory prefix from the Store install-metadata endpoint: exactly
 * header + manifest + both signature envelopes, without the payload. No I/O,
 * directory creation or allocation. Validates publisher/lineage and logical
 * file sizes for pre-download planning; raw compression/container bytes still
 * require full install verification. Output root is empty. Result buffers
 * own the returned manifest and may reuse the prefix buffer in place. As with
 * other result APIs, root must name caller storage; one writable byte suffices
 * for this metadata-only operation's empty root. */
pxa_status_t pxa_posix_installer_read_install_metadata(pxa_posix_installer_t *,
    pxa_bytes_t prefix,uint64_t expected_container_bytes,
    pxa_posix_installer_result_t *,pxa_container_header_t *);
/* Optional cooperative directory verification. begin reads/parses metadata;
 * successive steps authenticate that exact manifest, check the inventory,
 * then hash at most byte_budget bytes per call. Metadata stages each occupy
 * one step and are not bounded by byte_budget. WOULD_BLOCK means unfinished.
 * Result buffers are untrusted until step returns OK; keep them and the installer alive
 * until close. Single owner: serialize steps with other installer operations
 * and prevent the source from being replaced before activation. No worker,
 * persistent cache or extra I/O buffer is created. close also cancels a job. */
typedef struct pxa_posix_verification pxa_posix_verification_t;
pxa_status_t pxa_posix_installer_verify_begin(pxa_posix_installer_t *,
    const char *directory, pxa_posix_installer_result_t *, pxa_posix_verification_t **);
pxa_status_t pxa_posix_installer_verify_step(pxa_posix_verification_t *, size_t byte_budget);
void pxa_posix_installer_verify_close(pxa_posix_verification_t *);
/* Parse an already installed/prepared directory. No signature or file SHA
 * verification. Caller owns its trust decision; raw incoming packages must
 * first use install/verify_source. Used by the simulator's prepared app path. */
pxa_status_t pxa_posix_installer_load_directory(pxa_posix_installer_t *,
    const char *directory, pxa_posix_installer_result_t *);
pxa_status_t pxa_posix_installer_recover(
    pxa_posix_installer_t *installer,
    const pxa_posix_installer_identity_t *identity);
/* Cold-start recovery of abandoned session directories, including legacy
 * identities with owner records. Call before accepting installs/writers.
 * Successful recovery removes obsolete incoming/previous/session storage.
 * Unrecoverable data is preserved and reported, rather than guessed away. */
pxa_status_t pxa_posix_installer_recover_pending(pxa_posix_installer_t *);
pxa_status_t pxa_posix_installer_finish(pxa_posix_installer_t *,
    const pxa_posix_installer_identity_t *, uint8_t rollback);
/* Read the committed manifest without revalidating installed contents. */
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
