#include <pxa/app.hpp>

#include <array>
#include <cassert>

static std::uint64_t request_token;
static std::uint16_t request_opcode;
static std::size_t request_payload_size;
static int closes;
static pxa::Error result_error = pxa::Error::bad_state;
static bool created;
static bool fixed_mode;

extern "C" std::int32_t pxa_submit(const std::uint8_t* data,
                                      std::uint32_t size) {
    auto* bytes = reinterpret_cast<const std::byte*>(data);
    assert(size >= pxa::wire::header_bytes);
    auto service = pxa::wire::get16(bytes);
    auto opcode = pxa::wire::get16(bytes + 2);
    if (service == 18) {
        assert(opcode == (fixed_mode ? 1 : 2));
        request_opcode = opcode;
        request_token = pxa::wire::get64(bytes + 4);
        request_payload_size = pxa::wire::get32(bytes + 12);
        assert(request_token != 0 && request_payload_size == 12);
        assert(size == pxa::wire::header_bytes + request_payload_size);
        assert(pxa::wire::get16(bytes + 20) == (fixed_mode ? 160 : 0));
        assert(pxa::wire::get16(bytes + 22) == (fixed_mode ? 120 : 0));
        assert(std::to_integer<unsigned>(bytes[20 + 4]) == 2);
    } else {
        assert(service == 1 && opcode == 2);
        assert(pxa::wire::get64(bytes + 20) == 77);
        ++closes;
    }
    return 0;
}

extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t,
                                  std::uint8_t*, std::uint32_t) {
    assert(false);
    return -1;
}

pxa::Task<void> create_renderer(pxa::Context& context) {
    pxa::game::RenderOptions options;
    if (fixed_mode) {
        options.width = 160;
        options.height = 120;
    }
    auto renderer = co_await context.game().create(options);
    if (!renderer) {
        result_error = renderer.error();
        co_return pxa::Result<void>{};
    }
    assert(renderer->handle() == 77);
    assert(renderer->capabilities() == 9);
    assert(renderer->info().max_draw_bytes == 4096);
    assert(renderer->info().render_width == 160);
    assert(renderer->info().render_height == 120);
    assert(renderer->info().render_scale == (fixed_mode ? 0 : 2));
    created = true;
    co_return pxa::Result<void>{};
}

struct GameApp {
    pxa::Result<void> on_start(pxa::Context& context) {
        return context.tasks().start(create_renderer(context));
    }
};

PXA_APPLICATION(GameApp)

static void deliver(std::int32_t status) {
    std::array<std::byte, 56> event{};
    pxa::wire::put16(event.data(), 18);
    pxa::wire::put16(event.data() + 2, request_opcode);
    pxa::wire::put64(event.data() + 4, request_token);
    const auto payload_size = status == 0 ? (fixed_mode ? 24u : 36u) : 4u;
    pxa::wire::put32(event.data() + 12, payload_size);
    pxa::wire::put32(event.data() + 20,
                     static_cast<std::uint32_t>(status));
    if (status == 0) {
        pxa::wire::put64(event.data() + 24, 77);
        pxa::wire::put32(event.data() + 32, 9);
        pxa::wire::put32(event.data() + 36, 4096);
        pxa::wire::put16(event.data() + 40, 256);
        event[42] = std::byte{48};
        if (!fixed_mode) {
            pxa::wire::put16(event.data() + 44, 320);
            pxa::wire::put16(event.data() + 46, 240);
            pxa::wire::put16(event.data() + 48, 160);
            pxa::wire::put16(event.data() + 50, 120);
            event[52] = std::byte{2};
        }
    }
    assert(pxa_app_on_event(
               reinterpret_cast<const std::uint8_t*>(event.data()),
               pxa::wire::header_bytes + payload_size) == 1);
}

int main() {
    assert(pxa_app_start(nullptr, 0) == 0);
    deliver(0);
    assert(created && closes == 1);
    pxa_app_stop(0);

    created = false;
    fixed_mode = true;
    assert(pxa_app_start(nullptr, 0) == 0);
    deliver(0);
    assert(created && closes == 2);
    pxa_app_stop(0);

    created = false;
    assert(pxa_app_start(nullptr, 0) == 0);
    deliver(static_cast<std::int32_t>(pxa::Error::denied));
    assert(!created && result_error == pxa::Error::denied);
    pxa_app_stop(0);
    assert(closes == 2);
}
