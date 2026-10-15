#include <pxa/app.hpp>
using namespace pxa::ui;
using namespace pxa::ui::literals;
using namespace std::chrono_literals;
struct Gestures {
    State<int> taps{0}, holds{0};
    State<std::string> label{"Press here"};
    auto view() {
        return Column(Text("Tap or hold"), Text(taps), Text(holds),
            Button(label).on_click([this] { taps.update([](int n){return n+1;}); })
                .on_long_press([this] {
                    holds.update([](int n){return n+1;});
                    label.set("Hold received");
                }, {.long_press=500ms})
        ).gap(12_dp).padding(24_dp);
    }
};
PXA_APPLICATION(Gestures)
