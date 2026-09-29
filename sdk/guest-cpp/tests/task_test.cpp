#include <pxa/app.hpp>

#include <array>
#include <cassert>

static std::uint64_t request_token;
static int result_value;
static int accepted_requests;
static int failed_tasks;
static bool foreground_mode;

extern "C" std::int32_t pxa_submit(const std::uint8_t* data,
                                      std::uint32_t length) {
    assert(length >= 20);
    if (data[0] == 42) {
        request_token = pxa::wire::get64(
            reinterpret_cast<const std::byte*>(data) + 4);
        ++accepted_requests;
    }
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t,
                                  std::uint8_t*, std::uint32_t) {
    return -3;
}

pxa::Task<int> load(pxa::Context& context) {
    std::array<std::byte, 4> payload{};
    auto response = co_await context.request(42, 1, payload);
    if (!response) co_return std::unexpected(response.error());
    if (response->payload.size() != 4)
        co_return std::unexpected(pxa::Error::protocol_error);
    co_return static_cast<int>(pxa::wire::get32(response->payload.data()));
}

pxa::Task<void> root(pxa::Context& context) {
    auto result = co_await load(context);
    if (!result) co_return std::unexpected(result.error());
    result_value = *result;
    co_return pxa::Result<void>{};
}

pxa::Task<void> fail() {
    co_return std::unexpected(pxa::Error::denied);
}

struct TestApp {
    pxa::Result<void> on_start(pxa::Context& context) {
        return foreground_mode ? context.foreground_tasks().start(root(context))
                               : context.tasks().start(root(context));
    }
    pxa::Result<bool> on_event(pxa::Context& context, const pxa::Event& event) {
        if (event.service != 42 || event.opcode != 2) return false;
        auto started = context.foreground_tasks().start(root(context));
        if (!started) return std::unexpected(started.error());
        return true;
    }
};

PXA_APPLICATION(TestApp)

int main() {
    pxa::TaskScope scope;
    scope.on_error(nullptr, [](void*, pxa::Error error) noexcept {
        assert(error == pxa::Error::denied);
        ++failed_tasks;
    });
    assert(scope.start(fail()));
    assert(failed_tasks == 1);

    assert(pxa_app_start(nullptr, 0) == 0);
    assert(accepted_requests == 1 && request_token != 0);
    std::array<std::byte, 24> packet{};
    pxa::wire::put16(packet.data(), 42);
    pxa::wire::put16(packet.data() + 2, 1);
    pxa::wire::put64(packet.data() + 4, request_token);
    pxa::wire::put32(packet.data() + 12, 4);
    pxa::wire::put32(packet.data() + 20, 73);
    assert(pxa_app_on_event(
               reinterpret_cast<const std::uint8_t*>(packet.data()),
               packet.size()) == 1);
    assert(result_value == 73);
    pxa_app_stop(0);

    result_value = 0;
    assert(pxa_app_start(nullptr, 0) == 0);
    auto abandoned_token = request_token;
    pxa_app_stop(0);
    assert(pxa_app_on_event(
               reinterpret_cast<const std::uint8_t*>(packet.data()),
               packet.size()) == static_cast<int>(pxa::Error::bad_state));
    assert(result_value == 0 && abandoned_token != 0);

    foreground_mode = true;
    assert(pxa_app_start(nullptr, 0) == 0);
    for (unsigned i = 0; i < 16; ++i) {
        pxa::wire::put64(packet.data() + 4, request_token);
        pxa::wire::put32(packet.data() + 20, i);
        assert(pxa_app_on_event(
                   reinterpret_cast<const std::uint8_t*>(packet.data()),
                   packet.size()) == 1);
        assert(result_value == static_cast<int>(i));
        if (i == 15) break;
        std::array<std::byte, 20> launch{};
        pxa::wire::put16(launch.data(), 42);
        pxa::wire::put16(launch.data() + 2, 2);
        assert(pxa_app_on_event(
                   reinterpret_cast<const std::uint8_t*>(launch.data()),
                   launch.size()) == 1);
    }
    pxa_app_stop(0);
}
