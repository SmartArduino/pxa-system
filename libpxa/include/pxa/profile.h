#ifndef PXA_PROFILE_H
#define PXA_PROFILE_H

#include "pxa/package.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Encode one bounded diagnostic JSON record. Record 0 describes the Host;
 * records 1..service_count describe the actual activation service table.
 * Caller owns the output; no heap allocation or retained state. */
pxa_status_t pxa_package_profile_record(
    const pxa_package_host_profile_t *host,
    const pxa_package_activation_profile_t *activation,
    int wasm_enabled, size_t record, char *output, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
