#include <pxa/ui_widgets.hpp>
#include <pxa/ui_refresh.hpp>
#include <cassert>
#include <array>
#include <cstdlib>
#include <new>
#include <cstring>

static unsigned allocations = 0, submits = 0;
static bool reject_commit = false;
static std::array<std::byte, 8192> encoded{};
static std::size_t encoded_size = 0;
static bool capture = true;
void* operator new(std::size_t size) { ++allocations; return std::malloc(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
extern "C" std::int32_t pxa_submit(const std::uint8_t* data, std::uint32_t size) {
    ++submits;
    if (capture && data[2] == 2) {
        assert(size >= 24 && encoded_size + size - 24 <= encoded.size());
        std::memcpy(encoded.data()+encoded_size, data+24, size-24);
        encoded_size += size-24;
    }
    return reject_commit && data[2] == 3 ? -3 : 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { return -3; }

struct Click {
    int* count;
    void operator()() { ++*count; }
};
struct Controls {
    static constexpr pxa::ui::Capacity capacity{3, 1, 1};
    Click click;
    bool* invalid;
    pxa::ui::State<std::string>* text;
    template<class Owner> bool render(Owner& owner, std::uint32_t parent) {
        using namespace pxa::ui;
        return Box(WidgetStyle{{0, 0, 200, 100}, Color::text, Color::surface_container,
                              Font::body, 12, 0, 1, Color::outline_variant},
            Text(*text, WidgetStyle{{4, 4, 180, 30}, Color::text, 0, Font::title}),
            Button("自动翻页 · 关", WidgetStyle{{4, 40, 180, *invalid ? 0 : 30}, Color::on_primary, Color::primary})
                .on_click_ref(click)).render(owner, parent);
    }
};
static pxa::Event click_event(std::uint32_t node, std::uint32_t generation) {
    static std::array<std::byte, 24> bytes{};
    pxa::wire::put32(bytes.data(), 1);
    pxa::wire::put32(bytes.data() + 4, node);
    pxa::wire::put32(bytes.data() + 8, generation);
    pxa::wire::put16(bytes.data() + 12, 1);
    return {3, 0x8001, 0, bytes};
}
int main() {
    using namespace pxa::ui;
    pxa::Transport transport; transport.phase(pxa::Phase::event);
    State<std::uint32_t> revision{0};
    int clicks = 0; bool invalid = false; State<std::string> text{"阅读偏好"};
    auto page = Page(transport, Refresh(revision, [&] { return Controls{{&clicks}, &invalid, &text}; }));
    const auto before = allocations;
    assert(page.mount() && !page.dirty());
    capture = false;
    bool primary = false, on_primary = false, text_color = false, container = false, outline = false;
    for (std::size_t at = 0; at < encoded_size;) {
        const auto size = pxa::wire::get16(encoded.data()+at+2);
        assert(at + 4 + size <= encoded_size);
        if (encoded[at] == std::byte{2} && size == 14) {
            auto value = encoded.data()+at+10;
            if (value[0] == std::byte{0}) {
                primary |= value[1] == std::byte{2};
                on_primary |= value[1] == std::byte{3};
                text_color |= value[1] == std::byte{4};
                container |= value[1] == std::byte{11};
                outline |= value[1] == std::byte{26};
            }
        }
        at += 4 + size;
    }
    assert(primary && on_primary && text_color && container && outline);
    assert(page.handle(click_event(6, page.generation())) && clicks == 1);
    text.set("缓存进度");
    assert(page.dirty() && page.flush());
    // A bound label update keeps the same native button node alive.
    assert(page.handle(click_event(6, page.generation())) && clicks == 2);
    invalid = true; revision.set(1);
    auto generation = page.generation();
    assert(!page.flush() && page.dirty() && page.generation() == generation);
    // Failed candidate never replaces callbacks owned by the live fragment.
    assert(page.handle(click_event(6, generation)) && clicks == 3);
    invalid = false; reject_commit = true;
    assert(!page.flush() && page.dirty() && page.generation() == generation);
    assert(page.handle(click_event(6, generation)) && clicks == 4);
    reject_commit = false;
    assert(page.flush() && !page.dirty() && page.generation() != generation);
    assert(!page.handle(click_event(6, generation)));
    unsigned pointers = 0;
    auto pointer = [&](const CanvasPointer& p) { assert(p.phase == pointer_phase_up); ++pointers; };
    auto pointer_page = Page(transport, Button("书名", WidgetStyle{{0, 0, 200, 40}}).on_pointer_ref(pointer));
    assert(pointer_page.mount());
    std::array<std::byte, 36> pointer_bytes{};
    pxa::wire::put32(pointer_bytes.data(), 1);
    pxa::wire::put32(pointer_bytes.data() + 4, 2);
    pxa::wire::put32(pointer_bytes.data() + 8, pointer_page.generation());
    pxa::wire::put16(pointer_bytes.data() + 12, 7);
    pointer_bytes[25] = std::byte{pointer_phase_up};
    assert(pointer_page.handle({3, 0x8001, 0, pointer_bytes}) && pointers == 1);
    assert(!pointer_page.handle(click_event(2, pointer_page.generation())));
    assert(allocations == before && submits > 12);
}
