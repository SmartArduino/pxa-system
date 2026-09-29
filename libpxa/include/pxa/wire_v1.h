#ifndef PXA_WIRE_V1_H
#define PXA_WIRE_V1_H

#include "pxa/wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Core v1 envelope codec. The current runtime admits Log, Device runtime
 * info and GameRender contexts; other service payloads remain gated. */
#define PXA_V1_ENVELOPE_SIZE PXA_WIRE_V1_SIZE

typedef struct {
    uint16_t service;
    uint16_t opcode;
    uint64_t request_token;
    pxa_bytes_t payload;
} pxa_v1_message_view_t;

pxa_status_t pxa_v1_message_decode(const uint8_t *data, size_t size,
                                   size_t max_size,
                                   pxa_v1_message_view_t *output);
pxa_status_t pxa_v1_writer_message(pxa_writer_t *writer, uint16_t service,
                                   uint16_t opcode, uint64_t request_token,
                                   const void *payload, size_t payload_size);

#ifdef __cplusplus
}
#endif

#endif
