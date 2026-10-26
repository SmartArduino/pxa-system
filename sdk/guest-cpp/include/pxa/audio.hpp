#pragma once

#include "assets.hpp"
#include "audio_protocol_generated.hpp"
#include "audio_psg.hpp"
#include "permission.hpp"
#include "service_wire.hpp"

#include <array>
#include <span>
#include <string_view>
#include <utility>

namespace pxa {

enum class Waveform : std::uint8_t { sine, square, triangle, noise };
enum class PlaybackState : std::uint8_t { ready = 1, ended, stopped, replaced, error };
enum class MusicAction : std::uint8_t { pause = 1, resume, stop, gain };

// Session-local replacement categories, not reserved concurrent voices.
// Synthesized effects and prepared asset sounds have separate track spaces.
enum class EffectTrack : std::uint8_t { track0, track1, track2, track3, track4, track5 };
enum class SoundTrack : std::uint8_t { track0, track1, track2, track3, track4, track5 };
static_assert(std::to_underlying(EffectTrack::track5) + 1 == detail::audio_wire::effect_track_count);
static_assert(std::to_underlying(SoundTrack::track5) + 1 == detail::audio_wire::sound_tracks);

struct Tone {
    std::uint16_t frequency_hz = 440;
    std::uint16_t duration_ms = 100;
    std::int16_t gain_db_q8 = -12 * 256;
    Waveform waveform = Waveform::sine;
    std::uint16_t attack_ms = 3;
    std::uint16_t release_ms = 5;
    std::uint16_t delay_ms = 0;
};

struct AudioFormat {
    std::uint32_t sample_rate = 0;
    std::uint8_t channels = 0;
    std::uint16_t frame_ms = 0;
};

struct AudioPcmSink {
    std::uint32_t sample_rate = 0;
    std::uint32_t queue_frames = 0;
    std::uint8_t channels = 0;
};

enum class AudioMixEncoding : std::uint8_t { s8 = 1, u8, s16le };
// Explicit wasm32 offsets for the synchronous Audio 0.11 kernel.
// This native C++ structure is never sent across the ABI as raw bytes.
struct AudioMixSpan {
    std::uint32_t source_offset = 0, source_bytes = 0, position_q16 = 0;
    std::uint32_t step_q16 = 65536, target_first = 0, frames = 0;
    std::uint16_t left_gain_q8 = 256, right_gain_q8 = 256;
    AudioMixEncoding encoding = AudioMixEncoding::s16le;
    std::uint8_t channels = 1;

    Result<void> encode(std::span<std::byte> out) const noexcept {
        const auto format = std::to_underlying(encoding);
        if (out.size() < detail::audio_wire::mix_span_bytes || !source_bytes ||
            !frames || frames > detail::audio_wire::mix_max_frames ||
            !step_q16 || step_q16 > (16u << 16) ||
            left_gain_q8 > 256 || right_gain_q8 > 256 ||
            format < 1 || format > 3 || (channels != 1 && channels != 2))
            return std::unexpected(Error::invalid_argument);
        auto* p = out.data();
        wire::put32(p, source_offset); wire::put32(p + 4, source_bytes);
        wire::put32(p + 8, position_q16); wire::put32(p + 12, step_q16);
        wire::put32(p + 16, target_first); wire::put32(p + 20, frames);
        wire::put16(p + 24, left_gain_q8); wire::put16(p + 26, right_gain_q8);
        p[28] = std::byte(format); p[29] = std::byte(channels);
        p[30] = p[31] = std::byte{};
        return {};
    }
};

struct AudioState {
    std::uint64_t submitted_samples = 0;
    std::uint64_t accepted_samples = 0;
    std::uint32_t queued_samples = 0;
    bool accepted_is_sink_submitted = false;
};

struct EqBand {
    std::uint16_t frequency_hz;
    std::int16_t gain_db_q8;
    std::uint16_t q_q8;
};

struct PlaybackEvent {
    std::uint64_t session;
    std::uint64_t instance;
    PlaybackState state;
    std::int32_t status;
};

inline Result<PlaybackEvent> decode_playback(const Event& event) noexcept {
    if (event.service != detail::audio_wire::service ||
        event.opcode != detail::audio_wire::playback_event || event.token ||
        event.payload.size() != 24)
        return std::unexpected(Error::protocol_error);
    const auto* p = event.payload.data();
    const auto state = std::to_integer<unsigned>(p[16]);
    const auto status = static_cast<std::int32_t>(wire::get32(p + 20));
    if (!(wire::get64(p) >> 32) || !wire::get64(p + 8) ||
        state < 1 || state > 5 || p[17] != std::byte{} ||
        p[18] != std::byte{} || p[19] != std::byte{} ||
        (state == 5 ? status > -1 || status < -16 : status != 0))
        return std::unexpected(Error::protocol_error);
    return PlaybackEvent{wire::get64(p), wire::get64(p + 8),
                          static_cast<PlaybackState>(state), status};
}

struct AudioSessionTag {};

class AudioSession {
public:
    static constexpr std::size_t max_effect_layers = detail::audio_wire::effect_max_layers;
    static constexpr std::size_t effect_track_count = detail::audio_wire::effect_track_count;
    static constexpr std::size_t sound_track_count = detail::audio_wire::sound_tracks;

    AudioSession(Transport& transport, RequestTable& requests,
                 std::uint64_t handle, AudioFormat format, AudioPcmSink sink = {}) noexcept
        : transport_(transport), requests_(requests), resource_(transport, handle),
          format_(format), pcm_sink_(sink) {}
    AudioSession(const AudioSession&) = delete;
    AudioSession& operator=(const AudioSession&) = delete;
    AudioSession(AudioSession&&) noexcept = default;

    std::uint64_t handle() const noexcept { return resource_.handle(); }
    explicit operator bool() const noexcept { return bool(resource_); }
    Result<void> close() noexcept { return resource_.close(); }
    const AudioFormat& format() const & noexcept { return format_; }
    AudioFormat format() const && noexcept { return format_; }
    // Zero fields identify a legacy session; PCM sessions report their route.
    AudioPcmSink pcm_sink() const noexcept { return pcm_sink_; }

    Result<void> tone(Tone tone = {}) noexcept {
        if (!valid_tone(tone)) return std::unexpected(Error::invalid_argument);
        std::array<std::byte, detail::audio_wire::effect_tone_bytes> command;
        encode_tone(command.data(), tone);
        return command_io(detail::audio_wire::io_play_tone, command);
    }

    // Independent one-shot. Borrowed layers are copied by the Host before
    // returning; delays share the group's first sample, not a previous FIFO.
    [[nodiscard("Check admission: a busy effect is not queued")]]
    Result<void> effect(std::span<const Tone> layers) noexcept {
        return play_effect(layers, detail::audio_wire::independent_effect);
    }
    // Atomically replace this category, including when its slot is occupied.
    // Would-block submits nothing; don't retry obsolete gameplay events.
    [[nodiscard("Check admission: a busy effect is not queued")]]
    Result<void> effect(std::span<const Tone> layers, EffectTrack track) noexcept {
        if (std::to_underlying(track) >= effect_track_count)
            return std::unexpected(Error::invalid_argument);
        return play_effect(layers, std::to_underlying(track));
    }
    // Idempotent; samples already submitted to the device cannot be recalled.
    [[nodiscard]] Result<void> stop_effect(EffectTrack track) noexcept {
        if (std::to_underlying(track) >= effect_track_count)
            return std::unexpected(Error::invalid_argument);
        std::array<std::byte, detail::audio_wire::effect_stop_bytes> command{
            std::byte(std::to_underlying(track))};
        return command_io(detail::audio_wire::io_stop_effect, command);
    }

    Result<std::uint32_t> write_pcm(std::span<const std::byte> pcm) noexcept {
        if (!resource_) return std::unexpected(Error::bad_state);
        const auto alignment = 2u * format_.channels;
        if (!alignment || pcm.size() % alignment)
            return std::unexpected(Error::invalid_argument);
        auto written = transport_.io(handle(), 2,
            {const_cast<std::byte*>(pcm.data()), pcm.size()});
        if (written && (*written > pcm.size() || *written % alignment))
            return std::unexpected(Error::protocol_error);
        return written;
    }

    // Accumulates into caller-owned S32LE stereo; no playback, copying or
    // ownership transfer. All encoded spans and sources must stay immutable
    // until return. The Host rejects the entire batch before writing on error.
    Result<void> mix_pcm(std::uint32_t target_offset, std::uint32_t target_frames,
        std::uint32_t spans_offset, std::uint16_t count) noexcept {
        if (!target_frames || target_frames > detail::audio_wire::mix_max_frames ||
            !count || count > detail::audio_wire::mix_max_spans)
            return std::unexpected(Error::invalid_argument);
        std::array<std::byte, detail::audio_wire::mix_header_bytes> command{};
        wire::put32(command.data(), target_offset);
        wire::put32(command.data() + 4, target_frames);
        wire::put32(command.data() + 8, spans_offset);
        wire::put16(command.data() + 12, count);
        return command_io(detail::audio_wire::io_mix_pcm, command);
    }

    // Audio 0.12. Synchronous waveform/envelope/LFSR rendering. Encoded voices
    // and output are borrowed only until return. No playback is enqueued.
    Result<void> psg_render(std::uint32_t target_offset, std::uint32_t frames,
        std::uint32_t voices_offset, std::uint16_t count, std::uint32_t ticks_per_sample) noexcept {
        if (!frames || frames > detail::audio_wire::psg_max_frames ||
            !count || count > detail::audio_wire::psg_max_voices ||
            frames * count > detail::audio_wire::psg_max_work ||
            !ticks_per_sample || ticks_per_sample > detail::audio_wire::psg_max_ticks)
            return std::unexpected(Error::invalid_argument);
        std::array<std::byte, detail::audio_wire::psg_header_bytes> command{};
        wire::put32(command.data(), target_offset); wire::put32(command.data() + 4, frames);
        wire::put32(command.data() + 8, voices_offset);
        wire::put32(command.data() + 12, ticks_per_sample);
        wire::put16(command.data() + 16, count);
        return command_io(detail::audio_wire::io_psg_render, command);
    }

    // Audio 0.13 GB profile 1. State is 128 encoded LE bytes. Output is
    // interleaved S16LE stereo. Register/frame-sequencer logic stays in Guest.


    Result<void> sound(const Asset& asset,
                        std::int16_t gain_db_q8 = -12 * 256) noexcept {
        if (asset.descriptor().kind != AssetKind::audio ||
            !valid_gain(gain_db_q8))
            return std::unexpected(Error::invalid_argument);
        std::array<std::byte, 12> command{};
        wire::put64(command.data(), asset.handle());
        wire::put16(command.data() + 8, static_cast<std::uint16_t>(gain_db_q8));
        return command_io(detail::audio_wire::io_play_sound, command);
    }

    // Audio 0.8: one independently controlled resident sound per track.
    Result<void> sound_track(const Asset& asset, SoundTrack track,
        bool loop = false, std::int16_t gain_db_q8 = -12 * 256) noexcept {
        if (asset.descriptor().kind != AssetKind::audio ||
            std::to_underlying(track) >= sound_track_count || !valid_gain(gain_db_q8))
            return std::unexpected(Error::invalid_argument);
        std::array<std::byte, 16> command{};
        wire::put64(command.data(), asset.handle());
        wire::put16(command.data() + 8, static_cast<std::uint16_t>(gain_db_q8));
        command[10] = std::byte(std::to_underlying(track));
        command[11] = std::byte(loop ? 1 : 0);
        return command_io(detail::audio_wire::io_play_sound, command);
    }
    Result<void> control_sound(SoundTrack track, MusicAction action,
        std::int16_t gain_db_q8 = 0) noexcept {
        if (std::to_underlying(track) >= sound_track_count ||
            std::to_underlying(action) < 1 || std::to_underlying(action) > 4 ||
            (action == MusicAction::gain ? !valid_gain(gain_db_q8) : gain_db_q8 != 0))
            return std::unexpected(Error::invalid_argument);
        std::array<std::byte, 8> command{};
        command[0] = std::byte(std::to_underlying(track));
        command[1] = std::byte(std::to_underlying(action));
        wire::put16(command.data() + 2, static_cast<std::uint16_t>(gain_db_q8));
        return command_io(detail::audio_wire::io_control_sound, command);
    }
    Result<void> control_music(MusicAction action,std::int16_t gain_db_q8 = 0) noexcept {
        if (static_cast<unsigned>(action)<1 || static_cast<unsigned>(action)>4 ||
            (action==MusicAction::gain ? !valid_gain(gain_db_q8) : gain_db_q8!=0))
            return std::unexpected(Error::invalid_argument);
        std::array<std::byte,4> command{};
        command[0]=std::byte(static_cast<std::uint8_t>(action));
        wire::put16(command.data()+2,static_cast<std::uint16_t>(gain_db_q8));
        return command_io(detail::audio_wire::io_control_music,command);
    }

    Result<std::uint64_t> music(std::string_view path, bool loop = false,
                                std::int16_t gain_db_q8 = -12 * 256) noexcept {
        if (path.empty() || path.size() >= 512 ||
            path.find('\0') != std::string_view::npos || !valid_gain(gain_db_q8))
            return std::unexpected(Error::invalid_argument);
        std::array<std::byte, 528> command{};
        wire::put16(command.data() + 8, static_cast<std::uint16_t>(path.size()));
        wire::put16(command.data() + 10, static_cast<std::uint16_t>(gain_db_q8));
        command[12] = std::byte(loop ? 1 : 0);
        for (std::size_t i = 0; i < path.size(); ++i)
            command[16 + i] = std::byte(path[i]);
        auto result = command_io(detail::audio_wire::io_play_music, {command.data(), 16 + path.size()});
        if (!result) return std::unexpected(result.error());
        const auto instance = wire::get64(command.data());
        if (!instance) return std::unexpected(Error::protocol_error);
        return instance;
    }

    Result<void> control(MusicAction action,
                          std::int16_t gain_db_q8 = 0) noexcept {
        if (static_cast<unsigned>(action) < 1 ||
            static_cast<unsigned>(action) > 4 ||
            (action == MusicAction::gain ? !valid_gain(gain_db_q8) : gain_db_q8 != 0))
            return std::unexpected(Error::invalid_argument);
        std::array<std::byte, 4> command{};
        command[0] = std::byte(static_cast<std::uint8_t>(action));
        wire::put16(command.data() + 2, static_cast<std::uint16_t>(gain_db_q8));
        return command_io(detail::audio_wire::io_control_asset, command);
    }

    Task<void> graph(std::int16_t gain_db_q8,
                      std::span<const EqBand> bands = {}) {
        if (!resource_) return Task<void>::failed(Error::bad_state);
        if (gain_db_q8 < -48 * 256 || gain_db_q8 > 12 * 256 || bands.size() > 5)
            return Task<void>::failed(Error::invalid_argument);
        wire::RequestPacket<74> packet;
        wire::Writer writer(std::span(packet.bytes));
        std::array<std::byte, 8> value{};
        wire::put64(value.data(), handle());
        wire::record(writer, 1, value);
        wire::put16(value.data(), static_cast<std::uint16_t>(gain_db_q8));
        wire::record(writer, 2, {value.data(), 2});
        for (const auto& band : bands) {
            if (band.frequency_hz < 20 || band.frequency_hz > 20000 ||
                band.gain_db_q8 < -12 * 256 || band.gain_db_q8 > 12 * 256 ||
                band.q_q8 < 64 || band.q_q8 > 4096)
                return Task<void>::failed(Error::invalid_argument);
            wire::put16(value.data(), band.frequency_hz);
            wire::put16(value.data() + 2, static_cast<std::uint16_t>(band.gain_db_q8));
            wire::put16(value.data() + 4, band.q_q8);
            wire::record(writer, 3, {value.data(), 6});
        }
        wire::put16(value.data(), 1);
        wire::record(writer, 4, {value.data(), 2});
        packet.size = writer.size();
        return status_request<74>(endpoint(), std::move(packet), detail::audio_wire::commit_graph);
    }

    Task<AudioState> query() {
        if (!resource_) return Task<AudioState>::failed(Error::bad_state);
        return query_request(endpoint());
    }

    Task<void> flush() {
        if (!resource_) return Task<void>::failed(Error::bad_state);
        wire::RequestPacket<12> packet;
        auto* payload = packet.bytes.data();
        wire::put16(payload, 1);
        wire::put16(payload + 2, 8);
        wire::put64(payload + 4, handle());
        packet.size = 12;
        return status_request<12>(endpoint(), std::move(packet), detail::audio_wire::flush);
    }

private:
    Result<void> play_effect(std::span<const Tone> layers, std::uint8_t track) noexcept {
        if (layers.empty() || layers.size() > max_effect_layers)
            return std::unexpected(Error::invalid_argument);
        constexpr auto header = detail::audio_wire::effect_header_bytes;
        constexpr auto stride = detail::audio_wire::effect_tone_bytes;
        // Every transmitted byte is written explicitly; spare capacity is not
        // initialized or sent. There is no heap allocation or retained span.
        std::array<std::byte, header + stride * max_effect_layers> command;
        command[0] = std::byte(layers.size());
        command[1] = std::byte(track);
        wire::put16(command.data() + 2, 0);
        for (std::size_t i = 0; i < layers.size(); ++i) {
            if (!valid_tone(layers[i])) return std::unexpected(Error::invalid_argument);
            encode_tone(command.data() + header + stride * i, layers[i]);
        }
        return command_io(detail::audio_wire::io_play_effect,
                          {command.data(), header + stride * layers.size()});
    }
    static constexpr bool valid_tone(const Tone& tone) noexcept {
        return tone.frequency_hz >= 40 && tone.frequency_hz <= 8000 &&
            tone.duration_ms >= 10 && tone.duration_ms <= 1000 &&
            valid_gain(tone.gain_db_q8) && static_cast<unsigned>(tone.waveform) <= 3 &&
            tone.attack_ms <= tone.duration_ms && tone.release_ms <= tone.duration_ms &&
            tone.delay_ms <= 1000;
    }
    static constexpr void encode_tone(std::byte* p, const Tone& tone) noexcept {
        wire::put16(p, tone.frequency_hz);
        wire::put16(p + 2, tone.duration_ms);
        wire::put16(p + 4, static_cast<std::uint16_t>(tone.gain_db_q8));
        p[6] = std::byte(static_cast<std::uint8_t>(tone.waveform));
        p[7] = std::byte{};
        wire::put16(p + 8, tone.attack_ms);
        wire::put16(p + 10, tone.release_ms);
        wire::put16(p + 12, tone.delay_ms);
    }

    struct Endpoint {
        Transport& transport;
        RequestTable& requests;
        std::uint64_t handle;
    };
    Endpoint endpoint() const noexcept { return {transport_, requests_, handle()}; }
    template<std::size_t Capacity>
    static Task<void> status_request(Endpoint endpoint,
        wire::RequestPacket<Capacity> packet, std::uint16_t opcode) {
        auto event = co_await Response(endpoint.transport, endpoint.requests, detail::audio_wire::service, opcode,
            packet.packet());
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        if (!body->empty()) co_return std::unexpected(Error::protocol_error);
        co_return Result<void>{};
    }

    static Task<AudioState> query_request(Endpoint endpoint) {
        std::array<std::byte, 12> payload{};
        wire::put16(payload.data(), 1);
        wire::put16(payload.data() + 2, 8);
        wire::put64(payload.data() + 4, endpoint.handle);
        auto event = co_await Response(endpoint.transport, endpoint.requests,
            detail::audio_wire::service, detail::audio_wire::query_state, payload);
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        wire::Records records(*body);
        auto submitted = records.take(2, 8);
        auto accepted = records.take(3, 8);
        auto queued = records.take(4, 4);
        auto flags = records.take(5, 4);
        if (!submitted || !accepted || !queued || !flags || !records.empty())
            co_return std::unexpected(Error::protocol_error);
        AudioState state{wire::get64(submitted->data()), wire::get64(accepted->data()),
                         wire::get32(queued->data()), wire::get32(flags->data()) != 0};
        if (wire::get32(flags->data()) > 1 ||
            state.accepted_samples > state.submitted_samples ||
            state.queued_samples > state.submitted_samples)
            co_return std::unexpected(Error::protocol_error);
        co_return state;
    }

    static constexpr bool valid_gain(std::int16_t gain) noexcept {
        return gain >= -60 * 256 && gain <= 0;
    }
    Result<void> command_io(std::uint32_t operation,
                            std::span<std::byte> command) noexcept {
        if (!resource_) return std::unexpected(Error::bad_state);
        auto result = transport_.io(handle(), operation, command);
        if (!result) return std::unexpected(result.error());
        if (*result != command.size()) return std::unexpected(Error::protocol_error);
        return {};
    }
    Transport& transport_;
    RequestTable& requests_;
    Resource<AudioSessionTag> resource_;
    AudioFormat format_;
    AudioPcmSink pcm_sink_;
};

class AudioService {
public:
    AudioService(Transport& transport, RequestTable& requests) noexcept
        : transport_(transport), requests_(requests) {}

    Task<AudioSession> open(this AudioService self, const Permission& permission) {
        if (!(permission.handle() >> 32))
            return Task<AudioSession>::failed(Error::invalid_argument);
        return open_request(self, permission.handle());
    }
    Task<AudioSession> open_pcm(this AudioService self, const Permission& permission,
                               std::uint32_t rate, std::uint8_t channels) {
        if (!(permission.handle() >> 32) || rate < 8000 || rate > 48000 ||
            (channels != 1 && channels != 2))
            return Task<AudioSession>::failed(Error::invalid_argument);
        return open_request(self, permission.handle(), AudioFormat{rate, channels, 20});
    }
private:
    static Task<AudioSession> open_request(AudioService self, std::uint64_t permission,
                                         AudioFormat requested = {}) {
        auto& [transport_, requests_] = self;
        std::array<std::byte, 18> payload{};
        const bool pcm = requested.sample_rate != 0;
        if (pcm) {
            wire::put64(payload.data(), permission);
            wire::put32(payload.data() + 8, requested.sample_rate);
            payload[12] = std::byte(requested.channels);
        } else {
            wire::put16(payload.data(), 1);
            wire::put16(payload.data() + 2, 8);
            wire::put64(payload.data() + 4, permission);
            wire::put16(payload.data() + 12, 2);
            wire::put16(payload.data() + 14, 2);
            wire::put16(payload.data() + 16, 1);
        }
        auto event = co_await Response(transport_, requests_, detail::audio_wire::service,
            pcm ? detail::audio_wire::open_pcm : detail::audio_wire::open_session,
            std::span<const std::byte>(payload.data(), pcm ? 16 : 18), true, 8);
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        wire::Records records(*body);
        auto handle = records.take(3, 8);
        if (!handle || !(wire::get64(handle->data()) >> 32))
            co_return std::unexpected(Error::protocol_error);
        Resource<AudioSessionTag> pending(transport_, wire::get64(handle->data()));
        auto rate = records.take(4, 4);
        auto channels = records.take(5, 1);
        auto frame = records.take(6, 2);
        if (!rate || !channels || !frame)
            co_return std::unexpected(Error::protocol_error);
        AudioFormat format{wire::get32(rate->data()),
                           std::to_integer<std::uint8_t>((*channels)[0]),
                           wire::get16(frame->data())};
        if (format.sample_rate < 8000 || format.sample_rate > 48000 ||
            (format.channels != 1 && format.channels != 2) ||
            format.frame_ms < 5 || format.frame_ms > 120)
            co_return std::unexpected(Error::protocol_error);
        AudioPcmSink sink;
        if (pcm) {
            auto sink_rate = records.take(7, 4);
            auto sink_channels = records.take(8, 1);
            auto queue = records.take(9, 4);
            if (!sink_rate || !sink_channels || !queue)
                co_return std::unexpected(Error::protocol_error);
            sink = {wire::get32(sink_rate->data()), wire::get32(queue->data()),
                    std::to_integer<std::uint8_t>((*sink_channels)[0])};
            if (format.sample_rate != requested.sample_rate ||
                format.channels != requested.channels || format.frame_ms != 20 ||
                sink.sample_rate < 8000 || sink.sample_rate > 48000 ||
                (sink.channels != 1 && sink.channels != 2) ||
                !sink.queue_frames || sink.queue_frames > format.sample_rate*120u/1000u)
                co_return std::unexpected(Error::protocol_error);
        }
        if (!records.empty()) co_return std::unexpected(Error::protocol_error);
        co_return AudioSession(transport_, requests_, pending.release(), format, sink);
    }
private:
    Transport& transport_;
    RequestTable& requests_;
};

} // namespace pxa
