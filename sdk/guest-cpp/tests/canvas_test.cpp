#include "../../testing/pxa_control_capture.h"
#include <pxa/ui.hpp>
#include <cassert>
#include <cstring>
#include <vector>

static std::vector<std::vector<std::byte>> packets;
static bool reject_write;
extern "C" std::int32_t pxa_test_control(const std::uint8_t* data, std::uint32_t size) {
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
        const auto presented = commands;
        CanvasCommands<> identical;
        identical.rect(-1, 2, 40, 30, 0xff0000ff).line(0, 0, 10, 10, 0xffffffff)
            .clip({0, 0, 30, 30}).text(2, 2, 28, 0xffffffff, "Canvas").pop_clip();
        assert(commands.same_commands(identical));
        identical.rect(0, 0, 1, 1, 0xffffffff);
        assert(!commands.same_commands(identical));
        assert(commands.same_commands(presented));
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
        commands.reset();
        commands.arc(-5, 7, 9, 45, 315, 0x123456ff, 2);
        const auto arc_begin = packets.size();
        assert(canvas.present(commands));
        const auto& arc = packets[arc_begin + 1];
        assert(arc.size() == 58 && arc[32] == std::byte{4});
        assert(pxa::wire::get16(arc.data() + 34) == 22);
        assert(pxa::wire::get32(arc.data() + 36) == std::uint32_t(-5));
        assert(pxa::wire::get32(arc.data() + 40) == 7);
        assert(pxa::wire::get32(arc.data() + 44) == 9);
        assert(pxa::wire::get16(arc.data() + 48) == 45);
        assert(pxa::wire::get16(arc.data() + 50) == 315);
        assert(pxa::wire::get32(arc.data() + 52) == 0x123456ff);
        assert(pxa::wire::get16(arc.data() + 56) == 2);
        commands.reset();
        commands.rect(1, 2, 30, 20, 0, 4, 2, 0xff00ffff)
                .ellipse(-4, 6, 24, 12, 0x12345678, 3, 0xabcdef90)
                .circle(20, 30, 5, 0xffffffff)
                .text_box({1, 2, 60, 40}, 0xabcdef12, "Box", 2, 1, 2);
        const auto shapes_begin = packets.size();
        assert(canvas.present(commands));
        const auto& shapes = packets[shapes_begin + 1];
        assert(shapes.size() == 32 + 32 + 30 + 30 + 31);
        const auto* rectangle = shapes.data() + 36;
        assert(pxa::wire::get16(rectangle + 22) == 2);
        assert(pxa::wire::get32(rectangle + 24) == 0xff00ffff);
        const auto* ellipse = shapes.data() + 64;
        assert(ellipse[0] == std::byte{2} && pxa::wire::get16(ellipse + 2) == 26);
        assert(pxa::wire::get32(ellipse + 4) == std::uint32_t(-4));
        assert(pxa::wire::get16(ellipse + 24) == 3);
        assert(pxa::wire::get32(ellipse + 26) == 0xabcdef90);
        const auto* circle = ellipse + 30;
        assert(circle[0] == std::byte{2});
        assert(pxa::wire::get32(circle + 4) == 15 && pxa::wire::get32(circle + 8) == 25);
        assert(pxa::wire::get32(circle + 12) == 10 && pxa::wire::get32(circle + 16) == 10);
        const auto* box = circle + 30;
        assert(box[0] == std::byte{9} && pxa::wire::get16(box + 2) == 27);
        assert(box[24] == std::byte{2} && box[25] == std::byte{1} && box[26] == std::byte{2});
        assert(box[27] == std::byte{} && std::memcmp(box + 28, "Box", 3) == 0);
        for (unsigned invalid = 0; invalid < 7; ++invalid) {
            commands.reset();
            if (invalid == 0) commands.ellipse(0, 0, 0, 10, 0);
            if (invalid == 1) commands.circle(0, 0, 0, 0);
            if (invalid == 2) commands.circle(INT32_MIN, 0, 1, 0);
            if (invalid == 3) commands.circle(0, 0, UINT32_MAX, 0);
            if (invalid == 4) commands.text_box({0, 0, 0, 10}, 0, "");
            if (invalid == 5) commands.text_box({0, 0, 10, 10}, 0, "", 4);
            if (invalid == 6) commands.text_box({0, 0, 10, 10}, 0, "", 1, 0, 3);
            const auto before_invalid = packets.size();
            auto result = canvas.present(commands);
            assert(!result && result.error() == pxa::Error::invalid_argument);
            assert(packets.size() == before_invalid);
        }
        commands.reset();commands.image({-2,3,64,36},77,ImageFit::cover,128);
        const auto image_begin=packets.size();assert(canvas.present(commands));
        const auto& image=packets[image_begin+1];
        assert(image.size()==62 && image[32]==std::byte{11});
        assert(pxa::wire::get16(image.data()+34)==26);
        assert(pxa::wire::get32(image.data()+36)==std::uint32_t(-2));
        assert(image[52]==std::byte{128} && image[53]==std::byte{2});
        assert(pxa::wire::get64(image.data()+54)==77);
        assert(image[33] == std::byte{0});
        commands.reset();commands.image({0,0,64,36},77,ImageFit::cover,128,ImageSampling::nearest);
        auto nearest_begin=packets.size();assert(canvas.present(commands));
        assert(packets[nearest_begin+1][33]==std::byte{1});
        commands.reset();commands.image({0,0,64,36},77,ImageFit::cover,128,ImageSampling{2});
        auto invalid_sampling=packets.size();assert(!canvas.present(commands));
        assert(packets.size()==invalid_sampling);
        commands.reset();commands.image({0,0,64,36},77);
        auto default_begin=packets.size();assert(canvas.present(commands));
        assert(packets[default_begin+1][33]==std::byte{0});
        for (auto fit : {ImageFit::contain,ImageFit{3}}) {
            commands.reset();commands.image({0,0,10,10},fit==ImageFit::contain?0:77,fit);
            const auto count=packets.size();assert(!canvas.present(commands));
            assert(packets.size()==count);
        }
        for (const auto radius : {0u, 65536u}) {
            commands.reset(); commands.arc(0, 0, radius, 0, 90, 0xffffffff);
            const auto count = packets.size();
            auto invalid_arc = canvas.present(commands);
            assert(!invalid_arc && invalid_arc.error() == pxa::Error::invalid_argument);
            assert(packets.size() == count);
        }
        commands.reset(); commands.arc(0, 0, 1, 0, 90, 0xffffffff, 0);
        assert(!canvas.present(commands));
        CanvasCommands<4> tiny;
        tiny.rect(0, 0, 1, 1, 0xffffffff);
        const auto stable = packets.size();
        auto overflow = canvas.present(tiny);
        assert(!overflow && overflow.error() == pxa::Error::limit_exceeded);
        tiny.reset(); tiny.ellipse(0, 0, 1, 1, 0);
        assert(!canvas.present(tiny));
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
