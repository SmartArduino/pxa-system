#include <pxa/locale.hpp>
#include <cassert>

extern "C" std::int32_t pxa_submit(const std::uint8_t*, std::uint32_t) { assert(false); return -1; }
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { assert(false); return -1; }

constexpr auto english = pxa::Locale::parse("EN-latn-us-u-NU-latn");
static_assert(english && english->tag() == "en-Latn-US-u-nu-latn");
static_assert(english->language() == "en" && english->script() == "Latn" && english->region() == "US");
int main() {
    for (auto tag : {"zh", "zh-Hans-CN", "zh-Hant-TW", "en-US", "de-CH-1901", "sl-rozaj-biske", "es-419", "en-x-a", "en-u-nu-arab", "zh-cmn-Hans-CN"})
        assert(pxa::Locale::parse(tag));
    for (auto tag : {"", "a", "123", "1a", "en-", "en--US", "en_US", "中文", "en-a", "en-u-a-foo", "en-u-nu-u-foo", "en-rozaj-rozaj", "en-abcd-abcd", "en-123456789"})
        assert(!pxa::Locale::parse(tag));
    assert(pxa::Locale::parse("ar")->direction() == pxa::TextDirection::right_to_left);
    assert(pxa::Locale::parse("ar-Latn")->direction() == pxa::TextDirection::left_to_right);
    assert(pxa::Locale::parse("az-Arab")->direction() == pxa::TextDirection::right_to_left);
    assert(pxa::Locale::parse("zh-Hant")->direction() == pxa::TextDirection::left_to_right);
    char value[]="zh-CN"; auto owned=pxa::Locale::parse(value); value[0]='x'; assert(owned->tag()=="zh-CN");
    assert(!pxa::Locale::parse(std::string_view("en\0US",5)));
}
