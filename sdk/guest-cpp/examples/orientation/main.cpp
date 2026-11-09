#include <pxa/app.hpp>
#include <pxa/ui_environment.hpp>
#include <pxa/ui_layout.hpp>
#include <cstdio>

using namespace pxa;
using namespace pxa::ui;
using namespace pxa::ui::literals;

struct OrientationApp {
    Environment<> environment;
    State<std::string> metrics{"Reading display..."}, status{"Choose a direction"};
    Context* context = nullptr;

    void on_window_changed(const WindowMetrics& value) {
        char text[160];
        std::snprintf(text, sizeof(text), "%ux%u / %u deg / %s (%ux%u)",
            value.logical_width, value.logical_height, unsigned(value.rotation) * 90,
            value.orientation == 3 ? "square" : value.orientation == 1 ? "portrait" : "landscape",
            value.native_width, value.native_height);
        metrics.set(std::string(text));
        if (context) (void)context->log().write(LogLevel::info, text);
    }
    Task<void> read_display() {
        auto value = co_await context->window().snapshot(true);
        if (value) on_window_changed(*value);
        else {
            char text[64];
            std::snprintf(text, sizeof(text), "Display metrics error %d", int(value.error()));
            status.set(std::string(text));
        }
        co_return Result<void>{};
    }
    Result<void> on_start(Context& ctx, std::span<const std::byte> config) {
        context = &ctx;
        auto initialized = environment.initialize(config);
        if (!initialized) return initialized;
        return ctx.tasks().start(read_display());
    }
    Result<bool> on_event(Context&, const Event& event) { return environment.update(event); }
    void select(OrientationPreference preference) {
        auto result = context->window().request_orientation(preference);
        status.set(result ? "Direction requested" : "Direction unsupported");
    }
    auto view() {
        return SafeArea(environment.display(), Scroll(Column(
            Text("Display orientation").font(Font::title),
            Text(metrics).font(Font::caption).fill(),
            Row(Button("Portrait").on_click([this] { select(OrientationPreference::portrait); }).height(38_dp).grow(),
                Button("Landscape").on_click([this] { select(OrientationPreference::landscape); }).height(38_dp).grow()).fill().gap(6_dp),
            Row(Button("0").on_click([this] { select(OrientationPreference::rotation_0); }).height(38_dp).grow(),
                Button("90").on_click([this] { select(OrientationPreference::rotation_90); }).height(38_dp).grow(),
                Button("180").on_click([this] { select(OrientationPreference::rotation_180); }).height(38_dp).grow(),
                Button("270").on_click([this] { select(OrientationPreference::rotation_270); }).height(38_dp).grow()).fill().gap(6_dp),
            Row(Button("System").on_click([this] { select(OrientationPreference::system); }).height(38_dp).grow(),
                Button("Lock").on_click([this] { select(OrientationPreference::locked); }).height(38_dp).grow()).fill().gap(6_dp),
            Text(status).font(Font::caption)
        ).gap(6_dp).fill()).fill().fill_height()).padding(8_dp).background(Color::background);
    }
};
PXA_APPLICATION(OrientationApp)
