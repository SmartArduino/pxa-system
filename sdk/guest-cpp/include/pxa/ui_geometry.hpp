#pragma once
#include "ui_display.hpp"
#include <algorithm>
#include <cmath>
namespace pxa::ui {
struct PixelRect {
    int x=0,y=0,width=0,height=0;
    bool contains(int px,int py)const noexcept{return width>0&&height>0&&px>=x&&py>=y&&
        int64_t(px)-x<width&&int64_t(py)-y<height;}
};
// A conservative rectangle entirely inside the safe region and panel outline.
// Use for fixed HUD groups; the world/background can still fill the panel.
inline PixelRect safe_rectangle(const DisplayMetrics& d) noexcept {
    const int w=int(std::min(d.width,65535u)),h=int(std::min(d.height,65535u));
    auto inset=[&](unsigned corner){float radius=std::min(float(d.corners[corner]),float(std::min(w,h))/2);
        if(d.shape==2)radius=float(std::min(w,h))/2;
        return int(std::ceil(radius*(1.f-.70710678118f)));};
    int l=std::max(inset(0),inset(3)),r=std::max(inset(1),inset(2));
    int t=std::max(inset(0),inset(1)),b=std::max(inset(2),inset(3));
    if(d.shape==2){const int side=std::min(w,h);
        l+=(w-side)/2;r+=(w-side+1)/2;t+=(h-side)/2;b+=(h-side+1)/2;}
    // Insets are absolute panel margins, including any circular letterboxing.
    // Honour them exactly; silently clamping to a third can place controls in
    // an unsafe area. A fully consumed safe region is explicitly empty.
    l=std::max(l,int(std::min(d.safe.left,uint32_t(w))));
    r=std::max(r,int(std::min(d.safe.right,uint32_t(w))));
    t=std::max(t,int(std::min(d.safe.top,uint32_t(h))));
    b=std::max(b,int(std::min(d.safe.bottom,uint32_t(h))));
    return {l,t,std::max(0,w-l-r),std::max(0,h-t-b)};
}
inline DisplayMetrics scale_display(DisplayMetrics d,uint32_t width,uint32_t height) noexcept {
    const auto x=[&](uint32_t n){return d.width?uint32_t(uint64_t(n)*width/d.width):0;};
    const auto y=[&](uint32_t n){return d.height?uint32_t(uint64_t(n)*height/d.height):0;};
    d.safe={x(d.safe.left),y(d.safe.top),x(d.safe.right),y(d.safe.bottom)};
    for(auto& radius:d.corners)radius=std::min(x(radius),y(radius));
    d.density_q16=d.height?uint32_t(std::min(uint64_t(UINT32_MAX),
        uint64_t(d.density_q16)*height/d.height)):65536;
    d.width=width;d.height=height;return d;
}
} // namespace pxa::ui
