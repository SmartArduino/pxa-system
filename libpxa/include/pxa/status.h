#ifndef PXA_STATUS_H
#define PXA_STATUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef int32_t pxa_status_t;

#define PXA_STATUS_OK ((pxa_status_t)0)
#define PXA_STATUS_INVALID_ARGUMENT ((pxa_status_t)-1)
#define PXA_STATUS_BAD_STATE ((pxa_status_t)-2)
#define PXA_STATUS_UNSUPPORTED ((pxa_status_t)-3)
#define PXA_STATUS_DENIED ((pxa_status_t)-4)
#define PXA_STATUS_NOT_FOUND ((pxa_status_t)-5)
#define PXA_STATUS_BUSY ((pxa_status_t)-6)
#define PXA_STATUS_WOULD_BLOCK ((pxa_status_t)-7)
#define PXA_STATUS_QUOTA_EXCEEDED ((pxa_status_t)-8)
#define PXA_STATUS_RESOURCE_LIMIT ((pxa_status_t)-9)
#define PXA_STATUS_CANCELLED ((pxa_status_t)-10)
#define PXA_STATUS_INTERNAL ((pxa_status_t)-11)
#define PXA_STATUS_TIMED_OUT ((pxa_status_t)-12)
#define PXA_STATUS_UNAVAILABLE ((pxa_status_t)-13)
#define PXA_STATUS_IO_ERROR ((pxa_status_t)-14)
#define PXA_STATUS_PROTOCOL_ERROR ((pxa_status_t)-15)
#define PXA_STATUS_LIMIT_EXCEEDED ((pxa_status_t)-16)

/* Limit-status selection:
 * - QUOTA_EXCEEDED: a persistent Host/storage quota rejected the operation.
 * - RESOURCE_LIMIT: a Host memory allocation failed or output capacity is full.
 * - LIMIT_EXCEEDED: Guest-supplied data exceeds a protocol/service ceiling. */
int pxa_status_is_known(pxa_status_t status);

#ifdef __cplusplus
}
#endif

#endif
