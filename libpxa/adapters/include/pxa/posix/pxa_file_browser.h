/* SPDX-License-Identifier: MIT */
#ifndef PXA_FILE_BROWSER_H
#define PXA_FILE_BROWSER_H
#include "pxa/posix/pxa_scoped_fs.h"
/* Browser rows have caller-owned names, independent of directory cursors.
 * A NULL extension filter enumerates directories only; "" includes all files. */
typedef pxa_status_t (*pxa_file_browser_emit_fn)(void *,size_t,uint8_t,pxa_bytes_t);
pxa_status_t pxa_file_browser_page(pxa_scoped_fs_t *,const pxa_fs_backend_t *,
    uint64_t,const char *,const char *,size_t,size_t,size_t *,uint8_t *,
    pxa_file_browser_emit_fn,void *);
#endif
