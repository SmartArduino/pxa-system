// SPDX-License-Identifier: MIT
#include "../../testing/pxa_control_capture.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <pxa/lan.hpp>
using namespace pxa;
static std::uint64_t token;
static unsigned kind, closes;
static bool fail = false;
static constexpr std::uint64_t grant = UINT64_C(0x100000001),
                               handle = UINT64_C(0x200000002),
                               accepted = UINT64_C(0x300000003);
extern "C" std::int32_t pxa_test_control(const std::uint8_t *p,
                                         std::uint32_t size) {
  auto b = reinterpret_cast<const std::byte *>(p);
  auto service = wire::get16(b), op = wire::get16(b + 2);
  if (service == 9) {
    assert(op == 7 && size >= 68);
    token = wire::get64(b + 4);
    assert(wire::get64(b + 20) == grant);
    kind = wire::get16(b + 28);
    assert(wire::get16(b + 32) == 4);
  } else {
    assert(service == 1);
    if (op == 2)
      ++closes;
    else
      assert(op == 1 || op == 4);
  }
  return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t h, std::uint32_t op,
                               std::uint8_t *data, std::uint32_t size) {
  assert(h == handle);
  auto b = reinterpret_cast<std::byte *>(data);
  if (op == 4)
    return 1;
  if (op == 5) {
    assert(size == 24);
    NetworkEndpoint::ipv4(127, 0, 0, 1, 1234).encode({b, size});
    return 24;
  }
  if (op == 7) {
    assert(size == 8);
    wire::put64(b, accepted);
    return 8;
  }
  if (op == 2) {
    assert(kind == 2 && size == 27 && std::memcmp(data + 24, "abc", 3) == 0);
    return size;
  }
  if (op == 1 && kind == 2) {
    assert(size >= 27);
    NetworkEndpoint::ipv4(127, 0, 0, 1, 4321).encode({b, size});
    std::memcpy(data + 24, "xyz", 3);
    return 27;
  }
  if (op == 1 && kind == 4) {
    assert(size >= 36);
    std::memset(data, 0, 32);
    data[0] = 1;
    wire::put16(b + 2, 1);
    wire::put16(b + 4, 3);
    NetworkEndpoint::ipv4(127, 0, 0, 1, 5678).encode({b + 8, 24});
    std::memcpy(data + 32, "Aabc", 4);
    return 36;
  }
  return -7;
}
static void reply(RequestTable &requests) {
  std::array<std::byte, 12> b{};
  if (fail)
    wire::put32(b.data(), std::uint32_t(Error::unsupported));
  else
    wire::put64(b.data() + 4, handle);
  assert(requests.dispatch({9, 7, token, std::span{b}.first(fail ? 4 : 12)}));
}
static bool done = false;
static Task<void> exercise(LocalNetwork net, Permission &p) {
  {
    auto l = co_await net.listen(p);
    assert(l);
    auto e = l->local_endpoint();
    assert(e && e->port == 1234);
    auto peer = co_await l->accept();
    assert(peer);
  }
  {
    auto d = co_await net.bind_datagram(p);
    assert(d);
    std::array<std::byte, 64> b{};
    auto n = d->send_to(NetworkEndpoint::ipv4(127, 0, 0, 1, 9),
                        std::as_bytes(std::span{"abc", 3}), b);
    assert(n && *n == 3);
    auto received = co_await d->receive_from(b);
    assert(received && received->source.port == 4321 &&
           received->payload.size() == 3);
  }
  {
    auto browser = co_await net.browse("_pxatest._tcp", p);
    assert(browser);
    std::array<std::byte, 512> b{};
    auto s = co_await browser->next(b);
    assert(s && s->instance == "A" && s->type == "abc" &&
           s->endpoint.port == 5678);
  }
  fail = true;
  auto invalid = co_await net.listen(p);
  assert(!invalid && invalid.error() == Error::unsupported);
  done = true;
  co_return Result<void>{};
}
int main() {
  Transport t;
  t.phase(Phase::event);
  RequestTable requests;
  TaskScope scope;
  Permission permission(t, grant);
  assert(scope.start(exercise(LocalNetwork(t, requests), permission)));
  for (unsigned i = 0; i < 4; ++i)
    reply(requests);
  assert(done && closes == 4);
  std::cout << "LAN SDK PASS: listen/accept, datagram framing, borrowed "
               "discovery views, unsupported cleanup\n";
}
