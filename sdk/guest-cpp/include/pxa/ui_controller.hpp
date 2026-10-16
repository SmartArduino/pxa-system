#pragma once

#include "input.hpp"

// Compatibility names. Controllers are application input, independent of UI.
namespace pxa::ui {
using input::Controller;
using input::ControllerState;
using input::decode_controller;
using input::controller_up;
using input::controller_down;
using input::controller_left;
using input::controller_right;
using input::controller_a;
using input::controller_b;
using input::controller_start;
using input::controller_select;
using input::controller_x;
using input::controller_y;
using input::controller_l;
using input::controller_r;
using input::controller_button_mask;
} // namespace pxa::ui
