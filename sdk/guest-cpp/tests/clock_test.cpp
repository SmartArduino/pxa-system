#include <pxa/clock.hpp>
#include <cassert>
static std::uint64_t token;
static unsigned completed = 0, cancelled = 0;
static std::int32_t send_result = 0;
extern "C" std::int32_t pxa_submit(const std::uint8_t* data, std::uint32_t size) {
    auto p = reinterpret_cast<const std::byte*>(data);
    if (pxa::wire::get16(p) == 1) { ++cancelled; return 0; }
    assert(size == 20 && pxa::wire::get16(p) == 4 && pxa::wire::get16(p+2) == 2);
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
    assert(pxa::task_pool_stats().active_slots == 0 && pxa::task_pool_stats().peak_slots == 1);
}
