#pragma once

#include "permission.hpp"

namespace pxa {

enum class SensorUnit : std::uint16_t {
    milli_celsius = 1, micro_meters_per_second_squared = 2,
    micro_radians_per_second = 3, milli_lux = 4
};
struct SensorDescriptor {
    std::uint16_t id = 0;
    FixedText<64> semantic;
    SensorUnit unit{};
    std::uint8_t dimensions = 0;
    std::uint32_t min_period_ms = 0, max_period_ms = 0;
};
struct SensorSample {
    std::uint64_t handle = 0, timestamp_us = 0;
    std::uint8_t dimensions = 0;
    std::array<std::int32_t, 3> values{};
};

Result<std::size_t> decode_sensor_list(std::span<const std::byte> payload,
    std::span<SensorDescriptor> output) noexcept;
Result<SensorSample> decode_sensor_sample(const Event& event) noexcept;

struct SensorSubscriptionTag {};
class SensorSubscription {
public:
    SensorSubscription() = default;
    SensorSubscription(Transport& transport, std::uint64_t handle,
                       std::uint8_t dimensions) noexcept
        : resource_(transport, handle), dimensions_(dimensions) {}
    std::uint64_t handle() const noexcept { return resource_.handle(); }
    explicit operator bool() const noexcept { return bool(resource_); }
    void reset() noexcept { resource_.reset(); }
    Result<SensorSample> sample(const Event& event) const noexcept {
        auto result = decode_sensor_sample(event);
        if (!result) return result;
        if (!resource_ || result->handle != handle())
            return std::unexpected(Error::not_found);
        if (result->dimensions != dimensions_)
            return std::unexpected(Error::protocol_error);
        return result;
    }
private:
    Resource<SensorSubscriptionTag> resource_;
    std::uint8_t dimensions_ = 0;
};

class SensorService {
public:
    SensorService(Transport& transport, RequestTable& requests) noexcept
        : transport_(transport), requests_(requests) {}
    Task<std::size_t> list(this SensorService self,
                           std::span<SensorDescriptor> output) {
        auto event = co_await Response(self.transport_, self.requests_, 8, 1,
            std::span<const std::byte>{});
        if (!event) co_return std::unexpected(event.error());
        co_return decode_sensor_list(event->payload, output);
    }
    Task<SensorSubscription> subscribe(const SensorDescriptor& descriptor,
        std::uint32_t period_ms, const Permission& permission) {
        if (!descriptor.id || !descriptor.dimensions || descriptor.dimensions > 3 ||
            !descriptor.min_period_ms || descriptor.min_period_ms > descriptor.max_period_ms ||
            period_ms < descriptor.min_period_ms || period_ms > descriptor.max_period_ms ||
            !(permission.handle() >> 32))
            return Task<SensorSubscription>::failed(Error::invalid_argument);
        wire::RequestPacket<26> packet;
        wire::Writer writer(std::span(packet.bytes).subspan(wire::header_bytes));
        writer.u16(1); writer.u16(2); writer.u16(descriptor.id);
        writer.u16(2); writer.u16(4); writer.u32(period_ms);
        writer.u16(3); writer.u16(8); writer.u64(permission.handle());
        packet.size = writer.size();
        return subscribe_impl(*this, packet, descriptor.dimensions);
    }
private:
    static Task<SensorSubscription> subscribe_impl(SensorService self,
        wire::RequestPacket<26> packet, std::uint8_t dimensions) {
        auto event = co_await Response(self.transport_, self.requests_, 8, 2,
            Response::PrebuiltPacket{packet.packet()}, true);
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        if (body->size() != 8 || !(wire::get64(body->data()) >> 32))
            co_return std::unexpected(Error::protocol_error);
        co_return SensorSubscription(self.transport_, wire::get64(body->data()), dimensions);
    }
    Transport& transport_;
    RequestTable& requests_;
};

} // namespace pxa
