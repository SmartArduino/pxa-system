#include "pxa/http_stream.h"
#include "pxa/lease.h"
#include "pxa/net.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int32_t stream_io(void *context, uint32_t operation, uint8_t *data,
                         size_t size) {
    return pxa_http_stream_io(context, operation, data, size);
}
static pxa_status_t command(pxa_runtime_t *runtime, pxa_component_t owner,
                            uint16_t operation, uint64_t token, uint64_t target,
                            uint32_t interests) {
    uint8_t payload[12];
    pxa_message_view_t message;
    memset(&message, 0, sizeof(message));
    pxa_write_u64(payload, target);
    pxa_write_u32(payload + 8, interests);
    message.service = PXA_SERVICE_CORE;
    message.opcode = operation;
    message.request_token = token;
    message.payload =
        (pxa_bytes_t){payload, operation == PXA_CORE_WAIT_RESOURCE ? 12 : 8};
    return pxa_runtime_control_view(runtime, owner, &message);
}
static void consume(pxa_runtime_t *runtime, pxa_component_t owner, uint64_t token,
                    pxa_status_t status, uint32_t ready) {
    pxa_event_view_t view;
    uint8_t data[40];
    size_t size;
    assert(pxa_event_peek(runtime, owner, &view) == PXA_STATUS_OK);
    assert(pxa_event_read(runtime, view.token, 0, data, sizeof(data), &size) ==
           PXA_STATUS_OK);
    assert(pxa_read_u64(data + 4) == token &&
           (int32_t)pxa_read_u32(data + 20) == status);
    if (status == PXA_STATUS_OK)
        assert(pxa_read_u32(data + 24) & ready);
    assert(pxa_event_consume(runtime, owner, view.token) == PXA_STATUS_OK);
}
static void test_waits(void) {
    pxa_runtime_limits_t limits;
    pxa_lease_limits_t lease_limits;
    pxa_runtime_t *runtime;
    pxa_lease_service_t *leases;
    void *workspace, *lease_workspace;
    size_t size, count;
    pxa_component_t owner, other, affected[2];
    pxa_handle64_t handle;
    pxa_http_stream_t stream;
    uint8_t storage[512 + 1024];
    const pxa_resource_ops_t ops = {sizeof(ops), stream_io};
    pxa_resource_t resource = {&stream, &ops, NULL};
    pxa_event_view_t event;
    pxa_runtime_limits_init(&limits);
    limits.max_components = 2;
    limits.max_requests = 4;
    limits.max_requests_per_component = 4;
    limits.max_handles = 4;
    limits.max_services = 2;
    size = pxa_runtime_workspace_size(&limits);
    workspace = malloc(size);
    assert(pxa_runtime_init(workspace, size, &limits, &runtime) == PXA_STATUS_OK);
    pxa_lease_limits_init(&lease_limits);
    lease_limits.max_leases = 2;
    size = pxa_lease_service_workspace_size(&lease_limits);
    lease_workspace = malloc(size);
    assert(pxa_lease_service_init(lease_workspace, size, runtime, &lease_limits,
                                  &leases) == PXA_STATUS_OK);
    assert(pxa_lease_service_register(leases) == PXA_STATUS_OK);
    assert(pxa_component_create(runtime, 1, &owner) == PXA_STATUS_OK);
    assert(pxa_component_begin_start(runtime, owner) == PXA_STATUS_OK);
    assert(pxa_component_set_wire_profile(runtime, owner, 1) == PXA_STATUS_OK);
    pxa_http_stream_init(&stream, storage, 512, 1024, 4096, 0, PXA_NET_METHOD_GET);
    assert(pxa_handle_open64(runtime, owner, PXA_RESOURCE_STREAM, 0, &resource,
                             &handle) == PXA_STATUS_OK);
    assert(command(runtime, owner, PXA_CORE_WAIT_RESOURCE, 1, handle, PXA_READY_READ) ==
           PXA_STATUS_OK);
    assert(pxa_event_peek(runtime, owner, &event) == PXA_STATUS_BAD_STATE);
    assert(pxa_lease_has_active(leases));
    assert(pxa_component_finish_start(runtime, owner, PXA_STATUS_OK) == PXA_STATUS_OK);
    assert(pxa_lease_revoke_expired(leases, affected, 2, &count) == PXA_STATUS_OK &&
           count == 0);
    storage[0] = 42;
    pxa_http_ring_advance(&stream.receive, 1, 1);
    assert(pxa_lease_revoke_expired(leases, affected, 2, &count) == PXA_STATUS_OK &&
           count == 1);
    consume(runtime, owner, 1, PXA_STATUS_OK, PXA_READY_READ);
    assert(!pxa_lease_has_active(leases));
    assert(pxa_component_begin_event(runtime, owner) == PXA_STATUS_OK);
    assert(command(runtime, owner, PXA_CORE_WAIT_RESOURCE, 2, handle, PXA_READY_READ) ==
           PXA_STATUS_OK);
    consume(runtime, owner, 2, PXA_STATUS_OK, PXA_READY_READ); /* already ready */
    assert(pxa_http_stream_io(&stream, PXA_IO_READ, storage + 100, 1) == 1);
    assert(command(runtime, owner, PXA_CORE_WAIT_RESOURCE, 3, handle, PXA_READY_READ) ==
           PXA_STATUS_OK);
    assert(command(runtime, owner, PXA_CORE_CANCEL_REQUEST, 0, 3, 0) == PXA_STATUS_OK);
    consume(runtime, owner, 3, PXA_STATUS_CANCELLED, 0);
    /* Reusing a cancelled token must not inherit the old resource wait. */
    assert(command(runtime, owner, PXA_CORE_WAIT_RESOURCE, 3, handle,
                   PXA_READY_HEADERS) == PXA_STATUS_OK);
    pxa_http_stream_headers(&stream, 200, UINT64_MAX);
    assert(pxa_lease_revoke_expired(leases, affected, 2, &count) == PXA_STATUS_OK &&
           count == 1);
    consume(runtime, owner, 3, PXA_STATUS_OK, PXA_READY_HEADERS);
    assert(command(runtime, owner, PXA_CORE_WAIT_RESOURCE, 4, handle, PXA_READY_READ) ==
           PXA_STATUS_OK);
    assert(pxa_handle_close64(runtime, owner, handle) == PXA_STATUS_OK);
    assert(pxa_lease_revoke_expired(leases, affected, 2, &count) == PXA_STATUS_OK &&
           count == 1);
    consume(runtime, owner, 4, PXA_STATUS_NOT_FOUND, 0);
    assert(pxa_component_finish_event(runtime, owner, 0) == PXA_STATUS_OK);
    assert(pxa_component_create(runtime, 2, &other) == PXA_STATUS_OK);
    assert(pxa_component_begin_start(runtime, other) == PXA_STATUS_OK);
    assert(pxa_component_set_wire_profile(runtime, other, 1) == PXA_STATUS_OK);
    assert(command(runtime, other, PXA_CORE_WAIT_RESOURCE, 5, handle, PXA_READY_READ) ==
           PXA_STATUS_OK);
    assert(pxa_component_finish_start(runtime, other, PXA_STATUS_OK) == PXA_STATUS_OK);
    consume(runtime, other, 5, PXA_STATUS_NOT_FOUND, 0);
    pxa_runtime_deinit(runtime);
    free(lease_workspace);
    free(workspace);
}
static void test_ring(void) {
    uint8_t storage[512 + 1024], input[257], output[257];
    pxa_http_stream_t s;
    unsigned round;
    uint64_t position = 0;
    pxa_http_stream_init(&s, storage, 512, 1024, UINT64_MAX, 0, PXA_NET_METHOD_GET);
    s.receive.read_position = UINT32_MAX - 255u;
    s.receive.write_position = UINT32_MAX - 255u;
    for (round = 0; round < 10000; ++round) {
        uint8_t *block;
        size_t n, i;
        for (i = 0; i < sizeof(input); ++i)
            input[i] = (uint8_t)(position + i);
        n = pxa_http_ring_peek(&s.receive, &block, 1);
        if (n > sizeof(input))
            n = sizeof(input);
        memcpy(block, input, n);
        pxa_http_ring_advance(&s.receive, n, 1);
        assert(pxa_http_stream_io(&s, PXA_IO_READ, output, n) == (int32_t)n);
        assert(!memcmp(input, output, n));
        position += n;
    }
    assert(pxa_http_stream_io(&s, PXA_IO_READ, output, sizeof(output)) ==
           PXA_STATUS_WOULD_BLOCK);
    assert(pxa_http_stream_header(&s, (const uint8_t *)"Set-Cookie", 10,
                                  (const uint8_t *)"a=1", 3));
    assert(pxa_http_stream_header(&s, (const uint8_t *)"set-cookie", 10,
                                  (const uint8_t *)"b=2", 3));
    pxa_http_stream_headers(&s, 200, UINT64_MAX);
    assert(pxa_http_stream_io(&s, PXA_HTTP_IO_RESPONSE_SIZE, NULL, 0) == 50);
    assert(pxa_http_stream_io(&s, PXA_HTTP_IO_RESPONSE, output, 49) ==
           PXA_STATUS_LIMIT_EXCEEDED);
    pxa_http_stream_done(&s, PXA_STATUS_OK);
    assert(pxa_http_stream_io(&s, PXA_IO_READ, output, sizeof(output)) == 0);
    pxa_http_stream_init(&s, storage, 512, 1024, 4096, 512, PXA_NET_METHOD_POST);
    assert(pxa_http_stream_io(&s, PXA_HTTP_IO_FINISH_UPLOAD, NULL, 0) ==
           PXA_STATUS_BAD_STATE);
    assert(pxa_http_stream_io(&s, PXA_IO_WRITE, input, 257) == 256);
    assert(pxa_http_stream_io(&s, PXA_IO_WRITE, input, 1) == PXA_STATUS_WOULD_BLOCK);
    assert(!(pxa_http_stream_io(&s, PXA_IO_POLL, NULL, 0) & PXA_READY_WRITE));
    pxa_http_ring_advance(&s.send, 256, 0);
    assert(pxa_http_stream_io(&s, PXA_IO_WRITE, input, 256) == 256);
    assert(pxa_http_stream_io(&s, PXA_HTTP_IO_FINISH_UPLOAD, NULL, 0) == 0);
    assert(pxa_http_stream_io(&s, PXA_IO_WRITE, input, 1) == PXA_STATUS_BAD_STATE);
    pxa_http_stream_done(&s, PXA_STATUS_IO_ERROR);
    assert(pxa_http_stream_io(&s, PXA_IO_READ, output, sizeof(output)) ==
           PXA_STATUS_IO_ERROR);
    assert(!pxa_http_stream_storage_size(513, 1024));
}
typedef struct {
    pxa_http_stream_t stream;
    uint8_t storage[512 + 4096];
    unsigned opens, closes, starts, cancels;
    int fail_open;
} exchange_backend_t;
static pxa_status_t allow(void *context, pxa_bytes_t id, pxa_bytes_t name,
                          pxa_bytes_t scope, pxa_permission_decision_t *decision) {
    (void)context;
    (void)id;
    (void)name;
    (void)scope;
    *decision = PXA_PERMISSION_ALLOW;
    return PXA_STATUS_OK;
}
static pxa_status_t save(void *context, pxa_bytes_t id, pxa_bytes_t name,
                         pxa_bytes_t scope, pxa_permission_decision_t decision) {
    (void)context;
    (void)id;
    (void)name;
    (void)scope;
    (void)decision;
    return PXA_STATUS_OK;
}
static pxa_status_t legacy_start(void *context, const pxa_net_request_t *request,
                                 uint64_t *operation) {
    exchange_backend_t *b = context;
    (void)request;
    *operation = ++b->starts;
    return PXA_STATUS_OK;
}
static pxa_status_t legacy_poll(void *context, uint64_t operation,
                                pxa_net_response_t *response) {
    (void)context;
    (void)operation;
    (void)response;
    return PXA_STATUS_WOULD_BLOCK;
}
static void legacy_cancel(void *context, uint64_t operation) {
    exchange_backend_t *b = context;
    (void)operation;
    ++b->cancels;
}
static pxa_status_t legacy_read(void *context, void *stream, uint8_t *output,
                                size_t size, size_t *read) {
    (void)context;
    (void)stream;
    (void)output;
    (void)size;
    *read = 0;
    return PXA_STATUS_OK;
}
static void legacy_close(void *context, void *stream) {
    (void)context;
    (void)stream;
}
static pxa_status_t exchange_open(void *context, const pxa_net_request_t *request,
                                  void **exchange) {
    exchange_backend_t *b = context;
    assert(request->abi_minor == 2 && request->buffer_bytes == 512 &&
           request->response_header_bytes == 4096);
    assert(request->max_body_bytes == 1024 * 1024); /* independent of legacy cap */
    assert(request->timeout_ms == PXA_NET_MAX_STREAM_TIMEOUT_MS);
    ++b->opens;
    pxa_http_stream_init(&b->stream, b->storage, 512, 4096, request->max_body_bytes, 0,
                         PXA_NET_METHOD_GET);
    *exchange = &b->stream;
    return b->fail_open ? PXA_STATUS_IO_ERROR : PXA_STATUS_OK;
}
static int32_t exchange_io(void *context, void *exchange, uint32_t operation,
                           uint8_t *data, size_t size) {
    (void)context;
    return pxa_http_stream_io(exchange, operation, data, size);
}
static void exchange_close(void *context, void *exchange) {
    exchange_backend_t *b = context;
    assert(exchange == &b->stream);
    ++b->closes;
}
static uint64_t reply(pxa_runtime_t *runtime, pxa_component_t owner, uint64_t token,
                      pxa_status_t status) {
    pxa_event_view_t view;
    uint8_t data[128];
    size_t size;
    uint64_t handle = 0;
    assert(pxa_event_peek(runtime, owner, &view) == PXA_STATUS_OK);
    assert(pxa_event_read(runtime, view.token, 0, data, sizeof(data), &size) ==
           PXA_STATUS_OK);
    if (pxa_read_u64(data + 4) != token || (int32_t)pxa_read_u32(data + 20) != status)
        fprintf(stderr, "reply token=%llu status=%d, expected token=%llu status=%d\n",
                (unsigned long long)pxa_read_u64(data + 4),
                (int32_t)pxa_read_u32(data + 20), (unsigned long long)token, status);
    assert(pxa_read_u64(data + 4) == token &&
           (int32_t)pxa_read_u32(data + 20) == status);
    if (status == PXA_STATUS_OK && size >= 32)
        handle = pxa_read_u64(data + 24);
    assert(pxa_event_consume(runtime, owner, view.token) == PXA_STATUS_OK);
    return handle;
}
static pxa_status_t send_net(pxa_runtime_t *runtime, pxa_component_t owner,
                             uint16_t opcode, uint64_t token, uint64_t permission) {
    uint8_t data[128], value[8];
    pxa_writer_t writer;
    pxa_message_view_t message = {0};
    pxa_writer_init(&writer, data, sizeof(data));
    if (opcode != PXA_NET_CAPABILITIES) {
        assert(pxa_writer_record(&writer, 1, "https://example.test/", 21) ==
               PXA_STATUS_OK);
        pxa_write_u16(value, PXA_NET_METHOD_GET);
        assert(pxa_writer_record(&writer, 2, value, 2) == PXA_STATUS_OK);
        pxa_write_u64(value, permission);
        assert(pxa_writer_record(&writer, 3, value, 8) == PXA_STATUS_OK);
        if (opcode == PXA_NET_HTTP_OPEN) {
            pxa_write_u64(value, 1024 * 1024);
            assert(pxa_writer_record(&writer, 4, value, 8) == PXA_STATUS_OK);
            pxa_write_u32(value, PXA_NET_MAX_STREAM_TIMEOUT_MS);
            assert(pxa_writer_record(&writer, 8, value, 4) == PXA_STATUS_OK);
            pxa_write_u32(value, 512);
            assert(pxa_writer_record(&writer, 14, value, 4) == PXA_STATUS_OK);
            pxa_write_u64(value, 0);
            assert(pxa_writer_record(&writer, 15, value, 8) == PXA_STATUS_OK);
            pxa_write_u32(value, 4096);
            assert(pxa_writer_record(&writer, 16, value, 4) == PXA_STATUS_OK);
        } else {
            pxa_write_u32(value, 8);
            assert(pxa_writer_record(&writer, 4, value, 4) == PXA_STATUS_OK);
        }
    }
    message.service = PXA_NET_SERVICE_ID;
    message.opcode = opcode;
    message.request_token = token;
    message.payload = (pxa_bytes_t){data, writer.size};
    return pxa_runtime_control_view(runtime, owner, &message);
}
static void test_exchange_service(int streaming) {
    pxa_runtime_limits_t limits;
    pxa_runtime_t *runtime;
    void *workspace, *pw, *nw;
    pxa_permission_service_t *permissions;
    pxa_net_service_t *net;
    pxa_component_t owner, other, affected[2];
    size_t size, count;
    exchange_backend_t backend = {0};
    pxa_net_config_t nc = {0};
    pxa_permission_declaration_t declaration = {
        {(const uint8_t *)"net.client", 10}, {(const uint8_t *)"web", 3}, 1};
    pxa_permission_config_t pc = {0};
    pxa_writer_t writer;
    pxa_message_view_t message = {0};
    uint8_t data[32];
    uint64_t permission, handle;
    pxa_runtime_limits_init(&limits);
    limits.max_services = 2;
    limits.max_components = 2;
    limits.max_handles = 8;
    limits.max_requests = 4;
    limits.max_requests_per_component = 4;
    size = pxa_runtime_workspace_size(&limits);
    workspace = malloc(size);
    assert(pxa_runtime_init(workspace, size, &limits, &runtime) == PXA_STATUS_OK);
    pc.struct_size = sizeof(pc);
    pc.app_identity = (pxa_bytes_t){(const uint8_t *)"test.app", 8};
    pc.declarations = &declaration;
    pc.declaration_count = 1;
    pc.max_authorities = 2;
    pc.store.struct_size = sizeof(pc.store);
    pc.store.load = allow;
    pc.store.save = save;
    size = pxa_permission_service_workspace_size(&pc);
    pw = malloc(size);
    assert(pxa_permission_service_init(pw, size, runtime, &pc, &permissions) ==
           PXA_STATUS_OK);
    assert(pxa_permission_policy_load(permissions) == PXA_STATUS_OK);
    assert(pxa_permission_service_register(permissions) == PXA_STATUS_OK);
    nc.struct_size = sizeof(nc);
    nc.max_pending_requests = 2;
    nc.max_requests_per_component = 1;
    nc.max_response_streams = 2;
    nc.max_headers = 8;
    nc.max_response_bytes = 8;
    nc.max_inline_body_bytes = 1024;
    nc.max_request_header_bytes = 1024;
    nc.max_response_header_bytes = 128;
    nc.min_timeout_ms = 1;
    nc.default_timeout_ms = 1000;
    nc.max_timeout_ms = PXA_NET_MAX_STREAM_TIMEOUT_MS;
    nc.permissions = permissions;
    nc.backend.struct_size = sizeof(nc.backend);
    nc.backend.context = &backend;
    nc.backend.start = legacy_start;
    nc.backend.poll = legacy_poll;
    nc.backend.cancel = legacy_cancel;
    nc.backend.read_body = legacy_read;
    nc.backend.close_body = legacy_close;
    if (streaming) {
        nc.backend.open_exchange = exchange_open;
        nc.backend.exchange_io = exchange_io;
        nc.backend.close_exchange = exchange_close;
    }
    size = pxa_net_service_workspace_size(&nc);
    nw = malloc(size);
    assert(pxa_net_service_init(nw, size, runtime, &nc, &net) == PXA_STATUS_OK);
    assert(pxa_net_service_register(net) == PXA_STATUS_OK);
    assert(pxa_component_create(runtime, 1, &owner) == PXA_STATUS_OK);
    assert(pxa_component_begin_start(runtime, owner) == PXA_STATUS_OK);
    assert(pxa_component_set_wire_profile(runtime, owner, 1) == PXA_STATUS_OK);
    assert(pxa_component_finish_start(runtime, owner, PXA_STATUS_OK) == PXA_STATUS_OK);
    assert(pxa_component_begin_event(runtime, owner) == PXA_STATUS_OK);
    pxa_writer_init(&writer, data, sizeof(data));
    assert(pxa_writer_record(&writer, 1, "net.client", 10) == PXA_STATUS_OK);
    assert(pxa_writer_record(&writer, 2, "web", 3) == PXA_STATUS_OK);
    message.service = PXA_PERMISSION_SERVICE_ID;
    message.opcode = PXA_PERMISSION_ACQUIRE;
    message.request_token = 1;
    message.payload = (pxa_bytes_t){data, writer.size};
    assert(pxa_runtime_control_view(runtime, owner, &message) == PXA_STATUS_OK);
    permission = reply(runtime, owner, 1, PXA_STATUS_OK);
    assert(permission >> 32);
    assert(send_net(runtime, owner, PXA_NET_CAPABILITIES, 2, 0) == PXA_STATUS_OK);
    assert((reply(runtime, owner, 2, PXA_STATUS_OK) & UINT32_MAX) ==
           (streaming ? 15u : 0u));
    assert(send_net(runtime, owner, PXA_NET_HTTP_OPEN, 3, permission) == PXA_STATUS_OK);
    handle =
        reply(runtime, owner, 3, streaming ? PXA_STATUS_OK : PXA_STATUS_UNSUPPORTED);
    if (streaming) {
        assert(backend.opens == 1 && handle >> 32);
        assert(send_net(runtime, owner, PXA_NET_HTTP_OPEN, 4, permission) ==
               PXA_STATUS_OK);
        reply(runtime, owner, 4, PXA_STATUS_RESOURCE_LIMIT);
        assert(backend.opens == 1);
        assert(send_net(runtime, owner, PXA_NET_FETCH, 5, permission) == PXA_STATUS_OK);
        reply(runtime, owner, 5, PXA_STATUS_RESOURCE_LIMIT);
        assert(backend.starts == 0);
        assert(pxa_component_create(runtime, 2, &other) == PXA_STATUS_OK);
        assert(pxa_component_begin_start(runtime, other) == PXA_STATUS_OK);
        assert(pxa_component_set_wire_profile(runtime, other, 1) == PXA_STATUS_OK);
        assert(pxa_handle_poll64(runtime, other, handle) < 0); /* ownership */
        assert(pxa_handle_close64(runtime, owner, handle) == PXA_STATUS_OK &&
               backend.closes == 1);
        assert(send_net(runtime, owner, PXA_NET_FETCH, 6, permission) == PXA_STATUS_OK);
        assert(send_net(runtime, owner, PXA_NET_HTTP_OPEN, 7, permission) ==
               PXA_STATUS_OK);
        reply(runtime, owner, 7, PXA_STATUS_RESOURCE_LIMIT);
        assert(backend.opens == 1);
        assert(pxa_request_cancel(runtime, owner, 6) == PXA_STATUS_OK);
        reply(runtime, owner, 6, PXA_STATUS_CANCELLED);
        assert(pxa_net_poll(net, affected, 2, &count) == PXA_STATUS_OK &&
               backend.cancels == 1);
        backend.fail_open = 1;
        assert(send_net(runtime, owner, PXA_NET_HTTP_OPEN, 8, permission) ==
               PXA_STATUS_OK);
        reply(runtime, owner, 8, PXA_STATUS_IO_ERROR);
        assert(backend.closes == 2);
        backend.fail_open = 0;
        assert(send_net(runtime, owner, PXA_NET_HTTP_OPEN, 9, permission) ==
               PXA_STATUS_OK);
        handle = reply(runtime, owner, 9, PXA_STATUS_OK);
        assert(pxa_handle_close64(runtime, owner, permission) == PXA_STATUS_OK);
        assert(backend.closes == 3 &&
               pxa_handle_poll64(runtime, owner, handle) < 0); /* revoke cascades */
    }
    assert(pxa_component_finish_event(runtime, owner, 0) == PXA_STATUS_OK);
    pxa_runtime_deinit(runtime);
    free(nw);
    free(pw);
    free(workspace);
}
int main(void) {
    test_ring();
    test_waits();
    test_exchange_service(0);
    test_exchange_service(1);
    return 0;
}
