#include <pxa/app.hpp>

#include <cassert>

static constexpr std::uint64_t permission_handle = UINT64_C(0x100000011);
static constexpr std::uint64_t session_handle = UINT64_C(0x100000012);
static std::uint64_t token;
static std::uint16_t service;
static std::uint16_t opcode;
static unsigned closes;
static unsigned music_calls;
static bool finished;
static bool graph_ready;

extern "C" std::int32_t pxa_submit(const std::uint8_t* data,
                                      std::uint32_t size) {
    const auto* p = reinterpret_cast<const std::byte*>(data);
    service = pxa::wire::get16(p);
    opcode = pxa::wire::get16(p + 2);
    token = pxa::wire::get64(p + 4);
    assert(pxa::wire::get32(p + 12) == size - 20);
    if (service == 1) {
        if (opcode == 2) ++closes;
        else assert(opcode == 1);
        return 0;
    }
    assert(token);
    if (service == 11) {
        assert(opcode == 2 && pxa::wire::get16(p + 20) == 1);
        assert(pxa::wire::get16(p + 22) == 14);
        assert(pxa::wire::get16(p + 38) == 2);
        assert(pxa::wire::get16(p + 40) == 5 && p[42] == std::byte{'m'});
    } else {
        assert(service == 10);
        assert(pxa::wire::get16(p + 20) == 1);
        assert(pxa::wire::get16(p + 22) == 8);
        assert(pxa::wire::get64(p + 24) ==
               (opcode == 1 ? permission_handle : session_handle));
    }
    return 0;
}

extern "C" std::int32_t pxa_io(std::uint64_t handle, std::uint32_t operation,
                                  std::uint8_t* data, std::uint32_t size) {
    assert(handle == session_handle);
    assert(graph_ready);
    auto* p = reinterpret_cast<std::byte*>(data);
    if (operation == 0x100) {
        assert(size == 14 && pxa::wire::get16(p) == 440);
        return size;
    }
    if (operation == 2) return 4;
    if (operation == 0x104) {
        assert(size == 26 && pxa::wire::get16(p + 8) == 10);
        if (++music_calls == 2)
            return static_cast<std::int32_t>(pxa::Error::would_block);
        pxa::wire::put64(p, 41);
        return size;
    }
    assert(operation == 0x102 && size == 4);
    return size;
}

pxa::Task<void> run(pxa::Context& context) {
    auto permission = co_await context.permissions().acquire(
        "audio.playback", "media");
    assert(permission && permission->handle() == permission_handle);
    auto session = co_await context.audio().open(*permission);
    assert(session && session->format().sample_rate == 48000);
    auto graph = co_await session->graph(-12 * 256);
    assert(graph);
    graph_ready = true;
    assert(session->tone());
    std::array<std::byte, 8> pcm{};
    auto written = session->write_pcm(pcm);
    assert(written && *written == 4);
    auto music = session->music("music.opus");
    assert(music && *music == 41);
    auto blocked = session->music("music.opus");
    assert(!blocked && blocked.error() == pxa::Error::would_block);
    assert(session->control(pxa::MusicAction::pause));
    auto state = co_await session->query();
    assert(state && state->submitted_samples == 100 && state->queued_samples == 20);
    auto flushed = co_await session->flush();
    assert(flushed);
    finished = true;
    co_return pxa::Result<void>{};
}

struct App {
    pxa::Result<void> on_start(pxa::Context& context) {
        return context.tasks().start(run(context));
    }
};
PXA_APPLICATION(App)

static void deliver(std::span<const std::byte> payload) {
    std::array<std::byte, 100> bytes{};
    pxa::wire::put16(bytes.data(), service);
    pxa::wire::put16(bytes.data() + 2, opcode);
    pxa::wire::put64(bytes.data() + 4, token);
    pxa::wire::put32(bytes.data() + 12, payload.size());
    for (std::size_t i = 0; i < payload.size(); ++i) bytes[20 + i] = payload[i];
    assert(pxa_app_on_event(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                            20 + payload.size()) == 1);
}

int main() {
    assert(pxa_app_start(nullptr, 0) == 0);
    std::array<std::byte, 12> permission{};
    pxa::wire::put64(permission.data() + 4, permission_handle);
    deliver(permission);
    assert(service == 10 && opcode == 1);
    std::array<std::byte, 35> opened{};
    pxa::wire::put16(opened.data() + 4, 3);
    pxa::wire::put16(opened.data() + 6, 8);
    pxa::wire::put64(opened.data() + 8, session_handle);
    pxa::wire::put16(opened.data() + 16, 4);
    pxa::wire::put16(opened.data() + 18, 4);
    pxa::wire::put32(opened.data() + 20, 48000);
    pxa::wire::put16(opened.data() + 24, 5);
    pxa::wire::put16(opened.data() + 26, 1);
    opened[28] = std::byte{2};
    pxa::wire::put16(opened.data() + 29, 6);
    pxa::wire::put16(opened.data() + 31, 2);
    pxa::wire::put16(opened.data() + 33, 20);
    deliver(opened);
    assert(opcode == 2);
    const std::array<std::byte, 4> success{};
    deliver(success);
    assert(opcode == 3);
    std::array<std::byte, 44> queried{};
    std::size_t offset = 4;
    for (std::uint16_t tag = 2; tag <= 5; ++tag) {
        const unsigned size = tag <= 3 ? 8 : 4;
        pxa::wire::put16(queried.data() + offset, tag);
        pxa::wire::put16(queried.data() + offset + 2, size);
        pxa::wire::put32(queried.data() + offset + 4,
                         tag == 2 ? 100 : tag == 3 ? 80 : tag == 4 ? 20 : 1);
        offset += 4 + size;
    }
    deliver(queried);
    assert(opcode == 4);
    deliver(success);
    assert(finished && closes == 2);
    pxa_app_stop(0);

    std::array<std::byte, 24> playback{};
    pxa::wire::put64(playback.data(), session_handle);
    pxa::wire::put64(playback.data() + 8, 41);
    playback[16] = std::byte{1};
    auto parsed = pxa::decode_playback({10, 0x8001, 0, playback});
    assert(parsed && parsed->state == pxa::PlaybackState::ready);
    playback[16] = std::byte{5};
    assert(!pxa::decode_playback({10, 0x8001, 0, playback}));

    pxa::Transport transport;
    transport.phase(pxa::Phase::event);
    pxa::RequestTable requests;
    pxa::TaskScope scope;
    // A late Audio open result carries its session inside a TLV at offset 8.
    service = 10;
    opcode = 1;
    assert(requests.add(999, &scope, [](void*, const pxa::Event&) {}));
    requests.abandon(999, transport, 10, 1, true, 8);
    assert(requests.dispatch({10, 1, 999, opened}));
    assert(closes == 3);
}
