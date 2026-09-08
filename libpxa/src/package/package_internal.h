#ifndef PXA_PACKAGE_INTERNAL_H
#define PXA_PACKAGE_INTERNAL_H

#include "pxa/package.h"

int pxa_package_path_is_valid(pxa_bytes_t value);
pxa_status_t pxa_package_publisher_lineage_root(
    const pxa_package_manifest_t *manifest, const uint8_t **output);

#endif
