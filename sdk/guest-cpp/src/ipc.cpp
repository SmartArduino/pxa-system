#include <pxa/ipc.hpp>

#include <algorithm>

namespace pxa {
namespace {

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

bool status_valid(std::int32_t status) noexcept {
    return status <= 0 && status >= -16;
}

} // namespace

bool valid_ipc_endpoint(std::string_view endpoint) noexcept {
    if (endpoint.empty() || endpoint.size() > 64) return false;
    const auto letter = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    };
    if (!letter(endpoint.front())) return false;
    for (std::size_t i = 1; i < endpoint.size(); ++i) {
        const auto c = endpoint[i];
        if (!letter(c) && !(c >= '0' && c <= '9') &&
            c != '.' && c != '_' && c != '-') return false;
    }
    return true;
}

Result<IpcRequest> decode_ipc_request(const Event& event) noexcept {
    if (event.service != 7 || event.opcode != 0x8001 ||
        !event.token || event.token > UINT32_MAX)
        return std::unexpected(Error::protocol_error);
    IpcRequest result{static_cast<std::uint32_t>(event.token), {}, {}};
    std::size_t offset = 0;
    std::uint16_t previous = 0;
    bool has_endpoint = false, has_payload = false;
    while (offset < event.payload.size()) {
        std::uint16_t tag = 0;
        std::span<const std::byte> value;
        if (!next_record(event.payload, offset, tag, value) || tag < previous)
            return std::unexpected(Error::protocol_error);
        previous = tag;
        if ((tag & 0x7fff) == 1) {
            if (tag & 0x8000)
                return std::unexpected(Error::protocol_error);
            if (has_endpoint) return std::unexpected(Error::protocol_error);
            result.endpoint = {reinterpret_cast<const char*>(value.data()),
                               value.size()};
            if (!valid_ipc_endpoint(result.endpoint))
                return std::unexpected(Error::protocol_error);
            has_endpoint = true;
        } else if ((tag & 0x7fff) == 2) {
            if (tag & 0x8000)
                return std::unexpected(Error::protocol_error);
            if (has_payload || value.size() > 1024)
                return std::unexpected(Error::protocol_error);
            result.payload = value;
            has_payload = true;
        } else if (!(tag & 0x8000)) {
            return std::unexpected(Error::protocol_error);
        }
    }
    if (!has_endpoint) return std::unexpected(Error::protocol_error);
    return result;
}

Result<std::size_t> encode_ipc_call(std::string_view endpoint,
    std::span<const std::byte> payload, std::span<std::byte> packet) noexcept {
    if (!valid_ipc_endpoint(endpoint) || payload.size() > 1024)
        return std::unexpected(Error::invalid_argument);
    const auto size = wire::header_bytes + 4 + endpoint.size() +
                      (payload.empty() ? 0 : 4 + payload.size());
    if (packet.size() < size) return std::unexpected(Error::resource_limit);
    wire::Writer writer(packet.subspan(wire::header_bytes));
    if (!wire::record(writer, 1, bytes(endpoint)) ||
        (!payload.empty() && !wire::record(writer, 2, payload)))
        return std::unexpected(Error::resource_limit);
    return size;
}

Result<std::size_t> encode_ipc_call_in_place(std::string_view endpoint,
    std::size_t payload_size, std::span<std::byte> packet) noexcept {
    if (!valid_ipc_endpoint(endpoint) || payload_size > 1024)
        return std::unexpected(Error::invalid_argument);
    const auto size = wire::header_bytes + 4 + endpoint.size() +
                      (payload_size ? 4 + payload_size : 0);
    if (packet.size() < size) return std::unexpected(Error::resource_limit);
    auto* records = packet.data() + wire::header_bytes;
    wire::put16(records, 1);
    wire::put16(records + 2, static_cast<std::uint16_t>(endpoint.size()));
    std::copy(bytes(endpoint).begin(), bytes(endpoint).end(), records + 4);
    if (payload_size) {
        auto* payload = records + 4 + endpoint.size();
        wire::put16(payload, 2);
        wire::put16(payload + 2, static_cast<std::uint16_t>(payload_size));
    }
    return size;
}

Result<std::size_t> encode_ipc_reply(std::uint32_t call_id,
    std::int32_t status, std::span<const std::byte> payload,
    std::span<std::byte> packet) noexcept {
    if (!call_id || !status_valid(status) || payload.size() > 1024 ||
        (status != 0 && !payload.empty()))
        return std::unexpected(Error::invalid_argument);
    const auto size = wire::header_bytes + 16 +
                      (payload.empty() ? 0 : 4 + payload.size());
    if (packet.size() < size) return std::unexpected(Error::resource_limit);
    wire::Writer writer(packet.subspan(wire::header_bytes));
    std::array<std::byte, 4> scalar{};
    wire::put32(scalar.data(), call_id);
    if (!wire::record(writer, 1, scalar))
        return std::unexpected(Error::resource_limit);
    wire::put32(scalar.data(), static_cast<std::uint32_t>(status));
    if (!wire::record(writer, 2, scalar) ||
        (!payload.empty() && !wire::record(writer, 3, payload)))
        return std::unexpected(Error::resource_limit);
    return size;
}

Result<std::size_t> encode_ipc_reply_in_place(std::uint32_t call_id,
    std::size_t payload_size, std::span<std::byte> packet) noexcept {
    if (!call_id || payload_size > 1024)
        return std::unexpected(Error::invalid_argument);
    const auto size = wire::header_bytes + 16 +
                      (payload_size ? 4 + payload_size : 0);
    if (packet.size() < size) return std::unexpected(Error::resource_limit);
    auto* records = packet.data() + wire::header_bytes;
    wire::put16(records, 1);
    wire::put16(records + 2, 4);
    wire::put32(records + 4, call_id);
    wire::put16(records + 8, 2);
    wire::put16(records + 10, 4);
    wire::put32(records + 12, 0);
    if (payload_size) {
        wire::put16(records + 16, 3);
        wire::put16(records + 18,
                    static_cast<std::uint16_t>(payload_size));
    }
    return size;
}

Result<std::size_t> decode_ipc_result(const Event& event,
    std::span<std::byte> output) noexcept {
    if (event.service != 7 || event.opcode != 0x8002 ||
        !event.token || event.token > UINT32_MAX || event.payload.size() < 4)
        return std::unexpected(Error::protocol_error);
    const auto status = static_cast<std::int32_t>(
        wire::get32(event.payload.data()));
    if (!status_valid(status)) return std::unexpected(Error::protocol_error);
    if (status != 0) {
        if (event.payload.size() != 4)
            return std::unexpected(Error::protocol_error);
        return std::unexpected(static_cast<Error>(status));
    }
    std::size_t offset = 4;
    std::uint16_t previous = 0;
    bool seen = false;
    std::span<const std::byte> reply;
    while (offset < event.payload.size()) {
        std::uint16_t tag = 0;
        std::span<const std::byte> value;
        if (!next_record(event.payload, offset, tag, value) || tag < previous)
            return std::unexpected(Error::protocol_error);
        previous = tag;
        if ((tag & 0x7fff) == 3) {
            if (tag & 0x8000)
                return std::unexpected(Error::protocol_error);
            if (seen || value.size() > 1024)
                return std::unexpected(Error::protocol_error);
            seen = true;
            reply = value;
        } else if (!(tag & 0x8000)) {
            return std::unexpected(Error::protocol_error);
        }
    }
    if (reply.size() > output.size())
        return std::unexpected(Error::resource_limit);
    std::copy(reply.begin(), reply.end(), output.begin());
    return reply.size();
}

} // namespace pxa
