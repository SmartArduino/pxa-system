#pragma once

#include "assets.hpp"
#include "permission.hpp"
#include "service_wire.hpp"

#include <string_view>

namespace pxa {

enum class Waveform : std::uint8_t { sine, square, triangle, noise };
enum class PlaybackState : std::uint8_t { ready = 1, ended, stopped, replaced, error };
enum class MusicAction : std::uint8_t { pause = 1, resume, stop, gain };

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
    if (event.service != 10 || event.opcode != 0x8001 || event.token ||
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
    AudioSession(Transport& transport, RequestTable& requests,
                 std::uint64_t handle, AudioFormat format) noexcept
        : transport_(transport), requests_(requests), resource_(transport, handle),
          format_(format) {}
    AudioSession(const AudioSession&) = delete;
    AudioSession& operator=(const AudioSession&) = delete;
    AudioSession(AudioSession&&) noexcept = default;

    std::uint64_t handle() const noexcept { return resource_.handle(); }
    explicit operator bool() const noexcept { return bool(resource_); }
    Result<void> close() noexcept { return resource_.close(); }
    const AudioFormat& format() const & noexcept { return format_; }
    AudioFormat format() const && noexcept { return format_; }

    Result<void> tone(Tone tone = {}) noexcept {
        if (tone.frequency_hz < 40 || tone.frequency_hz > 8000 ||
            tone.duration_ms < 10 || tone.duration_ms > 1000 ||
            !valid_gain(tone.gain_db_q8) ||
            static_cast<unsigned>(tone.waveform) > 3 ||
            tone.attack_ms > tone.duration_ms ||
            tone.release_ms > tone.duration_ms || tone.delay_ms > 1000)
            return std::unexpected(Error::invalid_argument);
        std::array<std::byte, 14> command{};
        wire::put16(command.data(), tone.frequency_hz);
        wire::put16(command.data() + 2, tone.duration_ms);
        wire::put16(command.data() + 4,
                    static_cast<std::uint16_t>(tone.gain_db_q8));
        command[6] = std::byte(static_cast<std::uint8_t>(tone.waveform));
        wire::put16(command.data() + 8, tone.attack_ms);
        wire::put16(command.data() + 10, tone.release_ms);
        wire::put16(command.data() + 12, tone.delay_ms);
        return command_io(0x100, command);
    }

    Result<std::uint32_t> write_pcm(std::span<const std::byte> pcm) noexcept {
        const auto alignment = 2u * format_.channels;
        if (!alignment || pcm.size() % alignment)
            return std::unexpected(Error::invalid_argument);
        auto written = transport_.io(handle(), 2,
            {const_cast<std::byte*>(pcm.data()), pcm.size()});
        if (written && (*written > pcm.size() || *written % alignment))
            return std::unexpected(Error::protocol_error);
        return written;
    }

    Result<void> sound(const Asset& asset,
                        std::int16_t gain_db_q8 = -12 * 256) noexcept {
        if (asset.descriptor().kind != AssetKind::audio ||
            !valid_gain(gain_db_q8))
            return std::unexpected(Error::invalid_argument);
        std::array<std::byte, 12> command{};
        wire::put64(command.data(), asset.handle());
        wire::put16(command.data() + 8, static_cast<std::uint16_t>(gain_db_q8));
        return command_io(0x103, command);
    }

    // Audio 0.8: one independently controlled resident sound per track.
    Result<void> sound_track(const Asset& asset, std::uint8_t track,
        bool loop = false, std::int16_t gain_db_q8 = -12 * 256) noexcept {
        if (asset.descriptor().kind != AssetKind::audio || track >= 6 || !valid_gain(gain_db_q8))
            return std::unexpected(Error::invalid_argument);
        std::array<std::byte,16> command{};
        wire::put64(command.data(),asset.handle());
        wire::put16(command.data()+8,static_cast<std::uint16_t>(gain_db_q8));
        command[10]=std::byte(track);command[11]=std::byte(loop ? 1 : 0);
        return command_io(0x103,command);
    }
    Result<void> control_sound(std::uint8_t track, MusicAction action,
        std::int16_t gain_db_q8 = 0) noexcept {
        if (track >= 6 || static_cast<unsigned>(action)<1 || static_cast<unsigned>(action)>4 ||
            (action==MusicAction::gain ? !valid_gain(gain_db_q8) : gain_db_q8!=0))
            return std::unexpected(Error::invalid_argument);
        std::array<std::byte,8> command{};
        command[0]=std::byte(track);command[1]=std::byte(static_cast<std::uint8_t>(action));
        wire::put16(command.data()+2,static_cast<std::uint16_t>(gain_db_q8));
        return command_io(0x105,command);
    }
    Result<void> control_music(MusicAction action,std::int16_t gain_db_q8 = 0) noexcept {
        if (static_cast<unsigned>(action)<1 || static_cast<unsigned>(action)>4 ||
            (action==MusicAction::gain ? !valid_gain(gain_db_q8) : gain_db_q8!=0))
            return std::unexpected(Error::invalid_argument);
        std::array<std::byte,4> command{};
        command[0]=std::byte(static_cast<std::uint8_t>(action));
        wire::put16(command.data()+2,static_cast<std::uint16_t>(gain_db_q8));
        return command_io(0x106,command);
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
        auto result = command_io(0x104, {command.data(), 16 + path.size()});
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
        return command_io(0x102, command);
    }

    Task<void> graph(std::int16_t gain_db_q8,
                      std::span<const EqBand> bands = {}) {
        if (!resource_) return Task<void>::failed(Error::bad_state);
        if (gain_db_q8 < -48 * 256 || gain_db_q8 > 12 * 256 || bands.size() > 5)
            return Task<void>::failed(Error::invalid_argument);
        wire::RequestPacket<74> packet;
        wire::Writer writer(std::span(packet.bytes).subspan(wire::header_bytes));
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
        return status_request<74>(endpoint(), std::move(packet), 2);
    }

    Task<AudioState> query() {
        if (!resource_) return Task<AudioState>::failed(Error::bad_state);
        return query_request(endpoint());
    }

    Task<void> flush() {
        if (!resource_) return Task<void>::failed(Error::bad_state);
        wire::RequestPacket<12> packet;
        auto* payload = packet.bytes.data() + wire::header_bytes;
        wire::put16(payload, 1);
        wire::put16(payload + 2, 8);
        wire::put64(payload + 4, handle());
        packet.size = 12;
        return status_request<12>(endpoint(), std::move(packet), 4);
    }

private:
    struct Endpoint {
        Transport& transport;
        RequestTable& requests;
        std::uint64_t handle;
    };
    Endpoint endpoint() const noexcept { return {transport_, requests_, handle()}; }
    template<std::size_t Capacity>
    static Task<void> status_request(Endpoint endpoint,
        wire::RequestPacket<Capacity> packet, std::uint16_t opcode) {
        auto event = co_await Response(endpoint.transport, endpoint.requests, 10, opcode,
            Response::PrebuiltPacket{packet.packet()});
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
        auto event = co_await Response(endpoint.transport, endpoint.requests, 10, 3, payload);
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

    static bool valid_gain(std::int16_t gain) noexcept {
        return gain >= -60 * 256 && gain <= 0;
    }
    Result<void> command_io(std::uint32_t operation,
                            std::span<std::byte> command) noexcept {
        auto result = transport_.io(handle(), operation, command);
        if (!result) return std::unexpected(result.error());
        if (*result != command.size()) return std::unexpected(Error::protocol_error);
        return {};
    }
    Transport& transport_;
    RequestTable& requests_;
    Resource<AudioSessionTag> resource_;
    AudioFormat format_;
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
private:
    static Task<AudioSession> open_request(AudioService self, std::uint64_t permission) {
        auto& [transport_, requests_] = self;
        std::array<std::byte, 18> payload{};
        wire::put16(payload.data(), 1);
        wire::put16(payload.data() + 2, 8);
        wire::put64(payload.data() + 4, permission);
        wire::put16(payload.data() + 12, 2);
        wire::put16(payload.data() + 14, 2);
        wire::put16(payload.data() + 16, 1);
        auto event = co_await Response(transport_, requests_, 10, 1, payload, true, 8);
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
        if (!rate || !channels || !frame || !records.empty())
            co_return std::unexpected(Error::protocol_error);
        AudioFormat format{wire::get32(rate->data()),
                           std::to_integer<std::uint8_t>((*channels)[0]),
                           wire::get16(frame->data())};
        if (format.sample_rate < 8000 || format.sample_rate > 48000 ||
            (format.channels != 1 && format.channels != 2) ||
            format.frame_ms < 5 || format.frame_ms > 120)
            co_return std::unexpected(Error::protocol_error);
        co_return AudioSession(transport_, requests_, pending.release(), format);
    }
private:
    Transport& transport_;
    RequestTable& requests_;
};

} // namespace pxa
