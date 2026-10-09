#pragma once
#include "window.hpp"
#include <limits>

namespace pxa::ui {
// Optional UI environment geometry in Surface/window coordinates. Canvas
// pointer coordinates are density-independent and require conversion below;
// density/font scale are independent of the Window pixel-size ratio.
struct DisplayMetrics {
    std::uint32_t width=296, height=240, density_q16=65536, font_scale_q16=65536;
    Insets safe;
    std::array<std::uint32_t,4> corners{}; // TL, TR, BR, BL
    std::uint32_t shape=0;
};
inline std::int32_t canvas_to_surface_coordinate(std::int32_t value,
                                                const DisplayMetrics& metrics) noexcept {
    const std::int64_t scaled=std::int64_t(value)*metrics.density_q16/65536;
    if(scaled>std::numeric_limits<std::int32_t>::max())return std::numeric_limits<std::int32_t>::max();
    if(scaled<std::numeric_limits<std::int32_t>::min())return std::numeric_limits<std::int32_t>::min();
    return std::int32_t(scaled);
}
inline Result<DisplayMetrics> decode_display_metrics(std::span<const std::byte> data) noexcept {
    DisplayMetrics out; unsigned seen=0; std::uint32_t surface=0;
    for (std::size_t at=0;at<data.size();) {
        if(data.size()-at<4) return std::unexpected(Error::protocol_error);
        auto tag=wire::get16(data.data()+at), n=wire::get16(data.data()+at+2);at+=4;
        if(n>data.size()-at) return std::unexpected(Error::protocol_error);
        auto* p=data.data()+at;at+=n;
        if(tag<1 || tag>12) continue;
        if(seen&(1u<<tag)) return std::unexpected(Error::protocol_error);
        seen|=1u<<tag;
        const unsigned expected=tag<=5||tag==11?4:tag==6?16:tag==7||tag==8?1:tag==12?20:8;
        if(n!=expected) return std::unexpected(Error::protocol_error);
        switch(tag) {
        case 1:surface=wire::get32(p);break;
        case 2:out.width=wire::get32(p);break;
        case 3:out.height=wire::get32(p);break;
        case 4:out.density_q16=wire::get32(p);break;
        case 5:out.font_scale_q16=wire::get32(p);break;
        case 6:out.safe={wire::get32(p+12),wire::get32(p),wire::get32(p+4),wire::get32(p+8)};break;
        case 12:out.shape=wire::get32(p);for(unsigned i=0;i<4;++i)out.corners[i]=wire::get32(p+4+i*4);break;
        default:break;
        }
    }
    if((seen&0xffeu)!=0xffeu||!surface||!out.width||!out.height||!out.density_q16||!out.font_scale_q16||out.shape>2)
        return std::unexpected(Error::protocol_error);
    return out;
}
inline Result<DisplayMetrics> decode_start_display(std::span<const std::byte> data) noexcept {
    std::optional<DisplayMetrics> out;
    for(std::size_t at=0;at<data.size();) {
        if(data.size()-at<4)return std::unexpected(Error::protocol_error);
        auto tag=wire::get16(data.data()+at),n=wire::get16(data.data()+at+2);at+=4;
        if(n>data.size()-at)return std::unexpected(Error::protocol_error);
        if(tag==8) {
            if(out)return std::unexpected(Error::protocol_error);
            auto value=decode_display_metrics(data.subspan(at,n));if(!value)return value;
            out=*value;
        }
        at+=n;
    }
    if(!out)return std::unexpected(Error::not_found);
    return *out;
}
} // namespace pxa::ui
