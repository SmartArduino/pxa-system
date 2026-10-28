// SPDX-License-Identifier: MIT
#pragma once
#include "async_io.hpp"
#include "permission.hpp"
namespace pxa {
struct SocketTag {};
struct SocketOptions {
  std::uint32_t buffer_bytes = 32768, connect_timeout_ms = 15000;
};
class Socket {
  Transport *transport_ = nullptr;
  RequestTable *requests_ = nullptr;
  Resource<SocketTag> resource_;

public:
  Socket() = default;
  Socket(Transport &t, RequestTable &r, std::uint64_t h)
      : transport_(&t), requests_(&r), resource_(t, h) {}
  Socket(Socket &&) noexcept = default;
  Socket &operator=(Socket &&) noexcept = default;
  explicit operator bool() const noexcept { return bool(resource_); }
  Result<void> close() noexcept { return resource_.close(); }
  Result<std::uint32_t> state() noexcept {
    if (!resource_)
      return std::unexpected(Error::bad_state);
    return transport_->io(resource_.handle(), 4, {});
  }
  Result<std::uint32_t> read(std::span<std::byte> out) noexcept {
    if (!resource_)
      return std::unexpected(Error::bad_state);
    return transport_->io(resource_.handle(), 1, out);
  }
  Result<std::uint32_t> write(std::span<const std::byte> in) noexcept {
    if (!resource_)
      return std::unexpected(Error::bad_state);
    return transport_->io(resource_.handle(), 2,
                          {const_cast<std::byte *>(in.data()), in.size()});
  }
  AsyncIo read_some(std::span<std::byte> out) & noexcept {
    return {transport_, requests_, resource_.handle(), 1, 1, out};
  }
  AsyncIo write_some(std::span<const std::byte> in) & noexcept {
    return {
        transport_, requests_, resource_.handle(),
        2,          2,         {const_cast<std::byte *>(in.data()), in.size()}};
  }
};
class SocketClient {
  Transport &transport_;
  RequestTable &requests_;
  static Task<Socket> open_impl(SocketClient self,
                                wire::RequestPacket<384> packet) {
    auto event = co_await Response(self.transport_, self.requests_, 9, 5,
                                   packet.view(), true, 4);
    if (!event)
      co_return std::unexpected(event.error());
    auto body = wire::result_body(event->payload);
    if (!body)
      co_return std::unexpected(body.error());
    Socket pending;
    if (body->size() >= 8 && (wire::get64(body->data()) >> 32))
      pending =
          Socket(self.transport_, self.requests_, wire::get64(body->data()));
    if (body->size() != 8 || !pending)
      co_return std::unexpected(Error::protocol_error);
    co_return std::move(pending);
  }

public:
  SocketClient(Transport &t, RequestTable &r) : transport_(t), requests_(r) {}
  // tcp://host:port or tls://host:port. Exact endpoint grant or
  // net.socket/outbound. Returns an owned connecting socket. Writes/readiness
  // wait for the handshake; state() is 1 when connected, WOULD_BLOCK while
  // connecting, 0 at clean EOF.
  Task<Socket> open(std::string_view endpoint, const Permission &permission,
                    SocketOptions options = {}) {
    if (endpoint.size() > 300 || endpoint.empty() ||
        !(permission.handle() >> 32) || options.connect_timeout_ms < 100 ||
        options.connect_timeout_ms > 60000 || options.buffer_bytes < 512 ||
        options.buffer_bytes > 65536 ||
        (options.buffer_bytes & (options.buffer_bytes - 1)))
      return Task<Socket>::failed(Error::invalid_argument);
    wire::RequestPacket<384> packet;
    wire::Writer w(packet.bytes);
    bool ok = wire::record(
        w, 1, std::as_bytes(std::span{endpoint.data(), endpoint.size()}));
    std::array<std::byte, 8> handle;
    wire::put64(handle.data(), permission.handle());
    ok = ok && wire::record(w, 3, handle);
    std::array<std::byte, 4> number;
    wire::put32(number.data(), options.connect_timeout_ms);
    ok = ok && wire::record(w, 8, number);
    wire::put32(number.data(), options.buffer_bytes);
    ok = ok && wire::record(w, 14, number);
    if (!ok)
      return Task<Socket>::failed(Error::resource_limit);
    packet.size = w.size();
    return open_impl(*this, std::move(packet));
  }
};
} // namespace pxa
