#include "../../testing/pxa_control_capture.h"
#include <pxa/ui_volume_keys.hpp>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>

using namespace pxa;
using namespace pxa::ui;
static unsigned allocations, writes;
static std::uint64_t mask;
void* operator new(std::size_t bytes) { ++allocations; return std::malloc(bytes); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
extern "C" std::int32_t pxa_test_control(const std::uint8_t* data, std::uint32_t size) {
    const auto* bytes = reinterpret_cast<const std::byte*>(data);
    if (wire::get16(bytes) == 3 && wire::get16(bytes + 2) == 2) {
        ++writes;
        for (std::size_t i = 24; i < size;) {
            const auto length = wire::get16(bytes + i + 2);
            assert(i + length + 4 <= size);
            if (bytes[i] == std::byte{2} && wire::get32(bytes + i + 4) == 1 &&
                wire::get16(bytes + i + 8) == protocol::event_mask)
                mask = wire::get64(bytes + i + 10);
            i += length + 4;
        }
    }
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { return -3; }

int main() {
    Transport transport; transport.phase(Phase::event);
    State<bool> enabled{false}; unsigned calls = 0;
    VolumeKeyEvent last{};
    const auto before = allocations;
    {
        auto page = Page(transport, CaptureVolumeKeys(Text("Keys"), enabled,
            [&](const VolumeKeyEvent& key) { ++calls; last = key; }));
        static_assert(decltype(page)::binding_capacity == 1);
        static_assert(decltype(page)::handler_capacity == 1);
        assert(page.mount() && mask == 0);
        auto idle = writes; assert(page.flush() && writes == idle);
        enabled.set(true); assert(page.flush() && mask == 16);
        std::array<std::byte, 28> bytes{};
        wire::put32(bytes.data(), 1); wire::put32(bytes.data() + 4, 1);
        wire::put32(bytes.data() + 8, page.generation());
        wire::put16(bytes.data() + 12, 5); wire::put16(bytes.data() + 14, 1);
        wire::put64(bytes.data() + 16, 1234567);
        Event event{3, 0x8001, 0, bytes};
        for (unsigned key = 1; key <= 4; ++key) {
            wire::put32(bytes.data() + 24, key);
            assert(page.handle(event));
            assert(std::uint32_t(last.key) == key && calls == key);
            assert(last.timestamp_us == 1234567);
        }
        auto malformed = event; malformed.payload = std::span{bytes}.first(27);
        assert(!decode_volume_key(malformed) && !page.handle(malformed));
        malformed = event; malformed.token = 1; assert(!page.handle(malformed));
        malformed = event; malformed.service = 4; assert(!page.handle(malformed));
        wire::put32(bytes.data() + 24, 5); assert(!page.handle(event));
        wire::put32(bytes.data() + 24, 0); assert(!page.handle(event));
        wire::put32(bytes.data() + 24, 1);
        wire::put32(bytes.data() + 8, page.generation() + 1); assert(!page.handle(event));
        wire::put32(bytes.data() + 8, page.generation());
        wire::put32(bytes.data() + 4, 2); assert(!page.handle(event));
        wire::put32(bytes.data() + 4, 1);
        // A key already queued before capture is disabled must not invoke user code.
        enabled.set(false); assert(page.handle(event) && calls == 4);
        assert(page.flush() && mask == 0);
        assert(!page.handle(event)); // Previous generation after the commit.
    }
    enabled.set(true); // Destroyed view unsubscribed from the reactive source.
    assert(allocations == before);
    std::puts("UI volume keys: opt-in capture, release, reactive mask, stale/malformed events, no allocation OK");
}
