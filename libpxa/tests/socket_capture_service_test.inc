/* SPDX-License-Identifier: MIT
 * Reuses core_test's runtime/dispatch helpers. No sockets or microphones. */
#include "pxa/socket_stream.h"
typedef struct {
  unsigned opens, closes;
  uint32_t connectivity;
} stream_probe_t;
static pxa_status_t probe_connectivity(void *context, uint32_t *state) {
  *state = ((stream_probe_t *)context)->connectivity;
  return PXA_STATUS_OK;
}
static pxa_status_t
probe_socket_open(void *context, const pxa_socket_request_t *r, void **out) {
  stream_probe_t *p = context;
  assert(r->port == 443 && r->tls);
  p->opens++;
  *out = p;
  return 0;
}
static int32_t probe_socket_io(void *c, void *s, uint32_t op, uint8_t *data,
                               size_t size) {
  (void)c;
  assert(s);
  if (op == PXA_IO_WRITE)
    return (int32_t)size;
  if (op == PXA_IO_READ && size) {
    data[0] = 42;
    return 1;
  }
  return PXA_STATUS_UNSUPPORTED;
}
static void probe_socket_close(void *c, void *s) {
  assert(c == s);
  ((stream_probe_t *)c)->closes++;
}
static pxa_status_t probe_capture_open(void *context, uint64_t *id) {
  ((stream_probe_t *)context)->opens++;
  *id = 77;
  return 0;
}
static int32_t probe_capture_io(void *c, uint64_t id, uint32_t op,
                                uint8_t *data, size_t size) {
  (void)c;
  assert(id == 77);
  if (op != PXA_IO_READ)
    return PXA_STATUS_UNSUPPORTED;
  assert(size >= 1275);
  data[0] = 0xf8;
  return 1;
}
static void probe_capture_close(void *c, uint64_t id) {
  assert(id == 77);
  ((stream_probe_t *)c)->closes++;
}
static uint16_t probe_capture_frame_ms(void *context) {
  (void)context;
  return 60;
}
static void test_socket_capture_services(int capture_mode) {
  test_runtime_t t = make_runtime(2, 10, 10, 6, 12, 8, 2);
  pxa_component_t component;
  assert(!pxa_component_create(t.runtime, 17, &component));
  assert(!pxa_component_begin_start(t.runtime, component));
  assert(!pxa_component_set_wire_profile(t.runtime, component, 1));
  assert(!pxa_component_finish_start(t.runtime, component, 0));
  pxa_component_t other = create_started(t.runtime, 18);
  const char *name = capture_mode ? "audio.capture" : "net.socket",
             *scope = capture_mode ? "microphone" : "tls://example.test:443";
  pxa_permission_declaration_t declaration = {
      {(const uint8_t *)name, strlen(name)},
      {(const uint8_t *)scope, strlen(scope)},
      1};
  unsigned loads = 0;
  pxa_permission_config_t pc = {0};
  pc.struct_size = sizeof(pc);
  pc.app_identity = (pxa_bytes_t){(const uint8_t *)"stream.app", 10};
  pc.declarations = &declaration;
  pc.declaration_count = 1;
  pc.max_authorities = 2;
  pc.store.struct_size = sizeof(pc.store);
  pc.store.context = &loads;
  pc.store.load = sensor_permission_load;
  pc.store.save = sensor_permission_save;
  size_t n = pxa_permission_service_workspace_size(&pc);
  void *pw = malloc(n);
  pxa_permission_service_t *permissions;
  assert(pw &&
         !pxa_permission_service_init(pw, n, t.runtime, &pc, &permissions));
  assert(!pxa_permission_policy_load(permissions));
  assert(!pxa_permission_service_register(permissions));
  stream_probe_t probe = {0};
  void *sw;
  pxa_net_service_t *net = NULL;
  pxa_audio_service_t *audio = NULL;
  if (capture_mode) {
    pxa_audio_config_t c = {0};
    c.struct_size = sizeof(c);
    c.max_sessions = c.max_sessions_per_component = 1;
    c.permissions = permissions;
    c.backend.struct_size = sizeof(c.backend);
    c.backend.context = &probe;
    c.backend.open = audio_open;
    c.backend.commit = audio_commit;
    c.backend.submit = audio_submit;
    c.backend.close = audio_close;
    c.backend.open_capture = probe_capture_open;
    c.backend.capture_io = probe_capture_io;
    c.backend.close_capture = probe_capture_close;
    if (capture_mode == 2)
      c.backend.capture_frame_ms = probe_capture_frame_ms;
    n = pxa_audio_service_workspace_size(&c);
    sw = malloc(n);
    assert(sw && !pxa_audio_service_init(sw, n, t.runtime, &c, &audio));
    assert(!pxa_audio_service_register(audio));
  } else {
    pxa_net_config_t c = {0};
    c.struct_size = sizeof(c);
    c.max_pending_requests = c.max_requests_per_component =
        c.max_response_streams = 1;
    c.max_response_bytes = 2048;
    c.max_headers = 8;
    c.max_inline_body_bytes = 2048;
    c.max_request_header_bytes = c.max_response_header_bytes = 2048;
    c.min_timeout_ms = 100;
    c.default_timeout_ms = 15000;
    c.max_timeout_ms = 60000;
    c.permissions = permissions;
    c.backend.struct_size = sizeof(c.backend);
    c.backend.context = &probe;
    c.backend.start = net_start;
    c.backend.poll = net_poll;
    c.backend.cancel = net_cancel;
    c.backend.read_body = net_read_body;
    c.backend.close_body = net_close_body;
    c.backend.open_socket = probe_socket_open;
    c.backend.socket_io = probe_socket_io;
    c.backend.close_socket = probe_socket_close;
    c.backend.connectivity = probe_connectivity;
    n = pxa_net_service_workspace_size(&c);
    sw = malloc(n);
    assert(sw && !pxa_net_service_init(sw, n, t.runtime, &c, &net));
    assert(!pxa_net_service_register(net));
  }
  uint8_t command[256], event_bytes[256], payload[128], value[8];
  pxa_message_view_t event;
  if (!capture_mode) {
    // A local link query needs no network grant, and never opens a socket.
    for (uint32_t state = 0; state <= 3; ++state) {
      probe.connectivity = state;
      pxa_writer_t query;
      pxa_writer_init(&query, command, sizeof(command));
      assert(!pxa_writer_message(&query, PXA_NET_SERVICE_ID,
          PXA_NET_CONNECTIVITY, 690 + state, NULL, 0));
      dispatch_control_completion(t.runtime, component, command, query.size,
          event_bytes, sizeof(event_bytes), &event);
      if (state <= PXA_NET_CONNECTIVITY_ONLINE)
        assert(event.payload.size == 8 && !pxa_read_u32(event.payload.data) &&
            pxa_read_u32(event.payload.data + 4) == state);
      else
        assert(event.payload.size == 4 &&
            (int32_t)pxa_read_u32(event.payload.data) == PXA_STATUS_PROTOCOL_ERROR);
      assert(!probe.opens);
    }
  }
  n = make_permission_request(command, sizeof(command), PXA_PERMISSION_ACQUIRE,
                              700, (const uint8_t *)name, strlen(name),
                              (const uint8_t *)scope, strlen(scope));
  dispatch_control_completion(t.runtime, component, command, n, event_bytes,
                              sizeof(event_bytes), &event);
  assert(!pxa_read_u32(event.payload.data));
  uint64_t grant = pxa_read_u64(event.payload.data + 4);
  assert(grant >> 32);
  pxa_writer_t w;
  pxa_writer_init(&w, payload, sizeof(payload));
  if (capture_mode) {
    pxa_write_u64(payload, grant);
    w.size = 8;
  } else {
    assert(!pxa_writer_record(&w, 1, scope, strlen(scope)));
    pxa_write_u64(value, grant);
    assert(!pxa_writer_record(&w, 3, value, 8));
    pxa_write_u32(value, 15000);
    assert(!pxa_writer_record(&w, 8, value, 4));
    pxa_write_u32(value, 512);
    assert(!pxa_writer_record(&w, 14, value, 4));
  }
  pxa_writer_t msg;
  pxa_writer_init(&msg, command, sizeof(command));
  assert(!pxa_writer_message(&msg, capture_mode ? 10 : 9, capture_mode ? 6 : 5,
                             701, payload, w.size));
  dispatch_control_completion(t.runtime, component, command, msg.size,
                              event_bytes, sizeof(event_bytes), &event);
  assert(!pxa_read_u32(event.payload.data));
  uint64_t handle = pxa_read_u64(event.payload.data + 4);
  assert(handle >> 32);
  assert(probe.opens == 1);
  if (capture_mode)
    assert(event.payload.size == 24 &&
           pxa_read_u32(event.payload.data + 12) == 16000 &&
           pxa_read_u16(event.payload.data + 18) == (capture_mode == 2 ? 60 : 20));
  pxa_resource_t resource;
  assert(pxa_handle_get64(
             t.runtime, other, handle,
             (capture_mode ? PXA_RESOURCE_AUDIO_STREAM : PXA_RESOURCE_STREAM),
             &resource) == PXA_STATUS_NOT_FOUND);
  assert(!pxa_handle_get64(t.runtime, component, handle,
                           capture_mode ? PXA_RESOURCE_AUDIO_STREAM
                                        : PXA_RESOURCE_STREAM,
                           &resource));
  uint8_t data[1275];
  assert(!pxa_component_begin_event(t.runtime, component));
  assert(pxa_runtime_io64(t.runtime, component, handle, PXA_IO_READ, data,
                          sizeof(data)) == 1);
  assert(!pxa_component_finish_event(t.runtime, component, 1));
  /* A second open hits the per-component pool without entering the backend. */
  pxa_write_u64(command + 4, 702);
  dispatch_control_completion(t.runtime, component, command, msg.size,
                              event_bytes, sizeof(event_bytes), &event);
  assert((int32_t)pxa_read_u32(event.payload.data) ==
             PXA_STATUS_RESOURCE_LIMIT &&
         probe.opens == 1);
  assert(!pxa_handle_close64(t.runtime, component, grant));
  assert(probe.closes == 1);
  assert(pxa_handle_get64(
             t.runtime, component, handle,
             (capture_mode ? PXA_RESOURCE_AUDIO_STREAM : PXA_RESOURCE_STREAM),
             &resource) == PXA_STATUS_NOT_FOUND);
  pxa_runtime_deinit(t.runtime);
  assert(probe.closes == 1);
  free(sw);
  free(pw);
  free(t.workspace);
}
