#include "../../testing/pxa_control_capture.h"
#include <pxa/ui_geometry.hpp>
#include <pxa/canvas_geometry.hpp>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace pxa::ui;
extern "C" std::int32_t pxa_test_control(const std::uint8_t*, std::uint32_t) { std::abort(); }
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { std::abort(); }
int main() {
    DisplayMetrics d; d.width=300;d.height=240;d.safe={150,10,20,30};
    auto r=safe_rectangle(d); assert(r.x==150 && r.width==130 && r.y==10 && r.height==200);
    r=interactive_rectangle(d,{16,20,0,36});
    assert(r.x==150 && r.y==20 && r.width==130 && r.height==184);
    r=interactive_rectangle(d,{UINT32_MAX,0,0,0});
    assert(r.width==0);
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
    for(unsigned dpi: {80u,130u,160u,240u,320u,480u}) {
        d.density_q16=dpi*65536/160;
        for (int pixels : {0,1,2,4,13,128}) {
            const auto logical=surface_to_canvas_length(pixels,d);
            const auto scaled=std::int64_t(logical)*d.density_q16/65536;
            assert(scaled>=pixels);
            assert(scaled-pixels<=std::int64_t(d.density_q16)/65536);
        }
        assert(surface_to_canvas_length(-1,d)==0);
        for(int x: {-101,-1,0,1,17,130,296}) {
            auto box=surface_to_canvas_region({x,x,93,71},d);
            // Compare exact rational edges; clips always cover their source.
            assert(std::int64_t(box.x)*d.density_q16<=std::int64_t(x)*65536);
            assert(std::int64_t(box.x+box.width)*d.density_q16>=std::int64_t(x+93)*65536);
            assert(std::int64_t(box.y+box.height)*d.density_q16>=std::int64_t(x+71)*65536);
            auto empty=surface_to_canvas_region({x,x,0,-1},d);assert(!empty.width&&!empty.height);
        }
    }
    d.density_q16=1;auto huge=surface_to_canvas_region({INT32_MIN,INT32_MIN,INT32_MAX,INT32_MAX},d);
    assert(huge.width>=0&&huge.height>=0);
    d.density_q16=0;assert(surface_to_canvas_length(1,d)==1);
    std::puts("UI geometry: asymmetric/full insets, circular letterboxing, odd sizes, integer limits OK");
}
