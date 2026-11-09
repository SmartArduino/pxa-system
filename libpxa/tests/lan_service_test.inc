/* SPDX-License-Identifier: MIT */
#include "pxa/lan.h"
static int probe_network_scope;
static pxa_status_t probe_local_open(void *context, const pxa_lan_request_t *r,
                                     void **out) {
  assert(r->kind == PXA_LAN_LISTENER && r->bind.family == 4 &&
         r->buffer_bytes == 4096);
  ((stream_probe_t *)context)->opens++;
  probe_network_scope = r->network_scope;
  *out = context;
  return 0;
}
static pxa_status_t probe_local_accept(void *context, void *parent,
                                       void **out) {
  assert(parent == context);
  ((stream_probe_t *)context)->opens++;
  *out = context;
  return 0;
}
static int probe_local_poll(void *context) {
  (void)context;
  return 1;
}
static void test_lan_wire(void) {
  uint8_t bytes[512] = {0};
  pxa_write_u64(bytes, UINT64_C(0x100000001));
  pxa_write_u16(bytes + 8, PXA_LAN_ADVERTISE);
  pxa_write_u16(bytes + 12, 4);
  pxa_write_u16(bytes + 14, 1234);
  pxa_write_u32(bytes + 36, 4096);
  pxa_write_u16(bytes + 40, 3);
  pxa_write_u16(bytes + 42, 13);
  pxa_write_u16(bytes + 44, 4);
  pxa_write_u16(bytes + 46, 4);
  memcpy(bytes + 48, "_pxatest._tcpTest", 17);
  const uint8_t txt[] = {3, 'v', '=', '1'};
  memcpy(bytes + 65, txt, sizeof(txt));
  pxa_lan_request_t request;
  pxa_handle64_t grant;
  assert(!pxa_lan_request_parse((pxa_bytes_t){bytes, 69}, &request, &grant));
  assert(grant == UINT64_C(0x100000001) && request.bind.port == 1234 &&
         request.txt.size == 4 && request.instance.size == 4);
  for (unsigned n = 0; n < 69; ++n)
    assert(pxa_lan_request_parse((pxa_bytes_t){bytes, n}, &request, &grant));
  bytes[61] = 0xff; /* Invalid UTF-8 instance cannot become a DNS label. */
  assert(pxa_lan_request_parse((pxa_bytes_t){bytes, 69}, &request, &grant));
  bytes[61] = 'T';
  bytes[65] = 5; /* TXT length cannot cross its input boundary. */
  assert(pxa_lan_request_parse((pxa_bytes_t){bytes, 69}, &request, &grant));
  bytes[65] = 3;
  bytes[66] = '='; /* Empty metadata key. */
  assert(pxa_lan_request_parse((pxa_bytes_t){bytes, 69}, &request, &grant));
  bytes[66] = 'v';
  bytes[10] = 1; /* NO_DELAY is not an advertisement flag. */
  assert(pxa_lan_request_parse((pxa_bytes_t){bytes, 69}, &request, &grant));
  bytes[10] = 0;
  pxa_write_u16(bytes + 46, 1);
  bytes[65] = 0;
  assert(!pxa_lan_request_parse((pxa_bytes_t){bytes, 66}, &request, &grant));
  /* Exercise truncated/mutated scalar and trailing records under sanitizers. */
  uint32_t random = 19;
  for (unsigned i = 0; i < 10000; ++i) {
    random = random * 1664525u + 1013904223u;
    size_t size = random % sizeof(bytes);
    bytes[(random >> 16) % sizeof(bytes)] = (uint8_t)random;
    (void)pxa_lan_request_parse((pxa_bytes_t){bytes, size}, &request, &grant);
  }
}
static void test_lan_services(void) {
  test_lan_wire();
  test_runtime_t t = make_runtime(2, 12, 12, 6, 12, 8, 2);
  pxa_component_t component;
  assert(!pxa_component_create(t.runtime, 17, &component));
  assert(!pxa_component_begin_start(t.runtime, component));
  assert(!pxa_component_set_wire_profile(t.runtime, component, 1));
  assert(!pxa_component_finish_start(t.runtime, component, 0));
  pxa_component_t other = create_started(t.runtime, 18);
  pxa_permission_declaration_t declaration[] = {
      {{(const uint8_t *)"net.listen", 10},
       {(const uint8_t *)"local-network", 13},
       1},
      {{(const uint8_t *)"net.listen", 10},
       {(const uint8_t *)"network", 7},
       1}};
  unsigned loads = 0;
  pxa_permission_config_t pc = {0};
  pc.struct_size = sizeof(pc);
  pc.app_identity = (pxa_bytes_t){(const uint8_t *)"lan.app", 7};
  pc.declarations = declaration;
  pc.declaration_count = 2;
  pc.max_authorities = 2;
  pc.store.struct_size = sizeof(pc.store);
  pc.store.context = &loads;
  pc.store.load = sensor_permission_load;
  pc.store.save = sensor_permission_save;
  size_t n = pxa_permission_service_workspace_size(&pc);
  void *pw = malloc(n);
  pxa_permission_service_t *permissions;
  assert(!pxa_permission_service_init(pw, n, t.runtime, &pc, &permissions));
  assert(!pxa_permission_policy_load(permissions));
  assert(!pxa_permission_service_register(permissions));
  stream_probe_t probe = {0};
  pxa_lan_backend_t local = {sizeof(local),
                             PXA_NET_FEATURE_LISTEN,
                             &probe,
                             probe_local_open,
                             probe_socket_io,
                             probe_local_accept,
                             probe_socket_close,
                             probe_local_poll,
                             3,
                             4096};
  pxa_net_config_t c = {0};
  c.struct_size = sizeof(c);
  c.max_pending_requests = 1;
  c.max_requests_per_component = 2;
  c.max_response_streams = 3;
  c.max_response_bytes = 2048;
  c.max_headers = 8;
  c.max_inline_body_bytes = 2048;
  c.max_request_header_bytes = c.max_response_header_bytes = 2048;
  c.min_timeout_ms = 100;
  c.default_timeout_ms = 15000;
  c.max_timeout_ms = 60000;
  c.permissions = permissions;
  c.local = &local;
  c.backend.struct_size = sizeof(c.backend);
  c.backend.context = &probe;
  c.backend.start = net_start;
  c.backend.poll = net_poll;
  c.backend.cancel = net_cancel;
  c.backend.read_body = net_read_body;
  c.backend.close_body = net_close_body;
  n = pxa_net_service_workspace_size(&c);
  void *nw = malloc(n);
  pxa_net_service_t *net;
  assert(nw && !pxa_net_service_init(nw, n, t.runtime, &c, &net));
  /* A binary caller using the pre-0.7 config must not be over-read. */
  size_t legacy_size = offsetof(pxa_net_config_t, local);
  pxa_net_config_t *legacy = malloc(legacy_size);
  memcpy(legacy, &c, legacy_size);
  legacy->struct_size = (uint32_t)legacy_size;
  size_t legacy_workspace_size = pxa_net_service_workspace_size(legacy);
  void *legacy_workspace = malloc(legacy_workspace_size);
  pxa_net_service_t *legacy_net = NULL;
  assert(!pxa_net_service_init(legacy_workspace, legacy_workspace_size,
                               t.runtime, legacy, &legacy_net));
  free(legacy_workspace);
  free(legacy);
  assert(!pxa_net_service_register(net));
  uint8_t command[256], bytes[256], body[48] = {0};
  pxa_message_view_t event;
  pxa_writer_t capabilities_writer;
  pxa_writer_init(&capabilities_writer, command, sizeof(command));
  assert(!pxa_writer_message(&capabilities_writer, 9, 4, 799, NULL, 0));
  dispatch_control_completion(t.runtime, component, command, capabilities_writer.size,
                              bytes, sizeof(bytes), &event);
  assert(!pxa_read_u32(event.payload.data) && event.payload.size == 36);
  assert(pxa_read_u32(event.payload.data + 32) == 1); /* HTTP worker budget, not two shared handles. */
  n = make_permission_request(command, sizeof(command), PXA_PERMISSION_ACQUIRE,
                              800, (const uint8_t *)"net.listen", 10,
                              (const uint8_t *)"local-network", 13);
  dispatch_control_completion(t.runtime, component, command, n, bytes,
                              sizeof(bytes), &event);
  assert(!pxa_read_u32(event.payload.data));
  pxa_handle64_t grant = pxa_read_u64(event.payload.data + 4);
  pxa_write_u64(body, grant);
  pxa_write_u16(body + 8, 1);
  pxa_write_u16(body + 12, 4);
  pxa_write_u32(body + 36, 4096);
  pxa_write_u16(body + 40, 2);
  for (unsigned mode = 0; mode < 3; ++mode) {
    pxa_write_u16(body + 8, mode == 0 ? 99 : 1);
    pxa_write_u64(body, mode == 1 ? grant + 1 : grant);
    pxa_writer_t w;
    pxa_writer_init(&w, command, sizeof(command));
    assert(!pxa_writer_message(&w, 9, 7, 801 + mode, body, 48));
    dispatch_control_completion(t.runtime, component, command, w.size, bytes,
                                sizeof(bytes), &event);
    if (mode < 2) {
      assert((int32_t)pxa_read_u32(event.payload.data) < 0 && !probe.opens);
      continue;
    }
    assert(!pxa_read_u32(event.payload.data) && probe.opens == 1);
  }
  assert(!probe_network_scope);
  pxa_handle64_t listener = pxa_read_u64(event.payload.data + 4);
  pxa_resource_t resource;
  assert(pxa_handle_get64(t.runtime, other, listener, PXA_RESOURCE_STREAM,
                          &resource) == PXA_STATUS_NOT_FOUND);
  assert(!pxa_component_begin_event(t.runtime, component));
  assert(pxa_runtime_io64(t.runtime, component, listener, 7, body, 7) ==
         PXA_STATUS_INVALID_ARGUMENT);
  assert(probe.opens == 1);
  assert(pxa_runtime_io64(t.runtime, component, listener, 7, body, 8) == 8 &&
         probe.opens == 2);
  pxa_handle64_t peer = pxa_read_u64(body);
  assert(pxa_runtime_io64(t.runtime, component, listener, 7, body, 8) ==
             PXA_STATUS_RESOURCE_LIMIT &&
         probe.opens == 2);
  assert(!pxa_component_finish_event(t.runtime, component, 1));
  assert(!pxa_handle_close64(t.runtime, component, grant));
  assert(probe.closes == 2);
  assert(pxa_handle_get64(t.runtime, component, listener, PXA_RESOURCE_STREAM,
                          &resource) == PXA_STATUS_NOT_FOUND);
  assert(pxa_handle_get64(t.runtime, component, peer, PXA_RESOURCE_STREAM,
                          &resource) == PXA_STATUS_NOT_FOUND);
  n = make_permission_request(command, sizeof(command), PXA_PERMISSION_ACQUIRE,
                              810, (const uint8_t *)"net.listen", 10,
                              (const uint8_t *)"network", 7);
  dispatch_control_completion(t.runtime, component, command, n, bytes,
                              sizeof(bytes), &event);
  assert(!pxa_read_u32(event.payload.data));
  pxa_write_u64(body, pxa_read_u64(event.payload.data + 4));
  pxa_write_u16(body + 8, PXA_LAN_LISTENER);
  pxa_writer_t network_writer;
  pxa_writer_init(&network_writer, command, sizeof(command));
  assert(!pxa_writer_message(&network_writer, 9, 7, 811, body, 48));
  dispatch_control_completion(t.runtime, component, command,
                              network_writer.size, bytes, sizeof(bytes),
                              &event);
  assert(!pxa_read_u32(event.payload.data) && probe_network_scope == 1 &&
         probe.opens == 3);
  pxa_runtime_deinit(t.runtime);
  assert(probe.closes == 3);
  free(nw);
  free(pw);
  free(t.workspace);
}
