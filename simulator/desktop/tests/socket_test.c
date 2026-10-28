/* SPDX-License-Identifier: MIT */
#undef NDEBUG
#include "../desktop_net.c"
#include <assert.h>
static void notify(void *p) { (void)p; }
static uint64_t monotonic_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void pause_ms(void) {
  struct timespec t = {0, 1000000};
  nanosleep(&t, NULL);
}
int main(int argc, char **argv) {
  assert(argc == 4);
  pxa_net_backend_t backend;
  assert(pxsys_desktop_net_backend(&backend, notify, NULL));
  uint32_t connectivity;
  assert(!setenv("PXA_SIM_NETWORK_CONNECTED", "0", 1));
  assert(!backend.connectivity(backend.context, &connectivity) &&
         connectivity == PXA_NET_CONNECTIVITY_OFFLINE);
  assert(!setenv("PXA_SIM_NETWORK_CONNECTED", "1", 1));
  assert(!backend.connectivity(backend.context, &connectivity) &&
         connectivity == PXA_NET_CONNECTIVITY_ONLINE);
  assert(!unsetenv("PXA_SIM_NETWORK_CONNECTED"));
  assert(!setenv("PXA_SIM_NETWORK_STATE_FILE", argv[3], 1));
  for (unsigned i = 0; i < 3; ++i) {
    FILE *fixture = fopen(argv[3], "wb");
    assert(fixture &&
           fputc(i == 0   ? '0'
                 : i == 1 ? '1'
                          : '?',
                 fixture) != EOF &&
           !fclose(fixture));
    assert(!backend.connectivity(backend.context, &connectivity));
    assert(connectivity == (i == 0   ? PXA_NET_CONNECTIVITY_OFFLINE
                            : i == 1 ? PXA_NET_CONNECTIVITY_ONLINE
                                     : PXA_NET_CONNECTIVITY_UNKNOWN));
  }
  assert(!remove(argv[3]));
  assert(!backend.connectivity(backend.context, &connectivity) &&
         connectivity == PXA_NET_CONNECTIVITY_UNKNOWN);
  assert(!unsetenv("PXA_SIM_NETWORK_STATE_FILE"));
  char endpoint[80];
  snprintf(endpoint, sizeof(endpoint), "tcp://127.0.0.1:%s", argv[1]);
  pxa_socket_request_t request = {
      .endpoint = {(const uint8_t *)endpoint, strlen(endpoint)},
      .buffer_bytes = 512,
      .connect_timeout_ms = 3000};
  void *stream = NULL;
  assert(!backend.open_socket(backend.context, &request, &stream));
  uint64_t deadline = monotonic_ms() + 15000;
  size_t sent = 0, received = 0;
  uint8_t data[733];
  while (received < 131072) {
    assert(monotonic_ms() < deadline);
    if (sent < 131072) {
      size_t n = sizeof(data);
      if (n > 131072 - sent)
        n = 131072 - sent;
      for (size_t i = 0; i < n; i++)
        data[i] = (uint8_t)((sent + i) * 31 + 7);
      int32_t result =
          backend.socket_io(backend.context, stream, PXA_IO_WRITE, data, n);
      if (result > 0)
        sent += (size_t)result;
      else
        assert(result == PXA_STATUS_WOULD_BLOCK);
    }
    int32_t result = backend.socket_io(backend.context, stream, PXA_IO_READ,
                                       data, sizeof(data));
    if (result > 0) {
      for (int32_t i = 0; i < result; i++)
        assert(data[i] == (uint8_t)((received + i) * 31 + 7));
      received += (size_t)result;
    } else
      assert(result == PXA_STATUS_WOULD_BLOCK);
    pause_ms();
  }
  assert(sent == received);
  backend.close_socket(backend.context, stream);
  snprintf(endpoint, sizeof(endpoint), "tls://127.0.0.1:%s", argv[2]);
  request.endpoint.size = strlen(endpoint);
  request.tls = 1;
  assert(!backend.open_socket(backend.context, &request, &stream));
  int32_t state;
  do {
    assert(monotonic_ms() < deadline);
    state = backend.socket_io(backend.context, stream, 4, NULL, 0);
    pause_ms();
  } while (state == PXA_STATUS_WOULD_BLOCK);
  assert(state < 0); /* self-signed certificate must not be accepted */
  backend.close_socket(backend.context, stream);
  /* Cancellation while connecting and Host shutdown retain safe ownership. */
  for (unsigned i = 0; i < 8; i++) {
    int32_t status = backend.open_socket(backend.context, &request, &stream);
    if (!status)
      backend.close_socket(backend.context, stream);
    else
      assert(status == PXA_STATUS_RESOURCE_LIMIT);
  }
  pxsys_desktop_net_shutdown(&backend);
  puts("desktop sockets: 128 KiB short I/O echo, certificate rejection, "
       "cancellation and shutdown passed");
}
