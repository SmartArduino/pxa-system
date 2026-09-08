#ifndef PXA_WASI_H
#define PXA_WASI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Capability-only service describing the WASI Preview 1 environment made
 * available to one Component. It has no pxa_control opcodes. */
#define PXA_WASI_SERVICE_ID UINT16_C(14)
#define PXA_WASI_SERVICE_MAJOR UINT16_C(0)
#define PXA_WASI_SERVICE_MINOR UINT16_C(1)
#define PXA_WASI_SERVICE_PATCH UINT16_C(0)

#define PXA_WASI_FEATURE_STDIO UINT64_C(1)
#define PXA_WASI_FEATURE_MONOTONIC_CLOCK (UINT64_C(1) << 1)
#define PXA_WASI_FEATURE_WALL_CLOCK (UINT64_C(1) << 2)
#define PXA_WASI_FEATURE_RANDOM (UINT64_C(1) << 3)
#define PXA_WASI_FEATURE_ARGUMENTS (UINT64_C(1) << 4)
#define PXA_WASI_FEATURE_ENVIRONMENT (UINT64_C(1) << 5)
#define PXA_WASI_FEATURE_PRIVATE_FS (UINT64_C(1) << 6)

/* Preview 1 exposes all clock IDs through the same imports. Hosts and
 * Components therefore authorize the two ambient clock classes together. */
#define PXA_WASI_FEATURE_CLOCKS                       \
    (PXA_WASI_FEATURE_MONOTONIC_CLOCK |              \
     PXA_WASI_FEATURE_WALL_CLOCK)

#ifdef __cplusplus
}
#endif

#endif
