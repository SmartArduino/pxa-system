// SPDX-License-Identifier: MIT
#pragma once
#include "net.hpp"
#include "socket.hpp"
#include "websocket_codec.hpp"
#include <memory>
#include <unistd.h>
namespace pxa {
struct WebSocketOptions {
  std::uint32_t buffer_bytes = 32768, max_message_bytes = 4096,
                connect_timeout_ms = 15000;
};
class WebSocketClient;
class WebSocket {
  friend class WebSocketClient;
  struct State {
    Socket socket;
    std::vector<std::byte> input, message, output, control;
    std::size_t limit = 0, sent = 0;
    unsigned opcode = 0;
    bool complete = false, closed = false;
  };
  std::unique_ptr<State> state_;
  explicit WebSocket(Socket socket, std::uint32_t limit)
      : state_(std::make_unique<State>()) {
    state_->socket = std::move(socket);
    state_->limit = limit;
    state_->input.reserve(limit + 14);
    state_->message.reserve(limit);
  }
  Result<void> flush() {
    auto &s = *state_;
    for (unsigned i = 0; i < 2; i++) {
      if (s.output.empty()) {
        if (s.control.empty())
          return {};
        s.output = std::move(s.control);
        s.control.clear();
        s.sent = 0;
      }
      auto n =
          s.socket.write(std::span<const std::byte>{s.output}.subspan(s.sent));
      if (!n)
        return std::unexpected(n.error());
      s.sent += *n;
      if (s.sent == s.output.size()) {
        s.output.clear();
        s.sent = 0;
      } else
        return std::unexpected(Error::would_block);
    }
    return {};
  }
  static Result<std::array<std::byte, 4>> mask() {
    std::array<std::byte, 4> value;
    if (getentropy(value.data(), value.size()))
      return std::unexpected(Error::unavailable);
    return value;
  }

public:
  WebSocket() = default;
  WebSocket(WebSocket &&) noexcept = default;
  WebSocket &operator=(WebSocket &&) noexcept = default;
  explicit operator bool() const noexcept {
    return state_ && bool(state_->socket);
  }
  Result<void> close() noexcept {
    state_.reset();
    return {};
  }
  // Atomic admission of one message (opcode:u8 + payload). Partial socket
  // writes are retained here; WOULD_BLOCK consumes no caller data.
  Result<std::uint32_t> write(std::span<const std::byte> in) {
    if (!state_ || state_->closed)
      return std::unexpected(Error::bad_state);
    auto &s = *state_;
    if (in.empty() || in.size() > s.limit + 1 ||
        (in[0] != std::byte{1} && in[0] != std::byte{2}))
      return std::unexpected(Error::invalid_argument);
    auto progress = flush();
    if (!progress)
      return std::unexpected(progress.error());
    if (!s.output.empty() || !s.control.empty())
      return std::unexpected(Error::would_block);
    auto random = mask();
    if (!random)
      return std::unexpected(random.error());
    s.output = detail::websocket::frame(std::to_integer<unsigned>(in[0]),
                                        in.subspan(1), *random);
    return static_cast<std::uint32_t>(in.size());
  }
  Result<std::uint32_t> read(std::span<std::byte> out) {
    if (!state_)
      return std::unexpected(Error::bad_state);
    auto &s = *state_;
    auto progress = flush();
    if (!progress && progress.error() != Error::would_block)
      return std::unexpected(progress.error());
    for (unsigned work = 0; work < 32; work++) {
      if (s.complete) {
        if (out.size() < s.message.size() + 1)
          return std::unexpected(Error::limit_exceeded);
        out[0] = std::byte(s.opcode);
        std::copy(s.message.begin(), s.message.end(), out.begin() + 1);
        auto size = static_cast<std::uint32_t>(s.message.size() + 1);
        s.message.clear();
        s.complete = false;
        s.opcode = 0;
        return size;
      }
      if (s.closed)
        return 0;
      bool need_more = s.input.size() < 2;
      std::size_t header = 2, length = 0;
      unsigned opcode = 0;
      bool fin = false;
      if (!need_more) {
        auto first = std::to_integer<unsigned>(s.input[0]),
             second = std::to_integer<unsigned>(s.input[1]);
        fin = first & 128;
        opcode = first & 15;
        length = second & 127;
        if ((first & 112) || (second & 128) ||
            (opcode != 0 && opcode != 1 && opcode != 2 && opcode != 8 &&
             opcode != 9 && opcode != 10))
          return std::unexpected(Error::protocol_error);
        if (length == 127)
          return std::unexpected(Error::limit_exceeded);
        if (length == 126) {
          header = 4;
          need_more = s.input.size() < 4;
          if (!need_more) {
            length = (std::to_integer<unsigned>(s.input[2]) << 8) |
                     std::to_integer<unsigned>(s.input[3]);
            if (length < 126)
              return std::unexpected(Error::protocol_error);
          }
        }
        if (!need_more) {
          if (opcode >= 8 && (!fin || length > 125))
            return std::unexpected(Error::protocol_error);
          if (length > s.limit)
            return std::unexpected(Error::limit_exceeded);
          need_more = s.input.size() < header + length;
        }
      }
      if (need_more) {
        std::array<std::byte, 2048> buffer;
        auto room = std::max<std::size_t>(s.limit + 14, 2048) - s.input.size();
        if (!room)
          return std::unexpected(Error::limit_exceeded);
        auto n = s.socket.read(
            std::span{buffer}.first(std::min(room, buffer.size())));
        if (!n)
          return std::unexpected(n.error());
        if (!*n)
          return std::unexpected(Error::io_error);
        s.input.insert(s.input.end(), buffer.begin(), buffer.begin() + *n);
        continue;
      }
      auto payload =
          std::span<const std::byte>{s.input}.subspan(header, length);
      if (opcode >= 8) {
        if (opcode == 8) {
          if (length == 1)
            return std::unexpected(Error::protocol_error);
          if (length >= 2) {
            unsigned code = (std::to_integer<unsigned>(payload[0]) << 8) |
                            std::to_integer<unsigned>(payload[1]);
            if (!((code >= 1000 && code <= 1014 && code != 1004 &&
                   code != 1005 && code != 1006) ||
                  (code >= 3000 && code <= 4999)) ||
                !detail::websocket::utf8(payload.subspan(2)))
              return std::unexpected(Error::protocol_error);
          }
        }
        if (opcode == 8 || opcode == 9) {
          if (!s.control.empty())
            return std::unexpected(Error::would_block);
          auto random = mask();
          if (!random)
            return std::unexpected(random.error());
          s.control =
              detail::websocket::frame(opcode == 9 ? 10 : 8, payload, *random);
          if (opcode == 8)
            s.closed = true;
          auto sent = flush();
          if (!sent && sent.error() != Error::would_block)
            return std::unexpected(sent.error());
        }
      } else {
        if (opcode) {
          if (s.opcode)
            return std::unexpected(Error::protocol_error);
          s.opcode = opcode;
        } else if (!s.opcode)
          return std::unexpected(Error::protocol_error);
        if (length > s.limit - s.message.size())
          return std::unexpected(Error::limit_exceeded);
        s.message.insert(s.message.end(), payload.begin(), payload.end());
        if (fin) {
          if (s.opcode == 1 && !detail::websocket::utf8(s.message))
            return std::unexpected(Error::protocol_error);
          s.complete = true;
        }
      }
      s.input.erase(s.input.begin(), s.input.begin() + header + length);
    }
    return std::unexpected(Error::would_block);
  }
};
class WebSocketClient {
  Transport &transport_;
  RequestTable &requests_;
  static Task<WebSocket> handshake(Task<Socket> pending, std::string request,
                                   std::string key, WebSocketOptions options) {
    auto opened = co_await std::move(pending);
    if (!opened)
      co_return std::unexpected(opened.error());
    Socket socket = std::move(*opened);
    auto bytes = std::as_bytes(std::span{request.data(), request.size()});
    while (!bytes.empty()) {
      auto n = co_await socket.write_some(bytes);
      if (!n)
        co_return std::unexpected(n.error());
      if (!*n)
        co_return std::unexpected(Error::io_error);
      bytes = bytes.subspan(*n);
    }
    std::string head;
    head.reserve(2048);
    auto buffer = std::make_unique<std::array<std::byte, 1024>>();
    for (;;) {
      auto n = co_await socket.read_some(*buffer);
      if (!n)
        co_return std::unexpected(n.error());
      if (!*n)
        co_return std::unexpected(Error::io_error);
      head.append(reinterpret_cast<const char *>(buffer->data()), *n);
      auto end = head.find("\r\n\r\n");
      if (end != head.npos) {
        end += 4;
        if (end > 8192 || !detail::websocket::handshake(
                              std::string_view(head).substr(0, end), key))
          co_return std::unexpected(Error::protocol_error);
        WebSocket result(std::move(socket), options.max_message_bytes);
        const auto *p = reinterpret_cast<const std::byte *>(head.data() + end);
        result.state_->input.assign(p, p + head.size() - end);
        co_return std::move(result);
      }
      if (head.size() > 8192)
        co_return std::unexpected(Error::limit_exceeded);
    }
  }

public:
  WebSocketClient(Transport &t, RequestTable &r)
      : transport_(t), requests_(r) {}
  Task<WebSocket> open(std::string_view url, const Permission &permission,
                       std::span<const NetHeaderView> headers = {},
                       WebSocketOptions options = {}) {
    bool tls = url.starts_with("wss://");
    std::size_t start = tls ? 6 : 5;
    if (!(tls || url.starts_with("ws://")) || url.size() > 512 ||
        !options.max_message_bytes || options.max_message_bytes > 16384)
      return Task<WebSocket>::failed(Error::invalid_argument);
    auto path_at = url.find_first_of("/?", start);
    auto authority = url.substr(start, path_at == url.npos ? url.size() - start
                                                           : path_at - start);
    if (authority.empty() || authority.find('@') != authority.npos ||
        url.find('#') != url.npos)
      return Task<WebSocket>::failed(Error::invalid_argument);
    for (unsigned char c : url)
      if (c <= 32 || c >= 127)
        return Task<WebSocket>::failed(Error::invalid_argument);
    std::string target = tls ? "tls://" : "tcp://";
    target += authority;
    bool has_port = authority.starts_with("[")
                        ? authority.find("]:") != authority.npos
                        : authority.find(':') != authority.npos;
    if (!has_port)
      target += tls ? ":443" : ":80";
    std::array<std::byte, 16> nonce;
    if (getentropy(nonce.data(), nonce.size()))
      return Task<WebSocket>::failed(Error::unavailable);
    auto key = detail::websocket::base64(nonce);
    std::string path =
        path_at == url.npos ? "/" : std::string(url.substr(path_at));
    if (path.front() == '?')
      path = '/' + path;
    std::string request =
        "GET " + path + " HTTP/1.1\r\nHost: " + std::string(authority) +
        "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " +
        key + "\r\nSec-WebSocket-Version: 13\r\n";
    for (auto h : headers) {
      if (h.name.empty() || h.name.size() > 64 || h.value.size() > 2048)
        return Task<WebSocket>::failed(Error::invalid_argument);
      for (unsigned char c : h.name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-'))
          return Task<WebSocket>::failed(Error::invalid_argument);
      for (unsigned char c : h.value)
        if (c < 32 || c == 127)
          return Task<WebSocket>::failed(Error::invalid_argument);
      if (detail::websocket::equal(h.name, "host") ||
          detail::websocket::equal(h.name, "connection") ||
          detail::websocket::equal(h.name, "upgrade") ||
          (h.name.size() >= 14 &&
           detail::websocket::equal(h.name.substr(0, 14), "sec-websocket-")))
        return Task<WebSocket>::failed(Error::invalid_argument);
      request += std::string(h.name) + ": " + std::string(h.value) + "\r\n";
    }
    request += "\r\n";
    if (request.size() > 4096)
      return Task<WebSocket>::failed(Error::limit_exceeded);
    return handshake(
        SocketClient(transport_, requests_)
            .open(target, permission,
                  {options.buffer_bytes, options.connect_timeout_ms}),
        std::move(request), std::move(key), options);
  }
};
} // namespace pxa
