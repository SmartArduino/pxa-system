#pragma once

#include "task.hpp"
#include "service_wire.hpp"

namespace pxa {

enum class LeaseKind : std::uint16_t {
    foreground = 1, audio_playback, audio_capture, network_transfer,
    sensor_monitor, scheduled_job
};
struct LeaseTag {};
using Lease = Resource<LeaseTag>;
struct LeaseRevoked {
    std::uint64_t handle;
    Error reason;
};

inline Result<LeaseRevoked> decode_lease_revoked(const Event& event) noexcept {
    if (event.service != 1 || event.opcode != 0x8003 || event.token ||
        event.payload.size() != 12 || !(wire::get64(event.payload.data()) >> 32))
        return std::unexpected(Error::protocol_error);
    return LeaseRevoked{wire::get64(event.payload.data()),
        static_cast<Error>(wire::get32(event.payload.data() + 8))};
}

// Low-level execution/resource lifetime; ordinary background work uses Work.
class LeaseService {
public:
    LeaseService(Transport& transport, RequestTable& requests) noexcept
        : transport_(transport), requests_(requests) {}

    Task<Lease> acquire(this LeaseService self, LeaseKind kind,
                         std::uint32_t duration_ms = 0) {
        const auto value = static_cast<std::uint16_t>(kind);
        if (value < 1 || value > 6)
            co_return std::unexpected(Error::invalid_argument);
        std::array<std::byte, 14> packet{};
        wire::Writer writer(packet);
        writer.u16(1); writer.u16(2); writer.u16(value);
        if (duration_ms) {
            writer.u16(2); writer.u16(4); writer.u32(duration_ms);
        }
        auto event = co_await Response(self.transport_, self.requests_, 1, 3,
            std::span{packet}.first(writer.size()), true, 8);
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        wire::Records records(*body);
        auto handle = records.take(4, 8);
        Lease pending;
        if (handle && (wire::get64(handle->data()) >> 32))
            pending = Lease(self.transport_, wire::get64(handle->data()));
        if (!pending || !records.empty())
            co_return std::unexpected(Error::protocol_error);
        co_return std::move(pending);
    }
private:
    Transport& transport_;
    RequestTable& requests_;
};

} // namespace pxa
