// SPDX-License-Identifier: MIT
#include <cstdio>
#include <ctime>
#include <pxa/app.hpp>
#include <pxa/network.hpp>
#include <pxa/ui_layout.hpp>

namespace {
// One standard A query for example.com. No discovery grant or hostname lookup
// is used to reach the resolver; both transports address 1.1.1.1:53 directly.
constexpr unsigned char question[] = {
    0x70, 0x78, 1,   0,   0,   1, 0,   0,   0,   0, 0, 0, 7, 'e', 'x',
    'a',  'm',  'p', 'l', 'e', 3, 'c', 'o', 'm', 0, 0, 1, 0, 1};
std::uint64_t now_ms() {
  timespec t{};
  clock_gettime(CLOCK_MONOTONIC, &t);
  return std::uint64_t(t.tv_sec) * 1000 + std::uint64_t(t.tv_nsec) / 1000000;
}
struct App {
  pxa::ui::State<std::string> status{"DNS over UDP and TCP"};
  std::array<std::byte, 1232> buffer{}, scratch{};
  void report(pxa::Context &ctx, std::string_view text) {
    (void)ctx.log().write(pxa::LogLevel::info, text);
    status.set(std::string(text));
  }
  auto view() {
    using namespace pxa::ui;
    using namespace pxa::ui::literals;
    return Column(Text("IP direct: 1.1.1.1:53").font(Font::headline),
                  Text(status))
        .gap(8_dp)
        .padding(12_dp);
  }
  bool valid(std::span<const std::byte> body) {
    if (body.size() < 12)
      return false;
    auto p = reinterpret_cast<const unsigned char *>(body.data());
    return p[0] == question[0] && p[1] == question[1] && (p[2] & 0x80) &&
           !(p[2] & 2) && !(p[3] & 15) && (p[6] || p[7]);
  }
  pxa::Task<void> run(pxa::Context &ctx) {
    auto grant = co_await ctx.permissions().acquire("net.datagram", "network");
    if (!grant)
      co_return std::unexpected(grant.error());
    auto datagram = co_await ctx.network().bind_datagram(*grant);
    if (!datagram)
      co_return std::unexpected(datagram.error());
    auto start = now_ms();
    auto sent = datagram->send_to(pxa::NetworkEndpoint::ipv4(1, 1, 1, 1, 53),
                                  std::as_bytes(std::span{question}), scratch);
    if (!sent)
      co_return std::unexpected(sent.error());
    auto packet = co_await datagram->receive_from(buffer);
    if (!packet)
      co_return std::unexpected(packet.error());
    const auto expected = pxa::NetworkEndpoint::ipv4(1, 1, 1, 1, 53);
    if (packet->source.family != 4 || packet->source.port != 53 ||
        packet->source.address != expected.address || !valid(packet->payload))
      co_return std::unexpected(pxa::Error::protocol_error);
    char line[112];
    std::snprintf(line, sizeof(line),
                  "IP_DNS UDP PASS: public IPv4, bytes=%zu app_ms=%llu",
                  packet->payload.size(),
                  static_cast<unsigned long long>(now_ms() - start));
    report(ctx, line);
    auto closed = datagram->close();
    if (!closed)
      co_return std::unexpected(closed.error());
    auto tcp_grant =
        co_await ctx.permissions().acquire("net.socket", "tcp://1.1.1.1:53");
    if (!tcp_grant)
      co_return std::unexpected(tcp_grant.error());
    start = now_ms();
    auto socket = co_await ctx.network().connect(
        "tcp://1.1.1.1:53", *tcp_grant,
        {.buffer_bytes = 4096, .connect_timeout_ms = 15000});
    if (!socket)
      co_return std::unexpected(socket.error());
    scratch[0] = std::byte{0};
    scratch[1] = std::byte{sizeof(question)};
    std::copy_n(reinterpret_cast<const std::byte *>(question), sizeof(question),
                scratch.begin() + 2);
    auto outgoing =
        std::span<const std::byte>{scratch}.first(2 + sizeof(question));
    while (!outgoing.empty()) {
      auto n = co_await socket->write_some(outgoing);
      if (!n || !*n)
        co_return std::unexpected(n ? pxa::Error::protocol_error : n.error());
      outgoing = outgoing.subspan(*n);
    }
    std::size_t at = 0, required = 2;
    while (at != required) {
      auto n = co_await socket->read_some(
          std::span{buffer}.subspan(at, required - at));
      if (!n || !*n)
        co_return std::unexpected(n ? pxa::Error::protocol_error : n.error());
      at += *n;
      if (at == 2 && required == 2) {
        required += (std::to_integer<unsigned>(buffer[0]) << 8) |
                    std::to_integer<unsigned>(buffer[1]);
        if (required < 14 || required > buffer.size())
          co_return std::unexpected(pxa::Error::protocol_error);
      }
    }
    if (!valid(std::span<const std::byte>{buffer}.subspan(2, required - 2)))
      co_return std::unexpected(pxa::Error::protocol_error);
    std::snprintf(line, sizeof(line),
                  "IP_DNS TCP PASS: public IPv4, bytes=%zu app_ms=%llu",
                  required - 2,
                  static_cast<unsigned long long>(now_ms() - start));
    report(ctx, line);
    report(ctx, "IP_DNS ALL PASS: direct public IPv4 UDP + TCP");
    co_return pxa::Result<void>{};
  }
  pxa::Result<void> on_start(pxa::Context &ctx) {
    return ctx.tasks().start(run(ctx));
  }
  void on_error(pxa::Context &ctx, pxa::Error error) {
    char line[64];
    std::snprintf(line, sizeof(line), "IP_DNS ERROR %d",
                  static_cast<int>(error));
    report(ctx, line);
  }
};
} // namespace
PXA_APPLICATION(App)
