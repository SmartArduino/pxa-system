#ifndef PXA_NET_H
#define PXA_NET_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/permission.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_NET_SERVICE_ID UINT16_C(9)
#define PXA_NET_SERVICE_MAJOR UINT16_C(0)
#define PXA_NET_SERVICE_MINOR UINT16_C(6)
#define PXA_NET_SERVICE_PATCH UINT16_C(0)
#define PXA_NET_FETCH UINT16_C(1)
#define PXA_NET_HTTP_REQUEST UINT16_C(2)
#define PXA_NET_HTTP_OPEN UINT16_C(3)
#define PXA_NET_CAPABILITIES UINT16_C(4)
#define PXA_NET_SOCKET_OPEN UINT16_C(5)
#define PXA_NET_CONNECTIVITY UINT16_C(6)
#define PXA_NET_CONNECTIVITY_UNKNOWN UINT32_C(0)
#define PXA_NET_CONNECTIVITY_OFFLINE UINT32_C(1)
#define PXA_NET_CONNECTIVITY_ONLINE UINT32_C(2)
#define PXA_NET_FEATURE_SOCKET UINT32_C(16)
#define PXA_NET_FEATURE_STREAM_DOWNLOAD UINT32_C(1)
#define PXA_NET_FEATURE_STREAM_UPLOAD UINT32_C(2)
#define PXA_NET_FEATURE_RESPONSE_HEADERS UINT32_C(4)
#define PXA_NET_FEATURE_LONG_STREAM_TIMEOUT UINT32_C(8)
#define PXA_NET_MAX_STREAM_TIMEOUT_MS UINT32_C(86400000)
#define PXA_NET_METHOD_GET UINT16_C(1)
#define PXA_NET_METHOD_HEAD UINT16_C(2)
#define PXA_NET_METHOD_POST UINT16_C(3)
#define PXA_NET_METHOD_PUT UINT16_C(4)
#define PXA_NET_METHOD_PATCH UINT16_C(5)
#define PXA_NET_METHOD_DELETE UINT16_C(6)
#define PXA_NET_IO_READ UINT32_C(1)
#define PXA_NET_MAX_URL_BYTES ((size_t)512)
#define PXA_NET_MAX_CONTENT_TYPE_BYTES ((size_t)96)
#define PXA_NET_MAX_HEADERS ((size_t)8)
#define PXA_NET_MAX_HEADER_NAME_BYTES ((size_t)64)
#define PXA_NET_MAX_HEADER_VALUE_BYTES ((size_t)256)
#define PXA_NET_MAX_HEADER_BLOCK_BYTES ((size_t)2048)

#define PXA_NET_RESPONSE_BODY_PRESENT UINT32_C(1)
#define PXA_NET_RESPONSE_BODY_LENGTH_KNOWN UINT32_C(2)

typedef struct {
    pxa_bytes_t name;
    pxa_bytes_t value;
} pxa_net_header_t;

typedef struct {
    uint32_t struct_size;
    pxa_bytes_t url;
    pxa_bytes_t origin;
    uint16_t method;
    uint16_t header_count;
    uint32_t max_response_bytes;
    uint32_t timeout_ms;
    const pxa_net_header_t *headers;
    pxa_bytes_t body;
    const pxa_bytes_t *wanted_response_headers;
    uint16_t wanted_response_header_count;
    uint16_t abi_minor;
    /* HTTP_OPEN: separate bounded storage and total transfer budgets. */
    uint32_t buffer_bytes;
    uint32_t response_header_bytes;
    uint64_t max_body_bytes;
    uint64_t upload_bytes;
} pxa_net_request_t;

typedef struct {
    uint32_t struct_size;
    uint16_t status_code;
    uint16_t header_count;
    pxa_bytes_t content_type;
    void *body_stream;
    const pxa_net_header_t *headers;
    uint64_t body_length;
    uint32_t flags;
} pxa_net_response_t;

/* A successful start must return a nonzero operation ID that is unique among
 * this backend's currently active requests. */
typedef pxa_status_t (*pxa_net_start_fn)(
    void *context, const pxa_net_request_t *request, uint64_t *operation);
typedef pxa_status_t (*pxa_net_poll_fn)(
    void *context, uint64_t operation, pxa_net_response_t *response);
typedef void (*pxa_net_cancel_fn)(void *context, uint64_t operation);
typedef pxa_status_t (*pxa_net_read_body_fn)(
    void *context, void *body_stream, uint8_t *output, size_t capacity,
    size_t *size);
typedef void (*pxa_net_close_body_fn)(void *context, void *body_stream);

typedef struct {
    pxa_bytes_t endpoint; /* tcp://host:port or tls://host:port; exact permission scope */
    pxa_bytes_t host; /* no brackets for IPv6 */
    uint32_t buffer_bytes; /* combined RX/TX, power of two, 512..65536 */
    uint32_t connect_timeout_ms;
    uint16_t port;
    uint8_t tls;
} pxa_socket_request_t;

typedef struct {
    uint32_t struct_size;
    void *context;
    pxa_net_start_fn start;
    pxa_net_poll_fn poll;
    pxa_net_cancel_fn cancel;
    pxa_net_read_body_fn read_body;
    pxa_net_close_body_fn close_body;
    /* Optional v0.4 exchange backend. OPEN copies all retained input before
     * returning; I/O never blocks on sockets. CLOSE transfers cleanup to the
     * backend and is safe before headers, during transfer and after EOF. */
    pxa_status_t (*open_exchange)(void *context, const pxa_net_request_t *request,
                                  void **exchange);
    int32_t (*exchange_io)(void *context, void *exchange, uint32_t operation,
                           uint8_t *data, size_t size);
    void (*close_exchange)(void *context, void *exchange);
    /* Outbound byte-stream TCP/TLS. OPEN copies retained input and returns
     * immediately; I/O never blocks. CLOSE transfers cleanup to the worker.
     * TLS always verifies the peer name and certificate; no insecure mode. */
    pxa_status_t (*open_socket)(void *, const pxa_socket_request_t *, void **);
    int32_t (*socket_io)(void *, void *, uint32_t, uint8_t *, size_t);
    void (*close_socket)(void *, void *);
    /* Local link/IP state only; no remote probe, DNS or private identifiers. */
    pxa_status_t (*connectivity)(void *, uint32_t *state);
} pxa_net_backend_t;

typedef struct {
    uint32_t struct_size;
    uint16_t max_pending_requests;
    uint16_t max_requests_per_component;
    uint16_t max_response_streams;
    uint16_t max_headers;
    uint32_t max_response_bytes;
    uint32_t max_inline_body_bytes;
    uint32_t max_request_header_bytes;
    uint32_t max_response_header_bytes;
    uint32_t min_timeout_ms;
    uint32_t default_timeout_ms;
    uint32_t max_timeout_ms;
    pxa_net_backend_t backend;
    pxa_permission_service_t *permissions;
} pxa_net_config_t;

typedef struct pxa_net_service pxa_net_service_t;

int pxa_net_parse_https_url(pxa_bytes_t url, pxa_bytes_t *origin);
int pxa_net_parse_web_url(pxa_bytes_t url, pxa_bytes_t *origin);
int pxa_net_header_name_valid(pxa_bytes_t name, int request_header);
int pxa_net_header_value_valid(pxa_bytes_t value);
size_t pxa_net_service_workspace_size(const pxa_net_config_t *config);
pxa_status_t pxa_net_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_net_config_t *config, pxa_net_service_t **output);
pxa_status_t pxa_net_service_register(pxa_net_service_t *service);
int pxa_net_has_pending_requests(const pxa_net_service_t *service);
pxa_status_t pxa_net_poll(
    pxa_net_service_t *service, pxa_component_t *affected, size_t capacity,
    size_t *count);

#ifdef __cplusplus
}
#endif

#endif
