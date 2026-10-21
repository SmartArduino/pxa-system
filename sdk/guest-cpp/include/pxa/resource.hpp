#pragma once

#include "task.hpp"
#include "service_wire.hpp"

namespace pxa {

enum class Ready : std::uint32_t { read = 1, write = 2, headers = 4, end = 8 };
constexpr Ready operator|(Ready a, Ready b) noexcept {
    return static_cast<Ready>(std::to_underlying(a) | std::to_underlying(b));
}
constexpr Ready operator&(Ready a, Ready b) noexcept {
    return static_cast<Ready>(std::to_underlying(a) & std::to_underlying(b));
}
struct HandleReady { std::uint64_t handle; Ready flags; };
struct HandleClosed { std::uint64_t handle; Error reason; };

inline Result<HandleReady> decode_handle_ready(const Event& event) noexcept {
    if (event.service != 1 || event.opcode != 0x8001 || event.token ||
        event.payload.size() != 12 || !wire::get64(event.payload.data()))
        return std::unexpected(Error::protocol_error);
    auto flags = wire::get32(event.payload.data() + 8);
    if (!flags || (flags & ~15u)) return std::unexpected(Error::protocol_error);
    return HandleReady{wire::get64(event.payload.data()), static_cast<Ready>(flags)};
}
inline Result<HandleClosed> decode_handle_closed(const Event& event) noexcept {
    if (event.service != 1 || event.opcode != 0x8002 || event.token ||
        event.payload.size() != 12 || !wire::get64(event.payload.data()))
        return std::unexpected(Error::protocol_error);
    return HandleClosed{wire::get64(event.payload.data()),
        static_cast<Error>(wire::get32(event.payload.data() + 8))};
}

class ResourceService {
public:
    ResourceService(Transport& transport, RequestTable& requests) noexcept
        : transport_(transport), requests_(requests) {}
    Result<Ready> poll(std::uint64_t handle) noexcept {
        if (!handle) return std::unexpected(Error::invalid_argument);
        auto flags = transport_.io(handle, 3, {});
        if (!flags) return std::unexpected(flags.error());
        if (*flags & ~15u) return std::unexpected(Error::protocol_error);
        return static_cast<Ready>(*flags);
    }
    Task<Ready> wait(this ResourceService self, std::uint64_t handle, Ready interests) {
        const auto flags = std::to_underlying(interests);
        if (!handle || !flags || (flags & ~15u))
            co_return std::unexpected(Error::invalid_argument);
        std::array<std::byte, 12> packet;
        wire::put64(packet.data(), handle);
        wire::put32(packet.data() + 8, flags);
        auto event = co_await Response(self.transport_, self.requests_, 1, 4, packet);
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        if (body->size() != 4) co_return std::unexpected(Error::protocol_error);
        const auto ready = wire::get32(body->data());
        if ((ready & ~15u) || !(ready & (flags | 8u)))
            co_return std::unexpected(Error::protocol_error);
        co_return static_cast<Ready>(ready);
    }
private:
    Transport& transport_;
    RequestTable& requests_;
};

} // namespace pxa
