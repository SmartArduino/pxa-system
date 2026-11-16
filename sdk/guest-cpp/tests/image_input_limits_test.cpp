#include "../../testing/pxa_control_capture.h"
#include <pxa/app.hpp>
#include <pxa/image_frames.hpp>
#include <cassert>
#include <algorithm>

static std::uint64_t token;
static int mode, requests, cancellations;
static bool finished;
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) {
    assert(!"An input-limit query must never read a source"); return -3;
}
extern "C" std::int32_t pxa_test_control(const std::uint8_t *data, std::uint32_t size) {
    auto p=reinterpret_cast<const std::byte*>(data);
    if(pxa::wire::get16(p)==21) {
        assert(size==24 && pxa::wire::get16(p+2)==8 && pxa::wire::get32(p+20)==262144);
        token=pxa::wire::get64(p+4);++requests;
        return mode==8?-3:0;
    }
    assert(pxa::wire::get16(p)==1 && pxa::wire::get16(p+2)==1);++cancellations;return 0;
}
struct LimitsApp {
    pxa::Task<void> run(pxa::Context& c) {
        auto bad=co_await c.assets().images().input_limits(0);
        assert(!bad && bad.error()==pxa::Error::invalid_argument && !requests);
        auto result=co_await c.assets().images().input_limits(262144);
        if(mode<2) {
            assert(result && result->max_encoded_bytes==16777216);
            assert(result->available_encoded_bytes==(mode?0u:2097152u));
        } else if(mode==2 || mode==8)assert(!result && result.error()==pxa::Error::unsupported);
        else assert(!result && result.error()==pxa::Error::protocol_error);
        finished=true;co_return pxa::Result<void>{};
    }
    pxa::Result<void> on_start(pxa::Context& c) {return c.tasks().start(run(c));}
};
PXA_APPLICATION(LimitsApp)
int main() {
    for(mode=0;mode<9;++mode) {
        finished=false;requests=cancellations=0;
        assert(!pxa_app_start(nullptr,0) && requests==1);
        if(mode!=8) {
            std::array<std::byte,36> p{};
            pxa::wire::put16(p.data(),21);pxa::wire::put16(p.data()+2,8);
            pxa::wire::put64(p.data()+4,token);
            pxa::wire::put32(p.data()+20,mode==2?std::uint32_t(-3):0);
            pxa::wire::put32(p.data()+24,mode==3?0:16777216);
            pxa::wire::put32(p.data()+28,mode==1?0:mode==4?16777217:2097152);
            pxa::wire::put32(p.data()+32,mode==5?524289:524288);
            unsigned payload=mode==2?4:mode==6?15:mode==7?8:16;
            pxa::wire::put32(p.data()+12,payload);
            assert(pxa_app_on_event(reinterpret_cast<const std::uint8_t*>(p.data()),20+payload)==1);
        }
        assert(finished && !cancellations);pxa_app_stop(0);
    }
}
