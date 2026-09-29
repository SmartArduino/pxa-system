#pragma once

#include <pxa/ui.hpp>

struct Model {
    pxa::ui::State<int> count{0};
    pxa::Result<void> add(int amount);
};
