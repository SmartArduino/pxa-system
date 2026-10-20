#ifndef PXA_EVENT_PORT_H
#define PXA_EVENT_PORT_H

#include "pxa/runtime.h"

/* Core 2.1 optional, exclusive foreground main-loop execution.
 * OPEN: empty payload, nonzero token; result status:i32, handle:u64.
 * WAIT: caller buffer begins with timeout_us:u64; on success the complete
 * standard event envelope replaces it, and the import returns its byte size.
 * A short buffer leaves the queued event and caller bytes unchanged. */
#ifndef PXA_CORE_OPEN_EVENT_PORT
#define PXA_CORE_OPEN_EVENT_PORT UINT16_C(5)
#define PXA_EVENT_PORT_IO_WAIT UINT32_C(0x201)
#define PXA_EVENT_PORT_MAX_WAIT_US UINT64_C(60000000)
#endif

#endif
