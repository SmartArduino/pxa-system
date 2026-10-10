#include <pxa/startup.hpp>
#include <cassert>
#include <cstring>
#include <vector>

extern "C" std::int32_t pxa_submit(const std::uint8_t*, std::uint32_t) { assert(false); return -1; }
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { assert(false); return -1; }

using Bytes = std::vector<std::byte>;
static void record(Bytes& bytes, std::uint16_t tag, std::span<const std::byte> value) {
    const auto at = bytes.size(); bytes.resize(at + 4 + value.size());
    pxa::wire::put16(bytes.data() + at, tag); pxa::wire::put16(bytes.data() + at + 2, value.size());
    if (!value.empty()) std::memcpy(bytes.data() + at + 4, value.data(), value.size());
}
static Bytes start(const Bytes& nested) { Bytes out; record(out, 12, nested); return out; }
static Bytes locale(std::string_view value) {
    Bytes out; record(out, 1, std::as_bytes(std::span{value.data(), value.size()})); return out;
}
int main() {
    auto bytes = start(locale("zh-Hans-CN"));
    auto system = pxa::decode_start_system_environment(bytes);
    assert(system && system->locale == "zh-Hans-CN" && system->is_language("zh") && system->is_language("ZH"));
    assert(!system->is_language("z") && !system->is_language("en") && !system->is_language("zh-CN"));
    assert(!system->is_language(""));
    assert(system->locale.data() == reinterpret_cast<const char*>(bytes.data() + 8)); // Borrowed, no hidden copy.
    assert(system->direction == pxa::TextDirection::left_to_right);
    auto nested = locale("ar"); const std::array rtl{std::byte{1}};
    record(nested, 0x8002, rtl); record(nested, 0x8090, rtl);
    auto extended = start(nested); system = pxa::decode_start_system_environment(extended);
    assert(system && system->is_language("ar") && system->direction == pxa::TextDirection::right_to_left);
    record(nested, 0x8090, rtl);
    assert(pxa::decode_start_system_environment(start(nested)));
    record(nested, 2, rtl); assert(!pxa::decode_start_system_environment(start(nested)));
    auto en = start(locale("en-US")); assert(pxa::decode_start_system_environment(en)->is_language("en"));
    auto duplicate = en; duplicate.insert(duplicate.end(), en.begin(), en.end());
    assert(!pxa::decode_start_system_environment(duplicate));
    nested = locale("zh-CN"); record(nested, 0x8001, rtl);
    assert(!pxa::decode_start_system_environment(start(nested)));
    for (std::size_t n = 1; n < en.size(); ++n)
        assert(!pxa::decode_start_system_environment(std::span{en}.first(n)));
    auto optional_outer = en; pxa::wire::put16(optional_outer.data(), 0x800c);
    assert(!pxa::decode_start_system_environment(optional_outer));
    const auto absent = pxa::decode_start_system_environment({});
    assert(!absent && absent.error() == pxa::Error::not_found);
    assert(!pxa::decode_start_system_environment(start({})));
    for (auto bad : {"", "zh--CN", "-zh", "zh-", "zh_CN", "中文", "123456789"})
        assert(!pxa::decode_start_system_environment(start(locale(bad))));
    assert(!pxa::decode_start_system_environment(start(locale(std::string_view("zh\0CN", 5)))));
    nested = locale("en"); const std::array invalid_direction{std::byte{2}};
    record(nested, 0x8002, invalid_direction); assert(!pxa::decode_start_system_environment(start(nested)));
    nested = locale("en"); record(nested, 0x8002, {}); assert(!pxa::decode_start_system_environment(start(nested)));
}
