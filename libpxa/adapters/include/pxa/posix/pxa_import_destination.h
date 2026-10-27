/* SPDX-License-Identifier: MIT */
#ifndef PXA_IMPORT_DESTINATION_H
#define PXA_IMPORT_DESTINATION_H
#include "pxa/fs.h"
/* Host-only streaming destination. One open regular file, no directory scan,
 * no private-save quota. Caller supplies its verified app-media directory and
 * retains the root pathname and checks the retained volume epoch before every copy step. Single owner. */
typedef struct { int root_fd,file_fd; uint8_t open; const char *root_path; } pxa_import_destination_t;
pxa_status_t pxa_import_destination_open(pxa_import_destination_t *,const char *,pxa_fs_backend_t *);
pxa_status_t pxa_import_destination_flush(pxa_import_destination_t *,void *);
void pxa_import_destination_close(pxa_import_destination_t *);
#endif
