// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstring>
#include <pxa/app.hpp>
#include <pxa/ui_layout.hpp>

namespace {
constexpr std::string_view service_type = "_pxademo._tcp";
constexpr std::string_view message = "PXA LAN echo";
struct App {
  pxa::ui::State<std::string> status{"Starting local network"};
  std::array<std::byte, 1232> receive{}, scratch{};
  void report(pxa::Context &ctx, std::string_view text) {
    (void)ctx.log().write(pxa::LogLevel::info, text);
    status.set(std::string(text));
  }
  auto view() {
    using namespace pxa::ui;
    using namespace pxa::ui::literals;
    return Column(
               Text(PXA_LAN_DEMO_SERVER ? "Network server" : "Network client")
                   .font(Font::headline),
               Text(status))
        .gap(8_dp)
        .padding(12_dp);
  }
  pxa::Task<void> transfer(pxa::Socket &socket, bool send_first) {
    auto bytes = std::as_bytes(std::span{message.data(), message.size()});
    if (send_first) {
      while (!bytes.empty()) {
        auto n = co_await socket.write_some(bytes);
        if (!n || !*n)
          co_return std::unexpected(n ? pxa::Error::protocol_error : n.error());
        bytes = bytes.subspan(*n);
      }
    }
    std::size_t at = 0;
    while (at != message.size()) {
      auto n = co_await socket.read_some(
          std::span{receive}.subspan(at, message.size() - at));
      if (!n || !*n)
        co_return std::unexpected(n ? pxa::Error::protocol_error : n.error());
      at += *n;
    }
    if (std::memcmp(receive.data(), message.data(), message.size()))
      co_return std::unexpected(pxa::Error::protocol_error);
    if (!send_first) {
      auto echo = std::span<const std::byte>{receive}.first(message.size());
      while (!echo.empty()) {
        auto n = co_await socket.write_some(echo);
        if (!n || !*n)
          co_return std::unexpected(n ? pxa::Error::protocol_error : n.error());
        echo = echo.subspan(*n);
      }
    }
    co_return pxa::Result<void>{};
  }
  pxa::Task<void> server(pxa::Context &ctx) {
    auto caps = co_await ctx.network().capabilities();
    if (!caps || !caps->listen() || !caps->datagrams() || !caps->discovery())
      co_return std::unexpected(pxa::Error::unsupported);
    auto listen_grant =
        co_await ctx.permissions().acquire("net.listen", PXA_LAN_DEMO_SCOPE);
    if (!listen_grant)
      co_return std::unexpected(listen_grant.error());
    auto udp_grant =
        co_await ctx.permissions().acquire("net.datagram", PXA_LAN_DEMO_SCOPE);
    if (!udp_grant)
      co_return std::unexpected(udp_grant.error());
    auto discovery_grant =
        co_await ctx.permissions().acquire("net.discovery", "local-network");
    if (!discovery_grant)
      co_return std::unexpected(discovery_grant.error());
    pxa::NetworkOptions options;
    options.bind.port = PXA_LAN_DEMO_PORT;
    auto listener = co_await ctx.network().listen(*listen_grant, options);
    if (!listener)
      co_return std::unexpected(listener.error());
    auto datagram = co_await ctx.network().bind_datagram(*udp_grant, options);
    if (!datagram)
      co_return std::unexpected(datagram.error());
    auto tcp_address = listener->local_endpoint(),
         udp_address = datagram->local_endpoint();
    if (!tcp_address || !udp_address)
      co_return std::unexpected(pxa::Error::protocol_error);
    char txt[32];
    int size =
        std::snprintf(txt + 1, sizeof(txt) - 1, "udp=%u", udp_address->port);
    txt[0] = static_cast<char>(size);
    auto advertisement = co_await ctx.network().advertise(
        service_type, "PXA echo", *tcp_address,
        std::as_bytes(std::span{txt, static_cast<std::size_t>(size + 1)}),
        *discovery_grant);
    if (!advertisement)
      co_return std::unexpected(advertisement.error());
    char ready[128];
    auto &ip = tcp_address->address;
    std::snprintf(
        ready, sizeof(ready), "LAN_SERVER READY %u.%u.%u.%u TCP=%u UDP=%u",
        std::to_integer<unsigned>(ip[0]), std::to_integer<unsigned>(ip[1]),
        std::to_integer<unsigned>(ip[2]), std::to_integer<unsigned>(ip[3]),
        tcp_address->port, udp_address->port);
    report(ctx, ready);
    auto peer = co_await listener->accept();
    if (!peer)
      co_return std::unexpected(peer.error());
    auto tcp = co_await transfer(*peer, false);
    if (!tcp)
      co_return std::unexpected(tcp.error());
    auto packet = co_await datagram->receive_from(receive);
    if (!packet)
      co_return std::unexpected(packet.error());
    if (packet->payload.size() != message.size() ||
        std::memcmp(packet->payload.data(), message.data(), message.size()))
      co_return std::unexpected(pxa::Error::protocol_error);
    auto sent = datagram->send_to(packet->source, packet->payload, scratch);
    if (!sent)
      co_return std::unexpected(sent.error());
    // WRITE admits into a bounded queue. Keep the resource alive until sent.
    for (;;) {
      auto stats = datagram->stats();
      if (!stats)
        co_return std::unexpected(stats.error());
      if (stats->sent_bytes == message.size())
        break;
      auto yielded = co_await ctx.clock().yield();
      if (!yielded)
        co_return std::unexpected(yielded.error());
    }
    report(ctx, "LAN_SERVER PASS: discovered TCP + UDP echo");
    co_return pxa::Result<void>{};
  }
  struct Peer {
    pxa::NetworkEndpoint tcp;
    std::uint16_t udp;
  };
  pxa::Task<Peer> resolve(pxa::Context &ctx) {
    auto discovery_grant =
        co_await ctx.permissions().acquire("net.discovery", "local-network");
    if (!discovery_grant)
      co_return std::unexpected(discovery_grant.error());
    auto browser =
        co_await ctx.network().browse(service_type, *discovery_grant);
    if (!browser)
      co_return std::unexpected(browser.error());
    report(ctx, "LAN_CLIENT searching");
    pxa::NetworkEndpoint tcp_address;
    unsigned udp_port = 0;
    for (;;) {
      auto found = co_await browser->next(receive);
      if (!found)
        co_return std::unexpected(found.error());
      if (found->change == pxa::DiscoveredService::Change::removed)
        continue;
      tcp_address = found->endpoint;
      for (std::size_t at = 0; at < found->txt.size();) {
        auto n = std::to_integer<unsigned>(found->txt[at++]);
        if (n > found->txt.size() - at)
          break;
        if (n > 4 && std::memcmp(found->txt.data() + at, "udp=", 4) == 0) {
          unsigned value = 0;
          for (unsigned j = 4; j < n; ++j) {
            auto c = std::to_integer<unsigned>(found->txt[at + j]);
            if (c < '0' || c > '9' || value > 6553) {
              value = 0;
              break;
            }
            value = value * 10 + c - '0';
          }
          if (value <= 65535)
            udp_port = value;
        }
        at += n;
      }
      if (udp_port)
        break;
    }
    co_return Peer{tcp_address, static_cast<std::uint16_t>(udp_port)};
  }
  pxa::Task<void> client(pxa::Context &ctx) {
    auto udp_grant =
        co_await ctx.permissions().acquire("net.datagram", PXA_LAN_DEMO_SCOPE);
    if (!udp_grant)
      co_return std::unexpected(udp_grant.error());
    auto socket_grant =
        co_await ctx.permissions().acquire("net.socket", "outbound");
    if (!socket_grant)
      co_return std::unexpected(socket_grant.error());
    pxa::NetworkEndpoint tcp_address;
    unsigned udp_port = PXA_LAN_DEMO_PORT;
    constexpr std::string_view direct = PXA_LAN_DEMO_ENDPOINT;
    if constexpr (!direct.empty()) {
      unsigned a, b, c, d, port;
      char tail;
      if (std::sscanf(direct.data(), "tcp://%u.%u.%u.%u:%u%c", &a, &b, &c, &d,
                      &port, &tail) != 5 ||
          a > 255 || b > 255 || c > 255 || d > 255 || !port || port > 65535)
        co_return std::unexpected(pxa::Error::invalid_argument);
      tcp_address = pxa::NetworkEndpoint::ipv4(
          a, b, c, d, static_cast<std::uint16_t>(port));
      report(ctx, "LAN_CLIENT direct IP (no discovery)");
    } else {
      auto peer = co_await resolve(ctx);
      if (!peer)
        co_return std::unexpected(peer.error());
      tcp_address = peer->tcp;
      udp_port = peer->udp;
    }
    char endpoint[80];
    auto &ip = tcp_address.address;
    std::snprintf(endpoint, sizeof(endpoint), "tcp://%u.%u.%u.%u:%u",
                  std::to_integer<unsigned>(ip[0]),
                  std::to_integer<unsigned>(ip[1]),
                  std::to_integer<unsigned>(ip[2]),
                  std::to_integer<unsigned>(ip[3]), tcp_address.port);
    auto socket = co_await ctx.network().connect(endpoint, *socket_grant,
                                                 {.buffer_bytes = 4096});
    if (!socket)
      co_return std::unexpected(socket.error());
    auto tcp = co_await transfer(*socket, true);
    if (!tcp)
      co_return std::unexpected(tcp.error());
    auto datagram = co_await ctx.network().bind_datagram(*udp_grant);
    if (!datagram)
      co_return std::unexpected(datagram.error());
    tcp_address.port = static_cast<std::uint16_t>(udp_port);
    auto sent = datagram->send_to(
        tcp_address, std::as_bytes(std::span{message.data(), message.size()}),
        scratch);
    if (!sent)
      co_return std::unexpected(sent.error());
    auto packet = co_await datagram->receive_from(receive);
    if (!packet)
      co_return std::unexpected(packet.error());
    if (packet->payload.size() != message.size() ||
        std::memcmp(packet->payload.data(), message.data(), message.size()))
      co_return std::unexpected(pxa::Error::protocol_error);
    report(ctx, direct.empty() ? "LAN_CLIENT PASS: discovered TCP + UDP echo"
                               : "LAN_CLIENT PASS: direct IP TCP + UDP echo");
    co_return pxa::Result<void>{};
  }
  pxa::Result<void> on_start(pxa::Context &ctx) {
    return ctx.tasks().start(PXA_LAN_DEMO_SERVER ? server(ctx) : client(ctx));
  }
  void on_error(pxa::Context &ctx, pxa::Error error) {
    char line[80];
    std::snprintf(line, sizeof(line), "LAN_ERROR %d", static_cast<int>(error));
    report(ctx, line);
  }
};
} // namespace
PXA_APPLICATION(App)
