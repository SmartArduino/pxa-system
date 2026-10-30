#pragma once
#include "permission.hpp"
namespace pxa {
struct HapticsCapabilities {
    std::uint32_t flags = 0, max_duration_ms = 0;
    bool supported() const noexcept { return flags & 1u; }
    bool amplitude_control() const noexcept { return flags & 2u; }
};
struct HapticsActuatorTag {};
class HapticsActuator {
public:
    HapticsActuator() = default;
    HapticsActuator(Transport& transport, RequestTable& requests, std::uint64_t handle) noexcept
        : resource_(transport, handle), transport_(&transport), requests_(&requests) {}
    std::uint64_t handle() const noexcept { return resource_.handle(); }
    explicit operator bool() const noexcept { return bool(resource_); }
    Result<void> close() noexcept { return resource_.close(); }
    void reset() noexcept { resource_.reset(); }
    Task<void> vibrate(std::uint32_t duration_ms, std::uint16_t amplitude = 1000) {
        if (!resource_ || !duration_ms || !amplitude || amplitude > 1000)
            return Task<void>::failed(Error::invalid_argument);
        wire::RequestPacket<16> packet;
        wire::put64(packet.bytes.data(), handle());
        wire::put32(packet.bytes.data() + 8, duration_ms);
        wire::put16(packet.bytes.data() + 12, amplitude);
        wire::put16(packet.bytes.data() + 14, 0); packet.size = 16;
        return command(*transport_, *requests_, 3, packet);
    }
    Task<void> stop() {
        if (!resource_) return Task<void>::failed(Error::invalid_argument);
        wire::RequestPacket<16> packet;
        wire::put64(packet.bytes.data(), handle()); packet.size = 8;
        return command(*transport_, *requests_, 4, packet);
    }
private:
    static Task<void> command(Transport& transport, RequestTable& requests,
        std::uint16_t opcode, wire::RequestPacket<16> packet) {
        auto event = co_await Response(transport, requests, 23, opcode, packet.packet());
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        if (!body->empty()) co_return std::unexpected(Error::protocol_error);
        co_return Result<void>{};
    }
    Resource<HapticsActuatorTag> resource_;
    Transport* transport_ = nullptr;
    RequestTable* requests_ = nullptr;
};
class HapticsService {
public:
    HapticsService(Transport& transport, RequestTable& requests) noexcept
        : transport_(transport), requests_(requests) {}
    Task<HapticsCapabilities> capabilities(this HapticsService self) {
        auto event = co_await Response(self.transport_, self.requests_, 23, 1, std::span<const std::byte>{});
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        if (body->size() != 8) co_return std::unexpected(Error::protocol_error);
        HapticsCapabilities caps{wire::get32(body->data()), wire::get32(body->data()+4)};
        if ((caps.flags & ~3u) || ((caps.flags & 2u) && !(caps.flags & 1u)) ||
            (caps.supported() != (caps.max_duration_ms != 0)))
            co_return std::unexpected(Error::protocol_error);
        co_return caps;
    }
    Task<HapticsActuator> open(const Permission& permission) {
        if (!(permission.handle() >> 32)) return Task<HapticsActuator>::failed(Error::invalid_argument);
        wire::RequestPacket<8> packet;
        wire::put64(packet.bytes.data(), permission.handle()); packet.size = 8;
        return open_impl(*this, packet);
    }
private:
    static Task<HapticsActuator> open_impl(HapticsService self, wire::RequestPacket<8> packet) {
        auto event = co_await Response(self.transport_, self.requests_, 23, 2, packet.packet(), true);
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        auto pending = wire::result_resource<HapticsActuatorTag>(self.transport_, *body);
        if (body->size() != 8 || !(pending.handle() >> 32)) co_return std::unexpected(Error::protocol_error);
        co_return HapticsActuator(self.transport_, self.requests_, pending.release());
    }
    Transport& transport_;
    RequestTable& requests_;
};
}
