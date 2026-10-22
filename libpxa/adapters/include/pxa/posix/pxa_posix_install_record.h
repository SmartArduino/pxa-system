#ifndef PXA_POSIX_INSTALL_RECORD_H
#define PXA_POSIX_INSTALL_RECORD_H

#include "pxa/install_record.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_POSIX_INSTALL_RECORD_SEALED_BYTES (PXA_INSTALL_RECORD_BYTES+40u)
#define PXA_POSIX_INSTALL_RECORD_SEALED_V1_BYTES (PXA_INSTALL_RECORD_V1_BYTES+40u)
typedef pxa_status_t (*pxa_posix_record_tag_fn)(void *,pxa_bytes_t purpose,
    pxa_bytes_t binding,pxa_bytes_t body,uint8_t tag[32]);
typedef struct {
    void *context;
    /* Host-selected canonical home locator. The record itself also binds
     * volume, install UUID, publisher root, App ID and exact generation. */
    pxa_bytes_t binding;
    pxa_posix_record_tag_fn make_tag;
} pxa_posix_install_record_auth_t;
/* NULL auth uses plain PXIR records on a trusted filesystem. With auth the
 * fixed PXIS envelope is mandatory, including for pending records; a bare CRC
 * record never silently upgrades to authority on removable storage. */
pxa_status_t pxa_posix_install_record_pack(const pxa_install_record_t *,
    const pxa_posix_install_record_auth_t *,
    uint8_t output[PXA_POSIX_INSTALL_RECORD_SEALED_BYTES],size_t *bytes);
pxa_status_t pxa_posix_install_record_unpack(pxa_bytes_t,
    const pxa_volume_id_t *,const pxa_posix_install_record_auth_t *,pxa_install_record_t *);
pxa_status_t pxa_posix_install_record_load_with_auth(int app_directory,
    const pxa_volume_id_t *,const pxa_posix_install_record_auth_t *,pxa_install_record_t *);
pxa_status_t pxa_posix_install_record_commit_with_auth(int app_directory,
    uint64_t expected_generation,const pxa_install_record_t *,const pxa_posix_install_record_auth_t *);
/* After a commit, mirror the exact authoritative encoding into the other
 * slot before retiring its old code/data references. CAS checks the complete
 * record. A damaged mirror leaves the committed source slot untouched; v1
 * mirrors stay v1 instead of creating conflicting formats at one generation. */
pxa_status_t pxa_posix_install_record_mirror(int app_directory,
    const pxa_install_record_t *,const pxa_posix_install_record_auth_t *);

/* app_directory is a Host-owned, no-follow directory handle. Calls must be
 * serialized by its volume owner. Both slots live on the application volume;
 * no central database, NVS, side-volume journal or rename is required. */
pxa_status_t pxa_posix_install_record_load(int app_directory,
    const pxa_volume_id_t *, pxa_install_record_t *);
/* New records start at generation one. Updates require an exact generation
 * match and preserve install identity. A failed sync is an uncertain commit:
 * reload/recover before retrying, and retain referenced package generations. */
pxa_status_t pxa_posix_install_record_commit(int app_directory,
    uint64_t expected_generation, const pxa_install_record_t *);

#ifdef __cplusplus
}
#endif
#endif
