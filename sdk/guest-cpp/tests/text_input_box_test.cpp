#include <pxa/ui.hpp>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>

static std::size_t allocations = 0;
void* operator new(std::size_t size) {
    ++allocations;
    if (auto* p = std::malloc(size)) return p;
    std::abort();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

struct Packet { std::array<std::byte, 1024> data{}; std::size_t size = 0; };
static std::array<Packet, 64> packets;
static std::size_t count = 0;
extern "C" std::int32_t pxa_submit(const std::uint8_t* p, std::uint32_t n) {
    assert(count < packets.size() && n <= packets[count].data.size());
    auto& packet = packets[count++]; packet.size = n;
    std::memcpy(packet.data.data(), p, n); return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { return -3; }

static std::array<std::byte, 4096> stream;
static std::span<const std::byte> commands() {
    std::size_t n = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& p = packets[i];
        if (pxa::wire::get16(p.data.data() + 2) != pxa::ui::protocol::write) continue;
        assert(n + p.size - 24 <= stream.size());
        std::memcpy(stream.data() + n, p.data.data() + 24, p.size - 24);
        n += p.size - 24;
    }
    return std::span{stream}.first(n);
}
static std::span<const std::byte> property(std::span<const std::byte> data, std::uint16_t key) {
    for (std::size_t at = 0; at < data.size();) {
        assert(data.size() - at >= 4);
        auto kind = data[at]; auto n = pxa::wire::get16(data.data() + at + 2);
        at += 4; assert(n <= data.size() - at);
        if (kind == std::byte{2} && n >= 6 && pxa::wire::get32(data.data() + at) == 2 &&
            pxa::wire::get16(data.data() + at + 4) == key) return data.subspan(at + 6, n - 6);
        at += n;
    }
    return {};
}
static pxa::Event input_event(std::uint32_t generation, std::uint16_t kind, std::string_view text = {}) {
    static std::array<std::byte, 64> bytes{};
    bytes.fill(std::byte{});
    pxa::wire::put32(bytes.data(), 1); pxa::wire::put32(bytes.data() + 4, 2);
    pxa::wire::put32(bytes.data() + 8, generation); pxa::wire::put16(bytes.data() + 12, kind);
    assert(text.size() <= bytes.size() - 24);
    if (!text.empty()) std::memcpy(bytes.data() + 24, text.data(), text.size());
    return {3, 0x8001, 0, std::span{bytes}.first(24 + text.size())};
}

int main() {
    using namespace pxa::ui;
    pxa::Transport transport; transport.phase(pxa::Phase::event);
    State<std::string> input{"中文"}; TextInputRef ref;
    State<TextInputBox> box{TextInputBox{{7, 11, 120, 32}, true, Font::title, 0x253430ff}};
    static_assert(sizeof(TextInput(input)) == sizeof(void*));
    static_assert(sizeof(TextInputBox) == 24 && sizeof(CanvasRegion) == 16);
    static_assert(decltype(TextInput(input))::capacity.bindings == 1);
    unsigned submitted = 0;
    const auto before = allocations;
    {
        // Exercise configuration on both sides of box(), including callback preservation.
        auto page = Page(transport, TextInput(input, ref).single_line().on_submit([&] { ++submitted; })
                                   .box(box).max_bytes(512));
        static_assert(decltype(page)::binding_capacity == 2 && decltype(page)::handler_capacity == 2 &&
                      decltype(page)::ref_capacity == 1);
        assert(page.mount() && ref.mounted());
        auto data = commands();
        assert(property(data, protocol::visible)[0] == std::byte{1});
        assert(pxa::wire::get16(property(data, protocol::font_role).data()) == 2);
        auto color = property(data, protocol::foreground);
        assert(color.size() == 8 && color[0] == std::byte{1} && pxa::wire::get32(color.data() + 4) == 0x253430ff);
        auto length = property(data, protocol::x);
        assert(length.size() == 8 && length[0] == std::byte{1} && pxa::wire::get32(length.data() + 4) == 7 * 64);
        assert(pxa::wire::get32(property(data, protocol::text_max_bytes).data()) == 512);
        assert(property(data, protocol::text_single_line)[0] == std::byte{1});
        assert(ref.show_keyboard() && ref.hide_keyboard());
        assert(page.handle(input_event(page.generation(), 6, "编辑🙂")) && input.get() == "编辑🙂");
        assert(page.flush());
        assert(page.handle(input_event(page.generation(), 1)) && submitted == 1);

        count = 0;
        box.set(box.get()); assert(!page.dirty() && page.flush() && count == 0);
        box.update([](auto value) { value.region.x = 9; value.visible = false; return value; });
        assert(page.dirty() && page.flush() && count == 3);
        data = commands();
        assert(data.size() == 124); // Eight layout/color properties, no text or limit/mode rewrites.
        assert(property(data, protocol::visible)[0] == std::byte{0});
        assert(property(data, protocol::text_max_bytes).empty() && property(data, protocol::text_single_line).empty());
        assert(property(data, protocol::text_value).empty() && ref.mounted());

        count = 0;
        box.update([](auto value) { value.font = static_cast<Font>(99); return value; });
        auto result = page.flush();
        assert(!result && result.error() == pxa::Error::invalid_argument && page.dirty());
        assert(count == 2 && pxa::wire::get16(packets[1].data.data() + 2) == protocol::cancel);
        box.update([](auto value) { value.font = Font::body; value.region.x = -1; return value; });
        assert(!page.flush() && page.dirty());
        box.update([](auto value) { value.region.x = 0; value.region.width = 0; return value; });
        assert(page.flush() && !page.dirty());
    }
    assert(!ref.mounted() && !ref.show_keyboard());
    box.update([](auto value) { value.visible = true; return value; }); // Subscription detached.
    input.set("卸载");
    assert(allocations == before); // All layout, text, keyboard and callback operations above are allocation-free.
}
