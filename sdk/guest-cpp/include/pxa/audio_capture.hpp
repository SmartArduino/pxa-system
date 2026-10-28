// SPDX-License-Identifier: MIT
#pragma once
#include "audio_protocol_generated.hpp"
#include "permission.hpp"
namespace pxa {
struct AudioCaptureTag {};
class AudioCapture {
  Transport *transport_ = nullptr;
  Resource<AudioCaptureTag> resource_;
  std::uint16_t frame_ms_ = 20;

public:
  static constexpr std::uint32_t sample_rate = 16000,
                                 max_packet_bytes = 1275;
  AudioCapture() = default;
  AudioCapture(Transport &t, std::uint64_t h, std::uint16_t frame_ms = 20)
      : transport_(&t), resource_(t, h), frame_ms_(frame_ms) {}
  AudioCapture(AudioCapture &&) noexcept = default;
  AudioCapture &operator=(AudioCapture &&) noexcept = default;
  explicit operator bool() const noexcept { return bool(resource_); }
  Result<void> close() noexcept { return resource_.close(); }
  std::uint16_t frame_ms() const noexcept { return frame_ms_; }
  // One raw Opus packet, mono 16 kHz. Supply max_packet_bytes storage.
  Result<std::uint32_t> read(std::span<std::byte> out) noexcept {
    if (!resource_ || !transport_)
      return std::unexpected(Error::bad_state);
    return transport_->io(resource_.handle(), 1, out);
  }
};
class AudioCaptureService {
  Transport &transport_;
  RequestTable &requests_;
  static Task<AudioCapture> open_impl(AudioCaptureService self,
                                      std::uint64_t permission) {
    std::array<std::byte, 8> packet{};
    wire::put64(packet.data(), permission);
    auto event = co_await Response(
        self.transport_, self.requests_, detail::audio_wire::service,
        detail::audio_wire::open_capture, packet, true, 4);
    if (!event)
      co_return std::unexpected(event.error());
    auto b = wire::result_body(event->payload);
    if (!b)
      co_return std::unexpected(b.error());
    AudioCapture pending;
    if (b->size() >= 8 && (wire::get64(b->data()) >> 32))
      pending = AudioCapture(self.transport_, wire::get64(b->data()),
                             b->size() >= 16 ? wire::get16(b->data() + 14) : 0);
    if (b->size() != 20 || !pending || wire::get32(b->data() + 8) != 16000 ||
        (*b)[12] != std::byte{1} || (*b)[13] != std::byte{1} ||
        (pending.frame_ms() != 20 && pending.frame_ms() != 40 && pending.frame_ms() != 60) ||
        wire::get32(b->data() + 16) != 1275)
      co_return std::unexpected(Error::protocol_error);
    co_return std::move(pending);
  }

public:
  AudioCaptureService(Transport &t, RequestTable &r)
      : transport_(t), requests_(r) {}
  Task<AudioCapture> open(const Permission &permission) {
    return open_impl(*this, permission.handle());
  }
};
} // namespace pxa
