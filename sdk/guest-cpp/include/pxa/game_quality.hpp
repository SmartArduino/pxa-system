#pragma once
#include "game.hpp"
#include <array>

namespace pxa::game {
// Optional resolution hysteresis. Observe cumulative Host work, never Guest
// submission FPS or panel stalls. No allocations or frame/image cache.
class AdaptiveResolution {
public:
    int observe(const Telemetry& t,unsigned shift) noexcept {
        if(shift>2)return 0;
        if(t.rendered_frames<last_frames_||t.host_raster_us<last_raster_){
            last_frames_=t.rendered_frames;last_raster_=t.host_raster_us;return 0;
        }
        const auto count=t.rendered_frames-last_frames_;
        if(count<48)return 0;
        const auto average=(t.host_raster_us-last_raster_)/count;
        last_frames_=t.rendered_frames;last_raster_=t.host_raster_us;
        warm_frames_+=std::min<uint64_t>(count,600-warm_frames_);
        if(warm_frames_<600)return 0;
        history_[shift]=uint32_t(std::min<uint64_t>(average,UINT32_MAX));
        if(average>40000){
            under_=0;
            if(++over_>=4){over_=0;return shift<2?1:0;}
        }else{
            over_=0;
            if(shift&&average<=20000&&history_[shift-1]<=40000){
                if(++under_>=20){under_=0;return -1;}
            }else under_=0;
        }
        return 0;
    }
private:
    uint64_t last_frames_=0,last_raster_=0;
    std::array<uint32_t,3> history_{};
    uint32_t warm_frames_=0;
    uint8_t over_=0,under_=0;
};
} // namespace pxa::game
