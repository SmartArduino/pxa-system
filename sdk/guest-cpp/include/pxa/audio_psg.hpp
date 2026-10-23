#pragma once
#include "audio_protocol_generated.hpp"
#include "service_wire.hpp"
#include <span>

namespace pxa {
enum class AudioPsgWaveform : std::uint8_t { pulse = 1, wave, noise };

// Native value type. Keep the encoded state in linear memory between batches;
// the Host updates phase, cursor, envelope_phase and level in place.
struct AudioPsgVoice {
    std::uint32_t period = 1, phase = 0, cursor = 0;
    std::uint32_t envelope_period = 0, envelope_phase = 0, table_offset = 0;
    std::uint16_t left_gain_q8 = 256, right_gain_q8 = 256;
    AudioPsgWaveform waveform = AudioPsgWaveform::pulse;
    std::uint8_t invert_feedback = 0, noise_width = 0, noise_tap = 0;
    std::uint8_t level = 15, pulse_mask = 0x0f, table_length = 0;
    std::int8_t envelope_direction = 0;

    static AudioPsgVoice pulse(std::uint32_t period, std::uint8_t mask = 0x0f) noexcept {
        AudioPsgVoice v;
        v.period = period; v.pulse_mask = mask;
        return v;
    }
    static AudioPsgVoice wave(std::uint32_t period, std::uint32_t table_offset,
        std::uint8_t table_length) noexcept {
        auto v = pulse(period, 0);
        v.waveform = AudioPsgWaveform::wave;
        v.table_offset = table_offset; v.table_length = table_length;
        return v;
    }
    static AudioPsgVoice noise(std::uint32_t period, std::uint8_t width = 15,
        std::uint8_t tap = 1, bool inverted = false, std::uint32_t seed = 1) noexcept {
        auto v = pulse(period, 0);
        v.waveform = AudioPsgWaveform::noise;
        v.noise_width = width; v.noise_tap = tap;
        v.invert_feedback = inverted; v.cursor = seed;
        return v;
    }

    bool valid() const noexcept {
        const auto type = std::to_underlying(waveform);
        if (type < 1 || type > 3 || !period || period > INT32_MAX || phase >= period ||
            envelope_period > INT32_MAX || (envelope_period ?
            envelope_phase >= envelope_period : envelope_phase != 0) ||
            left_gain_q8 > 256 || right_gain_q8 > 256 || level > 15 ||
            envelope_direction < -1 || envelope_direction > 1) return false;
        if (type != 3 && (invert_feedback || noise_width || noise_tap)) return false;
        if (type != 1 && pulse_mask) return false;
        if (type != 2 && (table_offset || table_length)) return false;
        if (type == 1) return cursor < 8;
        if (type == 2) return table_length && table_length <= 32 && cursor < table_length;
        return invert_feedback <= 1 && (noise_width == 7 || noise_width == 15) &&
            noise_tap && noise_tap < noise_width && cursor < (1u << noise_width);
    }

    Result<void> encode(std::span<std::byte> out) const noexcept {
        if (out.size() < detail::audio_wire::psg_voice_bytes || !valid())
            return std::unexpected(Error::invalid_argument);
        if (wire::overlaps(out.first(detail::audio_wire::psg_voice_bytes),
            std::as_bytes(std::span(this, 1))))
            return std::unexpected(Error::invalid_argument);
        auto* p = out.data();
        p[0] = std::byte(std::to_underlying(waveform)); p[1] = std::byte(invert_feedback);
        p[2] = std::byte(noise_width); p[3] = std::byte(noise_tap);
        wire::put32(p + 4, period); wire::put32(p + 8, phase);
        wire::put32(p + 12, cursor); wire::put32(p + 16, envelope_period);
        wire::put32(p + 20, envelope_phase);
        wire::put16(p + 24, left_gain_q8); wire::put16(p + 26, right_gain_q8);
        p[28] = std::byte(level); p[29] = std::byte(static_cast<std::uint8_t>(envelope_direction));
        p[30] = std::byte(pulse_mask); p[31] = std::byte(table_length);
        wire::put32(p + 32, table_offset); wire::put32(p + 36, 0);
        return {};
    }

    static Result<AudioPsgVoice> decode(std::span<const std::byte> in) noexcept {
        if (in.size() < detail::audio_wire::psg_voice_bytes || wire::get32(in.data() + 36))
            return std::unexpected(Error::protocol_error);
        const auto* p = in.data();
        AudioPsgVoice v;
        v.waveform = static_cast<AudioPsgWaveform>(p[0]);
        v.invert_feedback = std::to_integer<std::uint8_t>(p[1]);
        v.noise_width = std::to_integer<std::uint8_t>(p[2]);
        v.noise_tap = std::to_integer<std::uint8_t>(p[3]);
        v.period = wire::get32(p + 4); v.phase = wire::get32(p + 8);
        v.cursor = wire::get32(p + 12); v.envelope_period = wire::get32(p + 16);
        v.envelope_phase = wire::get32(p + 20);
        v.left_gain_q8 = wire::get16(p + 24); v.right_gain_q8 = wire::get16(p + 26);
        v.level = std::to_integer<std::uint8_t>(p[28]);
        auto direction = std::to_integer<unsigned>(p[29]);
        v.envelope_direction = direction == 255 ? -1 : direction <= 1 ? direction : 2;
        v.pulse_mask = std::to_integer<std::uint8_t>(p[30]);
        v.table_length = std::to_integer<std::uint8_t>(p[31]); v.table_offset = wire::get32(p + 32);
        if (!v.valid()) return std::unexpected(Error::protocol_error);
        return v;
    }
};
} // namespace pxa
