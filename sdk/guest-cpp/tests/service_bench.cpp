// Optional native mock benchmark; never represents Host/device latency or displayed FPS.
#include <pxa/storage.hpp>
#include <pxa/permission.hpp>
#include <chrono>
#include <cassert>
#include <cstdio>
#include <cstdlib>
static unsigned mode, iterations=2000000, submits;
static std::uint64_t token;
static bool done;
extern "C" std::int32_t pxa_submit(const std::uint8_t* data,std::uint32_t size) {
    auto* b=reinterpret_cast<const std::byte*>(data);
    assert(pxa::wire::get16(b)==(mode?11:6));
    assert(pxa::wire::get16(b+2)==(mode?1:2));
    assert(size==(mode?47:36));
    token=pxa::wire::get64(b+4); ++submits; return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t,std::uint32_t,std::uint8_t*,std::uint32_t) {assert(false);return -1;}
static pxa::Task<void> run(pxa::StorageService storage,pxa::PermissionService permissions) {
    const std::array<std::byte,4> value{};
    for(unsigned i=0;i<iterations;++i) {
        if(mode) {auto result=co_await permissions.check("audio.playback","media");assert(result&&*result);}
        else {auto result=co_await storage.set("save",value);assert(result);}
    }
    done=true; co_return pxa::Result<void>{};
}
int main(int argc,char**argv) {
    if(argc>1)mode=std::atoi(argv[1]);
    if(argc>2)iterations=std::strtoul(argv[2],nullptr,10);
    if(mode>1 || !iterations) return 2;
    pxa::Transport transport;transport.phase(pxa::Phase::event);
    pxa::RequestTable requests;pxa::TaskScope tasks;
    const std::array<std::byte,5> result{std::byte{},std::byte{},std::byte{},std::byte{},std::byte{1}};
    const auto start=std::chrono::steady_clock::now();
    auto started=tasks.start(run({transport,requests},{transport,requests})); assert(started);
    while(!done) {
        auto dispatched=requests.dispatch({std::uint16_t(mode?11:6),std::uint16_t(mode?1:2),token,std::span{result}.first(mode?5:4)});
        assert(dispatched);
    }
    tasks.reap();
    const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    assert(submits==iterations&&pxa::task_pool_stats().active_slots==0);
    const auto stats=pxa::task_pool_stats();
    std::printf("{\"ns_per_request\":%f,\"peak_slots\":%u,\"pool_reserved_bytes\":%zu}\n",double(ns)/iterations,stats.peak_slots,stats.reserved_bytes);
}
