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
Result<std::size_t> encode_ipc_call_in_place(std::string_view endpoint,
    std::size_t payload_size, std::span<std::byte> packet) noexcept;
Result<std::size_t> encode_ipc_reply(std::uint32_t call_id,
    std::int32_t status, std::span<const std::byte> payload,
    std::span<std::byte> packet) noexcept;
Result<std::size_t> encode_ipc_reply_in_place(std::uint32_t call_id,
    std::size_t payload_size, std::span<std::byte> packet) noexcept;
Result<std::size_t> decode_ipc_result(const Event& event,
    std::span<std::byte> output) noexcept;

template<class Contract> struct TypedIpcRequest {
    std::uint32_t call_id = 0;
    typename Contract::Request value;
};

template<class Contract>
Result<TypedIpcRequest<Contract>> decode_ipc_request(
    const Event& event) noexcept {
    auto raw = decode_ipc_request(event);
    if (!raw) return std::unexpected(raw.error());
    if (raw->endpoint != Contract::endpoint)
        return std::unexpected(Error::not_found);
    auto value = Contract::decode_request(raw->payload);
    if (!value) return std::unexpected(value.error());
    return TypedIpcRequest<Contract>{raw->call_id, std::move(*value)};
}

template<class Contract> class TypedIpcCall {
public:
    TypedIpcCall(Task<IpcCallResult> pending,
                 std::span<std::byte> output) noexcept
        : pending_(std::move(pending)), output_(output) {}
    struct Awaiter {
        typename Task<IpcCallResult>::Awaiter pending;
        std::span<std::byte> output;
        bool await_ready() const noexcept { return pending.await_ready(); }
        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<> continuation) noexcept {
            return pending.await_suspend(continuation);
        }
        Result<typename Contract::Response> await_resume() noexcept {
            auto result = pending.await_resume();
            if (!result) return std::unexpected(result.error());
            return Contract::decode_response(output.first(result->size));
        }
    };
    Awaiter operator co_await() && noexcept {
        return {std::move(pending_).operator co_await(), output_};
    }
private:
    Task<IpcCallResult> pending_;
    std::span<std::byte> output_;
};

template<class Contract> struct IpcCallBuffers {
    std::array<std::byte, Contract::call_packet_bytes> packet{};
    std::array<std::byte, Contract::response_bytes> output{};
};

template<class Contract> struct IpcReplyBuffer {
    std::array<std::byte, Contract::reply_packet_bytes> packet{};
};

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

    template<class Contract>
    TypedIpcCall<Contract> call(const typename Contract::Request& request,
        std::span<std::byte> packet, std::span<std::byte> output) {
        constexpr auto endpoint = Contract::endpoint;
        const auto offset = wire::header_bytes + 8 + endpoint.size();
        if (packet.size() < offset)
            return {Task<IpcCallResult>::failed(Error::resource_limit), output};
        auto payload_size = Contract::encode_request(request,
                                                      packet.subspan(offset));
        if (!payload_size)
            return {Task<IpcCallResult>::failed(payload_size.error()), output};
        auto size = encode_ipc_call_in_place(endpoint, *payload_size, packet);
        if (!size) return {Task<IpcCallResult>::failed(size.error()), output};
        return {call_impl(transport_, requests_, packet.first(*size), output),
                output};
    }

    template<class Contract>
    TypedIpcCall<Contract> call(const typename Contract::Request& request,
        IpcCallBuffers<Contract>& buffers) {
        return call<Contract>(request, buffers.packet, buffers.output);
    }

    Task<void> reply(std::uint32_t call_id, std::int32_t status,
        std::span<const std::byte> payload, std::span<std::byte> packet) {
        auto size = encode_ipc_reply(call_id, status, payload, packet);
        if (!size) return Task<void>::failed(size.error());
        return reply_impl(transport_, requests_, packet.first(*size));
    }

    template<class Contract>
    Task<void> reply(std::uint32_t call_id,
        const typename Contract::Response& response,
        std::span<std::byte> packet) {
        constexpr auto offset = wire::header_bytes + 20;
        if (packet.size() < offset)
            return Task<void>::failed(Error::resource_limit);
        auto payload_size = Contract::encode_response(response,
                                                       packet.subspan(offset));
        if (!payload_size) return Task<void>::failed(payload_size.error());
        auto size = encode_ipc_reply_in_place(call_id, *payload_size, packet);
        if (!size) return Task<void>::failed(size.error());
        return reply_impl(transport_, requests_, packet.first(*size));
    }

    template<class Contract>
    Task<void> reply(std::uint32_t call_id,
        const typename Contract::Response& response,
        IpcReplyBuffer<Contract>& buffer) {
        return reply<Contract>(call_id, response, buffer.packet);
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
