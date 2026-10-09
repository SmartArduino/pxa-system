#include <pxa/app.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <optional>
#include <utility>

static std::optional<pxa::Surface> surface;
static std::uint64_t token;
static std::uint16_t opcode;
static unsigned registrations;
static unsigned presents;
static unsigned closes;
static unsigned action;
static bool ready;
static bool small_budget;
static pxa::Error create_error = pxa::Error::bad_state;

extern "C" std::int32_t pxa_submit(const std::uint8_t* data,
                                     std::uint32_t length) {
    auto* bytes = reinterpret_cast<const std::byte*>(data);
    const auto service = pxa::wire::get16(bytes);
    opcode = pxa::wire::get16(bytes + 2);
    token = pxa::wire::get64(bytes + 4);
    if (service == 1) {
        assert(opcode == 2 && length == 28);
        assert(pxa::wire::get64(bytes + 20) == 77);
        ++closes;
        return 0;
    }
    assert(service == 16 && token != 0);
    if (opcode == 1) {
        assert(length == 28 && pxa::wire::get16(bytes + 20) == 8);
        assert(pxa::wire::get16(bytes + 22) == 4);
        assert(pxa::wire::get16(bytes + 24) == 1);
        assert(bytes[26] == std::byte{2} && bytes[27] == std::byte{4});
    } else if (opcode == 2) {
        assert(length == 44 && pxa::wire::get64(bytes + 20) == 77);
        assert(pxa::wire::get16(bytes + 36) == 8);
        assert(pxa::wire::get16(bytes + 38) == 4);
    } else {
        assert(opcode == 4 && length == 28);
        assert(pxa::wire::get64(bytes + 20) == 77);
    }
    return 0;
}

extern "C" std::int32_t pxa_io(std::uint64_t handle,
                                 std::uint32_t operation,
                                 std::uint8_t* data, std::uint32_t length) {
    assert(handle == 77);
    auto* bytes = reinterpret_cast<std::byte*>(data);
    if (operation == 0x100) {
        assert(length == 128 &&
               (reinterpret_cast<std::uintptr_t>(data) & 63) == 0);
        ++registrations;
        return static_cast<std::int32_t>(length);
    }
    if (operation == 0x101) {
        assert(length == 4);
        if (action == 2) return -7;
        bytes[0] = std::byte(action == 3 ? 1 : 0);
        return 4;
    }
    assert(operation == 0x102 && length == 16);
    assert(bytes[0] == std::byte{0});
    assert(pxa::wire::get64(bytes + 8) == 1);
    ++presents;
    return 16;
}

// A service facade may be destroyed before the suspended coroutine starts.
// The noinline boundary makes its lifetime observable to ASan as well.
[[gnu::noinline]] static pxa::Task<pxa::Surface> create_after_return(
    pxa::Context& context, pxa::SurfaceOptions options) {
    return context.surface().create_mapped(options);
}

struct SurfaceApp {
    pxa::Task<void> initialize(pxa::Context& context) {
        auto created = co_await create_after_return(context, {
            .width = 8, .height = 4, .buffers = 2,
            .max_buffer_bytes = small_budget ? 100u : 128u});
        if (!created) {
            create_error = created.error();
            co_return pxa::Result<void>{};
        }
        surface.emplace(std::move(*created));
        assert(surface->frame_bytes() == 64 &&
               surface->stride_bytes() == 16 &&
               surface->buffer_count() == 2);
        auto configured = co_await surface->configure({
            .width = 8, .height = 4});
        assert(configured);
        auto state = co_await surface->query_state();
        assert(state && state->free_buffers == 2 &&
               state->presented_frames == 3);
        ready = true;
        co_return pxa::Result<void>{};
    }

    pxa::Result<void> on_start(pxa::Context& context) {
        return context.tasks().start(initialize(context));
    }

    pxa::Result<bool> on_event(pxa::Context&, const pxa::Event& event) {
        if (event.service != 99) return false;
        auto frame = surface->acquire();
        if (action == 2) {
            assert(!frame && frame.error() == pxa::Error::would_block);
        } else {
            assert(frame && frame->pixels().size() == 64);
            assert(frame->stride_bytes() == 16);
            auto pixels = frame->rgb565();
            assert(pixels && pixels->width() == 8 && pixels->height() == 4);
            auto row = *pixels->row(3);
            row[7] = 0xbeef;
            assert(frame->pixels()[62] == std::byte{0xef} &&
                   frame->pixels()[63] == std::byte{0xbe});
            frame->pixels()[0] = std::byte{0x34};
            if (action == 1) {
                auto moved = std::move(*surface);
                surface.emplace(std::move(moved));
                assert(frame->present(1));
            }
        }
        return true;
    }
};

PXA_APPLICATION(SurfaceApp)

static void deliver(std::uint16_t service, std::uint16_t event_opcode,
                    std::uint64_t event_token,
                    std::span<const std::byte> payload) {
    std::array<std::byte, 80> packet{};
    pxa::wire::put16(packet.data(), service);
    pxa::wire::put16(packet.data() + 2, event_opcode);
    pxa::wire::put64(packet.data() + 4, event_token);
    pxa::wire::put32(packet.data() + 12, payload.size());
    for (std::size_t i = 0; i < payload.size(); ++i)
        packet[20 + i] = payload[i];
    assert(pxa_app_on_event(reinterpret_cast<const std::uint8_t*>(packet.data()),
                            20 + payload.size()) == 1);
}

int main() {
    assert(pxa_app_start(nullptr, 0) == 0 && opcode == 1);
    std::array<std::byte, 24> create{};
    pxa::wire::put64(create.data() + 4, 77);
    pxa::wire::put32(create.data() + 12, 16);
    pxa::wire::put32(create.data() + 16, 64);
    create[20] = std::byte{2};
    deliver(16, 1, token, create);
    assert(registrations == 1 && opcode == 2);
    std::array<std::byte, 4> success{};
    deliver(16, 2, token, success);
    assert(opcode == 4);
    std::array<std::byte, 52> state{};
    pxa::wire::put64(state.data() + 12, 3);
    pxa::wire::put32(state.data() + 44, 2);
    deliver(16, 4, token, state);
    assert(ready);

    action = 1;
    deliver(99, 1, 0, {});
    assert(presents == 1 && closes == 0);
    action = 2;
    deliver(99, 1, 0, {});
    assert(closes == 0);
    action = 3;
    deliver(99, 1, 0, {});
    assert(closes == 1 && !*surface);
    surface.reset();
    pxa_app_stop(0);

    std::array<std::byte, 20> released{};
    pxa::wire::put64(released.data(), 77);
    released[8] = std::byte{1};
    pxa::wire::put64(released.data() + 12, 9);
    auto decoded = pxa::decode_surface_release({16, 0x8001, 0, released});
    assert(decoded && decoded->handle == 77 && decoded->buffer_index == 1 &&
           decoded->frame_id == 9);
    released[9] = std::byte{1};
    assert(!pxa::decode_surface_release({16, 0x8001, 0, released}));

    small_budget = true;
    ready = false;
    assert(pxa_app_start(nullptr, 0) == 0 && opcode == 1);
    deliver(16, 1, token, create);
    assert(create_error == pxa::Error::resource_limit &&
           registrations == 1 && closes == 2);
    pxa_app_stop(0);

    small_budget = false;
    assert(pxa_app_start(nullptr, 0) == 0 && opcode == 1);
    create[21] = std::byte{1};
    deliver(16, 1, token, create);
    assert(create_error == pxa::Error::protocol_error &&
           registrations == 1 && closes == 3);
    pxa_app_stop(0);
}
