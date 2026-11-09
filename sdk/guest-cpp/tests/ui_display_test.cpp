#include "../../testing/pxa_control_capture.h"
#include <pxa/ui_display.hpp>
#include <pxa/events.hpp>
#include <algorithm>
#include <cassert>
#include <climits>

extern "C" std::int32_t pxa_test_control(const std::uint8_t*,std::uint32_t){return 0;}
extern "C" std::int32_t pxa_io(std::uint64_t,std::uint32_t,std::uint8_t*,std::uint32_t){return -3;}

int main(){
    struct PreviousMetrics {
        std::uint32_t width,height,density,font_scale;pxa::Insets safe;
        std::array<std::uint32_t,4> corners;std::uint32_t shape;
    };
    static_assert(std::is_trivially_copyable_v<pxa::ui::DisplayMetrics>);
    static_assert(sizeof(pxa::ui::DisplayMetrics)<=sizeof(PreviousMetrics)+2*sizeof(pxa::Insets)+8);
    std::array<std::byte,256> bytes{};std::size_t used=0;
    auto scalar=[&](unsigned tag,unsigned value){
        pxa::wire::put16(bytes.data()+used,tag);pxa::wire::put16(bytes.data()+used+2,4);
        pxa::wire::put32(bytes.data()+used+4,value);used+=8;
    };
    scalar(1,1);scalar(2,412);scalar(3,412);scalar(4,131072);scalar(5,65536);
    pxa::wire::put16(bytes.data()+used,6);pxa::wire::put16(bytes.data()+used+2,16);
    for(unsigned i=0;i<4;++i)pxa::wire::put32(bytes.data()+used+4+i*4,10+i);
    used+=20;
    for(unsigned tag=7;tag<=11;++tag){
        unsigned n=tag<9?1:tag==11?4:8;
        pxa::wire::put16(bytes.data()+used,tag);pxa::wire::put16(bytes.data()+used+2,n);
        if(tag==10)pxa::wire::put64(bytes.data()+used+4,(1ull<<11)|(1ull<<12)|(1ull<<13)|(1ull<<63));
        used+=4+n;
    }
    const auto required=used;
    auto capabilities=pxa::ui::decode_ui_capabilities(std::span{bytes}.first(used));
    assert(capabilities&&capabilities->supports(pxa::ui::UiFeature::sized_text));
    assert(capabilities->supports(pxa::ui::UiFeature::dynamic_text)&&capabilities->supports(pxa::ui::UiFeature::text_input_control));
    assert(!capabilities->supports(pxa::ui::UiFeature::grid)&&(capabilities->bits&(1ull<<63)));
    pxa::Event changed{3,0x8002,0,std::span{bytes}.first(used)};
    assert(changed.is<pxa::ui::DisplayMetrics>()&&changed.is<pxa::ui::UiCapabilities>());
    assert(pxa::ui::decode_display_metrics(changed)&&pxa::ui::decode_ui_capabilities(changed));
    changed.token=1;assert(!pxa::ui::decode_display_metrics(changed)&&!pxa::ui::decode_ui_capabilities(changed));
    changed.token=0;changed.service=4;assert(!pxa::ui::decode_display_metrics(changed)&&!pxa::ui::decode_ui_capabilities(changed));
    changed.service=3;changed.opcode=0x8001;assert(!pxa::ui::decode_display_metrics(changed)&&!pxa::ui::decode_ui_capabilities(changed));
    auto plain=pxa::ui::decode_display_metrics(std::span{bytes}.first(used));
    assert(plain&&plain->width==412&&plain->height==412&&plain->corners[0]==0);
    assert(!plain->system_bars_known && plain->system_bars.top==0);
    assert(plain->safe.left==13&&plain->safe.top==10&&plain->safe.right==11&&plain->safe.bottom==12);
    assert(pxa::ui::canvas_to_surface_coordinate(103,*plain)==206);
    assert(pxa::ui::canvas_to_surface_coordinate(-7,*plain)==-14);
    assert(pxa::ui::canvas_to_surface_coordinate(INT32_MAX,*plain)==INT32_MAX);
    assert(pxa::ui::canvas_to_surface_coordinate(INT32_MIN,*plain)==INT32_MIN);
    pxa::wire::put16(bytes.data()+used,12);pxa::wire::put16(bytes.data()+used+2,20);
    pxa::wire::put32(bytes.data()+used+4,1);
    for(unsigned i=0;i<4;++i)pxa::wire::put32(bytes.data()+used+8+i*4,50+i);
    used+=24;
    auto shaped=pxa::ui::decode_display_metrics(std::span{bytes}.first(used));
    assert(shaped&&shaped->shape==1&&shaped->corners[3]==53);
    pxa::wire::put16(bytes.data()+used,13);pxa::wire::put16(bytes.data()+used+2,16);
    for(unsigned i=0;i<4;++i)pxa::wire::put32(bytes.data()+used+4+i*4,20+i);
    const auto bars_start=used;used+=20;
    auto bars=pxa::ui::decode_display_metrics(std::span{bytes}.first(used));
    assert(bars&&bars->system_bars_known&&bars->system_bars.top==20&&bars->system_bars.left==23);
    assert(bars->safe.top==10); // Physical margins stay independent.
    for(std::size_t n=bars_start+1;n<used;++n)assert(!pxa::ui::decode_display_metrics(std::span{bytes}.first(n)));
    // Complete unknown extension is accepted; duplicate required fields and
    // partial headers/value tails are errors, never partially applied state.
    assert(!bars->system_gestures_known);
    const auto gestures_start=used;
    pxa::wire::put16(bytes.data()+used,14);pxa::wire::put16(bytes.data()+used+2,16);
    for(unsigned i=0;i<4;++i)pxa::wire::put32(bytes.data()+used+4+i*4,30+i);
    used+=20;
    auto gestures=pxa::ui::decode_display_metrics(std::span{bytes}.first(used));
    assert(gestures&&gestures->system_gestures_known&&gestures->system_gestures.left==33);
    for(std::size_t n=gestures_start+1;n<used;++n)assert(!pxa::ui::decode_display_metrics(std::span{bytes}.first(n)));
    scalar(90,42);assert(pxa::ui::decode_display_metrics(std::span{bytes}.first(used)));
    const auto valid=used;scalar(2,800);assert(!pxa::ui::decode_display_metrics(std::span{bytes}.first(used)));
    auto duplicate_bars=bytes;
    std::copy_n(bytes.begin()+bars_start,20,duplicate_bars.begin()+valid);
    assert(!pxa::ui::decode_display_metrics(std::span{duplicate_bars}.first(valid+20)));
    auto duplicate_gestures=bytes;
    std::copy_n(bytes.begin()+gestures_start,20,duplicate_gestures.begin()+valid);
    assert(!pxa::ui::decode_display_metrics(std::span{duplicate_gestures}.first(valid+20)));
    for(std::size_t n=0;n<required;++n)assert(!pxa::ui::decode_display_metrics(std::span{bytes}.first(n)));
    for(std::size_t n=required+1;n<required+24;++n)assert(!pxa::ui::decode_display_metrics(std::span{bytes}.first(n)));
    std::array<std::byte,400> start{};
    pxa::wire::put16(start.data(),8);pxa::wire::put16(start.data()+2,valid);
    std::copy_n(bytes.begin(),valid,start.begin()+4);
    assert(pxa::ui::decode_start_display(std::span{start}.first(4+valid)));
    assert(pxa::ui::decode_start_ui_capabilities(std::span{start}.first(4+valid)));
    std::copy_n(start.begin(),4+valid,start.begin()+4+valid);
    assert(!pxa::ui::decode_start_display(std::span{start}.first(2*(4+valid))));
    assert(!pxa::ui::decode_start_ui_capabilities(std::span{start}.first(2*(4+valid))));
    for(std::size_t n=0;n<required;++n)assert(!pxa::ui::decode_ui_capabilities(std::span{bytes}.first(n)));
    auto duplicate_features=bytes;auto duplicate_at=valid;
    pxa::wire::put16(duplicate_features.data()+duplicate_at,10);
    pxa::wire::put16(duplicate_features.data()+duplicate_at+2,8);
    assert(!pxa::ui::decode_ui_capabilities(std::span{duplicate_features}.first(duplicate_at+12)));
    pxa::wire::put32(bytes.data()+4,0);assert(!pxa::ui::decode_display_metrics(std::span{bytes}.first(valid)));
}
