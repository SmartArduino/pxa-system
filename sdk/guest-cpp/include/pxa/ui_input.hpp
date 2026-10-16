#pragma once

#include "input.hpp"

// Compatibility names for UI controls; application input lives in pxa::input.
namespace pxa::ui {
using input::VolumeKey;
using input::VolumeKeyEvent;
using input::decode_volume_key;
using input::pointer_phase_down;
using input::pointer_phase_move;
using input::pointer_phase_up;
using input::pointer_phase_cancel;
using input::PointerPhase;
using CanvasPointer = input::Pointer;
using input::PointerCapture;
using input::decode_pointer;
} // namespace pxa::ui
