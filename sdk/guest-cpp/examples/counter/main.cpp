#include <pxa/app.hpp>
#include <pxa/ui_display.hpp>
#include <algorithm>

using namespace pxa::ui;
using namespace pxa::ui::literals;

struct Counter {
    State<int> count{0};
    std::int32_t content_padding_dp = 16;

    pxa::Result<void> on_start(pxa::Context&, std::span<const std::byte> config) {
        if (auto display = decode_start_display(config)) {
            const auto& safe = display->safe;
            auto largest = std::max({safe.left, safe.top, safe.right, safe.bottom});
            auto inset = (std::uint64_t(largest) * 65536 + display->density_q16 - 1) / display->density_q16;
            content_padding_dp = std::int32_t(std::min<std::uint64_t>(65535, inset + 16));
        }
        return {};
    }

    auto view() {
        return Column(
            Text("Counter").font(Font::title),
            Text(count).font(Font::headline),
            Button("Add one").on_click([this] {
                count.update([](int value) { return value + 1; });
            })
        ).gap(12_dp).padding(Dp{content_padding_dp});
    }
};

PXA_APPLICATION(Counter)
