#pragma once
#include "ui_display.hpp"
#include <algorithm>
#include <cmath>
namespace pxa::ui {
struct PixelRect {
    int x=0,y=0,width=0,height=0;
    bool contains(int px,int py)const noexcept{return px>=x&&py>=y&&px<x+width&&py<y+height;}
};
// A conservative rectangle entirely inside the safe region and panel outline.
// Use for fixed HUD groups; the world/background can still fill the panel.
inline PixelRect safe_rectangle(const DisplayMetrics& d) noexcept {
    const int w=int(std::min(d.width,65535u)),h=int(std::min(d.height,65535u));
    auto inset=[&](unsigned corner){float radius=std::min(float(d.corners[corner]),float(std::min(w,h))/2);
        if(d.shape==2)radius=float(std::min(w,h))/2;
        return int(std::ceil(radius*(1.f-.70710678118f)));};
    int l=std::max<int>(std::min(d.safe.left,d.width/3),std::max(inset(0),inset(3)));
    int r=std::max<int>(std::min(d.safe.right,d.width/3),std::max(inset(1),inset(2)));
    int t=std::max<int>(std::min(d.safe.top,d.height/3),std::max(inset(0),inset(1)));
    int b=std::max<int>(std::min(d.safe.bottom,d.height/3),std::max(inset(2),inset(3)));
    if(d.shape==2){l+=(w-std::min(w,h))/2;r+=(w-std::min(w,h))/2;
        t+=(h-std::min(w,h))/2;b+=(h-std::min(w,h))/2;}
    return {l,t,std::max(1,w-l-r),std::max(1,h-t-b)};
}
inline DisplayMetrics scale_display(DisplayMetrics d,uint32_t width,uint32_t height) noexcept {
    const auto x=[&](uint32_t n){return d.width?uint32_t(uint64_t(n)*width/d.width):0;};
    const auto y=[&](uint32_t n){return d.height?uint32_t(uint64_t(n)*height/d.height):0;};
    d.safe={x(d.safe.left),y(d.safe.top),x(d.safe.right),y(d.safe.bottom)};
    for(auto& radius:d.corners)radius=std::min(x(radius),y(radius));
    d.density_q16=d.height?uint32_t(uint64_t(d.density_q16)*height/d.height):65536;
    d.width=width;d.height=height;return d;
}
} // namespace pxa::ui
