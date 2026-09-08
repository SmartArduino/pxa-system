#ifndef PXA_NET_INTERNAL_H
#define PXA_NET_INTERNAL_H

#include "pxa/net.h"

typedef struct {
    uint32_t max_inline_body_bytes;
    uint32_t max_request_header_bytes;
    uint32_t min_timeout_ms;
    uint32_t default_timeout_ms;
    uint32_t max_timeout_ms;
    uint16_t max_headers;
} pxa_net_request_limits_t;

typedef struct {
    pxa_net_request_t request;
    pxa_handle_t permission_handle;
    pxa_net_header_t headers[PXA_NET_MAX_HEADERS];
    pxa_bytes_t wanted_response_headers[PXA_NET_MAX_HEADERS];
    size_t request_header_bytes;
    uint16_t seen;
} pxa_net_parsed_request_t;

pxa_status_t pxa_net_request_parse(
    const pxa_net_request_limits_t *limits, uint16_t opcode,
    pxa_bytes_t payload, pxa_net_parsed_request_t *output);

pxa_status_t pxa_net_response_validate(
    uint16_t max_headers, uint32_t max_response_header_bytes,
    uint16_t opcode, uint32_t max_response_bytes,
    const pxa_net_response_t *response);
pxa_status_t pxa_net_response_encode(
    uint8_t *output, size_t capacity, uint16_t opcode,
    const pxa_net_response_t *response, pxa_handle_t handle,
    size_t *result_size);

#endif
