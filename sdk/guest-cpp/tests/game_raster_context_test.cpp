#include "../../testing/pxa_control_capture.h"
#include <pxa/app.hpp>
#include <array>
#include <cassert>

static std::uint64_t token;
static std::uint32_t requested = 2, returned = 2, reserved;
static unsigned closes;
static bool created;
static pxa::Error error = pxa::Error::bad_state;
extern "C" std::int32_t pxa_test_control(const std::uint8_t* data, std::uint32_t size) {
    const auto* p = reinterpret_cast<const std::byte*>(data);
    if (pxa::wire::get16(p) == 18) {
        assert(size == 28 && pxa::wire::get16(p+2) == 3);
        token = pxa::wire::get64(p+4);
        assert(token && pxa::wire::get32(p+20) == requested && !pxa::wire::get32(p+24));
    } else {
        assert(pxa::wire::get16(p) == 1 && pxa::wire::get16(p+2) == 2);
        assert(pxa::wire::get64(p+20) == 77);
        ++closes;
    }
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) {
    assert(false); return -1;
}
static pxa::Task<void> create(pxa::Context& context) {
    auto result = co_await context.game().create_raster(requested);
    if (result) {
        assert(result->handle() == 77 && result->max_spans() == requested);
        created = true;
    } else error = result.error();
    co_return pxa::Result<void>{};
}
struct App {
    pxa::Result<void> on_start(pxa::Context& context) {
        return context.tasks().start(create(context));
    }
};
PXA_APPLICATION(App)

static void deliver(std::int32_t status) {
    std::array<std::byte, 40> event{};
    pxa::wire::put16(event.data(), 18); pxa::wire::put16(event.data()+2, 3);
    pxa::wire::put64(event.data()+4, token);
    const auto payload = status ? 4u : 20u;
    pxa::wire::put32(event.data()+12, payload);
    pxa::wire::put32(event.data()+20, static_cast<std::uint32_t>(status));
    if (!status) {
        pxa::wire::put64(event.data()+24, 77);
        pxa::wire::put32(event.data()+32, returned);
        pxa::wire::put32(event.data()+36, reserved);
    }
    assert(pxa_app_on_event(reinterpret_cast<const std::uint8_t*>(event.data()), 20+payload) == 1);
}
int main() {
    assert(pxa_app_start(nullptr, 0) == 0); deliver(0);
    assert(created && closes == 1); pxa_app_stop(0);
    created = false; returned = 3;
    assert(pxa_app_start(nullptr, 0) == 0); deliver(0);
    assert(!created && error == pxa::Error::protocol_error && closes == 2); pxa_app_stop(0);
    returned = 2; reserved = 1;
    assert(pxa_app_start(nullptr, 0) == 0); deliver(0);
    assert(!created && error == pxa::Error::protocol_error && closes == 3); pxa_app_stop(0);
    reserved = 0;
    assert(pxa_app_start(nullptr, 0) == 0); deliver(static_cast<std::int32_t>(pxa::Error::denied));
    assert(!created && error == pxa::Error::denied && closes == 3); pxa_app_stop(0);
    requested = 4097; token = 0;
    assert(pxa_app_start(nullptr, 0) == 0);
    assert(!token && error == pxa::Error::limit_exceeded && closes == 3); pxa_app_stop(0);
}
