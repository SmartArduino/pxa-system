#include "../../testing/pxa_control_capture.h"
#include <pxa/app.hpp>
#include <pxa/audio.h>

#include <algorithm>
#include <cassert>

template<class Track>
concept EffectTrackArgument = requires(pxa::AudioSession& s, std::span<const pxa::Tone> tones, Track track) {
    s.effect(tones, track);
    s.stop_effect(track);
};
template<class Track>
concept SoundTrackArgument = requires(pxa::AudioSession& s, const pxa::Asset& asset, Track track) {
    s.sound_track(asset, track);
    s.control_sound(track, pxa::MusicAction::stop);
};
static_assert(EffectTrackArgument<pxa::EffectTrack> && !EffectTrackArgument<pxa::SoundTrack>);
static_assert(SoundTrackArgument<pxa::SoundTrack> && !SoundTrackArgument<pxa::EffectTrack>);
static_assert(!EffectTrackArgument<int> && !SoundTrackArgument<int>);
static_assert(sizeof(pxa::EffectTrack) == 1 && sizeof(pxa::SoundTrack) == 1);
static_assert(pxa::AudioSession::max_effect_layers == PXA_AUDIO_EFFECT_LAYERS);
static_assert(pxa::AudioSession::effect_track_count == PXA_AUDIO_SOUND_TRACKS);
static_assert(pxa::AudioSession::sound_track_count == PXA_AUDIO_SOUND_TRACKS);
static_assert(pxa::detail::audio_wire::open_pcm == PXA_AUDIO_OPEN_PCM);
static_assert(pxa::detail::audio_wire::service == PXA_AUDIO_SERVICE_ID);
static_assert(pxa::detail::audio_wire::io_play_effect == PXA_AUDIO_IO_PLAY_EFFECT);
static_assert(pxa::detail::audio_wire::io_stop_effect == PXA_AUDIO_IO_STOP_EFFECT);
static_assert(pxa::detail::audio_wire::io_play_sound == PXA_AUDIO_IO_PLAY_SOUND);
static_assert(pxa::detail::audio_wire::io_control_sound == PXA_AUDIO_IO_CONTROL_SOUND);
static_assert(pxa::detail::audio_wire::independent_effect == PXA_AUDIO_SOUND_APPEND);
static_assert(pxa::detail::audio_wire::effect_header_bytes == 4);
static_assert(pxa::detail::audio_wire::effect_tone_bytes == 14);
static_assert(pxa::detail::audio_wire::effect_stop_bytes == 4);

static constexpr std::uint64_t permission_handle = UINT64_C(0x100000011);
static constexpr std::uint64_t session_handle = UINT64_C(0x100000012);
static constexpr std::uint64_t sound_handle = UINT64_C(0x100000013);
static std::uint64_t token;
static std::uint16_t service;
static std::uint16_t opcode;
static unsigned closes;
static unsigned music_calls;
static bool finished;
static bool graph_ready;
static unsigned io_calls;
static std::optional<std::int32_t> effect_reply;
static std::array<std::byte, 116> last_effect;
static unsigned last_effect_size;

extern "C" std::int32_t pxa_test_control(const std::uint8_t* data,
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
        if (opcode == 5) {
            assert(size == 36 && pxa::wire::get64(p+20) == permission_handle &&
                pxa::wire::get32(p+28) == 22050 && p[32] == std::byte{2} &&
                p[33] == std::byte{} && p[34] == std::byte{} && p[35] == std::byte{});
            return 0;
        }
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
    ++io_calls;
    auto* p = reinterpret_cast<std::byte*>(data);
    if (operation == 0x100) {
        assert(size == 14 && pxa::wire::get16(p) == 440 && p[7] == std::byte{});
        return size;
    }
    if (operation == 0x107) {
        assert(size == 4 + 14 * std::to_integer<unsigned>(p[0]));
        assert(p[2] == std::byte{} && p[3] == std::byte{});
        for (unsigned i = 0; i < std::to_integer<unsigned>(p[0]); ++i)
            assert(p[4 + 14 * i + 7] == std::byte{});
        std::copy_n(p, size, last_effect.data());
        last_effect_size = size;
        return effect_reply.value_or(size);
    }
    if (operation == 0x108) {
        assert(size == 4 && p[0] < std::byte{6} && p[1] == std::byte{} &&
               p[2] == std::byte{} && p[3] == std::byte{});
        return size;
    }
    if (operation == 2) return 4;
    if (operation == 0x103) {
        assert(size == 16 && pxa::wire::get64(p) == sound_handle);
        assert(pxa::wire::get16(p + 8) == static_cast<std::uint16_t>(-20 * 256));
        assert(p[10] == std::byte{5} && p[11] == std::byte{1});
        for (unsigned i = 12; i < 16; ++i) assert(p[i] == std::byte{});
        return size;
    }
    if (operation == 0x104) {
        assert(size == 26 && pxa::wire::get16(p + 8) == 10);
        if (++music_calls == 2)
            return static_cast<std::int32_t>(pxa::Error::would_block);
        pxa::wire::put64(p, 41);
        return size;
    }
    if (operation==0x105) {
        assert(size==8 && p[0]==std::byte{2} && p[1]==std::byte{3});return size;
    }
    if (operation==0x106) {
        assert(size==4 && p[0]==std::byte{4} && pxa::wire::get16(p+2)==static_cast<std::uint16_t>(-20*256));return size;
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
    std::array tones{pxa::Tone{}, pxa::Tone{.delay_ms = 40}};
    constexpr auto weapon = pxa::EffectTrack::track3;
    assert(session->effect(tones, weapon));
    constexpr std::array<std::uint8_t, 32> golden{
        2, 3, 0, 0,
        0xb8, 1, 100, 0, 0, 0xf4, 0, 0, 3, 0, 5, 0, 0, 0,
        0xb8, 1, 100, 0, 0, 0xf4, 0, 0, 3, 0, 5, 0, 40, 0,
    };
    assert(!std::memcmp(last_effect.data(), golden.data(), golden.size()));
    assert(last_effect_size == 32 && last_effect[1] == std::byte{3});
    assert(pxa::wire::get16(last_effect.data() + 4) == 440 &&
           pxa::wire::get16(last_effect.data() + 30) == 40);
    assert(session->stop_effect(weapon));
    assert(session->effect(tones));
    assert(last_effect[1] == std::byte{255});
    for (unsigned track = 0; track < pxa::AudioSession::effect_track_count; ++track) {
        const auto category = static_cast<pxa::EffectTrack>(track);
        assert(session->effect(tones, category));
        assert(last_effect[1] == std::byte(track));
        assert(session->stop_effect(category));
    }
    std::array<pxa::Tone, pxa::AudioSession::max_effect_layers> full{};
    assert(session->effect(full));
    assert(last_effect_size == 116);
    const auto before_invalid = io_calls;
    assert(!session->effect({}));
    assert(!session->effect({}, weapon));
    std::array<pxa::Tone, pxa::AudioSession::max_effect_layers + 1> too_many{};
    assert(!session->effect(too_many));
    for (unsigned track : {6, 255}) {
        assert(!session->effect(tones, static_cast<pxa::EffectTrack>(track)));
        assert(!session->stop_effect(static_cast<pxa::EffectTrack>(track)));
    }
    const pxa::Tone invalid_tones[] = {
        {.frequency_hz = 39}, {.frequency_hz = 8001},
        {.duration_ms = 9}, {.duration_ms = 1001},
        {.gain_db_q8 = 1}, {.gain_db_q8 = -60 * 256 - 1},
        {.waveform = static_cast<pxa::Waveform>(4)},
        {.attack_ms = 101}, {.release_ms = 101}, {.delay_ms = 1001},
    };
    for (auto invalid : invalid_tones) {
        tones[1] = invalid; // Valid prefix must never be submitted on its own.
        auto result = session->effect(tones, weapon);
        assert(!result && result.error() == pxa::Error::invalid_argument);
    }
    assert(io_calls == before_invalid);
    tones[1] = pxa::Tone{.delay_ms = 40};
    for (auto error : {pxa::Error::would_block, pxa::Error::unsupported}) {
        effect_reply = static_cast<std::int32_t>(error);
        const auto before = io_calls;
        auto result = session->effect(tones, weapon);
        assert(!result && result.error() == error && io_calls == before + 1);
    }
    for (auto short_reply : {0, 31, 33}) {
        effect_reply = short_reply;
        auto result = session->effect(tones);
        assert(!result && result.error() == pxa::Error::protocol_error);
    }
    effect_reply.reset();
    std::array<std::byte, 8> pcm{};
    auto written = session->write_pcm(pcm);
    assert(written && *written == 4);
    auto music = session->music("music.opus");
    assert(music && *music == 41);
    auto blocked = session->music("music.opus");
    assert(!blocked && blocked.error() == pxa::Error::would_block);
    assert(session->control(pxa::MusicAction::pause));
    {
        pxa::Asset sound(context.transport(), sound_handle, {.kind = pxa::AssetKind::audio});
        assert(session->sound_track(sound, pxa::SoundTrack::track5, true, -20 * 256));
        const auto before = io_calls;
        for (unsigned track : {6, 255}) {
            assert(!session->sound_track(sound, static_cast<pxa::SoundTrack>(track)));
            assert(!session->control_sound(static_cast<pxa::SoundTrack>(track), pxa::MusicAction::stop));
        }
        assert(!session->control_sound(pxa::SoundTrack::track2, static_cast<pxa::MusicAction>(0)));
        assert(!session->control_sound(pxa::SoundTrack::track2, pxa::MusicAction::stop, -256));
        assert(io_calls == before);
    }
    assert(session->control_sound(pxa::SoundTrack::track2,pxa::MusicAction::stop));
    assert(!session->control_sound(static_cast<pxa::SoundTrack>(6),pxa::MusicAction::stop));
    assert(session->control_music(pxa::MusicAction::gain,-20*256));
    auto state = co_await session->query();
    assert(state && state->submitted_samples == 100 && state->queued_samples == 20);
    auto flushed = co_await session->flush();
    assert(flushed);
    auto moved = std::move(*session);
    auto check_inactive = [&] {
        const auto before = io_calls;
        for (auto result : {session->effect(tones), session->effect(tones, weapon),
                            session->stop_effect(weapon), session->tone()})
            assert(!result && result.error() == pxa::Error::bad_state);
        auto pcm_result = session->write_pcm(pcm);
        assert(!pcm_result && pcm_result.error() == pxa::Error::bad_state);
        assert(io_calls == before);
    };
    check_inactive();
    assert(moved.effect(tones, weapon));
    assert(moved.close());
    const auto before_closed = io_calls;
    auto closed = moved.effect(tones);
    assert(!closed && closed.error() == pxa::Error::bad_state && io_calls == before_closed);
    assert(!(co_await context.audio().open_pcm(*permission, 7999, 2)));
    auto malformed = co_await context.audio().open_pcm(*permission, 22050, 2);
    assert(!malformed && malformed.error() == pxa::Error::protocol_error);
    auto pcm_session = co_await context.audio().open_pcm(*permission, 22050, 2);
    assert(pcm_session && pcm_session->format().sample_rate == 22050 &&
        pcm_session->format().channels == 2 && pcm_session->format().frame_ms == 20);
    const auto sink = pcm_session->pcm_sink();
    assert(sink.sample_rate == 48000 && sink.channels == 1 && sink.queue_frames == 1764);
    assert(pcm_session->close());
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
    assert(opcode == 5 && !finished);
    std::array<std::byte, 56> pcm_opened{};
    std::copy(opened.begin(), opened.end(), pcm_opened.begin());
    pxa::wire::put32(pcm_opened.data()+20, 22051); // Host must echo the exact source.
    pxa::wire::put16(pcm_opened.data()+35, 7); pxa::wire::put16(pcm_opened.data()+37, 4);
    pxa::wire::put32(pcm_opened.data()+39, 48000);
    pxa::wire::put16(pcm_opened.data()+43, 8); pxa::wire::put16(pcm_opened.data()+45, 1);
    pcm_opened[47] = std::byte{1};
    pxa::wire::put16(pcm_opened.data()+48, 9); pxa::wire::put16(pcm_opened.data()+50, 4);
    pxa::wire::put32(pcm_opened.data()+52, 1764);
    deliver(pcm_opened);
    assert(opcode == 5 && !finished);
    pxa::wire::put32(pcm_opened.data()+20, 22050);
    deliver(pcm_opened);
    assert(finished && closes == 5);
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
    assert(closes == 6);
    assert(requests.add(1000, &scope, [](void*, const pxa::Event&) {}));
    requests.abandon(1000, transport, 10, 5, true, 8);
    assert(requests.dispatch({10, 5, 1000, pcm_opened}));
    assert(closes == 7);
}
