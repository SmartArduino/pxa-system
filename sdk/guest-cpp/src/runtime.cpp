#include <pxa/core.hpp>

namespace pxa {

Result<Event> parse_event(std::span<const std::byte> bytes) noexcept {
    if (bytes.size() < wire::header_bytes ||
        bytes.size() > wire::max_control_bytes ||
        wire::get16(bytes.data()) == 0 || wire::get16(bytes.data() + 2) == 0 ||
        wire::get32(bytes.data() + 16) != 0 ||
        wire::get32(bytes.data() + 12) != bytes.size() - wire::header_bytes)
        return std::unexpected(Error::protocol_error);
    return Event{wire::get16(bytes.data()), wire::get16(bytes.data() + 2),
                 wire::get64(bytes.data() + 4),
                 bytes.subspan(wire::header_bytes)};
}

Result<void> Transport::scratch(std::span<std::byte> buffer) noexcept {
    if (buffer.size() < wire::header_bytes + 1 ||
        buffer.size() > wire::max_control_bytes)
        return std::unexpected(Error::invalid_argument);
    packet_ = buffer;
    return {};
}

std::uint64_t Transport::next_token() noexcept {
    token_ = token_ == UINT64_MAX ? 1 : token_ + 1;
    return token_;
}

Result<void> Transport::send(std::uint16_t service, std::uint16_t opcode,
                             std::uint64_t token,
                             std::span<const std::byte> payload) noexcept {
    if (phase_ != Phase::start && phase_ != Phase::event)
        return std::unexpected(Error::bad_state);
    if (!service || !opcode || payload.size() >
        packet_.size() - wire::header_bytes)
        return std::unexpected(Error::invalid_argument);
    wire::put16(packet_.data(), service);
    wire::put16(packet_.data() + 2, opcode);
    wire::put64(packet_.data() + 4, token);
    wire::put32(packet_.data() + 12,
                static_cast<std::uint32_t>(payload.size()));
    wire::put32(packet_.data() + 16, 0);
    for (std::size_t i = 0; i < payload.size(); ++i)
        packet_[wire::header_bytes + i] = payload[i];
    auto result = pxa_submit(
        reinterpret_cast<const std::uint8_t*>(packet_.data()),
        static_cast<std::uint32_t>(wire::header_bytes + payload.size()));
    if (result != 0) return std::unexpected(static_cast<Error>(result));
    return {};
}

Result<std::uint32_t> Transport::io(std::uint64_t handle,
                                     std::uint32_t operation,
                                     std::span<std::byte> buffer) noexcept {
    if (phase_ != Phase::start && phase_ != Phase::event)
        return std::unexpected(Error::bad_state);
    if (!handle || buffer.size() > UINT32_MAX)
        return std::unexpected(Error::invalid_argument);
    auto result = pxa_io(handle, operation,
                         reinterpret_cast<std::uint8_t*>(buffer.data()),
                         static_cast<std::uint32_t>(buffer.size()));
    if (result < 0) return std::unexpected(static_cast<Error>(result));
    return static_cast<std::uint32_t>(result);
}

Result<void> Transport::close(std::uint64_t handle) noexcept {
    if (!handle) return std::unexpected(Error::invalid_argument);
    std::array<std::byte, 8> payload{};
    wire::put64(payload.data(), handle);
    return send(1, 2, 0, payload);
}

} // namespace pxa
