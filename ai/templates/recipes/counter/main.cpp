#include <pxa/app.hpp>
#include <pxa/ui_environment.hpp>
#include <pxa/ui_layout.hpp>
#include <algorithm>

using namespace pxa;
using namespace pxa::ui;
using namespace pxa::ui::literals;

struct Counter {
    Environment<> environment;
    Context* context = nullptr;
    State<int> count{0};
    State<bool> ready{false};
    State<std::string> storage_status{"读取存档"};
    input::Controller controller;
    bool writable = true, saving = false, dirty = false;
    static constexpr int limit = 999999;

    Task<void> load() {
        auto stored = co_await context->storage().get_value<std::int32_t>("counter.value.v1");
        if (stored && *stored >= -limit && *stored <= limit) {
            count.set(*stored);
            storage_status.set("已载入");
        } else if (!stored && stored.error() == Error::not_found) {
            storage_status.set("新存档");
        } else {
            writable = false; // Preserve unreadable data; don't overwrite it with zero.
            storage_status.set("仅本次会话");
        }
        ready.set(true);
        co_return Result<void>{};
    }
    Task<void> save() {
        while (dirty) {
            dirty = false;
            auto result = co_await context->storage().set_value("counter.value.v1", std::int32_t(count.get()));
            if (!result) {
                storage_status.set("保存失败");
                saving = false;
                co_return Result<void>{};
            }
        }
        saving = false;
        storage_status.set("已保存");
        co_return Result<void>{};
    }
    void change(int delta, bool reset = false) {
        if (!ready.get()) return;
        count.set(reset ? 0 : std::clamp(count.get() + delta, -limit, limit));
        if (!writable) return;
        dirty = true;
        storage_status.set("保存中");
        if (saving) return;
        saving = true;
        if (auto result = context->tasks().start(save()); !result) {
            saving = false;
            storage_status.set("保存失败");
        }
    }
    Result<void> on_start(Context& ctx, std::span<const std::byte> config) {
        context = &ctx;
        auto result = environment.initialize(config);
        if (!result) return result;
        WindowConfig window;
        window.edge_to_edge = true; // Continuous background behind visible bars.
        if (auto configured = ctx.window().configure(window); !configured) return configured;
        return ctx.tasks().start(load());
    }
    Result<bool> on_event(Context&, const Event& event) { return environment.update(event); }
    void on_controller(Context&, const input::ControllerState& state) {
        if (!controller.update(state)) return;
        if (controller.pressed(input::controller_a) || controller.pressed(input::controller_right)) change(1);
        if (controller.pressed(input::controller_left)) change(-1);
        if (controller.pressed(input::controller_x)) change(0, true);
    }
    void on_background(Context&) { controller.reset(); }
    void on_error(Context&, Error) { ready.set(true); storage_status.set("操作失败"); }
    auto view() {
        return SafeArea(environment.display(), Column(
            Row(Text(@PXA_TITLE@).font(Font::title), Text(storage_status).font(Font::caption))
                .justify(Justify::space_between).fill(),
            Text(count).font(Font::headline).fill().text_align(TextAlign::center),
            Row(Button("−1").on_click([this] { change(-1); }).enabled(ready).height(44_dp).grow().justify(Justify::center),
                Button("重置").on_click([this] { change(0, true); }).enabled(ready).height(44_dp).grow().justify(Justify::center),
                Button("+1").on_click([this] { change(1); }).enabled(ready).height(44_dp).grow().justify(Justify::center))
                .gap(8_dp).fill()
        ).gap(8_dp).fill().fill_height().justify(Justify::center))
            .padding(6_dp).background(Color::background);
    }
};

PXA_APPLICATION(Counter)
