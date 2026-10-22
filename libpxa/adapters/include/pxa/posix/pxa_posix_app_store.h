#ifndef PXA_POSIX_APP_STORE_H
#define PXA_POSIX_APP_STORE_H

#include "pxa/posix/pxa_posix_install_record.h"
#include "pxa/posix/pxa_posix_installer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_POSIX_APP_LOCATOR_BYTES 130u
#define PXA_POSIX_APP_PATH_BYTES 512u

typedef pxa_status_t (*pxa_posix_media_ready_fn)(void *, const pxa_install_record_t *);
struct pxa_posix_app_home;
typedef struct {
    /* This volume's PXA directory. Root/trust/callback pointers stay alive for
     * each operation. There is no singleton or mandatory primary volume. */
    const char *root;
    pxa_volume_id_t volume_id;
    pxa_posix_installer_config_t installer;
    void *media_context;
    pxa_posix_media_ready_fn media_ready;
    /* External records, including pending handoffs and discovery, are
     * authenticated with this provider and the actual home locator. */
    void *record_auth_context;
    pxa_posix_record_tag_fn record_tag;
    /* Reused across verification, recovery and commit phases, never freed by
     * the coordinator. One owner at a time. ESP requires this to keep the
     * installer scratch under the Host's PSRAM/budget policy. */
    void *installer_workspace;
    size_t installer_workspace_bytes;
    /* Idempotent preparation of a verified pending generation (for example,
     * its private filesystem). Runs after code/resource verification and
     * before the local record commits, including recovery after a reset.
     * Failure restores the previous executable while it is still retained.
     * Never changes the interpretation of existing private data. */
    void *prepare_context;
    pxa_status_t (*prepare_home)(void *,const struct pxa_posix_app_home *);
    /* Optional SHA-256 of the exact signed manifest approved by the Host.
     * Applies only to install(), not recovery of an existing generation. */
    const uint8_t *approved_manifest_sha256;
    /* Optional durable handoff gate. Runs only for an uncommitted pending
     * generation, after signed code, resources and private state are ready.
     * Cross-volume owners can retire the source and persist its proof here.
     * Recovery may repeat it: implementations must be idempotent and retain
     * their own authenticated receipt on uncertain failure. This hook never
     * authorizes execution or replaces record CAS/resource revalidation. */
    void *commit_context;
    pxa_status_t (*commit_ready)(void *, const struct pxa_posix_app_home *);
} pxa_posix_app_store_config_t;
size_t pxa_posix_app_store_workspace_size(const pxa_posix_app_store_config_t *);

typedef struct pxa_posix_app_home {
    pxa_install_record_t record;
    char locator[PXA_POSIX_APP_LOCATOR_BYTES];
    char home[PXA_POSIX_APP_PATH_BYTES];
} pxa_posix_app_home_t;
#define PXA_POSIX_APP_HOME_PACKAGE UINT8_C(1)
#define PXA_POSIX_APP_HOME_PRIVATE UINT8_C(2)
/* Resolve only the path currently needed. Retaining three copies of the home
 * prefix costs 1 KiB per snapshot, including nested recovery stack frames. */
pxa_status_t pxa_posix_app_home_path(const pxa_posix_app_home_t *,uint8_t kind,char *,size_t);

/* Owner-serialized operations. Caller supplies manifest/result buffers just as
 * for the signed installer. Locator is a Host-selected publisher/app pair,
 * never a Guest path. New install_id and media binding come from the approved
 * Host plan; signed identity, manifest digest and sequence are checked here.
 * Expected generation prevents a stale confirmation replacing a newer app.
 * An identical current release/policy is idempotent after full verification;
 * it preserves the record generation and does not copy or prepare data. */
pxa_status_t pxa_posix_app_store_install(const pxa_posix_app_store_config_t *,
    const char *locator, const char *source, uint64_t expected_generation,
    const pxa_install_record_t *approved, pxa_posix_installer_result_t *,
    pxa_posix_app_home_t *);
pxa_status_t pxa_posix_app_store_recover(const pxa_posix_app_store_config_t *,
    const char *locator, pxa_posix_installer_result_t *, pxa_posix_app_home_t *);
/* Authenticated local metadata for maintenance, without payload hashing.
 * Refuses an outstanding handoff. This does not authorize executing code. */
pxa_status_t pxa_posix_app_store_lookup(const pxa_posix_app_store_config_t *,
    const char *locator,pxa_posix_app_home_t *);
/* Host policy mutation on this volume. CAS against the local authenticated
 * record; refuses pending code handoffs. Does not change code/data locations.
 * A populated output on error means commit succeeded but mirroring failed;
 * retain old references and retry maintenance instead of reporting rollback. */
pxa_status_t pxa_posix_app_store_set_enabled(const pxa_posix_app_store_config_t *,
    const char *locator,uint64_t expected_generation,uint8_t enabled,pxa_posix_app_home_t *);
/* Idle home only. Rebind the same signed resource generation after the Host
 * has prepared it on another volume. Compares the complete approved record,
 * requires media_ready, and commits/mirrors only local metadata; code, private
 * data and permissions are untouched. A populated output on error identifies
 * a committed change whose mirror still needs retry. Keep the source resources
 * until both record slots are durable. No change is execution authority. */
pxa_status_t pxa_posix_app_store_rebind_media(const pxa_posix_app_store_config_t *,
    const char *locator,const pxa_install_record_t *approved,
    const pxa_volume_id_t *target_media,pxa_posix_app_home_t *);
/* prepare_home builds the NEXT data generation while the old one stays live.
 * Commit/mirror then atomically selects it, preserving code and install ID.
 * The Host may retire old data only after OK and after closing all old users.
 * Failure/uncertain sync requires reload before cleanup or retry. A populated
 * output on error identifies a durable commit awaiting mirror repair. */
pxa_status_t pxa_posix_app_store_reset_data(const pxa_posix_app_store_config_t *,
    const char *locator,uint64_t expected_generation,uint64_t expected_data_generation,pxa_posix_app_home_t *);
/* Idle installation only. Durably archive both local record slots before
 * retiring executable slots. Data/install ID remain available for reinstall.
 * A populated output on error identifies the committed archive; retry with
 * that generation. Callers may remove the whole home only after success. */
pxa_status_t pxa_posix_app_store_archive(const pxa_posix_app_store_config_t *,
    const char *locator,uint64_t expected_generation,pxa_posix_app_home_t *);
/* Discovery reads bounded local records only, without hashing all packages or
 * media. Records do not grant executable or permission authority. Activation
 * uses recover/load and the signed APP_HOME installer to authenticate code. */
typedef int (*pxa_posix_app_home_visitor_fn)(void *, const pxa_posix_app_home_t *);
pxa_status_t pxa_posix_app_store_visit(const pxa_posix_app_store_config_t *,
    pxa_posix_app_home_visitor_fn, void *, size_t *invalid_records);

#ifdef __cplusplus
}
#endif
#endif
