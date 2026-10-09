#pragma once

#include "core.hpp"
#include <string_view>

namespace pxa {
namespace wire { bool valid_utf8(std::span<const std::byte> bytes) noexcept; }

// Owned, bounded UTF-8 text used by service results and generated contracts.
// Failed assignments leave the previous value intact. No heap allocation.
template<std::size_t Capacity> class FixedText {
public:
    Result<void> assign(std::span<const std::byte> bytes) noexcept {
        if (bytes.empty() || bytes.size() > Capacity || !wire::valid_utf8(bytes))
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
} // namespace pxa
