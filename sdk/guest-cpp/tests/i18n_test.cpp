#include <pxa/i18n.hpp>
#include <pxa/events.hpp>
#include "pxa_i18n_test_messages.hpp"
#include <cassert>

extern "C" std::int32_t pxa_submit(const std::uint8_t*, std::uint32_t) { assert(false); return -1; }
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { assert(false); return -1; }
#include <cstdio>
#include <cstdlib>
#include <new>
#include <cstring>

static bool forbid_allocation = false;
static std::size_t allocations = 0;
void* operator new(std::size_t size) {
    assert(!forbid_allocation);
    ++allocations;
    if (auto pointer = std::malloc(size ? size : 1)) return pointer;
    std::abort();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
namespace msg = pxa::messages::sdk_test;
using namespace pxa::i18n;

template<class T> concept HasText = requires(Translator t, T message) { t.text(message); };
template<class T> concept AcceptsInteger = requires(Translator t, std::span<char> out, T message) { t.format_to(out, message, 1); };
template<class T> concept MissingArguments = requires(Translator t, std::span<char> out, T message) { t.format_to(out, message); };
template<class T> concept ExtraArguments = requires(Translator t, T message) { t.format(message, 1u, 2u); };
template<class T> concept AcceptsBoolean = requires(Translator t, T message) { t.format(message, true); };
static_assert(!MissingArguments<decltype(msg::items)> && !ExtraArguments<decltype(msg::items)> && !AcceptsBoolean<decltype(msg::items)>);
static_assert(HasText<decltype(msg::title)> && !HasText<decltype(msg::items)>);
static_assert(!AcceptsInteger<decltype(msg::items)>); // u32 never accepts a negative signed argument.
static_assert(sizeof(Translator) <= sizeof(void*) + sizeof(pxa::Locale) + 8);

int main() {
    Translator t(msg::bundle);
    std::array<char, 256> out{};
    assert(t.locale().tag() == "en" && t.text(msg::title) == "Hello");
    forbid_allocation = true;
    auto format = [&](auto key, auto&&... args) {
        auto result = t.format_to(out, key, std::forward<decltype(args)>(args)...);
        assert(result && out[result->size()] == '\0'); return *result;
    };
    assert(format(msg::items, 0u) == "No items");
    assert(format(msg::items, 1u) == "1 item");
    assert(format(msg::items, 2u) == "2 items");
    assert(t.format_to(out,msg::args::items{.count=3}).value()=="3 items");
    assert(t.format_to(out,msg::args::nested{.count=2,.gender="male"}).value()=="He has 2 apples");
    const NumberSymbols indian{.primary_group=3,.secondary_group=2};
    assert(format_number_to(out,Decimal{123456789},&indian).value()=="12,34,56,789");
    const NumberSymbols french{.decimal=",",.group=" ",.primary_group=3};
    assert(format_number_to(out,Decimal{-123456,2},&french).value()=="-1 234,56");
    const auto saved=out;
    assert(!format_number_to(std::span(out).first(2),Decimal{12}) && out==saved);
    assert(!format_number_to(out,Decimal{12,19}) && out==saved);
    const NumberSymbols repeated_digits{.digits="0000000000"};
    assert(!format_number_to(out,Decimal{12},&repeated_digits) && out==saved);
    TextBuffer<16> owned;
    assert(owned.format(t,msg::args::items{.count=2}) && owned.view()=="2 items");
    assert(t.formatted_size(msg::args::welcome{.name="中文🙂"}).value()==std::string_view("Hello 中文🙂!").size());
    assert(!t.format_limited(2,msg::args::items{.count=2}));
    assert(!t.format(msg::welcome,std::string_view("a\0b",3)));
    const auto before_owned=owned;
    assert(!owned.format(t,msg::welcome,"a name that exceeds sixteen bytes") && owned==before_owned);
    assert(format(msg::ordinal, std::uint64_t{21}) == "21st");
    assert(format(msg::ordinal, std::uint64_t{11}) == "11th");
    assert(format(msg::ordinal, std::uint64_t{22}) == "22nd");
    assert(format(msg::ordinal, std::uint64_t{23}) == "23rd");
    assert(format(msg::nested, std::int64_t{2}, "male") == "He has 2 apples");
    assert(format(msg::nested, std::int64_t{-1}, "unknown") == "They have -1 apple");
    assert(format(msg::quoted, 7u) == "don't {count} ' 7");
    assert(format(msg::welcome, "中文🙂") == "Hello 中文🙂!");
    assert(format(msg::value, Decimal{120, 2}) == "1.20");
    assert(format(msg::value, Decimal{-1, 18}) == "-0.000000000000000001");
    assert(format(msg::value, Decimal{INT64_MIN}) == "-9223372036854775808");
    assert(format(msg::ordinal, UINT64_MAX) == "18446744073709551615th");
    assert(format(msg::decimal_plural, Decimal{1}) == "one");
    assert(format(msg::decimal_plural, Decimal{10, 1}) == "other");
    assert(t.set_locale("ru-RU").value());
    assert(format(msg::items, 1u) == "1 предмет");
    assert(format(msg::items, 2u) == "2 предмета");
    assert(format(msg::items, 5u) == "5 предметов");
    assert(format(msg::items, 21u) == "21 предмет");
    assert(format(msg::decimal_plural, Decimal{10, 1}) == "other");
    // Missing Russian translation uses the English catalog's plural rules.
    assert(format(msg::nested, std::int64_t{2}, "male") == "He has 2 apples");
    assert(t.resolved_locale(msg::nested) == "en");
    assert(t.set_locale("en-GB-x-demo").value());
    assert(t.text(msg::title) == "British title" && t.text(msg::fallback) == "English fallback");
    assert(t.set_locale("ZH-hans-cn").value());
    assert(t.locale().tag() == "zh-Hans-CN" && t.text(msg::title) == "简体中文");
    assert(t.set_locale("zh-TW").value() && t.text(msg::title) == "繁體中文");
    assert(t.set_locale("zh-HK").value() && t.text(msg::title) == "繁體中文");
    assert(t.set_locale("zh-SG").value() && t.text(msg::title) == "Hello"); // No arbitrary region substitution.
    assert(t.set_locale("ar").value() && t.direction() == pxa::TextDirection::right_to_left);
    assert(format(msg::value, Decimal{123456789, 2}) == "١٬٢٣٤٬٥٦٧٫٨٩");
    for (auto [n, expected] : {std::pair{0u, "zero"}, {1u,"one"}, {2u,"two"}, {3u,"few"}, {11u,"many"}, {100u,"other"}})
        assert(format(msg::items, n) == expected);
    assert(!t.set_locale("ar").value());
    assert(t.set_locale("ar-Latn").value() && t.direction() == pxa::TextDirection::left_to_right);
    assert(t.set_locale("en").value());
    const auto unchanged = out;
    assert(!t.format_to(std::span(out).first(4), msg::welcome, "世界"));
    assert(out == unchanged);
    assert(!t.format_to(out, msg::welcome, std::string_view(out.data(), 2)) && out == unchanged);
    assert(!t.format_to(out, msg::welcome, std::string_view("\xc0\x80", 2)) && out == unchanged);
    assert(!t.format_to(out, msg::welcome, std::string_view("a\0b", 3)) && out == unchanged);
    const char unterminated[3]={'a','b','c'};
    assert(!t.format_to(out, msg::welcome, unterminated) && out == unchanged);
    assert(!t.format_to(out, msg::value, Decimal{1,19}) && out == unchanged);
    assert(!t.format_to(out, Message<>{999}) && out == unchanged);
    assert(!t.set_locale("en--US") && t.locale().tag() == "en");
    std::array<std::byte, 32> event_data{};
    pxa::wire::put16(event_data.data(), 1); pxa::wire::put16(event_data.data()+2, 5);
    std::memcpy(event_data.data()+4, "zh-TW", 5);
    pxa::Event event{17,0x8004,0,std::span(event_data).first(9)};
    assert(event.is<pxa::SystemEnvironment>());
    assert(t.update(event).value()); event_data[4] = std::byte{'x'};
    assert(t.locale().tag() == "zh-TW" && t.text(msg::title) == "繁體中文"); // Owns locale, never event storage.
    event.token=1; assert(!t.update(event)); assert(t.locale().tag() == "zh-TW");
    assert(!t.initialize({}).value());
    // Every truncation of each compiled program must be safe and atomic.
    for (const auto& catalog : msg::bundle.catalogs) for (const auto& entry : catalog.entries) {
        if (entry.id != msg::items.id) continue;
        for (std::size_t size=0; size<entry.value.size(); ++size) {
            const Entry broken{1,entry.value.substr(0,size),true};
            const Catalog bad_catalog{"en",std::span(&broken,1),catalog.cardinal,catalog.ordinal};
            const Bundle bundle{std::span(&bad_catalog,1)}; Translator bad(bundle);
            out.fill('X'); const auto before=out;
            auto result=bad.format_to(out,Message<ArgumentKind::u32>{1},3u);
            if (!result) assert(before==out);
        }
    }
    forbid_allocation = false;
    assert(t.set_locale("en").value());
    std::string name(32768, 'x');
    const auto allocation_count = allocations;
    auto long_text = t.format(msg::args::welcome{.name=name});
    assert(long_text && long_text->size()==name.size()+7);
    assert(long_text->starts_with("Hello ") && long_text->ends_with("!"));
    assert(allocations==allocation_count+1); // One owned output, no temporary vector/cache.
    const auto allocated = allocations;
    forbid_allocation = true;
    assert(!t.format_limited(100,msg::args::welcome{.name=name}));
    assert(t.formatted_size(msg::welcome,name).value()==long_text->size());
    assert(allocations==allocated);
    forbid_allocation = false;
    assert(t.set_locale("zh-CN").value());
    assert(t.format(msg::items,5u).value()=="5 件物品");
    assert(t.format(msg::title).value()==t.text(msg::title));
    std::printf("i18n dynamic: %zu UTF-8 bytes, capacity=%zu, one allocation; limits checked before allocation\n", long_text->size(), long_text->capacity());
    std::printf("i18n: Translator=%zu Locale=%zu bytes; lookup/format/update: zero allocations\n", sizeof(Translator), sizeof(pxa::Locale));
}
