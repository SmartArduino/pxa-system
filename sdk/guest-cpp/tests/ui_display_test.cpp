#include <pxa/ui_display.hpp>
#include <algorithm>
#include <cassert>
#include <climits>

extern "C" std::int32_t pxa_submit(const std::uint8_t*,std::uint32_t){return 0;}
extern "C" std::int32_t pxa_io(std::uint64_t,std::uint32_t,std::uint8_t*,std::uint32_t){return -3;}

int main(){
    std::array<std::byte,192> bytes{};std::size_t used=0;
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
        pxa::wire::put16(bytes.data()+used,tag);pxa::wire::put16(bytes.data()+used+2,n);used+=4+n;
    }
    const auto required=used;
    auto plain=pxa::ui::decode_display_metrics(std::span{bytes}.first(used));
    assert(plain&&plain->width==412&&plain->height==412&&plain->corners[0]==0);
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
    // Complete unknown extension is accepted; duplicate required fields and
    // partial headers/value tails are errors, never partially applied state.
    scalar(90,42);assert(pxa::ui::decode_display_metrics(std::span{bytes}.first(used)));
    const auto valid=used;scalar(2,800);assert(!pxa::ui::decode_display_metrics(std::span{bytes}.first(used)));
    for(std::size_t n=0;n<required;++n)assert(!pxa::ui::decode_display_metrics(std::span{bytes}.first(n)));
    for(std::size_t n=required+1;n<required+24;++n)assert(!pxa::ui::decode_display_metrics(std::span{bytes}.first(n)));
    std::array<std::byte,400> start{};
    pxa::wire::put16(start.data(),8);pxa::wire::put16(start.data()+2,valid);
    std::copy_n(bytes.begin(),valid,start.begin()+4);
    assert(pxa::ui::decode_start_display(std::span{start}.first(4+valid)));
    std::copy_n(start.begin(),4+valid,start.begin()+4+valid);
    assert(!pxa::ui::decode_start_display(std::span{start}.first(2*(4+valid))));
    pxa::wire::put32(bytes.data()+4,0);assert(!pxa::ui::decode_display_metrics(std::span{bytes}.first(valid)));
}
