#include <pxa/audio.hpp>
#include <pxa/clock.hpp>
#include <pxa/fs.hpp>
#include <pxa/game_service.hpp>
#include <pxa/sensor.hpp>
#include <pxa/surface.hpp>
#include <pxa/binary.hpp>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <string_view>

using namespace pxa;
static constexpr std::uint64_t handle = 0x100000019;
static std::array<std::byte, 4096> packet;
static std::size_t packet_size;
static unsigned closes, submissions, draws;
static bool block_draw;
static bool block_close;
static unsigned heap_allocations;
void* operator new(std::size_t size) {
    ++heap_allocations;
    if (void* memory = std::malloc(size)) return memory;
    std::abort();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

extern "C" std::int32_t pxa_submit(const std::uint8_t* data, std::uint32_t size) {
    const auto* bytes = reinterpret_cast<const std::byte*>(data);
    if (wire::get16(bytes) == 1) {
        if (wire::get16(bytes + 2) == 2) {
            assert(wire::get64(bytes + 20) == handle);
            if (block_close) return static_cast<std::int32_t>(Error::busy);
            ++closes;
        }
        return 0;
    }
    assert(size <= packet.size());
    std::memcpy(packet.data(), data, size);
    packet_size = size;
    ++submissions;
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t value, std::uint32_t operation,
    std::uint8_t*, std::uint32_t size) {
    assert(value == handle);
    if (operation == 0x100) return size; // Surface registration.
    assert(operation == 0x101);
    ++draws;
    return block_draw ? static_cast<std::int32_t>(Error::would_block) : size;
}

template<class T, class Inspect>
static Result<T> complete(Task<T> task, RequestTable& requests,
    std::span<const std::byte> response, Inspect inspect) {
    assert(task.valid());
    auto coroutine = task.release();
    coroutine.resume();
    inspect();
    if (!coroutine.done()) {
        assert(requests.dispatch({wire::get16(packet.data()), wire::get16(packet.data() + 2),
                                 wire::get64(packet.data() + 4), response}));
    }
    assert(coroutine.done() && coroutine.promise().result);
    auto result = std::move(*coroutine.promise().result);
    coroutine.destroy();
    return result;
}
template<class T>
static Result<T> complete(Task<T> task, RequestTable& requests,
    std::span<const std::byte> response) {
    return complete(std::move(task), requests, response, [] {});
}

static void exercise(std::string_view which) {
    Transport transport;
    transport.phase(Phase::event);
    RequestTable requests;
    const std::array<std::byte, 4> success{};
    if (which == "audio-graph") {
        std::optional<AudioSession> owner;
        auto task = [&] {
            AudioSession original(transport, requests, handle, {48000, 2, 20});
            std::array bands{EqBand{1000, -256, 256}};
            auto pending = original.graph(-12 * 256, bands);
            bands[0].frequency_hz = 2000;
            owner.emplace(std::move(original));
            return pending;
        }(); // Both the original wrapper and bands have already died.
        assert(complete(std::move(task), requests, success, [] {
            assert(wire::get64(packet.data() + 24) == handle);
            assert(wire::get16(packet.data() + 42) == 1000);
        }));
    } else if (which == "audio-query" || which == "audio-flush") {
        AudioSession original(transport, requests, handle, {48000, 2, 20});
        if (which == "audio-flush") {
            auto task = original.flush();
            AudioSession moved(std::move(original));
            assert(complete(std::move(task), requests, success, [] {
                assert(wire::get64(packet.data() + 24) == handle);
            }));
        } else {
            auto task = original.query();
            AudioSession moved(std::move(original));
            auto result = complete(std::move(task), requests, success, [] {
                assert(wire::get64(packet.data() + 24) == handle);
            });
            assert(!result && result.error() == Error::protocol_error);
        }
    } else if (which == "audio-open") {
        Permission permission(transport, handle);
        auto task = AudioService(transport, requests).open(permission);
        Permission moved(std::move(permission));
        auto result = complete(std::move(task), requests, success, [] {
            assert(wire::get64(packet.data() + 24) == handle);
        });
        assert(!result && result.error() == Error::protocol_error);
    } else if (which == "surface-configure" || which == "surface-query" || which == "surface-close") {
        std::array<std::byte, 24> created{};
        wire::put64(created.data() + 4, handle);
        wire::put32(created.data() + 12, 16);
        wire::put32(created.data() + 16, 64);
        created[20] = std::byte{2};
        auto original = complete(SurfaceService(transport, requests).create_mapped(
            {.width = 8, .height = 4, .max_buffer_bytes = 128}), requests, created);
        assert(original);
        if (which == "surface-close") {
#if !defined(PXA_BASELINE)
            auto lease = original->acquire();
            assert(lease && lease->pixels().size() == 64);
            block_close = true;
            auto closed = original->close();
            assert(!closed && closed.error() == Error::busy && *original);
            assert(original->handle() == handle);
            assert(lease->pixels().size() == 64);
            block_close = false;
            assert(original->close() && !*original);
            assert(lease->pixels().empty());
            assert(!lease->rgb565());
            assert(original->close());
#endif
        } else if (which == "surface-configure") {
            auto task = original->configure({.width = 8, .height = 4});
            Surface moved(std::move(*original));
            const auto before = submissions;
            assert(complete(std::move(task), requests, success));
            assert(submissions == before + 1);
        } else {
            auto task = original->query_state();
            Surface moved(std::move(*original));
            const std::array<std::byte, 52> state{};
            const auto before = submissions;
            assert(complete(std::move(task), requests, state));
            assert(submissions == before + 1);
        }
    } else if (which == "fs-handle" || which == "asset-handle" ||
               which == "sensor-handle" || which == "game-handle") {
        // A recognizable returned handle must be closed even if extra/missing
        // metadata makes the success response invalid.
        std::array<std::byte, 13> malformed{};
        wire::put64(malformed.data() + 4, handle);
        const auto before = closes;
        if (which == "fs-handle") {
            auto result = complete(FilesystemService(transport, requests).open("save.bin"), requests, malformed);
            assert(!result && result.error() == Error::protocol_error);
        } else if (which == "asset-handle") {
            auto result = complete(AssetService(transport, requests).load(AssetKind::texture, "tile.tex"), requests, malformed);
            assert(!result && result.error() == Error::protocol_error);
        } else if (which == "sensor-handle") {
            Permission permission(transport, handle);
            SensorDescriptor descriptor{.id = 1, .semantic = {}, .unit = {}, .dimensions = 1, .min_period_ms = 10, .max_period_ms = 100};
            auto result = complete(SensorService(transport, requests).subscribe(descriptor, 20, permission), requests, malformed);
            assert(!result && result.error() == Error::protocol_error);
            assert(permission.release() == handle); // Only count returned ownership.
        } else {
            auto result = complete(game::Service(transport, requests).create(), requests, malformed);
            assert(!result && result.error() == Error::protocol_error);
        }
        assert(closes == before + 1);
    } else if (which == "clock-status" || which == "asset-status" || which == "game-status") {
        for (int status : {1, -17, INT32_MIN, INT32_MAX}) {
            std::array<std::byte, 4> invalid{};
            wire::put32(invalid.data(), static_cast<std::uint32_t>(status));
            if (which == "clock-status") {
                auto result = complete(ClockService(transport, requests).now(), requests, invalid);
                assert(!result && result.error() == Error::protocol_error);
            } else if (which == "asset-status") {
                auto result = complete(AssetService(transport, requests).query("tile.tex"), requests, invalid);
                assert(!result && result.error() == Error::protocol_error);
            } else {
                auto result = complete(game::Service(transport, requests).create(), requests, invalid);
                assert(!result && result.error() == Error::protocol_error);
            }
        }
    } else if (which == "frame-submit") {
        game::Renderer renderer(transport, handle, 0);
        game::DrawBuffer<256> buffer;
        auto frame = renderer.frame(buffer);
        frame.clear({0x1234});
        block_draw = true;
        assert(!frame.submit());
        block_draw = false;
        assert(frame.submit()); // Backpressure can be retried.
        const auto before = draws;
        auto repeated = frame.submit();
        assert(!repeated && repeated.error() == Error::bad_state && draws == before);
        auto next = renderer.frame(buffer);
        next.clear({0x4567});
        assert(next.submit());
    } else if (which == "transport-alias") {
        std::array<std::byte, 96> scratch;
        for (std::size_t i = 0; i < scratch.size(); ++i) scratch[i] = std::byte(i);
        const auto expected = scratch;
        assert(transport.scratch(scratch));
        assert(transport.send(4, 2, 1, std::span(scratch).first(64)));
        assert(packet_size == 84 && std::equal(expected.begin(), expected.begin() + 64, packet.begin() + 20));
    } else if (which == "writer-alias") {
        for (bool binary : {false, true}) {
            std::array<std::byte, 32> bytes;
            for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = std::byte(i);
            const auto original = bytes;
            if (binary) {
                binary::Writer writer(std::span(bytes).subspan(4));
                assert(writer.bytes(std::span(bytes).first(24)));
            } else {
                wire::Writer writer(std::span(bytes).subspan(4));
                assert(writer.bytes(std::span(bytes).first(24)));
            }
            assert(std::equal(original.begin(), original.begin() + 24, bytes.begin() + 4));
        }
    } else assert(false);
    assert(task_pool_stats().active_slots == 0);
    assert(heap_allocations == 0);
}
int main(int argc, char** argv) {
    if (argc == 2) exercise(argv[1]);
    else for (auto name : {"audio-graph", "audio-query", "audio-flush", "audio-open",
        "surface-configure", "surface-query", "surface-close", "fs-handle", "asset-handle", "sensor-handle", "game-handle",
        "clock-status", "asset-status", "game-status", "frame-submit", "transport-alias", "writer-alias"}) exercise(name);
}
