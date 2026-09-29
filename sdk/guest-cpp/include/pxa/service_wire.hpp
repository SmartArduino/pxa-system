#pragma once

#include "core.hpp"

namespace pxa::wire {

template<std::size_t Capacity> struct RequestPacket {
    std::array<std::byte, header_bytes + Capacity> bytes{};
    std::size_t size = 0;
    std::span<const std::byte> view() const noexcept {
        return {bytes.data() + header_bytes, size};
    }
    std::span<std::byte> packet() noexcept {
        return {bytes.data(), header_bytes + size};
    }
};

inline Result<std::span<const std::byte>> result_body(
    std::span<const std::byte> payload) noexcept {
    if (payload.size() < 4) return std::unexpected(Error::protocol_error);
    const auto status = static_cast<std::int32_t>(get32(payload.data()));
    if (status == 0) return payload.subspan(4);
    if (payload.size() != 4 || status > -1 || status < -16)
        return std::unexpected(Error::protocol_error);
    return std::unexpected(static_cast<Error>(status));
}

inline bool record(Writer& writer, std::uint16_t tag,
                    std::span<const std::byte> value) noexcept {
    if (!tag || value.size() > UINT16_MAX ||
        writer.remaining() < 4 + value.size()) return false;
    return writer.u16(tag) &&
           writer.u16(static_cast<std::uint16_t>(value.size())) &&
           writer.bytes(value);
}

class Records {
public:
    explicit Records(std::span<const std::byte> bytes) noexcept : bytes_(bytes) {}
    Result<std::span<const std::byte>> take(std::uint16_t tag,
                                           std::size_t size) noexcept {
        if (bytes_.size() < 4 || get16(bytes_.data()) != tag ||
            get16(bytes_.data() + 2) != size || bytes_.size() - 4 < size)
            return std::unexpected(Error::protocol_error);
        auto value = bytes_.subspan(4, size);
        bytes_ = bytes_.subspan(4 + size);
        return value;
    }
    bool empty() const noexcept { return bytes_.empty(); }
private:
    std::span<const std::byte> bytes_;
};

} // namespace pxa::wire
