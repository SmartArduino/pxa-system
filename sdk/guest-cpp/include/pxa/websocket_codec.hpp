// SPDX-License-Identifier: MIT
#pragma once
#include "service_wire.hpp"
#include <algorithm>
#include <bit>
#include <string>
#include <vector>
namespace pxa::detail::websocket {
inline std::string base64(std::span<const std::byte> bytes) {
  constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (std::size_t i = 0; i < bytes.size(); i += 3) {
    std::uint32_t n = std::to_integer<unsigned>(bytes[i]) << 16;
    if (i + 1 < bytes.size())
      n |= std::to_integer<unsigned>(bytes[i + 1]) << 8;
    if (i + 2 < bytes.size())
      n |= std::to_integer<unsigned>(bytes[i + 2]);
    out += alphabet[n >> 18];
    out += alphabet[(n >> 12) & 63];
    out += i + 1 < bytes.size() ? alphabet[(n >> 6) & 63] : '=';
    out += i + 2 < bytes.size() ? alphabet[n & 63] : '=';
  }
  return out;
}
// SHA-1 is used only for the RFC 6455 handshake accept value.
inline std::string accept(std::string_view key) {
  std::string value(key);
  value += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  std::vector<std::byte> padded((value.size() + 9 + 63) / 64 * 64);
  std::memcpy(padded.data(), value.data(), value.size());
  padded[value.size()] = std::byte{128};
  std::uint64_t bits = value.size() * 8;
  for (unsigned i = 0; i < 8; i++)
    padded[padded.size() - 1 - i] = std::byte(bits >> (i * 8));
  std::uint32_t h[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476,
                        0xc3d2e1f0};
  for (std::size_t offset = 0; offset < padded.size(); offset += 64) {
    std::uint32_t w[80]{};
    for (unsigned i = 0; i < 16; i++)
      for (unsigned j = 0; j < 4; j++)
        w[i] =
            (w[i] << 8) | std::to_integer<unsigned>(padded[offset + 4 * i + j]);
    for (unsigned i = 16; i < 80; i++)
      w[i] = std::rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (unsigned i = 0; i < 80; i++) {
      std::uint32_t f, k;
      if (i < 20) {
        f = (b & c) | (~b & d);
        k = 0x5a827999;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ed9eba1;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8f1bbcdc;
      } else {
        f = b ^ c ^ d;
        k = 0xca62c1d6;
      }
      auto n = std::rotl(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = std::rotl(b, 30);
      b = a;
      a = n;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
  }
  std::array<std::byte, 20> digest;
  for (unsigned i = 0; i < 20; i++)
    digest[i] = std::byte(h[i / 4] >> (24 - (i % 4) * 8));
  return base64(digest);
}
inline bool utf8(std::span<const std::byte> bytes) {
  std::size_t start = 0;
  for (std::size_t i = 0; i < bytes.size(); i++)
    if (bytes[i] == std::byte{}) {
      if (!wire::valid_utf8(bytes.subspan(start, i - start)))
        return false;
      start = i + 1;
    }
  return wire::valid_utf8(bytes.subspan(start));
}
inline bool equal(std::string_view a, std::string_view b) {
  if (a.size() != b.size())
    return false;
  for (std::size_t i = 0; i < a.size(); i++) {
    auto lower = [](unsigned char c) {
      return c >= 'A' && c <= 'Z' ? c + 32 : c;
    };
    if (lower(a[i]) != lower(b[i]))
      return false;
  }
  return true;
}
inline std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
    s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
    s.remove_suffix(1);
  return s;
}
inline bool token(std::string_view field, std::string_view wanted) {
  while (!field.empty()) {
    auto comma = field.find(',');
    if (equal(trim(field.substr(0, comma)), wanted))
      return true;
    if (comma == field.npos)
      break;
    field.remove_prefix(comma + 1);
  }
  return false;
}
inline bool handshake(std::string_view head, std::string_view key) {
  auto line = head.find("\r\n");
  if (line == head.npos)
    return false;
  auto status = head.substr(0, line);
  if (!(status == "HTTP/1.1 101" || status.starts_with("HTTP/1.1 101 ")))
    return false;
  head.remove_prefix(line + 2);
  unsigned seen = 0;
  while (!head.empty() && head != "\r\n") {
    line = head.find("\r\n");
    if (line == head.npos)
      return false;
    auto field = head.substr(0, line);
    auto colon = field.find(':');
    if (colon == field.npos)
      return false;
    auto name = field.substr(0, colon), value = trim(field.substr(colon + 1));
    if (equal(name, "upgrade")) {
      if ((seen & 1) || !equal(value, "websocket"))
        return false;
      seen |= 1;
    }
    if (equal(name, "connection")) {
      if ((seen & 2) || !token(value, "upgrade"))
        return false;
      seen |= 2;
    }
    if (equal(name, "sec-websocket-accept")) {
      if ((seen & 4) || value != accept(key))
        return false;
      seen |= 4;
    }
    if (equal(name, "sec-websocket-extensions") ||
        equal(name, "sec-websocket-protocol"))
      return false;
    head.remove_prefix(line + 2);
  }
  return seen == 7;
}
inline std::vector<std::byte> frame(unsigned opcode,
                                    std::span<const std::byte> payload,
                                    std::array<std::byte, 4> mask) {
  std::vector<std::byte> out;
  out.reserve(payload.size() + 8);
  out.push_back(std::byte(0x80 | opcode));
  if (payload.size() < 126)
    out.push_back(std::byte(0x80 | payload.size()));
  else {
    out.push_back(std::byte{254});
    out.push_back(std::byte(payload.size() >> 8));
    out.push_back(std::byte(payload.size()));
  }
  out.insert(out.end(), mask.begin(), mask.end());
  for (std::size_t i = 0; i < payload.size(); i++)
    out.push_back(payload[i] ^ mask[i % 4]);
  return out;
}
} // namespace pxa::detail::websocket
