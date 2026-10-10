#pragma once

#include "i18n_plural.hpp"
#include "startup.hpp"
#include <concepts>
#include <type_traits>
#include <tuple>
#include <string>
#include <limits>

namespace pxa::i18n {
enum class ArgumentKind : std::uint8_t { string, u32, i32, u64, i64, number };
template<ArgumentKind... Kinds> struct Message { std::uint16_t id; };

// Optional per-catalog number symbols. Null means ASCII, without grouping.
struct NumberSymbols {
    std::string_view decimal = ".", group = ",", digits = "0123456789";
    std::uint8_t primary_group = 0, secondary_group = 0;
};
struct Entry { std::uint16_t id; std::string_view value; bool formatted = false; };
struct Catalog {
    std::string_view locale;
    std::span<const Entry> entries;
    std::span<const PluralRule> cardinal{}, ordinal{};
    const NumberSymbols* numbers = nullptr;
};
struct Bundle { std::span<const Catalog> catalogs; std::uint16_t default_index = 0; };

namespace detail {
inline bool utf8(std::string_view value) noexcept {
    for (std::size_t i = 0; i < value.size();) {
        auto c = static_cast<unsigned char>(value[i++]);
        if (!c) return false;
        if (c < 0x80) continue;
        unsigned count; std::uint32_t point;
        if (c >= 0xc2 && c <= 0xdf) { count = 1; point = c & 31; }
        else if (c >= 0xe0 && c <= 0xef) { count = 2; point = c & 15; }
        else if (c >= 0xf0 && c <= 0xf4) { count = 3; point = c & 7; }
        else return false;
        if (count > value.size() - i) return false;
        for (unsigned n = 0; n < count; ++n) {
            c = static_cast<unsigned char>(value[i++]);
            if ((c & 0xc0) != 0x80) return false;
            point = (point << 6) | (c & 63);
        }
        if ((count == 1 && point < 0x80) || (count == 2 && point < 0x800) ||
            (count == 3 && point < 0x10000) || point > 0x10ffff ||
            (point >= 0xd800 && point <= 0xdfff)) return false;
    }
    return true;
}
inline bool overlap(std::string_view a, std::span<char> b) noexcept {
    if (a.empty() || b.empty()) return false;
    const auto x = reinterpret_cast<std::uintptr_t>(a.data());
    const auto y = reinterpret_cast<std::uintptr_t>(b.data());
    return x <= y ? y - x < a.size() : x - y < b.size();
}
struct Argument {
    std::string_view text{};
    Number numeric{};
    bool string = false;
    bool valid = true;
};
template<ArgumentKind Kind, class T> constexpr bool accepts() noexcept {
    using V = std::remove_cvref_t<T>;
    if constexpr (Kind == ArgumentKind::string)
        return std::is_convertible_v<T, std::string_view> && !std::is_pointer_v<V>;
    else if constexpr (Kind == ArgumentKind::number) return std::is_same_v<V, Decimal>;
    else if constexpr (!std::integral<V> || std::is_same_v<V, bool>) return false;
    else if constexpr (Kind == ArgumentKind::i32) return std::is_signed_v<V> && sizeof(V) <= 4;
    else if constexpr (Kind == ArgumentKind::u32) return std::is_unsigned_v<V> && sizeof(V) <= 4;
    else if constexpr (Kind == ArgumentKind::i64) return std::is_signed_v<V> && sizeof(V) <= 8;
    else return std::is_unsigned_v<V> && sizeof(V) <= 8;
}
template<ArgumentKind Kind, class T> inline Argument argument(T&& value) noexcept {
    if constexpr (Kind == ArgumentKind::string) {
        if constexpr (std::is_array_v<std::remove_reference_t<T>>) {
            constexpr auto capacity = std::extent_v<std::remove_reference_t<T>>;
            std::size_t length = 0;
            while (length < capacity && value[length]) ++length;
            return {{value, length}, {}, true, length < capacity};
        } else return {std::string_view(value), {}, true};
    }
    else if constexpr (Kind == ArgumentKind::number) return {{}, number(value.coefficient, value.scale), false};
    else if constexpr (std::is_signed_v<std::remove_cvref_t<T>>) return {{}, number(value), false};
    else return {{}, {std::uint64_t(value), 0, false}, false};
}
class Output {
public:
    explicit Output(std::span<char> output, bool write = false) noexcept
        : output_(output), capacity_(output.size()), write_(write) {}
    explicit Output(std::size_t capacity) noexcept : capacity_(capacity), write_(false) {}
    bool append(std::string_view value) noexcept {
        // Reserve one byte for NUL in both passes. Never silently truncate.
        if (used_ >= capacity_ || value.size() > capacity_ - used_ - 1) return false;
        if (write_) for (char c : value) output_[used_++] = c;
        else used_ += value.size();
        return true;
    }
    std::size_t size() const noexcept { return used_; }
private:
    std::span<char> output_{};
    std::size_t capacity_;
    std::size_t used_ = 0;
    bool write_;
};
inline std::size_t codepoint_size(unsigned char c) noexcept {
    return c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
}
inline bool symbols_valid(const NumberSymbols& symbols) noexcept {
    if (symbols.decimal.empty() || !utf8(symbols.decimal) || !utf8(symbols.group) ||
        !utf8(symbols.digits) || symbols.primary_group > 9 || symbols.secondary_group > 9 ||
        (symbols.primary_group && symbols.group.empty())) return false;
    unsigned count = 0;
    for (std::size_t at = 0; at < symbols.digits.size(); ++count) {
        const auto size = codepoint_size(static_cast<unsigned char>(symbols.digits[at]));
        for (std::size_t previous = 0; previous < at;) {
            const auto old_size = codepoint_size(static_cast<unsigned char>(symbols.digits[previous]));
            if (symbols.digits.substr(previous, old_size) == symbols.digits.substr(at, size)) return false;
            previous += old_size;
        }
        at += size;
    }
    return count == 10;
}
inline bool emit_number(Output& out, Number n, const NumberSymbols* symbols) noexcept {
    char digits[40]; std::size_t used = 0;
    auto remaining = n.magnitude;
    do { digits[used++] = char('0' + remaining % 10); remaining /= 10; }
    while (remaining);
    while (used <= n.scale) digits[used++] = '0';
    if (n.negative && !out.append("-")) return false;
    for (std::size_t at = used; at > 0; --at) {
        const auto position = at - 1;
        if (at == n.scale && !out.append(symbols ? symbols->decimal : ".")) return false;
        if (symbols) {
            std::size_t offset = 0;
            for (int d = 0; d < digits[position] - '0'; ++d)
                offset += codepoint_size(static_cast<unsigned char>(symbols->digits[offset]));
            if (!out.append(symbols->digits.substr(offset, codepoint_size(
                    static_cast<unsigned char>(symbols->digits[offset]))))) return false;
        } else if (!out.append({digits + position, 1})) return false;
        const auto integer_left = position > n.scale ? position - n.scale : 0;
        if (symbols && symbols->primary_group && integer_left >= symbols->primary_group) {
            const auto secondary = symbols->secondary_group ? symbols->secondary_group : symbols->primary_group;
            if ((integer_left - symbols->primary_group) % secondary == 0 && !out.append(symbols->group)) return false;
        }
    }
    return true;
}
inline PluralCategory category(std::span<const PluralRule> rules, Number value) noexcept {
    const Operands operands(value);
    for (const auto& rule : rules) if (predicate(rule.predicate, operands)) return rule.category;
    return PluralCategory::other;
}
inline Result<void> render(Output& out, std::string_view program, std::span<const Argument> args,
                           const Catalog& catalog, unsigned depth = 0) noexcept {
    if (depth > 8) return std::unexpected(Error::protocol_error);
    Reader r(program);
    while (!r.empty() && r.valid()) {
        const auto op = r.u8();
        if (op == 1) {
            const auto size = r.u16(); const auto literal = r.bytes(size);
            if (!r.valid()) break;
            if (!out.append(literal)) return std::unexpected(Error::limit_exceeded);
            continue;
        }
        const auto index = r.u8();
        if (index >= args.size()) return std::unexpected(Error::protocol_error);
        const auto& arg = args[index];
        if (op == 2) {
            const auto style = r.u8();
            if (!r.valid() || style > 1 || (arg.string && style)) return std::unexpected(Error::protocol_error);
            if (!(arg.string ? out.append(arg.text) : emit_number(out, arg.numeric, style ? catalog.numbers : nullptr)))
                return std::unexpected(Error::limit_exceeded);
            continue;
        }
        if (op != 3 && op != 4) return std::unexpected(Error::protocol_error);
        if (arg.string != (op == 4)) return std::unexpected(Error::protocol_error);
        std::uint8_t ordinal = 0;
        if (op == 3) { ordinal = r.u8(); if (ordinal > 1) return std::unexpected(Error::protocol_error); }
        const auto branches = r.u8();
        const auto plural = op == 3 ? category(ordinal ? catalog.ordinal : catalog.cardinal, arg.numeric) : PluralCategory::other;
        std::string_view selected{}, fallback{}; bool found = false, exact_found = false, has_other = false;
        for (unsigned b = 0; b < branches; ++b) {
            bool match = false, exact = false, other = false;
            if (op == 4) {
                const auto size = r.u8(); const auto selector = r.bytes(size);
                match = selector == arg.text; other = selector == "other";
            } else {
                const auto selector = r.u8();
                if (selector == 255) {
                    const auto negative = r.u8(); const auto magnitude = r.u64();
                    if (negative > 1) return std::unexpected(Error::protocol_error);
                    const auto divisor = power10(arg.numeric.scale);
                    exact = true;
                    match = ! (arg.numeric.magnitude % divisor) && arg.numeric.magnitude / divisor == magnitude &&
                        (magnitude == 0 || arg.numeric.negative == bool(negative));
                } else {
                    if (selector > 5) return std::unexpected(Error::protocol_error);
                    match = selector == static_cast<unsigned>(plural); other = selector == 5;
                }
            }
            const auto size = r.u16(); const auto body = r.bytes(size);
            if (other) { fallback = body; has_other = true; }
            if (match && (!found || (exact && !exact_found))) { selected = body; found = true; exact_found = exact; }
        }
        if (!r.valid() || !has_other) return std::unexpected(Error::protocol_error);
        auto result = render(out, found ? selected : fallback, args, catalog, depth + 1);
        if (!result) return result;
    }
    return r.valid() && r.empty() ? Result<void>{} : std::unexpected(Error::protocol_error);
}
inline const Entry* find(const Catalog& catalog, std::uint16_t id) noexcept {
    std::size_t first = 0, last = catalog.entries.size();
    while (first < last) {
        const auto middle = first + (last - first) / 2;
        if (catalog.entries[middle].id < id) first = middle + 1; else last = middle;
    }
    return first < catalog.entries.size() && catalog.entries[first].id == id ? &catalog.entries[first] : nullptr;
}
inline std::string_view chinese_script(const Locale& locale) noexcept {
    if (!locale.script().empty()) return locale.script();
    const auto region = locale.region();
    if (region == "TW" || region == "HK" || region == "MO") return "Hant";
    if (region == "CN" || region == "SG") return "Hans";
    return {};
}
} // namespace detail

// Standalone number formatting uses explicit symbols and never process locale.
inline Result<std::string_view> format_number_to(std::span<char> output, Decimal value,
    const NumberSymbols* symbols = nullptr) noexcept {
    if (value.scale > 18 || (symbols && (!detail::symbols_valid(*symbols) ||
        detail::overlap(symbols->decimal, output) || detail::overlap(symbols->group, output) ||
        detail::overlap(symbols->digits, output)))) return std::unexpected(Error::invalid_argument);
    detail::Output measure(output);
    const auto number = detail::number(value.coefficient, value.scale);
    if (!detail::emit_number(measure, number, symbols)) return std::unexpected(Error::limit_exceeded);
    detail::Output writer(output, true);
    (void)detail::emit_number(writer, number, symbols);
    output[writer.size()] = '\0';
    return std::string_view(output.data(), writer.size());
}

// Owns only a locale and a pointer to immutable application data. No caches/heap.
class Translator {
public:
    explicit Translator(const Bundle& bundle) noexcept : bundle_(&bundle) {
        if (bundle.default_index < bundle.catalogs.size()) {
            if (auto locale = Locale::parse(bundle.catalogs[bundle.default_index].locale)) {
                locale_ = *locale; direction_ = locale_.direction();
            }
        }
    }
    Translator(const Bundle&&) = delete; // The immutable bundle must outlive this object.
    const Locale& locale() const noexcept { return locale_; }
    TextDirection direction() const noexcept { return direction_; }
    Result<bool> set_locale(std::string_view tag) noexcept {
        auto parsed = Locale::parse(tag);
        if (!parsed) return std::unexpected(parsed.error());
        return set_environment(*parsed, parsed->direction());
    }
    Result<bool> initialize(std::span<const std::byte> start_configuration) noexcept {
        auto environment = decode_start_system_environment(start_configuration);
        if (!environment) {
            if (environment.error() == Error::not_found) return false;
            return std::unexpected(environment.error());
        }
        return update(*environment);
    }
    Result<bool> update(const SystemEnvironment& environment) noexcept {
        auto parsed = Locale::parse(environment.locale);
        if (!parsed || static_cast<unsigned>(environment.direction) > 1)
            return std::unexpected(Error::protocol_error);
        return set_environment(*parsed, environment.direction);
    }
    Result<bool> update(const Event& event) noexcept {
        auto environment = decode_system_environment(event);
        if (!environment) return std::unexpected(environment.error());
        return update(*environment);
    }
    std::string_view text(Message<> message) const noexcept {
        auto resolved = resolve(message.id);
        return resolved.entry && !resolved.entry->formatted ? resolved.entry->value : std::string_view{};
    }
    template<ArgumentKind... Kinds, class... Args>
        requires (sizeof...(Kinds) == sizeof...(Args) && (detail::accepts<Kinds, Args>() && ...))
    Result<std::string_view> format_to(std::span<char> output, Message<Kinds...> message, Args&&... values) const noexcept {
        const std::array<detail::Argument, sizeof...(Args)> args{detail::argument<Kinds>(std::forward<Args>(values))...};
        auto resolved = prepare(message.id, args, output);
        if (!resolved) return std::unexpected(resolved.error());
        detail::Output measure(output);
        auto checked = run(*resolved, args, measure);
        if (!checked) return std::unexpected(checked.error());
        detail::Output writer(output, true);
        auto written = run(*resolved, args, writer);
        if (!written) return std::unexpected(written.error());
        output[writer.size()] = '\0';
        return std::string_view(output.data(), writer.size());
    }
    // UTF-8 bytes, excluding NUL. No allocation and no fixed output buffer.
    template<ArgumentKind... Kinds, class... Args>
        requires (sizeof...(Kinds) == sizeof...(Args) && (detail::accepts<Kinds, Args>() && ...))
    Result<std::size_t> formatted_size(Message<Kinds...> message, Args&&... values) const noexcept {
        const std::array<detail::Argument, sizeof...(Args)> args{detail::argument<Kinds>(std::forward<Args>(values))...};
        auto resolved = prepare(message.id, args, {});
        if (!resolved) return std::unexpected(resolved.error());
        detail::Output measure(std::numeric_limits<std::size_t>::max());
        auto checked = run(*resolved, args, measure);
        if (!checked) return std::unexpected(checked.error());
        return measure.size();
    }
    // Opt-in owned result. Invalid messages/arguments are checked before allocation.
    // Allocation follows std::string's allocator/failure contract (no exceptions
    // in the default Guest build). For bounded fallible storage use format_to.
    template<ArgumentKind... Kinds, class... Args>
        requires (sizeof...(Kinds) == sizeof...(Args) && (detail::accepts<Kinds, Args>() && ...))
    Result<std::string> format(Message<Kinds...> message, Args&&... values) const {
        return format_limited(std::numeric_limits<std::size_t>::max() - 1, message,
            std::forward<Args>(values)...);
    }
    template<ArgumentKind... Kinds, class... Args>
        requires (sizeof...(Kinds) == sizeof...(Args) && (detail::accepts<Kinds, Args>() && ...))
    Result<std::string> format_limited(std::size_t max_bytes, Message<Kinds...> message, Args&&... values) const {
        const std::array<detail::Argument, sizeof...(Args)> args{detail::argument<Kinds>(std::forward<Args>(values))...};
        auto resolved = prepare(message.id, args, {});
        if (!resolved) return std::unexpected(resolved.error());
        const auto capacity = max_bytes < std::numeric_limits<std::size_t>::max() ? max_bytes + 1 : max_bytes;
        detail::Output measure(capacity);
        auto checked = run(*resolved, args, measure);
        if (!checked) return std::unexpected(checked.error());
        std::string result;
        if (measure.size() > result.max_size()) return std::unexpected(Error::limit_exceeded);
        // size() excludes NUL; C++17 permits writing the existing NUL terminator.
        result.resize(measure.size());
        detail::Output writer(std::span<char>(result.data(), result.size() + 1), true);
        auto written = run(*resolved, args, writer);
        if (!written) return std::unexpected(written.error());
        return result;
    }
    // Generated argument records support designated fields without positional
    // ordering. std::tie/apply only borrow fields for the duration of this call.
    template<class Arguments>
        requires requires(const Arguments& args) { Arguments::_pxa_message; args._pxa_arguments(); }
    Result<std::string_view> format_to(std::span<char> output, const Arguments& args) const noexcept {
        return std::apply([&](const auto&... values) {
            return format_to(output, Arguments::_pxa_message, values...);
        }, args._pxa_arguments());
    }
    template<class Arguments>
        requires requires(const Arguments& args) { Arguments::_pxa_message; args._pxa_arguments(); }
    Result<std::size_t> formatted_size(const Arguments& args) const noexcept {
        return std::apply([&](const auto&... values) {
            return formatted_size(Arguments::_pxa_message, values...);
        }, args._pxa_arguments());
    }
    template<class Arguments>
        requires requires(const Arguments& args) { Arguments::_pxa_message; args._pxa_arguments(); }
    Result<std::string> format(const Arguments& args) const {
        return std::apply([&](const auto&... values) {
            return format(Arguments::_pxa_message, values...);
        }, args._pxa_arguments());
    }
    template<class Arguments>
        requires requires(const Arguments& args) { Arguments::_pxa_message; args._pxa_arguments(); }
    Result<std::string> format_limited(std::size_t max_bytes, const Arguments& args) const {
        return std::apply([&](const auto&... values) {
            return format_limited(max_bytes, Arguments::_pxa_message, values...);
        }, args._pxa_arguments());
    }
    // Actual catalog supplying a message; useful for diagnostics/font selection.
    template<ArgumentKind... Kinds> std::string_view resolved_locale(Message<Kinds...> message) const noexcept {
        auto result = resolve(message.id);
        return result.catalog ? result.catalog->locale : std::string_view{};
    }
private:
    struct Resolved { const Catalog* catalog = nullptr; const Entry* entry = nullptr; };
    Result<Resolved> prepare(std::uint16_t id, std::span<const detail::Argument> args,
                             std::span<char> output) const noexcept {
        const auto resolved = resolve(id);
        if (!resolved.entry) return std::unexpected(Error::not_found);
        for (const auto& arg : args) {
            if (!arg.valid || (arg.string && (!detail::utf8(arg.text) || detail::overlap(arg.text, output))) ||
                (!arg.string && arg.numeric.scale > 18)) return std::unexpected(Error::invalid_argument);
        }
        if (detail::overlap(resolved.entry->value, output)) return std::unexpected(Error::invalid_argument);
        const auto* symbols = resolved.catalog->numbers;
        if (symbols && (!detail::symbols_valid(*symbols) || detail::overlap(symbols->digits, output) ||
            detail::overlap(symbols->decimal, output) || detail::overlap(symbols->group, output)))
            return std::unexpected(Error::invalid_argument);
        return resolved;
    }
    static Result<void> run(Resolved resolved, std::span<const detail::Argument> args, detail::Output& output) noexcept {
        if (!resolved.entry->formatted)
            return output.append(resolved.entry->value) ? Result<void>{} : std::unexpected(Error::limit_exceeded);
        return detail::render(output, resolved.entry->value, args, *resolved.catalog);
    }
    Result<bool> set_environment(const Locale& locale, TextDirection direction) noexcept {
        const bool changed = locale.tag() != locale_.tag() || direction != direction_;
        locale_ = locale; direction_ = direction;
        return changed;
    }
    Resolved exact(std::string_view locale, std::uint16_t id) const noexcept {
        for (const auto& catalog : bundle_->catalogs)
            if (Locale::equal_ascii(catalog.locale, locale))
                if (auto entry = detail::find(catalog, id)) return {&catalog, entry};
        return {};
    }
    Resolved resolve(std::uint16_t id) const noexcept {
        if (!id) return {};
        auto candidate = locale_.tag();
        while (!candidate.empty()) {
            // Chinese script/region compatibility is considered before bare zh.
            if (candidate == "zh") {
                const auto script = detail::chinese_script(locale_);
                if (!script.empty()) {
                    if (!locale_.region().empty()) {
                        for (const auto& catalog : bundle_->catalogs) {
                            auto tag = Locale::parse(catalog.locale);
                            if (tag && tag->is_language("zh") && tag->region() == locale_.region() &&
                                detail::chinese_script(*tag) == script)
                                if (auto entry = detail::find(catalog, id)) return {&catalog, entry};
                        }
                    }
                    char script_tag[7] = {'z', 'h', '-', script[0], script[1], script[2], script[3]};
                    if (auto result = exact({script_tag, 7}, id); result.entry) return result;
                }
            }
            if (auto result = exact(candidate, id); result.entry) return result;
            const auto dash = candidate.rfind('-');
            if (dash == candidate.npos) break;
            candidate = candidate.substr(0, dash);
            // RFC 4647 lookup removes a dangling extension singleton too.
            const auto tail = candidate.rfind('-');
            if (tail != candidate.npos && candidate.size() - tail == 2) candidate = candidate.substr(0, tail);
        }
        if (bundle_->default_index < bundle_->catalogs.size()) {
            const auto& catalog = bundle_->catalogs[bundle_->default_index];
            if (auto entry = detail::find(catalog, id)) return {&catalog, entry};
        }
        return {};
    }
    const Bundle* bundle_;
    Locale locale_{};
    TextDirection direction_ = TextDirection::left_to_right;
};
// Owned text for reactive UI. Capacity excludes the trailing NUL. Value equality
// prevents mutable string_view aliases from concealing a same-length update.
template<std::size_t Capacity> class TextBuffer {
public:
    static_assert(Capacity <= 65535);
    template<ArgumentKind... Kinds, class... Args>
        requires requires(const Translator& t, std::span<char> out, Message<Kinds...> key, Args&&... values) {
            t.format_to(out, key, std::forward<Args>(values)...);
        }
    Result<void> format(const Translator& translator, Message<Kinds...> key, Args&&... values) noexcept {
        auto text = translator.format_to(bytes_, key, std::forward<Args>(values)...);
        if (!text) return std::unexpected(text.error());
        size_ = static_cast<std::uint16_t>(text->size());
        return {};
    }
    template<class Arguments>
        requires requires(const Translator& t, std::span<char> out, const Arguments& args) { t.format_to(out, args); }
    Result<void> format(const Translator& translator, const Arguments& args) noexcept {
        auto text = translator.format_to(bytes_, args);
        if (!text) return std::unexpected(text.error());
        size_ = static_cast<std::uint16_t>(text->size());
        return {};
    }
    std::string_view view() const noexcept { return {bytes_.data(), size_}; }
    const char* c_str() const noexcept { return bytes_.data(); }
    friend bool operator==(const TextBuffer& a, const TextBuffer& b) noexcept { return a.view() == b.view(); }
private:
    std::array<char, Capacity + 1> bytes_{};
    std::uint16_t size_ = 0;
};

} // namespace pxa::i18n
