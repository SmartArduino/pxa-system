#include <pxa/app.hpp>
#include <pxa/events.hpp>
#include <pxa/ui_action.hpp>
#include <pxa/ui_component.hpp>
#include <pxa/ui_layout.hpp>
#include <pxa/ui_environment.hpp>

using namespace pxa;
using namespace pxa::ui;
using namespace pxa::ui::literals;

struct LocalCounter {
    State<int> count{0};
    auto view() {
        return Row(Text("Local:").font(Font::label), Text(count).font(Font::label),
            Button("+ local").on_click([this] {
                count.update([](int n) { return n + 1; });
            }).font(Font::label).height(30_dp)).gap(8_dp).align(Align::center);
    }
};

struct Workshop {
    Environment<> environment;
    State<int> count{0};
    State<bool> enabled{true}, details{false};
    State<bool> locked{false};
    State<std::string> add{"Add"}, status{"Ready"};
    Context* context = nullptr;

    Result<void> on_start(Context& ctx, std::span<const std::byte> config) {
        context = &ctx;
        return environment.initialize(config);
    }
    Result<bool> on_event(Context&, const Event& event) {
        return environment.update(event);
    }
    Task<void> increment_later() {
        status.set("Working");
        auto waited = co_await context->clock().yield();
        if (!waited) co_return std::unexpected(waited.error());
        count.update([](int n) { return n + 1; });
        status.set("Async complete");
        co_return Result<void>{};
    }
    void on_error(Context&, Error) { status.set("Action error handled"); }
    void on_background(Context&) { status.set("Background"); }
    void on_foreground(Context&) { status.set("Ready"); }
    auto view() {
        return SafeArea(environment.display(), Scroll(Column(
            Text("UI Workshop").font(Font::title),
            Text(Computed([](int n, bool active) {
                return std::string(active ? "Count: " : "Paused: ") + std::to_string(n);
            }, count, enabled)).font(Font::headline),
            Row(
                Button(add).on_click([this] {
                    count.update([](int n) { return n + 1; });
                    add.set(count.get() == 1 ? "Again" : "Add");
                }).enabled(enabled).height(36_dp).grow(),
                Button("Async").on_click(Action(context->foreground_tasks(), [this] {
                    return increment_later();
                })).height(36_dp).grow(),
                Button("Fail").on_click(Action(context->tasks(), []() -> Result<void> {
                    return std::unexpected(Error::io_error);
                })).height(36_dp).grow()
            ).gap(6_dp).fill(),
            Toggle("Enabled", enabled),
            Toggle("Details", details),
            When(details, [this] { return Column(
                              Text("Details shown").font(Font::caption),
                              Text(Computed([](int n) { return n / 8.0; }, count)).font(Font::caption)); },
                          [] { return Text("Details hidden").font(Font::caption); }),
            Component<LocalCounter>(),
            Toggle("Locked", locked).enabled(false),
            Text(status).font(Font::label)
        ).gap(8_dp).fill())).padding(12_dp);
    }
};
PXA_APPLICATION(Workshop)
