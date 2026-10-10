#include <pxa/ui_geometry.hpp>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace pxa::ui;
extern "C" std::int32_t pxa_submit(const std::uint8_t*, std::uint32_t) { std::abort(); }
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { std::abort(); }
int main() {
    DisplayMetrics d; d.width=300;d.height=240;d.safe={150,10,20,30};
    auto r=safe_rectangle(d); assert(r.x==150 && r.width==130 && r.y==10 && r.height==200);
    d.safe.right=200; r=safe_rectangle(d); assert(r.width==0 && !r.contains(r.x,r.y));
    d.width=800;d.height=480;d.shape=2;d.safe={300,0,0,0};
    r=safe_rectangle(d);assert(r.x==300);
    const double radius=240;
    for(int x:{r.x,r.x+r.width})for(int y:{r.y,r.y+r.height})
        assert((x-400.)*(x-400.)+(y-240.)*(y-240.)<=radius*radius);
    for (unsigned side=1;side<=301;++side) {
        d.width=side+100;d.height=side;d.safe={};
        r=safe_rectangle(d);
        if (!r.width || !r.height) continue;
        for(int x:{r.x,r.x+r.width})for(int y:{r.y,r.y+r.height}) {
            const double dx=x-d.width/2.,dy=y-d.height/2.;
            assert(dx*dx+dy*dy<=side*side/4.);
        }
    }
    d.width=0;d.height=0;r=safe_rectangle(d);assert(!r.width && !r.height);
    PixelRect edge{INT32_MAX-1,INT32_MAX-1,10,10};
    assert(edge.contains(INT32_MAX,INT32_MAX));
    d.height=1;d.width=1;d.density_q16=UINT32_MAX;
    assert(scale_display(d,10,10).density_q16==UINT32_MAX);
    std::puts("UI geometry: asymmetric/full insets, circular letterboxing, odd sizes, integer limits OK");
}
