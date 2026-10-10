#include <pxa/core.hpp>
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string_view>

static bool contains_en = false, contains_zh = false, contains_hant = false, contains_count = false;
#if PXA_I18N_DYNAMIC_TEXT
static bool allocation_allowed = true;
#else
static bool allocation_allowed = false;
#endif
static std::uint32_t last_generation=0;
void* operator new(std::size_t size) {
    assert(allocation_allowed);
    if (auto pointer = std::malloc(size ? size : 1)) return pointer;
    std::abort();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
extern "C" std::int32_t pxa_submit(const std::uint8_t* data, std::uint32_t length) {
    if (length >= 40 && data[0]==3 && data[1]==0 && data[2]==1 && data[3]==0)
        last_generation=pxa::wire::get32(reinterpret_cast<const std::byte*>(data)+24);
    std::string_view packet(reinterpret_cast<const char*>(data),length);
    contains_en |= packet.find("Internationalization") != packet.npos;
    contains_zh |= packet.find("国际化示例") != packet.npos;
    contains_hant |= packet.find("國際化範例") != packet.npos;
    contains_count |= packet.find("2 items") != packet.npos;
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { return -3; }
#include "../examples/i18n/main.cpp"

int main() {
    for (unsigned repeat = 0; repeat < 10; ++repeat) {
        contains_en=contains_zh=contains_hant=contains_count=false;
        assert(pxa_app_start(nullptr,0)==0 && contains_en);
        std::array<std::byte,44> click{};
        pxa::wire::put16(click.data(),3); pxa::wire::put16(click.data()+2,0x8001);
        pxa::wire::put32(click.data()+12,24);
        pxa::wire::put32(click.data()+20,1); pxa::wire::put32(click.data()+24,5);
        pxa::wire::put32(click.data()+28,last_generation); pxa::wire::put16(click.data()+32,1);
        assert(pxa_app_on_event(reinterpret_cast<const std::uint8_t*>(click.data()),click.size())==1);
        assert(contains_count); // Same-length text update is observable.
        // Exercise the actual second button, not only the system event path.
        contains_zh=false;
        pxa::wire::put32(click.data()+24,7);
        pxa::wire::put32(click.data()+28,last_generation);
        assert(pxa_app_on_event(reinterpret_cast<const std::uint8_t*>(click.data()),click.size())==1);
        assert(contains_zh);
        std::array<std::byte,29> event{};
        pxa::wire::put16(event.data(),17); pxa::wire::put16(event.data()+2,0x8004);
        pxa::wire::put32(event.data()+12,9); pxa::wire::put16(event.data()+20,1);
        pxa::wire::put16(event.data()+22,5); std::memcpy(event.data()+24,"zh-CN",5);
        assert(pxa_app_on_event(reinterpret_cast<const std::uint8_t*>(event.data()),event.size())==1 && contains_zh);
        std::memcpy(event.data()+24,"zh-TW",5);
        assert(pxa_app_on_event(reinterpret_cast<const std::uint8_t*>(event.data()),event.size())==1 && contains_hant);
        pxa_app_stop(0);
        std::array<std::byte,13> config{};
        pxa::wire::put16(config.data(),12); pxa::wire::put16(config.data()+2,9);
        std::memcpy(config.data()+4,event.data()+20,9);
        contains_hant=false;
        assert(pxa_app_start(reinterpret_cast<const std::uint8_t*>(config.data()),config.size())==0 && contains_hant);
        pxa_app_stop(0);
    }
}
