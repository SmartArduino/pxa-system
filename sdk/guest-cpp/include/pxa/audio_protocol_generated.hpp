// Generated from spec/draft/pxa-audio.json. Do not edit by hand.
#pragma once
#include <cstddef>
#include <cstdint>

namespace pxa::detail::audio_wire {
inline constexpr std::uint16_t service = 10;
inline constexpr std::uint16_t open_session = 1;
inline constexpr std::uint16_t commit_graph = 2;
inline constexpr std::uint16_t query_state = 3;
inline constexpr std::uint16_t flush = 4;
inline constexpr std::uint16_t open_pcm = 5;
inline constexpr std::uint32_t io_play_tone = 256;
inline constexpr std::uint32_t io_play_asset = 257;
inline constexpr std::uint32_t io_control_asset = 258;
inline constexpr std::uint32_t io_play_sound = 259;
inline constexpr std::uint32_t io_play_music = 260;
inline constexpr std::uint32_t io_control_sound = 261;
inline constexpr std::uint32_t io_control_music = 262;
inline constexpr std::uint32_t io_play_effect = 263;
inline constexpr std::uint32_t io_stop_effect = 264;
inline constexpr std::uint16_t playback_event = 32769;
inline constexpr std::size_t sound_tracks = 6;
inline constexpr std::size_t effect_max_layers = 8;
inline constexpr std::size_t effect_track_count = 6;
inline constexpr std::size_t effect_header_bytes = 4;
inline constexpr std::size_t effect_tone_bytes = 14;
inline constexpr std::size_t effect_stop_bytes = 4;
inline constexpr std::uint8_t independent_effect = 255;
} // namespace pxa::detail::audio_wire
