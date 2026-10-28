// SPDX-License-Identifier: MIT
#include "../../testing/pxa_control_capture.h"
#include <cassert>
#include <cstdio>
#include <pxa/websocket.hpp>
using namespace pxa;
static constexpr std::uint64_t socket_handle = UINT64_C(0x200000003),
                               permission_handle = UINT64_C(0x100000002);
static std::uint64_t open_token;
static unsigned closes = 0;
static bool handshake_done = false, blocked = false, bad_accept = false;
static std::string request;
static std::vector<std::byte> received, sent;
static std::size_t cursor = 0;
extern "C" std::int32_t pxa_test_control(const std::uint8_t *input,
                                         std::uint32_t size) {
  auto b = reinterpret_cast<const std::byte *>(input);
  auto service = wire::get16(b), op = wire::get16(b + 2);
  if (service == 9) {
    assert(op == 5);
    open_token = wire::get64(b + 4);
    wire::Records r({b + 20, size - 20});
    auto endpoint = r.take(1);
    assert(endpoint &&
           std::string_view(reinterpret_cast<const char *>(endpoint->data()),
                            endpoint->size()) == "tcp://example.test:80");
    assert(r.take(3, 8) && r.take(8, 4) && r.take(14, 4) && r.empty());
  } else {
    assert(service == 1);
    if (op == 4)
      assert(wire::get64(b + 4));
    else if (op == 2)
      closes++;
    else
      assert(op == 1);
  }
  return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t h, std::uint32_t op,
                               std::uint8_t *bytes, std::uint32_t size) {
  assert(h == socket_handle);
  if (op == 2) {
    if (blocked)
      return -7;
    auto n = std::min<std::uint32_t>(7, size);
    if (!handshake_done) {
      request.append(reinterpret_cast<const char *>(bytes), n);
      if (request.ends_with("\r\n\r\n")) {
        auto start = request.find("Sec-WebSocket-Key: ") + 19;
        auto key = std::string_view(request).substr(start, 24);
        auto response =
            "HTTP/1.1 101 Switching Protocols\r\nUpgrade: "
            "websocket\r\nConnection: keep-alive, "
            "Upgrade\r\nSec-WebSocket-Accept: " +
            (bad_accept ? "invalid" : detail::websocket::accept(key)) +
            "\r\n\r\n";
        auto p = reinterpret_cast<const std::byte *>(response.data());
        received.assign(p, p + response.size());
        cursor = 0;
        handshake_done = true;
      }
    } else {
      auto p = reinterpret_cast<const std::byte *>(bytes);
      sent.insert(sent.end(), p, p + n);
    }
    return n;
  }
  assert(op == 1);
  if (cursor == received.size())
    return -7;
  bytes[0] = std::to_integer<unsigned char>(received[cursor++]);
  return 1; // every boundary is a short read
}
static WebSocket socket;
static bool ready = false, failed = false;
static Task<void> connect(WebSocketClient client, Permission &permission) {
  auto result = co_await client.open("ws://example.test/chat", permission);
  if (!result) {
    failed = true;
    co_return Result<void>{};
  }
  socket = std::move(*result);
  ready = true;
  co_return Result<void>{};
}
static void reply(RequestTable &requests) {
  std::array<std::byte, 12> payload{};
  wire::put64(payload.data() + 4, socket_handle);
  Event event{};
  event.service = 9;
  event.opcode = 5;
  event.token = open_token;
  event.payload = payload;
  assert(requests.dispatch(event));
}
static void feed(std::initializer_list<unsigned> values) {
  received.clear();
  cursor = 0;
  for (auto b : values)
    received.push_back(std::byte(b));
}
static Result<std::uint32_t> read(std::span<std::byte> out) {
  for (unsigned i = 0; i < 100; i++) {
    auto n = socket.read(out);
    if (n || n.error() != Error::would_block)
      return n;
    if (cursor == received.size())
      return n;
  }
  assert(false);
  return std::unexpected(Error::internal);
}
int main() {
  assert(detail::websocket::accept("dGhlIHNhbXBsZSBub25jZQ==") ==
         "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
  Transport transport;
  transport.phase(Phase::event);
  RequestTable requests;
  TaskScope tasks;
  Permission permission(transport, permission_handle);
  auto reset = [&] {
    (void)socket.close();
    handshake_done = false;
    blocked = false;
    request.clear();
    received.clear();
    sent.clear();
    cursor = 0;
    ready = failed = false;
    assert(
        tasks.start(connect(WebSocketClient(transport, requests), permission)));
    reply(requests);
  };
  reset();
  assert(ready && !failed);
  // Fragmented UTF-8 with a control frame between fragments.
  feed({0x01, 1, 0xe4, 0x89, 1, 'P', 0x80, 2, 0xbd, 0xa0});
  std::array<std::byte, 4097> out;
  auto n = read(out);
  assert(n && *n == 4 && out[0] == std::byte{1} && out[1] == std::byte{0xe4});
  for (unsigned i = 0; i < 10; i++)
    (void)socket.read(out);
  assert(sent.size() == 7 && sent[0] == std::byte{0x8a} &&
         sent[1] == std::byte{0x81} && (sent[6] ^ sent[2]) == std::byte{'P'});
  feed({0x82, 3, 1, 2, 3});
  n = read(std::span{out}.first(2));
  assert(!n && n.error() == Error::limit_exceeded);
  n = read(out);
  assert(n && *n == 4 && out[0] == std::byte{2} && out[3] == std::byte{3});
  // Bounded atomic message admission, partial TCP writes and fresh masking.
  sent.clear();
  std::array<std::byte, 4> message{std::byte{1}, std::byte{'a'}, std::byte{'b'},
                                   std::byte{'c'}};
  n = socket.write(message);
  assert(n && *n == 4);
  blocked = true;
  n = socket.write(message);
  assert(!n && n.error() == Error::would_block);
  blocked = false;
  for (unsigned i = 0; i < 10; i++)
    (void)socket.read(out);
  assert(sent.size() == 9 && sent[0] == std::byte{0x81} &&
         sent[1] == std::byte{0x83});
  for (unsigned i = 0; i < 3; i++)
    assert((sent[6 + i] ^ sent[2 + i]) == message[1 + i]);
  feed({0x88, 2, 3, 0xe8});
  n = read(out);
  assert(n && !*n);
  reset();
  feed({0x81, 0x80, 0, 0, 0, 0});
  n = read(out);
  assert(!n && n.error() == Error::protocol_error);
  reset();
  feed({0x89, 126, 0, 126});
  n = read(out);
  assert(!n && n.error() == Error::protocol_error);
  reset();
  feed({0x81, 2, 0xc0, 0x80});
  n = read(out);
  assert(!n && n.error() == Error::protocol_error);
  reset();
  feed({0x82, 126, 0x40, 0x01});
  n = read(out);
  assert(!n && n.error() == Error::limit_exceeded);
  bad_accept = true;
  reset();
  assert(failed && !ready);
  bad_accept = false;
  // Cancel an accepted open before its response: the late handle is closed.
  (void)socket.close();
  request.clear();
  handshake_done = false;
  failed = ready = false;
  assert(
      tasks.start(connect(WebSocketClient(transport, requests), permission)));
  auto before = closes;
  tasks.cancel();
  reply(requests);
  assert(closes == before + 1 && !ready);
  permission.release();
  transport.phase(Phase::stopped);
  puts("SDK sockets/WebSocket: SHA-1 handshake, fragmented UTF-8, control "
       "frames, masking, short I/O, bounds and cancellation passed");
}
