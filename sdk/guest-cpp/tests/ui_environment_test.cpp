#include <pxa/ui_environment.hpp>
#include <pxa/ui_layout.hpp>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
using namespace pxa;
using namespace pxa::ui;
static unsigned allocations, submissions;
void* operator new(std::size_t n) { ++allocations; return std::malloc(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
extern "C" std::int32_t pxa_submit(const std::uint8_t*, std::uint32_t) { ++submissions; return 0; }
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { return -3; }
static_assert(sizeof(Environment<>) == sizeof(State<DisplayMetrics>));
static_assert(sizeof(Environment<UiAppearance>) == sizeof(State<UiAppearance>));
template<class E> concept HasAppearance = requires(E& e) { e.appearance(); };
static_assert(!HasAppearance<Environment<>>);
int main() {
    const auto before = allocations;
    std::array<std::byte, 192> bytes{}; std::size_t used = 0;
    auto scalar = [&](unsigned tag, unsigned value) {
        wire::put16(bytes.data()+used,tag); wire::put16(bytes.data()+used+2,4);
        wire::put32(bytes.data()+used+4,value); used+=8;
    };
    scalar(1,1); scalar(2,240); scalar(3,320); scalar(4,65536); scalar(5,65536);
    wire::put16(bytes.data()+used,6); wire::put16(bytes.data()+used+2,16); used+=20;
    for (unsigned tag=7;tag<=11;++tag) {
        unsigned n=tag<9?1:tag==11?4:8;
        wire::put16(bytes.data()+used,tag); wire::put16(bytes.data()+used+2,n);
        if (tag==10) wire::put64(bytes.data()+used+4,1ull<<11);
        used+=4+n;
    }
    std::array<std::byte, 400> config{};
    wire::put16(config.data(),8); wire::put16(config.data()+2,used);
    std::copy_n(bytes.begin(),used,config.begin()+4);
    auto start=std::span{config}.first(used+4);
    Environment<DisplayMetrics, UiAppearance, UiCapabilities> all;
    assert(all.initialize(start));
    assert(all.display().get().width==240 && !all.appearance().get().dark);
    assert(all.capabilities().get().supports(UiFeature::sized_text));
    Transport transport; transport.phase(Phase::event);
    {
        Environment<> display;
        assert(display.initialize(start));
        Page page(transport, SafeArea(display.display(), Text("Environment")));
        assert(page.mount()); const auto idle=submissions;
        Event event{3,0x8002,0,std::span{bytes}.first(used)};
        for (unsigned i=0;i<100;++i) assert(display.update(event)==Result<bool>{true});
        assert(!page.dirty() && page.flush() && submissions==idle);
        wire::put32(bytes.data()+12,320);
        assert(display.update(event) && page.dirty() && page.flush());
        assert(display.display().get().width==320);
        event.token=1;
        assert(display.update(event).error()==Error::protocol_error && !page.dirty());
        event={9,0x8002,0,{}}; assert(display.update(event)==Result<bool>{false});
        event={3,0x8006,0,{}}; assert(display.update(event)==Result<bool>{false});
    }
    // A malformed environment preserves every selected snapshot.
    auto valid=used;
    wire::put16(bytes.data()+used,10); wire::put16(bytes.data()+used+2,8); used+=12;
    assert(all.update({3,0x8002,0,std::span{bytes}.first(used)}).error()==Error::protocol_error);
    assert(all.display().get().width==240);
    // Complete init is atomic even when a later selected theme field is invalid.
    std::copy_n(bytes.begin(),valid,config.begin()+4);
    config[4+64]=std::byte{2}; // Color-scheme value inside the environment.
    auto invalid=all.initialize(start); assert(!invalid && all.display().get().width==240);
    std::array<std::byte, 60> theme{};
    wire::put32(theme.data(),7); theme[4]=std::byte{1};
    for (unsigned i=0;i<6;++i) wire::put16(theme.data()+48+2*i,16);
    assert(all.update({3,0x8006,0,theme}) && all.appearance().get().dark);
    const auto previous=all.appearance().get(); theme[5]=std::byte{1};
    assert(!all.update({3,0x8006,0,theme}) && all.appearance().get()==previous);
    assert(allocations==before);
    std::printf("UI Environment: selected State storage only, atomic init/notifications, 100 identical metrics/zero submits, zero heap; display=%zu B all=%zu B\n",sizeof(Environment<>),sizeof(all));
}
