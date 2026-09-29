#pragma once

#include "task.hpp"
#include "service_wire.hpp"

#include <array>
#include <string_view>

namespace pxa {

struct WorkRequest {
    std::string_view worker;
    std::uint32_t initial_delay_ms = 0;
    std::uint32_t execution_hint_ms = 0;
    std::span<const std::byte> input{};
    std::uint32_t retry_delay_ms = 0;
    std::uint8_t max_attempts = 1;
};

struct WorkSchedule {
    std::uint32_t id = 0;
    std::uint32_t granted_execution_ms = 0;
};

enum class WorkResult : std::uint8_t { success = 1, retry = 2, failure = 3 };

struct WorkStart {
    std::uint32_t id = 0;
    std::uint8_t attempt = 0;
    std::uint64_t deadline_ms = 0;
    std::array<std::byte, 24> input{};
    std::uint8_t input_size = 0;
    std::span<const std::byte> input_view() const noexcept {
        return std::span{input}.first(input_size);
    }
};

struct WorkStopRequested {
    std::uint32_t id = 0;
    std::uint64_t deadline_ms = 0;
};

Result<std::size_t> encode_work_enqueue(const WorkRequest& request,
    std::span<std::byte> packet) noexcept;
Result<WorkStart> decode_work_start(std::span<const std::byte> config) noexcept;
Result<WorkStopRequested> decode_work_stop_requested(
    const Event& event) noexcept;

class WorkService {
    using Packet = wire::RequestPacket<125>;
public:
    WorkService(Transport& transport, RequestTable& requests) noexcept
        : transport_(transport), requests_(requests) {}

    Task<WorkSchedule> enqueue(const WorkRequest& request) {
        Packet packet;
        auto size = encode_work_enqueue(request, packet.bytes);
        if (!size) return Task<WorkSchedule>::failed(size.error());
        packet.size = *size - wire::header_bytes;
        return enqueue_impl(transport_, requests_, std::move(packet));
    }

    Task<void> cancel(std::uint32_t id) {
        if (!id) return Task<void>::failed(Error::invalid_argument);
        return finish_impl(transport_, requests_, id, 2, WorkResult::success);
    }

    Task<void> complete(std::uint32_t id, WorkResult result) {
        if (!id || result < WorkResult::success || result > WorkResult::failure)
            return Task<void>::failed(Error::invalid_argument);
        return finish_impl(transport_, requests_, id, 3, result);
    }

private:
    static Task<WorkSchedule> enqueue_impl(Transport& transport,
        RequestTable& requests, Packet packet) {
        auto event = co_await Response(transport, requests, 13, 1,
            Response::PrebuiltPacket{packet.packet()});
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        wire::Records records(*body);
        auto id = records.take(4, 4);
        auto granted = records.take(8, 4);
        if (!id || !granted || !records.empty() ||
            !wire::get32(id->data()) || !wire::get32(granted->data()))
            co_return std::unexpected(Error::protocol_error);
        co_return WorkSchedule{wire::get32(id->data()),
                               wire::get32(granted->data())};
    }

    static Task<void> finish_impl(Transport& transport, RequestTable& requests,
                                  std::uint32_t id, std::uint16_t opcode,
                                  WorkResult result) {
        std::array<std::byte, 13> payload{};
        wire::put16(payload.data(), 4);
        wire::put16(payload.data() + 2, 4);
        wire::put32(payload.data() + 4, id);
        if (opcode == 3) {
            wire::put16(payload.data() + 8, 9);
            wire::put16(payload.data() + 10, 1);
            payload[12] = std::byte(result);
        }
        auto event = co_await Response(transport, requests, 13, opcode,
            std::span{payload}.first(opcode == 3 ? 13 : 8));
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
