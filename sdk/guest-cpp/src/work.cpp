#include <pxa/work.hpp>

#include <algorithm>

namespace pxa {
namespace {

bool valid_worker(std::string_view worker) noexcept {
    if (worker.empty() || worker.size() > 64 ||
        worker.front() < 'a' || worker.front() > 'z') return false;
    for (std::size_t i = 1; i < worker.size(); ++i) {
        const auto c = worker[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '.' || c == '_' || c == '-') continue;
        return false;
    }
    return true;
}

std::span<const std::byte> bytes(std::string_view text) noexcept {
    return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

bool next_record(std::span<const std::byte> input, std::size_t& offset,
                 std::uint16_t& tag,
                 std::span<const std::byte>& value) noexcept {
    if (input.size() - offset < 4) return false;
    const auto* data = input.data() + offset;
    tag = wire::get16(data);
    const auto size = wire::get16(data + 2);
    if (!tag || input.size() - offset - 4 < size) return false;
    value = input.subspan(offset + 4, size);
    offset += 4 + size;
    return true;
}

} // namespace

Result<std::size_t> encode_work_enqueue(const WorkRequest& request,
    std::span<std::byte> packet) noexcept {
    if (!valid_worker(request.worker) ||
        request.initial_delay_ms > 604800000 ||
        request.retry_delay_ms > 604800000 ||
        request.input.size() > 24 ||
        !request.max_attempts || request.max_attempts > 5 ||
        (request.max_attempts > 1 && request.retry_delay_ms < 1000))
        return std::unexpected(Error::invalid_argument);
    const auto size = wire::header_bytes + 4 + request.worker.size() +
                      8 + 8 + (request.input.empty() ? 0 : 4 + request.input.size()) +
                      8 + 5;
    if (packet.size() < size) return std::unexpected(Error::resource_limit);
    wire::Writer writer(packet.subspan(wire::header_bytes));
    std::array<std::byte, 4> scalar{};
    if (!wire::record(writer, 1, bytes(request.worker)))
        return std::unexpected(Error::resource_limit);
    wire::put32(scalar.data(), request.initial_delay_ms);
    if (!wire::record(writer, 2, scalar))
        return std::unexpected(Error::resource_limit);
    wire::put32(scalar.data(), request.execution_hint_ms);
    if (!wire::record(writer, 3, scalar) ||
        (!request.input.empty() && !wire::record(writer, 5, request.input)))
        return std::unexpected(Error::resource_limit);
    wire::put32(scalar.data(), request.retry_delay_ms);
    if (!wire::record(writer, 6, scalar) ||
        !wire::record(writer, 7,
            std::span{reinterpret_cast<const std::byte*>(&request.max_attempts),
                      std::size_t{1}}))
        return std::unexpected(Error::resource_limit);
    return size;
}

Result<WorkStart> decode_work_start(
    std::span<const std::byte> config) noexcept {
    WorkStart result;
    std::size_t offset = 0;
    std::uint16_t previous = 0;
    unsigned seen = 0;
    while (offset < config.size()) {
        std::uint16_t tag = 0;
        std::span<const std::byte> value;
        if (!next_record(config, offset, tag, value) || tag < previous)
            return std::unexpected(Error::protocol_error);
        previous = tag;
        if ((tag & 0x7fff) == 7) {
            if (tag != 7 || (seen & 1) || value.size() != 4)
                return std::unexpected(Error::protocol_error);
            result.id = wire::get32(value.data());
            seen |= 1;
        } else if ((tag & 0x7fff) == 9) {
            if (tag != 9 || (seen & 2) || value.size() != 1)
                return std::unexpected(Error::protocol_error);
            result.attempt = std::to_integer<std::uint8_t>(value[0]);
            seen |= 2;
        } else if ((tag & 0x7fff) == 10) {
            if (tag != 10 || (seen & 4) || value.size() != 8)
                return std::unexpected(Error::protocol_error);
            result.deadline_ms = wire::get64(value.data());
            seen |= 4;
        } else if ((tag & 0x7fff) == 11) {
            if (tag != 11 || (seen & 8) || value.size() > 24)
                return std::unexpected(Error::protocol_error);
            std::copy(value.begin(), value.end(), result.input.begin());
            result.input_size = static_cast<std::uint8_t>(value.size());
            seen |= 8;
        } else if (!(tag & 0x8000)) {
            return std::unexpected(Error::protocol_error);
        }
    }
    if ((seen & 7) != 7 || !result.id || !result.attempt ||
        !result.deadline_ms)
        return std::unexpected(Error::protocol_error);
    return result;
}

Result<WorkStopRequested> decode_work_stop_requested(
    const Event& event) noexcept {
    if (event.service != 13 || event.opcode != 0x8001 || event.token ||
        event.payload.size() != 12)
        return std::unexpected(Error::protocol_error);
    WorkStopRequested result{wire::get32(event.payload.data()),
                             wire::get64(event.payload.data() + 4)};
    if (!result.id || !result.deadline_ms)
        return std::unexpected(Error::protocol_error);
    return result;
}

} // namespace pxa
