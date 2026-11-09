// SPDX-License-Identifier: MIT
#pragma once
#include "http.hpp"
#include "socket.hpp"
namespace pxa {
struct NetworkOptions {
  NetworkEndpoint bind{};
  std::uint32_t buffer_bytes = 4096;
  std::uint16_t max_items = 3;
  bool no_delay = true, broadcast = false;
};
struct LocalResourceTag {};
class LocalResource {
protected:
  Transport *transport_ = nullptr;
  RequestTable *requests_ = nullptr;
  Resource<LocalResourceTag> resource_;

public:
  LocalResource() = default;
  LocalResource(Transport &t, RequestTable &r, std::uint64_t h)
      : transport_(&t), requests_(&r), resource_(t, h) {}
  LocalResource(LocalResource &&) = default;
  LocalResource &operator=(LocalResource &&) = default;
  explicit operator bool() const { return bool(resource_); }
  Result<void> close() { return resource_.close(); }
  Result<NetworkEndpoint> local_endpoint() {
    return detail::network_endpoint(transport_, resource_.handle(), 5);
  }
  Result<NetworkStats> stats() {
    return detail::network_stats(transport_, resource_.handle());
  }
  Result<std::uint32_t> read(std::span<std::byte> b) {
    if (!transport_ || !resource_)
      return std::unexpected(Error::bad_state);
    return transport_->io(resource_.handle(), 1, b);
  }
  AsyncIo read_some(std::span<std::byte> b) & {
    return {transport_, requests_, resource_.handle(), 1, 1, b};
  }
};
class Listener : public LocalResource {
public:
  using LocalResource::LocalResource;
  Task<Socket> accept() & {
    std::array<std::byte, 8> b{};
    auto n =
        co_await AsyncIo(transport_, requests_, resource_.handle(), 7, 1, b);
    if (!n)
      co_return std::unexpected(n.error());
    auto h = wire::get64(b.data());
    if (*n != 8 || !(h >> 32))
      co_return std::unexpected(Error::protocol_error);
    co_return Socket(*transport_, *requests_, h);
  }
};
struct ReceivedDatagram {
  NetworkEndpoint source;
  std::span<const std::byte> payload;
};
class DatagramSocket : public LocalResource {
public:
  using LocalResource::LocalResource;
  Task<ReceivedDatagram> receive_from(std::span<std::byte> buffer) & {
    auto n = co_await read_some(buffer);
    if (!n)
      co_return std::unexpected(n.error());
    if (*n < 24 || *n > buffer.size())
      co_return std::unexpected(Error::protocol_error);
    auto e = NetworkEndpoint::decode(buffer.first(*n));
    if (!e)
      co_return std::unexpected(e.error());
    co_return ReceivedDatagram{*e, buffer.subspan(24, *n - 24)};
  }
  // Scratch is caller-owned; send admission is atomic, never a partial packet.
  Result<std::uint32_t> send_to(const NetworkEndpoint &e,
                                std::span<const std::byte> data,
                                std::span<std::byte> scratch) & {
    if (!transport_ || !resource_)
      return std::unexpected(Error::bad_state);
    if (data.size() > 1200 || scratch.size() < data.size() + 24)
      return std::unexpected(Error::invalid_argument);
    e.encode(scratch);
    std::copy(data.begin(), data.end(), scratch.begin() + 24);
    auto n =
        transport_->io(resource_.handle(), 2, scratch.first(data.size() + 24));
    if (!n)
      return std::unexpected(n.error());
    if (*n != data.size() + 24)
      return std::unexpected(Error::protocol_error);
    return std::uint32_t(data.size());
  }
};
struct DiscoveredService {
  enum class Change : std::uint8_t { added = 1, updated = 2, removed = 3 };
  Change change;
  NetworkEndpoint endpoint;
  std::string_view instance, type;
  std::span<const std::byte> txt;
};
class ServiceBrowser : public LocalResource {
public:
  using LocalResource::LocalResource;
  // Returned views borrow the caller's buffer until its next modification.
  Task<DiscoveredService> next(std::span<std::byte> buffer) & {
    auto n = co_await read_some(buffer);
    if (!n)
      co_return std::unexpected(n.error());
    if (*n < 32 || *n > buffer.size() || buffer[0] < std::byte{1} ||
        buffer[0] > std::byte{3})
      co_return std::unexpected(Error::protocol_error);
    const auto name = wire::get16(buffer.data() + 2),
               type = wire::get16(buffer.data() + 4),
               txt = wire::get16(buffer.data() + 6);
    if (32u + name + type + txt != *n)
      co_return std::unexpected(Error::protocol_error);
    auto e = NetworkEndpoint::decode(buffer.subspan(8, 24));
    if (!e)
      co_return std::unexpected(e.error());
    co_return DiscoveredService{
        DiscoveredService::Change(std::to_integer<unsigned>(buffer[0])),
        *e,
        {reinterpret_cast<const char *>(buffer.data() + 32), name},
        {reinterpret_cast<const char *>(buffer.data() + 32 + name), type},
        buffer.subspan(32 + name + type, txt)};
  }
};
class ServiceAdvertisement : public LocalResource {
public:
  using LocalResource::LocalResource;
};
struct NetworkCapabilities {
  std::uint32_t features = 0, min_buffer_bytes = 0, max_buffer_bytes = 0,
                max_datagram_bytes = 0;
  std::uint16_t max_resources = 0, max_per_component = 0,
                max_discovered_services = 0, max_instance_bytes = 0,
                max_txt_bytes = 0, max_type_bytes = 0;
  bool connect() const { return features & 16; }
  bool listen() const { return features & 32; }
  bool datagrams() const { return features & 64; }
  bool discovery() const { return features & 128; }
  bool ipv4() const { return features & 256; }
};
class Network {
  Transport &transport_;
  RequestTable &requests_;
  template <class T>
  static Task<T> open(Network self, wire::RequestPacket<512> packet) {
    auto event = co_await Response(self.transport_, self.requests_, 9, 7,
                                   packet.view(), true, 4);
    if (!event)
      co_return std::unexpected(event.error());
    auto b = wire::result_body(event->payload);
    if (!b)
      co_return std::unexpected(b.error());
    if (b->size() != 8 || !(wire::get64(b->data()) >> 32))
      co_return std::unexpected(Error::protocol_error);
    T resource(self.transport_, self.requests_, wire::get64(b->data()));
    auto ready = co_await AsyncIo(&self.transport_, &self.requests_,
                                  wire::get64(b->data()), 4, 2, {});
    if (!ready)
      co_return std::unexpected(ready.error());
    if (*ready != 1)
      co_return std::unexpected(Error::protocol_error);
    co_return std::move(resource);
  }
  template <class T>
  Task<T> create(unsigned kind, const Permission &p, NetworkOptions options,
                 std::string_view type = {}, std::string_view name = {},
                 std::span<const std::byte> txt = {}) {
    if (!p || type.size() > 63 || name.size() > 54 || txt.size() > 256 ||
        options.buffer_bytes < 512 || options.buffer_bytes > 16384 ||
        (options.buffer_bytes & (options.buffer_bytes - 1)) ||
        !options.max_items || options.max_items > 8 ||
        48 + type.size() + name.size() + txt.size() > 512)
      return Task<T>::failed(Error::invalid_argument);
    wire::RequestPacket<512> packet;
    packet.size = 48 + type.size() + name.size() + txt.size();
    wire::put64(packet.bytes.data(), p.handle());
    wire::put16(packet.bytes.data() + 8, kind);
    wire::put16(packet.bytes.data() + 10, kind == 1 ? unsigned(options.no_delay)
                                          : kind == 2
                                              ? unsigned(options.broadcast) * 2
                                              : 0);
    options.bind.encode(std::span{packet.bytes}.subspan(12, 24));
    wire::put32(packet.bytes.data() + 36, options.buffer_bytes);
    wire::put16(packet.bytes.data() + 40, options.max_items);
    wire::put16(packet.bytes.data() + 42, type.size());
    wire::put16(packet.bytes.data() + 44, name.size());
    wire::put16(packet.bytes.data() + 46, txt.size());
    auto out = packet.bytes.begin() + 48;
    for (char c : type)
      *out++ = std::byte(static_cast<unsigned char>(c));
    for (char c : name)
      *out++ = std::byte(static_cast<unsigned char>(c));
    std::copy(txt.begin(), txt.end(), out);
    return open<T>(*this, std::move(packet));
  }

  static Task<NetworkCapabilities> capabilities_impl(Network self) {
    auto event = co_await Response(self.transport_, self.requests_, 9, 8, {});
    if (!event)
      co_return std::unexpected(event.error());
    auto b = wire::result_body(event->payload);
    if (!b)
      co_return std::unexpected(b.error());
    if (b->size() != 32)
      co_return std::unexpected(Error::protocol_error);
    auto p = b->data();
    NetworkCapabilities c;
    c.features = wire::get32(p);
    c.max_resources = wire::get16(p + 4);
    c.max_per_component = wire::get16(p + 6);
    c.min_buffer_bytes = wire::get32(p + 8);
    c.max_buffer_bytes = wire::get32(p + 12);
    c.max_datagram_bytes = wire::get32(p + 16);
    c.max_discovered_services = wire::get16(p + 20);
    c.max_instance_bytes = wire::get16(p + 22);
    c.max_txt_bytes = wire::get16(p + 24);
    c.max_type_bytes = wire::get16(p + 26);
    co_return c;
  }

public:
  Network(Transport &t, RequestTable &r) : transport_(t), requests_(r) {}
  Task<NetworkCapabilities> capabilities() { return capabilities_impl(*this); }

  // Direct IP/hostname TCP or TLS. Discovery is optional; the existing
  // net.socket endpoint/outbound grant remains independent of LAN discovery.
  Task<Socket> connect(std::string_view endpoint, const Permission &p,
                       SocketOptions options = {}) {
    return SocketClient(transport_, requests_).open(endpoint, p, options);
  }
  Task<Listener> listen(const Permission &p, NetworkOptions options = {}) {
    return create<Listener>(1, p, options);
  }
  Task<DatagramSocket> bind_datagram(const Permission &p,
                                     NetworkOptions options = {}) {
    return create<DatagramSocket>(2, p, options);
  }
  Task<ServiceBrowser> browse(std::string_view type, const Permission &p,
                              NetworkOptions options = {}) {
    return create<ServiceBrowser>(4, p, options, type);
  }
  Task<ServiceAdvertisement>
  advertise(std::string_view type, std::string_view name,
            NetworkEndpoint endpoint, std::span<const std::byte> txt,
            const Permission &p, NetworkOptions options = {}) {
    options.bind = endpoint;
    return create<ServiceAdvertisement>(3, p, options, type, name, txt);
  }
};
// Compatibility names for the initial Net 0.7 SDK snapshot.
using LocalNetwork = Network;
using LocalNetworkOptions = NetworkOptions;
using LocalNetworkCapabilities = NetworkCapabilities;
} // namespace pxa
