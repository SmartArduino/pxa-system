#ifndef PXA_POSIX_ASSET_H
#define PXA_POSIX_ASSET_H

#include "pxa/asset_loader.h"
#include "pxa/posix/pxa_posix_storage_gate.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int fd;
    pxa_posix_storage_gate_t *gate;
    unsigned storage_lane;
    void *cancel_context;
    int (*cancelled)(void *);
    uint64_t expected_bytes;
    uint64_t read_bytes;
    uint64_t file_bytes;
    uint64_t position;
} pxa_posix_asset_input_t;

/* Worker-only. Opens each path component relative to a directory descriptor,
 * rejects symlinks and non-regular files, and checks the signed byte length.
 * root is trusted Host configuration, path/size come from the signed catalog.
 * Close after success AND any later loader error. */
pxa_status_t pxa_posix_asset_open(pxa_posix_asset_input_t *stream,
                                  const char *root, pxa_bytes_t path,
                                  uint64_t expected_bytes,
                                  pxa_asset_input_t *input);
void pxa_posix_asset_close(pxa_posix_asset_input_t *stream);
/* Same confined open/full signed-length check, then positions the file at
 * one bounded range. Installed contents are not hashed again. */
pxa_status_t pxa_posix_asset_open_range(pxa_posix_asset_input_t *stream,
    const char *root, pxa_bytes_t path, uint64_t file_bytes,
    uint32_t offset, uint32_t bytes, pxa_asset_input_t *input);
/* Reuses the confined descriptor for subsequent blocks. Resets read
 * accounting, never reopens a potentially replaced path. Worker-only. */
pxa_status_t pxa_posix_asset_select_range(pxa_posix_asset_input_t *,
    uint32_t offset, uint32_t bytes);

#ifdef __cplusplus
}
#endif
#endif
