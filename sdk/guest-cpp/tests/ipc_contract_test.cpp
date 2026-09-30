#include "../examples/ipc-stats/stats_contract.hpp"

#include <cassert>

extern "C" std::int32_t pxa_submit(const std::uint8_t*, std::uint32_t) {
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t,
                                 std::uint8_t*, std::uint32_t) {
    return -3;
}

int main() {
    static_assert(stats::Get::request_bytes == 28);
    static_assert(stats::Get::response_bytes == 54);
    static_assert(stats::Get::call_packet_bytes == 68);
    static_assert(stats::Get::reply_packet_bytes == 94);
    stats::Get::Request request{};
    request.seed = 41;
    assert(request.label.set("test"));
    std::array<std::byte, 128> packet{};
    const auto offset = pxa::wire::header_bytes + 8 + stats::Get::endpoint.size();
    auto payload_size = stats::Get::encode_request(request,
                                                   std::span(packet).subspan(offset));
    assert(payload_size && *payload_size == 16);
    auto size = pxa::encode_ipc_call_in_place(stats::Get::endpoint,
                                             *payload_size, packet);
    assert(size);
    std::array<std::byte, 128> raw{};
    auto raw_size = pxa::encode_ipc_call(stats::Get::endpoint,
        std::span<const std::byte>(packet).subspan(offset, *payload_size), raw);
    assert(raw_size == size);
    assert(std::equal(packet.begin() + 20, packet.begin() + *size,
                      raw.begin() + 20));
    auto incoming = pxa::decode_ipc_request<stats::Get>({
        7, 0x8001, 9, std::span<const std::byte>(packet).subspan(20, *size - 20)});
    assert(incoming && incoming->call_id == 9);
    assert(incoming->value.seed == 41 && incoming->value.label.view() == "test");

    stats::Get::Response response{};
    response.next = 42;
    response.valid = true;
    assert(response.label.set("ready"));
    constexpr auto reply_offset = pxa::wire::header_bytes + 20;
    auto response_size = stats::Get::encode_response(
        response, std::span(packet).subspan(reply_offset));
    assert(response_size && *response_size == 22);
    auto reply_size = pxa::encode_ipc_reply_in_place(9, *response_size, packet);
    assert(reply_size);
    auto raw_reply_size = pxa::encode_ipc_reply(9, 0,
        std::span<const std::byte>(packet).subspan(reply_offset, *response_size), raw);
    assert(raw_reply_size == reply_size);
    assert(std::equal(packet.begin() + 20, packet.begin() + *reply_size,
                      raw.begin() + 20));
    auto decoded = stats::Get::decode_response(
        std::span<const std::byte>(packet).subspan(reply_offset, *response_size));
    assert(decoded && decoded->next == 42 && decoded->valid);
    assert(decoded->label.view() == "ready");
    assert(!decoded->cached);
    assert(!decoded->note);

    response.cached = false;
    auto extended_size = stats::Get::encode_response(
        response, std::span(packet).subspan(reply_offset));
    assert(extended_size && *extended_size == *response_size + 5);
    assert(pxa::wire::get16(packet.data() + reply_offset + *response_size) ==
           0x8004);
    auto extended = stats::Get::decode_response(
        std::span<const std::byte>(packet).subspan(reply_offset, *extended_size));
    assert(extended && extended->cached && !*extended->cached);
    assert(!extended->note);

    assert(response.note.emplace().set("remote"));
    auto text_size = stats::Get::encode_response(
        response, std::span(packet).subspan(reply_offset));
    assert(text_size && *text_size == *extended_size + 10);
    auto with_text = stats::Get::decode_response(
        std::span<const std::byte>(packet).subspan(reply_offset, *text_size));
    assert(with_text && with_text->note && with_text->note->view() == "remote");

    // A newer minor revision may append records unknown to this decoder.
    auto* future = packet.data() + reply_offset + *text_size;
    pxa::wire::put16(future, 0x8006);
    pxa::wire::put16(future + 2, 1);
    future[4] = std::byte{9};
    assert(stats::Get::decode_response(
        std::span<const std::byte>(packet).subspan(reply_offset, *text_size + 5)));
    pxa::wire::put16(future, 0x8005);
    assert(!stats::Get::decode_response(
        std::span<const std::byte>(packet).subspan(reply_offset, *text_size + 5)));
    pxa::wire::put16(future, 0x8006);
    assert(!stats::Get::decode_response(
        std::span<const std::byte>(packet).subspan(reply_offset, *text_size + 4)));
    pxa::wire::put16(future, 6);
    assert(!stats::Get::decode_response(
        std::span<const std::byte>(packet).subspan(reply_offset, *text_size + 5)));

    auto bad = packet;
    bad[reply_offset + 12] = std::byte{2};
    assert(!stats::Get::decode_response(
        std::span<const std::byte>(bad).subspan(reply_offset, *response_size)));
    bad = packet;
    bad[reply_offset + 17] = std::byte{0xff};
    assert(!stats::Get::decode_response(
        std::span<const std::byte>(bad).subspan(reply_offset, *response_size)));
    assert(!stats::Get::encode_request(request, std::span(packet).first(1)));
    assert(!pxa::encode_ipc_call_in_place(stats::Get::endpoint, 1025, packet));
    assert(!pxa::encode_ipc_reply_in_place(0, *response_size, packet));

    pxa::Transport transport;
    pxa::RequestTable requests;
    pxa::IpcService ipc(transport, requests);
    pxa::IpcCallBuffers<stats::Get> call_buffers;
    auto pending = ipc.call<stats::Get>(request, call_buffers);
    pxa::IpcReplyBuffer<stats::Get> reply_buffer;
    auto unsent = ipc.reply<stats::Get>(9, response, reply_buffer);
    (void)pending;
    (void)unsent;
}
