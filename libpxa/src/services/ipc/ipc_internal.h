#ifndef PXA_IPC_INTERNAL_H
#define PXA_IPC_INTERNAL_H

#include "pxa/ipc.h"

typedef struct {
    pxa_bytes_t endpoint;
    pxa_bytes_t payload;
} pxa_ipc_call_request_t;

typedef struct {
    uint32_t call_id;
    pxa_status_t status;
    pxa_bytes_t payload;
} pxa_ipc_reply_request_t;

pxa_status_t pxa_ipc_parse_call(pxa_bytes_t payload,
                                pxa_ipc_call_request_t *output);
pxa_status_t pxa_ipc_parse_reply(pxa_bytes_t payload,
                                 pxa_ipc_reply_request_t *output);

#endif
