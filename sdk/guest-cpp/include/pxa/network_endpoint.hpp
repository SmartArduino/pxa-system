// SPDX-License-Identifier: MIT
#pragma once
#include "async_io.hpp"
#include <algorithm>
namespace pxa {
struct NetworkEndpoint {
  std::uint16_t family = 4, port = 0;
  std::uint32_t scope = 0;
  std::array<std::byte, 16> address{};
  static NetworkEndpoint ipv4(unsigned a, unsigned b, unsigned c, unsigned d,
                              std::uint16_t port = 0) {
    NetworkEndpoint e;
    e.port = port;
    e.address[0] = std::byte(a);
    e.address[1] = std::byte(b);
    e.address[2] = std::byte(c);
    e.address[3] = std::byte(d);
    return e;
  }
  void encode(std::span<std::byte> out) const {
    wire::put16(out.data(), family);
    wire::put16(out.data() + 2, port);
    wire::put32(out.data() + 4, scope);
    std::copy(address.begin(), address.end(), out.begin() + 8);
  }
  static Result<NetworkEndpoint> decode(std::span<const std::byte> in) {
    if (in.size() < 24)
      return std::unexpected(Error::protocol_error);
    NetworkEndpoint e;
    e.family = wire::get16(in.data());
    e.port = wire::get16(in.data() + 2);
    e.scope = wire::get32(in.data() + 4);
    if (e.family != 4 && e.family != 6)
      return std::unexpected(Error::protocol_error);
    std::copy_n(in.begin() + 8, 16, e.address.begin());
    if (e.family == 4) {
      if (e.scope)
        return std::unexpected(Error::protocol_error);
      for (unsigned i = 4; i < 16; ++i)
        if (e.address[i] != std::byte{})
          return std::unexpected(Error::protocol_error);
    }
    return e;
  }
};
struct NetworkStats {
  std::uint64_t received_bytes = 0, sent_bytes = 0;
  std::uint32_t dropped_packets = 0, receive_queued_bytes = 0,
                send_queued_bytes = 0, buffer_bytes = 0;
};
namespace detail {
inline Result<NetworkEndpoint>
network_endpoint(Transport *transport, std::uint64_t handle, unsigned op) {
  if (!transport || !handle)
    return std::unexpected(Error::bad_state);
  std::array<std::byte, 24> b{};
  auto n = transport->io(handle, op, b);
  if (!n)
    return std::unexpected(n.error());
  if (*n != b.size())
    return std::unexpected(Error::protocol_error);
  return NetworkEndpoint::decode(b);
}
inline Result<NetworkStats> network_stats(Transport *transport,
                                          std::uint64_t handle) {
  if (!transport || !handle)
    return std::unexpected(Error::bad_state);
  std::array<std::byte, 32> b{};
  auto n = transport->io(handle, 8, b);
  if (!n)
    return std::unexpected(n.error());
  if (*n != b.size())
    return std::unexpected(Error::protocol_error);
  return NetworkStats{wire::get64(b.data()),      wire::get64(b.data() + 8),
                      wire::get32(b.data() + 16), wire::get32(b.data() + 20),
                      wire::get32(b.data() + 24), wire::get32(b.data() + 28)};
}
} // namespace detail
} // namespace pxa
