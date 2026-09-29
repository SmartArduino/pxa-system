#include <pxa/app.hpp>
#include <pxa/list.hpp>

using namespace pxa::ui;
using namespace pxa::ui::literals;

struct ListApp {
    ListState items{10000};
    State<int> selected{0};

    auto view() {
        return Column(
            Text("Items").font(Font::title),
            Text(selected),
            VirtualList<32, 0, 1>(items, 48_dp,
                [](std::uint32_t index) { return index + 1; },
                [this](std::uint32_t key) {
                    char number[16];
                    auto [end, error] = std::to_chars(number, number + sizeof(number), key);
                    (void)error;
                    std::string label("Item ");
                    label.append(number, end);
                    return Button(std::move(label)).on_click([this, key] {
                        selected.set(static_cast<int>(key));
                    });
                }).grow()
        ).fill().fill_height().gap(8_dp).padding(12_dp);
    }
};

PXA_APPLICATION(ListApp)
