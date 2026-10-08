#include <pxa/ui.hpp>
#include <cassert>
#include <vector>

static std::vector<std::vector<std::byte>> packets;
static bool reject_write;
extern "C" std::int32_t pxa_submit(const std::uint8_t* data, std::uint32_t size) {
    auto* bytes = reinterpret_cast<const std::byte*>(data);
    packets.emplace_back(bytes, bytes + size);
    return reject_write && pxa::wire::get16(bytes + 2) == 8 ? -9 : 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { return -3; }

int main() {
    using namespace pxa::ui;
    pxa::Transport transport;
    transport.phase(pxa::Phase::event);
    CanvasRef canvas;
    CanvasCommands<> commands;
    commands.rect(-1, 2, 40, 30, 0xff0000ff).line(0, 0, 10, 10, 0xffffffff)
            .clip({0, 0, 30, 30}).text(2, 2, 28, 0xffffffff, "Canvas").pop_clip();
    assert(!canvas.present(commands) && packets.empty());
    {
        auto page = Page(transport, Canvas(canvas, 80, 60));
        static_assert(decltype(page)::ref_capacity == 1);
        static_assert(decltype(page)::binding_capacity == 0);
        static_assert(decltype(page)::handler_capacity == 0);
        assert(page.mount() && canvas.mounted());
        const auto before = packets.size();
        assert(canvas.present(commands));
        assert(packets.size() == before + 3);
        const auto& write = packets[before + 1];
        assert(pxa::wire::get16(write.data() + 2) == 8);
        assert(pxa::wire::get32(write.data() + 20) == 1);
        assert(pxa::wire::get32(write.data() + 24) == 2);
        assert(pxa::wire::get32(write.data() + 28) == 1);
        assert(write[32] == std::byte{1} && pxa::wire::get16(write.data() + 34) == 28);
        assert(pxa::wire::get32(write.data() + 36) == UINT32_MAX);
        assert(pxa::wire::get32(write.data() + 52) == 0xff0000ff);
        commands.reset();
        commands.rect(0, 0, 20, 20, 0x00ff00ff);
        reject_write = true;
        assert(!canvas.present(commands));
        reject_write = false;
        const auto retry = packets.size();
        const std::array<CanvasRegion, 1> dirty{{{0, 0, 20, 20}}};
        assert(canvas.present(commands, dirty));
        assert(pxa::wire::get32(packets[retry].data() + 28) == 3);
        const auto& present = packets.back();
        assert(present[32] == std::byte{1} && present.size() == 49);
        CanvasCommands<4> tiny;
        tiny.rect(0, 0, 1, 1, 0xffffffff);
        const auto stable = packets.size();
        auto overflow = canvas.present(tiny);
        assert(!overflow && overflow.error() == pxa::Error::limit_exceeded);
        commands.reset(); commands.pop_clip();
        assert(!canvas.present(commands));
        commands.reset(); commands.clip({0, 0, 5, 5});
        assert(!canvas.present(commands));
        commands.reset();
        const std::array<CanvasRegion, 1> invalid{{{0, 0, 0, 1}}};
        assert(!canvas.present(commands, invalid));
        assert(packets.size() == stable);
        transport.phase(pxa::Phase::stopped);
        assert(!canvas.present(commands));
        assert(packets.size() == stable);
    }
    assert(!canvas.mounted() && !canvas.present(commands));
}
