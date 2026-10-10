#pragma once

#include "core.hpp"
#include "locale.hpp"

#include <string_view>
#include <optional>

namespace pxa {
namespace detail {
// Borrow a unique configuration record after validating the entire TLV stream.
// Bit 15 marks optional records; both forms identify the same field.
inline Result<std::span<const std::byte>> configuration_record(
    std::span<const std::byte> data, std::uint16_t key, bool optional_allowed = true) noexcept {
    std::optional<std::span<const std::byte>> found;
    for (std::size_t at = 0; at < data.size();) {
        if (data.size() - at < 4) return std::unexpected(Error::protocol_error);
        const auto tag = wire::get16(data.data() + at);
        const auto size = wire::get16(data.data() + at + 2);
        at += 4;
        if (!(tag & 0x7fff) || size > data.size() - at)
            return std::unexpected(Error::protocol_error);
        if ((tag & 0x7fff) == key) {
            if (found || (!optional_allowed && (tag & 0x8000))) return std::unexpected(Error::protocol_error);
            found = data.subspan(at, size);
        }
        at += size;
    }
    if (!found) return std::unexpected(Error::not_found);
    return *found;
}
} // namespace detail

// locale borrows START/event storage. Consume it within the callback, or
// copy explicitly if it must outlive that callback. No strings/cache are allocated.
struct SystemEnvironment {
    std::string_view locale;
    TextDirection direction = TextDirection::left_to_right;

    bool is_language(std::string_view language) const noexcept {
        if (language.empty()) return false;
        const auto primary = locale.substr(0, locale.find('-'));
        if (primary.size() != language.size()) return false;
        auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; };
        for (std::size_t i = 0; i < primary.size(); ++i)
            if (lower(primary[i]) != lower(language[i])) return false;
        return true;
    }
};

inline Result<SystemEnvironment> decode_system_environment(
    std::span<const std::byte> records) noexcept {
    auto locale = detail::configuration_record(records, 1);
    if (!locale || locale->empty() || locale->size() > 63)
        return std::unexpected(Error::protocol_error);
    // Reject unknown required fields and out-of-order records; known fields
    // must be unique. Repeated unknown optional fields remain forward compatible.
    std::uint16_t previous = 0;
    for (std::size_t at = 0; at < records.size();) {
        const auto tag = wire::get16(records.data() + at);
        const auto size = wire::get16(records.data() + at + 2);
        if (tag < previous || ((tag & 0x7fff) == 1 && tag != 1) ||
            ((tag & 0x7fff) == 2 && tag != 0x8002) ||
            (!(tag & 0x8000) && (tag & 0x7fff) > 2))
            return std::unexpected(Error::protocol_error);
        previous = tag; at += 4 + size;
    }
    SystemEnvironment out{std::string_view(
        reinterpret_cast<const char*>(locale->data()), locale->size())};
    auto parsed = Locale::parse(out.locale);
    if (!parsed) return std::unexpected(Error::protocol_error);
    auto direction = detail::configuration_record(records, 2);
    if (direction) {
        if (direction->size() != 1 || (*direction)[0] > std::byte{1})
            return std::unexpected(Error::protocol_error);
        out.direction = static_cast<TextDirection>(std::to_integer<std::uint8_t>((*direction)[0]));
    } else if (direction.error() != Error::not_found) {
        return std::unexpected(direction.error());
    }
    return out;
}

inline Result<SystemEnvironment> decode_start_system_environment(
    std::span<const std::byte> config) noexcept {
    auto record = detail::configuration_record(config, 12, false);
    if (!record) return std::unexpected(record.error());
    return decode_system_environment(*record);
}

inline Result<SystemEnvironment> decode_system_environment(const Event& event) noexcept {
    if (event.service != 17 || event.opcode != 0x8004 || event.token)
        return std::unexpected(Error::protocol_error);
    return decode_system_environment(event.payload);
}
} // namespace pxa
