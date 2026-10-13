#include "../../testing/pxa_control_capture.h"
#include <pxa/clock.hpp>
#include <cassert>
static std::uint64_t token;
static unsigned completed = 0, cancelled = 0;
static std::int32_t send_result = 0;
static std::uint16_t opcode=2;
extern "C" std::int32_t pxa_test_control(const std::uint8_t* data, std::uint32_t size) {
    auto p = reinterpret_cast<const std::byte*>(data);
    if (pxa::wire::get16(p) == 1) { ++cancelled; return 0; }
    assert(size == 20 && pxa::wire::get16(p) == 4 && pxa::wire::get16(p+2) == opcode);
    token = pxa::wire::get64(p+4);
    // Only the caller's coroutine is allocated. Yield has no Task/frame slot.
    assert(pxa::task_pool_stats().active_slots == 1);
    return send_result;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { return -1; }
static pxa::Task<void> run(pxa::Transport& transport, pxa::RequestTable& requests, std::optional<pxa::Error> error = {}) {
    auto result = co_await pxa::ClockService(transport, requests).yield();
    if (error) assert(!result && result.error() == *error);
    else assert(result);
    ++completed;
    co_return pxa::Result<void>{};
}
template<bool ZoneOnly>
static pxa::Task<void> wall(pxa::Transport& transport,pxa::RequestTable& requests,
    std::int64_t unix_us,std::int32_t offset,bool dst,std::optional<pxa::Error> error={}) {
    pxa::ClockService clock(transport,requests);
    if constexpr(ZoneOnly){
        auto value=co_await clock.time_zone();
        if(error)assert(!value&&value.error()==*error);
        else assert(value&&value->utc_offset.count()==offset&&value->daylight_saving==dst);
    }else{
        auto value=co_await clock.wall_time();
        if(error)assert(!value&&value.error()==*error);
        else {assert(value&&value->utc.time_since_epoch().count()==unix_us);
            assert(value->monotonic_us==987654321&&value->zone.utc_offset.count()==offset);
            assert(value->zone.daylight_saving==dst);
            assert(value->local().time_since_epoch().count()==unix_us+std::int64_t(offset)*1000000);}
    }
    ++completed;co_return pxa::Result<void>{};
}
int main() {
    pxa::Transport transport; transport.phase(pxa::Phase::event);
    pxa::RequestTable requests; pxa::TaskScope scope;
    std::array<std::byte, 12> reply{};
    assert(scope.start(run(transport, requests)));
    assert(requests.dispatch({4, 2, token, reply})); scope.reap(); assert(completed == 1);
    assert(scope.start(run(transport, requests, pxa::Error::protocol_error)));
    assert(requests.dispatch({4, 2, token, std::span{reply}.first(11)})); scope.reap(); assert(completed == 2);
    assert(scope.start(run(transport, requests, pxa::Error::denied)));
    pxa::wire::put32(reply.data(), static_cast<std::uint32_t>(pxa::Error::denied));
    assert(requests.dispatch({4, 2, token, std::span{reply}.first(4)})); scope.reap(); assert(completed == 3);
    send_result = -13;
    assert(scope.start(run(transport, requests, pxa::Error::unavailable))); assert(completed == 4);
    send_result = 0;
    assert(scope.start(run(transport, requests))); scope.cancel(); assert(cancelled == 1);
    opcode=3;
    std::array<std::byte,28> snapshot{};
    auto reply_wall=[&](std::size_t size=28){assert(requests.dispatch({4,3,token,std::span{snapshot}.first(size)}));scope.reap();};
    for(int offset:{0,28800,-12600,20700,45900}){
        pxa::wire::put64(snapshot.data()+4,std::uint64_t(-1234567));
        pxa::wire::put64(snapshot.data()+12,987654321);
        pxa::wire::put32(snapshot.data()+20,std::uint32_t(offset));
        assert(scope.start(wall<false>(transport,requests,-1234567,offset,false)));reply_wall();
        assert(scope.start(wall<true>(transport,requests,-1234567,offset,false)));reply_wall();
    }
    pxa::wire::put32(snapshot.data()+24,1);
    assert(scope.start(wall<false>(transport,requests,-1234567,45900,true)));reply_wall();
    assert(scope.start(wall<false>(transport,requests,0,0,false,pxa::Error::protocol_error)));reply_wall(27);
    pxa::wire::put32(snapshot.data()+24,2);
    assert(scope.start(wall<true>(transport,requests,0,0,false,pxa::Error::protocol_error)));reply_wall();
    pxa::wire::put32(snapshot.data()+24,0);pxa::wire::put32(snapshot.data()+20,86401);
    assert(scope.start(wall<false>(transport,requests,0,0,false,pxa::Error::protocol_error)));reply_wall();
    pxa::wire::put32(snapshot.data()+20,28800);pxa::wire::put64(snapshot.data()+4,INT64_MAX);
    assert(scope.start(wall<false>(transport,requests,0,0,false,pxa::Error::protocol_error)));reply_wall();
    pxa::wire::put32(snapshot.data(),std::uint32_t(pxa::Error::denied));
    assert(scope.start(wall<false>(transport,requests,0,0,false,pxa::Error::denied)));reply_wall(4);
    send_result=-13;
    assert(scope.start(wall<true>(transport,requests,0,0,false,pxa::Error::unavailable)));scope.reap();
    send_result=0;
    assert(scope.start(wall<false>(transport,requests,0,0,false)));scope.cancel();assert(cancelled==2);
    assert(pxa::task_pool_stats().active_slots == 0 && pxa::task_pool_stats().peak_slots == 1);
}
