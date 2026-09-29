#pragma once

#include "task.hpp"
#include "service_wire.hpp"

#include <string_view>

namespace pxa {

struct IpcRequest {
    std::uint32_t call_id = 0;
    std::string_view endpoint;
    std::span<const std::byte> payload;
};

struct IpcCallResult {
    std::uint32_t call_id = 0;
    std::size_t size = 0;
};

bool valid_ipc_endpoint(std::string_view endpoint) noexcept;
Result<IpcRequest> decode_ipc_request(const Event& event) noexcept;
Result<std::size_t> encode_ipc_call(std::string_view endpoint,
    std::span<const std::byte> payload, std::span<std::byte> packet) noexcept;
Result<std::size_t> encode_ipc_reply(std::uint32_t call_id,
    std::int32_t status, std::span<const std::byte> payload,
    std::span<std::byte> packet) noexcept;
Result<std::size_t> decode_ipc_result(const Event& event,
    std::span<std::byte> output) noexcept;

class IpcResultAwaiter {
public:
    IpcResultAwaiter(RequestTable& requests, std::uint32_t call_id) noexcept
        : requests_(requests), call_id_(call_id) {}
    IpcResultAwaiter(const IpcResultAwaiter&) = delete;
    IpcResultAwaiter& operator=(const IpcResultAwaiter&) = delete;
    ~IpcResultAwaiter() {
        if (registered_) requests_.remove(call_id_, 7, 0x8002);
    }
    bool await_ready() const noexcept { return false; }
    bool await_suspend(std::coroutine_handle<> suspended) noexcept {
        suspended_ = suspended;
        auto added = requests_.add(call_id_, this, [](void* pointer,
                                                      const Event& event) {
            auto* self = static_cast<IpcResultAwaiter*>(pointer);
            self->registered_ = false;
            self->event_ = event;
            self->suspended_.resume();
        }, 7, 0x8002);
        if (!added) { error_ = added.error(); return false; }
        registered_ = true;
        return true;
    }
    Result<Event> await_resume() const noexcept {
        if (error_) return std::unexpected(*error_);
        if (!event_) return std::unexpected(Error::bad_state);
        return *event_;
    }
private:
    RequestTable& requests_;
    std::uint32_t call_id_;
    std::coroutine_handle<> suspended_{};
    std::optional<Event> event_;
    std::optional<Error> error_;
    bool registered_ = false;
};

class IpcService {
public:
    IpcService(Transport& transport, RequestTable& requests) noexcept
        : transport_(transport), requests_(requests) {}

    // The packet and output storage must remain alive until completion/cancel.
    Task<IpcCallResult> call(std::string_view endpoint,
        std::span<const std::byte> payload, std::span<std::byte> packet,
        std::span<std::byte> output) {
        auto size = encode_ipc_call(endpoint, payload, packet);
        if (!size) return Task<IpcCallResult>::failed(size.error());
        return call_impl(transport_, requests_, packet.first(*size), output);
    }

    Task<void> reply(std::uint32_t call_id, std::int32_t status,
        std::span<const std::byte> payload, std::span<std::byte> packet) {
        auto size = encode_ipc_reply(call_id, status, payload, packet);
        if (!size) return Task<void>::failed(size.error());
        return reply_impl(transport_, requests_, packet.first(*size));
    }

private:
    static Task<IpcCallResult> call_impl(Transport& transport,
        RequestTable& requests, std::span<std::byte> packet,
        std::span<std::byte> output) {
        auto accepted = co_await Response(transport, requests, 7, 1,
            Response::PrebuiltPacket{packet});
        if (!accepted) co_return std::unexpected(accepted.error());
        auto body = wire::result_body(accepted->payload);
        if (!body) co_return std::unexpected(body.error());
        if (body->size() != 4 || !wire::get32(body->data()))
            co_return std::unexpected(Error::protocol_error);
        const auto call_id = wire::get32(body->data());
        auto result = co_await IpcResultAwaiter(requests, call_id);
        if (!result) co_return std::unexpected(result.error());
        auto size = decode_ipc_result(*result, output);
        if (!size) co_return std::unexpected(size.error());
        co_return IpcCallResult{call_id, *size};
    }

    static Task<void> reply_impl(Transport& transport, RequestTable& requests,
                                 std::span<std::byte> packet) {
        auto event = co_await Response(transport, requests, 7, 2,
            Response::PrebuiltPacket{packet});
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        if (!body->empty()) co_return std::unexpected(Error::protocol_error);
        co_return Result<void>{};
    }

    Transport& transport_;
    RequestTable& requests_;
};

} // namespace pxa
