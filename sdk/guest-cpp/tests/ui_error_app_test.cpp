#include "../../testing/pxa_control_capture.h"
#include <pxa/app.hpp>
#include <array>
#include <cassert>

static bool reject_commit;
static unsigned errors;
static unsigned log_errors;
static pxa::Error last_error;
extern "C" std::int32_t pxa_test_control(const std::uint8_t* data, std::uint32_t) {
    const auto* bytes = reinterpret_cast<const std::byte*>(data);
    if (pxa::wire::get16(bytes) == 19) ++log_errors;
    if (reject_commit && pxa::wire::get16(bytes) == 3 && pxa::wire::get16(bytes + 2) == 3)
        return static_cast<std::int32_t>(pxa::Error::busy);
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { return -3; }

struct Probe {
    pxa::ui::State<int> count{0};
    auto view() { return pxa::ui::Text(count); }
    pxa::Result<bool> on_event(pxa::Context&, const pxa::Event&) {
        count.set(count.get() + 1);
        return true;
    }
    void on_error(pxa::Context&, pxa::Error error) { ++errors; last_error = error; }
};
PXA_APPLICATION(Probe)

int main() {
    reject_commit = true;
    assert(pxa_app_start(nullptr, 0) == static_cast<std::int32_t>(pxa::Error::busy));
    assert(errors == 1 && last_error == pxa::Error::busy);
    reject_commit = false;
    assert(pxa_app_start(nullptr, 0) == 0);
    std::array<std::byte, 20> event{};
    pxa::wire::put16(event.data(), 99);
    pxa::wire::put16(event.data() + 2, 1);
    reject_commit = true;
    assert(pxa_app_on_event(reinterpret_cast<const std::uint8_t*>(event.data()), event.size()) == 1);
    assert(errors == 2 && last_error == pxa::Error::busy);
    reject_commit = false;
    assert(pxa_app_on_event(reinterpret_cast<const std::uint8_t*>(event.data()), event.size()) == 1);
    assert(errors == 2);
    assert(log_errors == 2);
    pxa_app_stop(0);
}
