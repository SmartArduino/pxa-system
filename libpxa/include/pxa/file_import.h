/* SPDX-License-Identifier: MIT */
#ifndef PXA_FILE_IMPORT_H
#define PXA_FILE_IMPORT_H
#include "pxa/fs.h"

/* Trusted Host selection. The UI owns this list until the job is finished;
 * source paths are never accepted from or returned to a Guest. */
typedef struct { uint64_t epoch; char *path; } pxa_file_choice_t;
typedef struct {
    void *context;
    pxa_fs_backend_t source,destination;
    pxa_status_t (*open_source)(void *,const pxa_file_choice_t *,void **,pxa_fs_entry_t *);
    pxa_status_t (*destination_ready)(void *);
    pxa_status_t (*flush_destination)(void *,void *);
    int (*same_file)(void *,const pxa_file_choice_t *);
    /* Zero leaves the scoped path ceiling. FAT counts UTF-16 units; POSIX
     * byte-name filesystems set maximum_name_bytes instead. */
    uint16_t maximum_name_bytes,maximum_name_utf16_units;
} pxa_file_import_config_t;
typedef struct {
    pxa_file_import_config_t config;
    const pxa_file_choice_t *choices;
    size_t count,index;
    void *input,*output;
    uint64_t expected,copied,total_copied,nonce;
    uint32_t suffix;
    uint8_t active,temporary_created;
    char temporary[64],name[PXA_FS_SCOPED_MAX_PATH_BYTES+1];
    uint8_t buffer[4096];
    pxa_fs_import_summary_t result;
} pxa_file_import_t;
int pxa_file_import_matches(const char *name,const char *extensions);
pxa_status_t pxa_file_import_begin(pxa_file_import_t *,const pxa_file_import_config_t *,
    const pxa_file_choice_t *,size_t count,uint64_t nonce);
/* Bounded work, at most chunks * 4096 data bytes. Returns WOULD_BLOCK until
 * finished. A failed file is discarded and counted; the next file proceeds.
 * Complete files publish individually and are retained when a batch cancels. */
pxa_status_t pxa_file_import_step(pxa_file_import_t *,unsigned chunks);
void pxa_file_import_cancel(pxa_file_import_t *);
#endif
