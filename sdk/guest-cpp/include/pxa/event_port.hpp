#pragma once

#include "task.hpp"
#include "service_wire.hpp"

namespace pxa {

struct EventPortTag {};

// Requires Core 2.1, a signed main-loop component and Host execution support.
// Open during startup, then wait in that component's main loop. Returned events
// borrow the caller's buffer until it is reused. Zero timeout is nonblocking.
class EventPort {
public:
    EventPort(Transport& transport, std::uint64_t handle) noexcept
        : transport_(&transport), resource_(transport, handle) {}
    explicit operator bool() const noexcept { return bool(resource_); }
    std::uint64_t handle() const noexcept { return resource_.handle(); }
    Result<void> close() noexcept { return resource_.close(); }
    Result<Event> wait(std::span<std::byte> buffer, std::uint64_t timeout_us = 0) {
        if (!resource_) return std::unexpected(Error::bad_state);
        if (buffer.size() < wire::header_bytes || buffer.size() > wire::max_control_bytes)
            return std::unexpected(Error::invalid_argument);
        if (timeout_us > 60'000'000) return std::unexpected(Error::limit_exceeded);
        wire::put64(buffer.data(), timeout_us);
        auto amount = transport_->io(handle(), 0x201, buffer);
        if (!amount) return std::unexpected(amount.error());
        if (*amount > buffer.size()) return std::unexpected(Error::protocol_error);
        return parse_event(buffer.first(*amount));
    }
private:
    Transport* transport_;
    Resource<EventPortTag> resource_;
};

class EventPortService {
public:
    EventPortService(Transport& transport, RequestTable& requests) noexcept
        : transport_(transport), requests_(requests) {}
    Task<EventPort> open(this EventPortService self) {
        auto event = co_await Response(self.transport_, self.requests_, 1, 5, {}, true);
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        auto pending = wire::result_resource<EventPortTag>(self.transport_, *body);
        if (body->size() != 8 || !pending)
            co_return std::unexpected(Error::protocol_error);
        co_return EventPort(self.transport_, pending.release());
    }
private:
    Transport& transport_;
    RequestTable& requests_;
};

} // namespace pxa
