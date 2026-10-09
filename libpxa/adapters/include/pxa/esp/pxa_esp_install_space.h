#ifndef PXA_ESP_INSTALL_SPACE_H
#define PXA_ESP_INSTALL_SPACE_H

#include "pxa/package.h"
#include <string.h>

/* Conservative LittleFS admission budget, not a reservation. Count decoded
 * files (compressed uploads can be much smaller), CTZ pointers, metadata and
 * directory pairs. Leave 128 KiB for commit/cleanup and concurrent small writes.
 * No new cache, installer workspace or per-app state is required. */
static inline uint64_t pxa_esp_install_space_required(
    const pxa_package_manifest_t *manifest, size_t manifest_bytes) {
    const uint64_t block = 4096;
    const uint64_t payload = block - 64;
    uint64_t needed = UINT64_C(128) * 1024;
    if (manifest == NULL) return UINT64_MAX;
    if ((uint64_t)manifest_bytes > UINT64_MAX - needed - 4 * block)
        return UINT64_MAX;
    needed += (uint64_t)manifest_bytes + 4 * block;
    for (size_t i = 0; i < manifest->file_count; ++i) {
        const pxa_package_file_t *file = &manifest->files[i];
        uint64_t blocks = file->size / payload + (file->size % payload != 0);
        if (blocks > UINT64_MAX / block) return UINT64_MAX;
        uint64_t bytes = blocks * block;
        if (needed > UINT64_MAX - block ||
            bytes > UINT64_MAX - needed - block) return UINT64_MAX;
        /* One extra block per file bounds inline data and entry metadata;
         * directory pairs and compaction headroom are accounted separately. */
        needed += bytes + block;
        /* Count unique directory prefixes directly in the verified manifest.
         * This is a cold install path; no temporary path set is allocated. */
        for (size_t j = 0; j < file->path.size; ++j) {
            if (file->path.data[j] == '/') {
                int seen = 0;
                for (size_t k = 0; k < i; ++k) {
                    pxa_bytes_t other = manifest->files[k].path;
                    if (other.size > j && other.data[j] == '/' &&
                        memcmp(other.data, file->path.data, j) == 0) {
                        seen = 1;
                        break;
                    }
                }
                if (seen) continue;
                if (needed > UINT64_MAX - 2 * block) return UINT64_MAX;
                needed += 2 * block;
            }
        }
    }
    return needed;
}

static inline int pxa_esp_install_space_available(
    uint64_t required, size_t total, size_t used) {
    return required != UINT64_MAX && used <= total && required <= total - used;
}

#endif
