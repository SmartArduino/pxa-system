#pragma once

#include "core.hpp"
#include <string_view>

namespace pxa::wire {

bool valid_utf8(std::span<const std::byte> bytes) noexcept;

template<std::size_t Capacity> class OwnedText {
public:
    Result<void> assign(std::span<const std::byte> bytes) noexcept {
        if (bytes.empty() || bytes.size() > Capacity || !valid_utf8(bytes))
            return std::unexpected(Error::protocol_error);
        for (std::size_t i = 0; i < bytes.size(); ++i)
            bytes_[i] = static_cast<char>(std::to_integer<unsigned char>(bytes[i]));
        size_ = static_cast<std::uint16_t>(bytes.size());
        bytes_[size_] = '\0';
        return {};
    }
    Result<void> set(std::string_view text) noexcept {
        auto result = assign(std::as_bytes(std::span{text.data(), text.size()}));
        if (!result) return std::unexpected(Error::invalid_argument);
        return {};
    }
    std::string_view view() const noexcept { return {bytes_.data(), size_}; }
    const char* c_str() const noexcept { return bytes_.data(); }
private:
    static_assert(Capacity <= UINT16_MAX);
    std::array<char, Capacity + 1> bytes_{};
    std::uint16_t size_ = 0;
};

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
    Result<std::span<const std::byte>> take(std::uint16_t tag) noexcept {
        if (bytes_.size() < 4)
            return std::unexpected(Error::protocol_error);
        return take(tag, get16(bytes_.data() + 2));
    }
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
