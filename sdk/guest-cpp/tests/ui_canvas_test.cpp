// Canvas node and raw pointer decoding (the input path games need).
#include <pxa/ui.hpp>

#include <array>
#include <cassert>
#include <vector>

static std::vector<std::vector<std::byte>> packets;

extern "C" std::int32_t pxa_submit(const std::uint8_t* data,
                                   std::uint32_t length) {
    auto* bytes = reinterpret_cast<const std::byte*>(data);
    packets.emplace_back(bytes, bytes + length);
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*,
                               std::uint32_t) {
    return -3;
}

namespace {

/* Build the UI wire event the host sends for a pointer sample. */
pxa::Event pointer_event(std::uint8_t phase, std::int32_t x, std::int32_t y,
                         std::uint64_t timestamp_us, std::uint16_t buttons) {
    static std::array<std::byte, 36> payload{};
    payload.fill(std::byte{});
    pxa::wire::put32(payload.data(), 1);      /* surface */
    pxa::wire::put32(payload.data() + 4, 2);  /* node    */
    pxa::wire::put32(payload.data() + 8, 3);  /* generation */
    pxa::wire::put16(payload.data() + 12, pxa::ui::protocol::event_pointer_kind);
    pxa::wire::put64(payload.data() + 16, timestamp_us);
    payload[24] = std::byte{1};               /* pointer id */
    payload[25] = std::byte{phase};
    pxa::wire::put16(payload.data() + 26, buttons);
    pxa::wire::put32(payload.data() + 28, static_cast<std::uint32_t>(x));
    pxa::wire::put32(payload.data() + 32, static_cast<std::uint32_t>(y));
    return {pxa::ui::protocol::service, 0x8001, 0, {payload.data(),
                                                    payload.size()}};
}

}  // namespace

int main() {
    using namespace pxa::ui;
    pxa::Transport transport;
    transport.phase(pxa::Phase::event);

    /* Mounting a canvas emits the node and the pointer event mask. */
    auto page = Page(transport, Overlay(Canvas(296, 240)));
    assert(page.mount());
    assert(!packets.empty());

    /* Records are [command][pad][size u16][payload...]; the create payload is
     * [id u32][parent u32][type u8][subtype u8] and a property payload is
     * [id u32][key u16][value...]. */
    bool saw_canvas = false;
    bool saw_pointer_mask = false;
    for (const auto& packet : packets) {
        for (std::size_t i = 32; i + 4 <= packet.size();) {
            const auto command = std::to_integer<std::uint8_t>(packet[i]);
            const std::size_t payload = i + 4;
            const std::size_t size = pxa::wire::get16(packet.data() + i + 2);
            if (payload + size > packet.size()) break;
            if (command == pxa::ui::protocol::create && size >= 14 &&
                std::to_integer<std::uint8_t>(packet[payload + 12]) ==
                    pxa::ui::protocol::canvas)
                saw_canvas = true;
            if (command == pxa::ui::protocol::set_property && size >= 12 &&
                pxa::wire::get16(packet.data() + payload + 4) ==
                    pxa::ui::protocol::event_mask &&
                pxa::wire::get64(packet.data() + payload + 6) ==
                    pxa::ui::protocol::event_mask_pointer)
                saw_pointer_mask = true;
            i = payload + size;
        }
    }
    assert(saw_canvas);
    assert(saw_pointer_mask);

    /* Pointer decoding covers the three phases a control scheme needs. */
    auto down = decode_pointer(pointer_event(pointer_phase_down, 40, 160,
                                             1000, 1));
    assert(down);
    assert(down->x == 40 && down->y == 160);
    assert(down->phase == pointer_phase_down && down->buttons == 1);

    auto move = decode_pointer(pointer_event(pointer_phase_move, 72, 150,
                                             16000, 1));
    assert(move && move->phase == pointer_phase_move);
    assert(move->x == 72 && move->y == 150 && move->timestamp_us == 16000);

    auto up = decode_pointer(pointer_event(pointer_phase_up, 72, 150, 20000, 0));
    assert(up && up->phase == pointer_phase_up && up->buttons == 0);

    /* A non-pointer UI event must not decode as one. */
    static std::array<std::byte, 28> action{};
    action.fill(std::byte{});
    pxa::wire::put32(action.data(), 1);
    pxa::wire::put32(action.data() + 4, 2);
    pxa::wire::put32(action.data() + 8, 3);
    pxa::wire::put16(action.data() + 12, 1); /* action kind */
    pxa::Event not_pointer{pxa::ui::protocol::service, 0x8001, 0,
                           {action.data(), action.size()}};
    assert(!decode_pointer(not_pointer));

    return 0;
}
