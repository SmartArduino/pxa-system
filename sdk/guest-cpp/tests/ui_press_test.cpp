#include "../../testing/pxa_control_capture.h"
#include <pxa/app.hpp>
#include <pxa/ui_widgets.hpp>
#include <pxa/ui_refresh.hpp>
#include <pxa/list.hpp>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>
using namespace pxa;
using namespace pxa::ui;
using namespace std::chrono_literals;
static unsigned allocations, clicks, holds, clocks;
static std::uint16_t period;
static std::uint32_t host_generation, candidate;
static bool fail_commit;
void* operator new(std::size_t n) { ++allocations; if(auto p=std::malloc(n)) return p;std::abort(); }
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
extern "C" std::int32_t pxa_test_control(const std::uint8_t* bytes,std::uint32_t) {
    auto p=reinterpret_cast<const std::byte*>(bytes);
    if (wire::get16(p)==4) {
        assert(wire::get16(p+2)==1); ++clocks;period=wire::get16(p+20);
    }
    if(wire::get16(p)==3) {
        if(wire::get16(p+2)==1) candidate=wire::get32(p+24);
        if(wire::get16(p+2)==3) {
            if(fail_commit)return std::int32_t(Error::busy);
            host_generation=candidate;
        }
    }
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t,std::uint32_t,std::uint8_t*,std::uint32_t){return -3;}
static auto button() {
    return Button("Test").on_click([]{++clicks;}).on_long_press([]{++holds;},{.long_press=500ms});
}
struct IdleApp { auto view(){return button();} };
struct FrameApp : IdleApp {
    static constexpr game::LoopOptions loop_options{20000,40,1};
    void on_frame(Context&,game::FrameTick){}
};
static Event pointer(std::uint32_t node,std::uint32_t generation,unsigned phase,std::uint64_t time,int x=0) {
    static std::array<std::byte,36> p{};
    wire::put32(p.data(),1);wire::put32(p.data()+4,node);wire::put32(p.data()+8,generation);
    wire::put16(p.data()+12,7);wire::put64(p.data()+16,time);
    p[24]=std::byte{1};p[25]=std::byte(phase);wire::put32(p.data()+28,std::uint32_t(x));
    return {3,0x8001,0,p};
}
template<class App> static int send(const Event& event) {
    std::array<std::byte,56> p{};
    wire::put16(p.data(),event.service);wire::put16(p.data()+2,event.opcode);
    wire::put32(p.data()+12,event.payload.size());
    std::copy(event.payload.begin(),event.payload.end(),p.begin()+20);
    return AppRuntime<App>::event(reinterpret_cast<const std::uint8_t*>(p.data()),event.payload.size()+20);
}
template<class App> static void tick(std::uint64_t time) {
    std::array<std::byte,8> p{};wire::put64(p.data(),time);
    assert(send<App>({4,0x8001,0,p})==1);
}
template<class App> static void foreground(bool value) {
    std::array p{std::byte(value)};assert(send<App>({17,0x8005,0,p})==1);
}
int main() {
    const auto before=allocations;
    assert(AppRuntime<IdleApp>::start(nullptr,0)==0 && clocks==0);
    assert(send<IdleApp>(pointer(2,host_generation,pointer_phase_down,100))==1 && period==16);
    tick<IdleApp>(500099);assert(holds==0);
    tick<IdleApp>(500100);assert(holds==1 && period==0);
    assert(send<IdleApp>(pointer(2,host_generation,pointer_phase_up,600000))==1 && clicks==0);
    assert(send<IdleApp>(pointer(2,host_generation,pointer_phase_down,700000))==1);
    assert(send<IdleApp>(pointer(2,host_generation,pointer_phase_up,710000))==1 && clicks==1 && period==0);
    assert(send<IdleApp>(pointer(2,host_generation,pointer_phase_down,800000))==1);
    assert(send<IdleApp>(pointer(2,host_generation,pointer_phase_move,800001,9))==1 && period==0);
    tick<IdleApp>(1400000);assert(holds==1);
    assert(send<IdleApp>(pointer(2,host_generation,pointer_phase_up,1500000))==1 && clicks==1);
    assert(send<IdleApp>(pointer(2,host_generation,pointer_phase_down,1600000))==1);
    foreground<IdleApp>(false);assert(period==0);
    tick<IdleApp>(2200000);foreground<IdleApp>(true);tick<IdleApp>(2300000);
    assert(holds==1 && period==0);
    AppRuntime<IdleApp>::stop(0);
    assert(AppRuntime<FrameApp>::start(nullptr,0)==0 && period==40);
    assert(send<FrameApp>(pointer(2,host_generation,pointer_phase_down,100))==1 && period==16);
    tick<FrameApp>(500100);assert(holds==2 && period==40);
    foreground<FrameApp>(false);assert(period==0);
    foreground<FrameApp>(true);assert(period==40);
    AppRuntime<FrameApp>::stop(0);
    Transport transport;transport.phase(Phase::event);
    // State patches preserve the captured pointer, failed commits preserve the old generation.
    State<int> state{0};
    auto page=Page(transport,Column(Text(state),button(),
        Button("Styled",WidgetStyle{{0,0,80,30}}).on_long_press([]{++holds;}).on_click([]{++clicks;})));
    assert(page.mount());
    assert(page.handle(pointer(4,page.generation(),pointer_phase_down,100)));
    const auto generation=page.generation();state.set(1);fail_commit=true;
    assert(!page.flush() && page.generation()==generation);
    fail_commit=false;assert(page.flush() && page.generation()!=generation);
    assert(page.handle(pointer(4,page.generation(),pointer_phase_up,200)) && clicks==2);
    assert(page.handle(pointer(6,page.generation(),pointer_phase_down,300)));
    page.gesture_tick(550300);assert(holds==3);
    page.gesture_cancel();assert(page.gesture_deadline()==0);
    // Replacing a fragment cancels its old hold; a property patch keeps it.
    State<std::uint32_t> revision{0};
    auto refreshed=Page(transport,Refresh(revision,[]{return button();}));
    assert(refreshed.mount());
    assert(refreshed.handle(pointer(4,refreshed.generation(),pointer_phase_down,100)));
    assert(refreshed.gesture_deadline()==500100);
    revision.set(1);fail_commit=true;assert(!refreshed.flush());
    fail_commit=false;refreshed.gesture_tick(500100);assert(holds==4);
    assert(refreshed.flush());assert(refreshed.gesture_deadline()==0);
    State<bool> shown{true};
    auto conditional=Page(transport,When(shown,[]{return button();},[]{return Text("Hidden");}));
    assert(conditional.mount());
    assert(conditional.handle(pointer(4,conditional.generation(),pointer_phase_down,100)));
    shown.set(false);assert(conditional.flush());conditional.gesture_tick(700000);
    assert(holds==4 && conditional.gesture_deadline()==0);
    ListState rows{2};std::array<unsigned,2> keys{1,2};
    auto list=Page(transport,KeyedList<2,0,1>(rows,[&](std::uint32_t i){return keys[i];},
        [](unsigned){return button();}));
    assert(list.mount());
    assert(list.handle(pointer(4,list.generation(),pointer_phase_down,100)));
    std::swap(keys[0],keys[1]);rows.invalidate();assert(list.flush());
    // Stable keys keep their gesture even when moved; stale generations cannot release it.
    assert(!list.handle(pointer(4,list.generation()-1,pointer_phase_up,200)));
    list.gesture_tick(500100);assert(holds==5);
    assert(list.handle(pointer(4,list.generation(),pointer_phase_up,500200)) && clicks==2);
    assert(list.handle(pointer(7,list.generation(),pointer_phase_down,600000)));
    rows.set_count(0);assert(list.flush());list.gesture_tick(1200000);
    assert(holds==5 && list.gesture_deadline()==0);
    ListState virtual_rows{100};
    auto virtual_list=Page(transport,VirtualList<2,0,1>(virtual_rows,Dp{40},
        [](std::uint32_t i){return i;},[](unsigned){return button();}));
    assert(virtual_list.mount());
    std::array<std::byte,32> range{};
    auto visible=[&](unsigned first) {
        wire::put32(range.data(),1);wire::put32(range.data()+4,2);
        wire::put32(range.data()+8,virtual_list.generation());wire::put16(range.data()+12,9);
        wire::put32(range.data()+24,first);wire::put32(range.data()+28,2);
        assert(virtual_list.handle({3,0x8001,0,range}) && virtual_list.flush());
    };
    visible(0);assert(virtual_list.handle(pointer(4,virtual_list.generation(),pointer_phase_down,100)));
    visible(10);virtual_list.gesture_tick(700000);
    assert(holds==5 && virtual_list.gesture_deadline()==0);
    struct Route { auto view(){return button();} };
    Navigator<2> navigation;
    assert(navigation.push<Route>() && navigation.attach(transport));
    assert(navigation.handle(pointer(2,navigation.generation(),pointer_phase_down,100)));
    navigation.gesture_tick(500100);assert(holds==6);
    assert(navigation.replace<Route>() && navigation.flush() && navigation.gesture_deadline()==0);
    auto invalid=Page(transport,Button("Invalid").on_long_press([]{},{.long_press=0us}));
    assert(!invalid.mount());
    State<std::string> label{"Dynamic"};
    auto bound=Page(transport,Button(label).on_long_press([]{++holds;}).on_click([]{++clicks;}));
    assert(bound.mount());
    assert(bound.handle(pointer(2,bound.generation(),pointer_phase_down,100)));
    label.set("Changed");assert(bound.flush());
    bound.gesture_tick(550100);assert(holds==7);
    assert(bound.handle(pointer(2,bound.generation(),pointer_phase_up,600000)) && clicks==2);
    auto computed=Page(transport,Button(Computed([](int n){return n;},state))
        .on_click([]{++clicks;}).on_long_press([]{++holds;}));
    assert(computed.mount());
    assert(computed.handle(pointer(2,computed.generation(),pointer_phase_down,100)));
    state.set(2);assert(computed.flush());
    assert(computed.handle(pointer(2,computed.generation(),pointer_phase_up,200)) && clicks==3);
    // Short labels and gesture dispatch introduce no heap allocation.
    assert(allocations==before);
    std::printf("Press gestures: state=%zu B, native/styled/bound labels, clock/foreground, "
                "PATCH/rollback, When/Refresh/keyed/virtual/navigation; zero heap OK\n", sizeof(PressGesture));
}
