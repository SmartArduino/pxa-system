#pragma once

#include "core.hpp"
#include <string_view>

namespace pxa::i18n {
enum class PluralCategory : std::uint8_t { zero, one, two, few, many, other };

// coefficient / 10^scale; trailing decimal zeros are significant to CLDR.
struct Decimal {
    std::int64_t coefficient = 0;
    std::uint8_t scale = 0;
};

namespace detail {
struct Number {
    std::uint64_t magnitude = 0;
    std::uint8_t scale = 0;
    bool negative = false;
};
constexpr Number number(std::int64_t value, std::uint8_t scale = 0) noexcept {
    return {value < 0 ? std::uint64_t(-(value + 1)) + 1 : std::uint64_t(value), scale, value < 0};
}
constexpr std::uint64_t power10(std::uint8_t scale) noexcept {
    std::uint64_t result = 1;
    while (scale--) result *= 10;
    return result;
}
struct Operands {
    std::uint64_t i, f, t;
    std::uint8_t v, w;
    explicit constexpr Operands(Number n) noexcept : v(n.scale), w(n.scale) {
        const auto divisor = power10(v);
        i = n.magnitude / divisor; f = n.magnitude % divisor; t = f;
        while (w && !(t % 10)) { t /= 10; --w; }
    }
    constexpr std::uint64_t get(unsigned code) const noexcept {
        switch (code) {
        case 0: case 1: return i; case 2: return v; case 3: return w;
        case 4: return f; case 5: return t; default: return 0; // c,e: ordinary notation
        }
    }
};

// Bytecode and catalogs are generated trusted data, but reads remain bounded.
class Reader {
public:
    explicit Reader(std::string_view input) noexcept : input_(input) {}
    std::uint8_t u8() noexcept {
        if (input_.empty()) { valid_ = false; return 0; }
        auto value = static_cast<std::uint8_t>(input_.front()); input_.remove_prefix(1); return value;
    }
    std::uint16_t u16() noexcept { auto lo = u8(); return lo | (std::uint16_t(u8()) << 8); }
    std::uint32_t u32() noexcept {
        std::uint32_t result = 0;
        for (unsigned i = 0; i < 4; ++i) result |= std::uint32_t(u8()) << (i * 8);
        return result;
    }
    std::uint64_t u64() noexcept {
        std::uint64_t result = 0;
        for (unsigned i = 0; i < 8; ++i) result |= std::uint64_t(u8()) << (i * 8);
        return result;
    }
    std::string_view bytes(std::size_t count) noexcept {
        if (count > input_.size()) { valid_ = false; input_ = {}; return {}; }
        auto result = input_.substr(0, count); input_.remove_prefix(count); return result;
    }
    bool valid() const noexcept { return valid_; }
    bool empty() const noexcept { return input_.empty(); }
private:
    std::string_view input_;
    bool valid_ = true;
};
inline bool predicate(std::string_view program, const Operands& operands) noexcept {
    Reader r(program);
    bool any = false;
    const auto alternatives = r.u8();
    for (unsigned a = 0; a < alternatives; ++a) {
        bool all = true;
        const auto relations = r.u8();
        for (unsigned b = 0; b < relations; ++b) {
            const auto operand = r.u8(); const auto mod = r.u32();
            const bool negate = r.u8(); const auto ranges = r.u8();
            auto value = operands.get(operand);
            if (mod) value %= mod;
            bool match = false;
            for (unsigned c = 0; c < ranges; ++c) {
                const auto low = r.u32(); const auto high = r.u32();
                match |= value >= low && value <= high;
            }
            // '='/'!=' ranges match integers only. n retains its fraction.
            if (operand == 0 && operands.f) match = false;
            all &= (match != negate);
        }
        any |= all;
    }
    return r.valid() && r.empty() && any;
}
} // namespace detail

struct PluralRule { PluralCategory category; std::string_view predicate; };
inline PluralCategory plural_category(std::span<const PluralRule> rules, Decimal value) noexcept {
    if (value.scale > 18) return PluralCategory::other;
    const detail::Operands operands(detail::number(value.coefficient, value.scale));
    for (const auto& rule : rules)
        if (detail::predicate(rule.predicate, operands)) return rule.category;
    return PluralCategory::other;
}
} // namespace pxa::i18n
