#pragma once

#include "core.hpp"
#include <string_view>

namespace pxa {
enum class TextDirection : std::uint8_t { left_to_right, right_to_left };

// An owned, bounded locale tag. No process-global locale and no heap storage.
class Locale {
public:
    static constexpr std::size_t capacity = 63;

    static constexpr Result<Locale> parse(std::string_view tag) noexcept {
        if (tag.size() < 2 || tag.size() > capacity)
            return std::unexpected(Error::invalid_argument);
        Locale result;
        bool script = false, region = false, variant = false, private_use = false;
        bool extension = false, extension_value = false;
        std::uint64_t singletons = 0;
        std::size_t start = 0, part = 0, primary_length = 0, extlangs = 0;
        for (std::size_t end = 0; end <= tag.size(); ++end) {
            if (end != tag.size() && tag[end] != '-') continue;
            const auto length = end - start;
            if (!length || length > 8 || (!part && length < 2))
                return std::unexpected(Error::invalid_argument);
            bool alpha = true, digits = true;
            for (std::size_t i = start; i < end; ++i) {
                const auto c = lower(tag[i]);
                const bool letter = c >= 'a' && c <= 'z';
                const bool digit = c >= '0' && c <= '9';
                if (!letter && !digit) return std::unexpected(Error::invalid_argument);
                alpha &= letter; digits &= digit; result.bytes_[i] = c;
            }
            if (!part) {
                if (!alpha) return std::unexpected(Error::invalid_argument);
                primary_length = length;
            } else if (private_use) {
                extension_value = true;
            } else if (length == 1) {
                if (extension && !extension_value) return std::unexpected(Error::invalid_argument);
                const auto c = lower(tag[start]);
                const auto bit = std::uint64_t{1} << (c <= '9' ? c - '0' : c - 'a' + 10);
                if (singletons & bit) return std::unexpected(Error::invalid_argument);
                singletons |= bit; extension = true; extension_value = false;
                private_use = c == 'x';
            } else if (extension) {
                if (length < 2) return std::unexpected(Error::invalid_argument);
                extension_value = true;
            } else if (length == 3 && alpha && primary_length <= 3 &&
                       !script && !region && !variant && extlangs < 3) {
                ++extlangs;
            } else if (length == 4 && alpha && !script && !region && !variant) {
                result.bytes_[start] = upper(result.bytes_[start]); script = true;
            } else if (((length == 2 && alpha) || (length == 3 && digits)) && !region && !variant) {
                for (auto i = start; i < end; ++i) result.bytes_[i] = upper(result.bytes_[i]);
                region = true;
            } else if (length >= 5 || (length == 4 && tag[start] >= '0' && tag[start] <= '9')) {
                // A variant cannot occur twice in one well-formed language tag.
                for (std::size_t at = primary_length + 1; at < start;) {
                    const auto dash = tag.find('-', at);
                    const auto previous = tag.substr(at, dash - at);
                    if (equal_ascii(previous, tag.substr(start, length)))
                        return std::unexpected(Error::invalid_argument);
                    at = dash + 1;
                }
                variant = true;
            } else return std::unexpected(Error::invalid_argument);
            if (end != tag.size()) result.bytes_[end] = '-';
            start = end + 1; ++part;
        }
        if (extension && !extension_value) return std::unexpected(Error::invalid_argument);
        result.size_ = static_cast<std::uint8_t>(tag.size());
        return result;
    }

    constexpr std::string_view tag() const noexcept { return {bytes_.data(), size_}; }
    constexpr std::string_view language() const noexcept { return tag().substr(0, tag().find('-')); }
    constexpr bool is_language(std::string_view language) const noexcept {
        return equal_ascii(this->language(), language);
    }
    constexpr std::string_view script() const noexcept {
        for (auto at = language().size() + 1; at < size_;) {
            const auto end = tag().find('-', at);
            const auto part = tag().substr(at, end == tag().npos ? tag().size() - at : end - at);
            if (part.size() == 1) break;
            if (part.size() == 4 && part.front() >= 'A' && part.front() <= 'Z') return part;
            at += part.size() + 1;
        }
        return {};
    }
    constexpr std::string_view region() const noexcept {
        for (auto at = language().size() + 1; at < size_;) {
            const auto end = tag().find('-', at);
            const auto part = tag().substr(at, end == tag().npos ? tag().size() - at : end - at);
            if (part.size() == 1) break;
            if (part.size() == 2 || (part.size() == 3 && part.front() >= '0' && part.front() <= '9')) return part;
            at += part.size() + 1;
        }
        return {};
    }
    constexpr TextDirection direction() const noexcept {
        if (!script().empty()) {
            for (auto rtl : {"Adlm", "Arab", "Hebr", "Mand", "Nkoo", "Rohg", "Samr", "Syrc", "Thaa"})
                if (script() == rtl) return TextDirection::right_to_left;
            return TextDirection::left_to_right;
        }
        for (auto language : {"ar", "dv", "fa", "he", "ps", "sd", "ug", "ur", "yi"})
            if (is_language(language)) return TextDirection::right_to_left;
        return TextDirection::left_to_right;
    }
    static constexpr bool equal_ascii(std::string_view a, std::string_view b) noexcept {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) if (lower(a[i]) != lower(b[i])) return false;
        return true;
    }

private:
    static constexpr char lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; }
    static constexpr char upper(char c) noexcept { return c >= 'a' && c <= 'z' ? char(c - 32) : c; }
    std::array<char, capacity + 1> bytes_{};
    std::uint8_t size_ = 0;
};
} // namespace pxa
