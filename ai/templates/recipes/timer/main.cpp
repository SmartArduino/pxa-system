#include <pxa/app.hpp>
#include <pxa/ui_environment.hpp>
#include <pxa/ui_layout.hpp>

using namespace pxa;
using namespace pxa::ui;
using namespace pxa::ui::literals;

#ifdef PXA_RECIPE_TIMER_SECONDS
static_assert(PXA_RECIPE_TIMER_SECONDS >= 1 && PXA_RECIPE_TIMER_SECONDS <= 86400);
#endif

struct Timer {
    enum class Phase { idle, running, paused, done };
    Environment<> environment;
    Context* context = nullptr;
    input::Controller controller;
    State<bool> ready{false};
    State<int> progress{0};
    State<std::string> time_text{"--:--"}, phase_text{"读取存档"}, start_text{"开始"};
    State<std::string> mode_text{"短休"}, storage_status{"读取存档"};
    Phase phase = Phase::idle;
    bool rest = false, foreground = true, writable = true, saving = false, dirty = false;
    std::uint64_t remaining = 0, deadline = 0, last_sample = 0, control_generation = 0;
#ifdef PXA_RECIPE_TIMER_SECONDS
    static constexpr auto save_key = "timer.test.snapshot.v1";
#else
    static constexpr auto save_key = "timer.snapshot.v1";
#endif

    std::uint64_t duration() const noexcept {
#ifdef PXA_RECIPE_TIMER_SECONDS
        return std::uint64_t(PXA_RECIPE_TIMER_SECONDS) * 1000000;
#else
        return std::uint64_t(rest ? @PXA_BREAK_MINUTES@ : @PXA_MINUTES@) * 60 * 1000000;
#endif
    }
    void update_labels() {
        auto seconds = (remaining + 999999) / 1000000;
        const auto tail = seconds % 60;
        time_text.set(std::to_string(seconds / 60) + (tail < 10 ? ":0" : ":") + std::to_string(tail));
        progress.set(int((duration() - remaining) * 100 / duration()));
        start_text.set(phase == Phase::running ? "暂停" : "开始");
        mode_text.set(rest ? "专注" : "短休");
        phase_text.set(phase == Phase::running ? (rest ? "短休中" : "专注中") :
                       phase == Phase::paused ? "已暂停" : phase == Phase::done ? "已完成" :
                       rest ? "就绪 · 短休" : "就绪 · 专注");
    }
    Task<void> load() {
        remaining = duration();
        auto stored = co_await context->storage().get_value<std::uint64_t>(save_key);
        if (stored) {
            rest = (*stored >> 63) != 0;
            auto value = *stored & ~(std::uint64_t{1} << 63);
            if (value <= duration()) {
                remaining = value;
                phase = value == 0 ? Phase::done : value == duration() ? Phase::idle : Phase::paused;
                storage_status.set("已载入");
            } else {
                remaining = duration(); writable = false; storage_status.set("存档不匹配");
            }
        } else if (stored.error() == Error::not_found) storage_status.set("新存档");
        else { writable = false; storage_status.set("仅本次会话"); }
        update_labels(); ready.set(true);
        co_return Result<void>{};
    }
    Task<void> save() {
        while (dirty) {
            dirty = false;
            const auto value = remaining | (std::uint64_t(rest) << 63);
            auto result = co_await context->storage().set_value(save_key, value);
            if (!result) { saving = false; storage_status.set("保存失败"); co_return Result<void>{}; }
        }
        saving = false; storage_status.set("已保存");
        co_return Result<void>{};
    }
    void checkpoint() {
        if (!writable) return;
        dirty = true; storage_status.set("保存中");
        if (saving) return;
        saving = true;
        if (auto result = context->tasks().start(save()); !result) {
            saving = false; storage_status.set("保存失败");
        }
    }
    void sample(std::uint64_t now) {
        if (now < last_sample) return; // Ignore an older coalesced tick after resume.
        last_sample = now;
        remaining = now >= deadline ? 0 : deadline - now;
        if (!remaining) {
            phase = Phase::done;
            if (auto stopped = context->clock().set_period(0); !stopped) on_error(*context, stopped.error());
            checkpoint();
        }
        update_labels();
    }
    Task<void> control(bool run, std::uint64_t generation) {
        auto now = co_await context->clock().now();
        if (generation != control_generation) co_return Result<void>{};
        if (!now) { on_error(*context, now.error()); co_return Result<void>{}; }
        if (phase == Phase::running) sample(*now);
        if (run && foreground) {
            if (!remaining) remaining = duration();
            deadline = *now + remaining;
            last_sample = *now;
            auto configured = context->clock().set_period(1000);
            if (!configured) { on_error(*context, configured.error()); co_return Result<void>{}; }
            phase = Phase::running;
        } else {
            auto stopped = context->clock().set_period(0);
            if (!stopped) { on_error(*context, stopped.error()); co_return Result<void>{}; }
            if (phase != Phase::done) phase = Phase::paused;
        }
        update_labels(); ready.set(true); checkpoint();
        co_return Result<void>{};
    }
    void request(bool run) {
        if (!ready.get()) return;
        ready.set(false);
        if (auto result = context->tasks().start(control(run, ++control_generation)); !result)
            on_error(*context, result.error());
    }
    void reset(bool change_mode = false) {
        if (!ready.get()) return;
        auto stopped = context->clock().set_period(0);
        if (!stopped) { on_error(*context, stopped.error()); return; }
        if (change_mode) rest = !rest;
        remaining = duration(); phase = Phase::idle;
        update_labels(); checkpoint();
    }
    Result<void> on_start(Context& ctx, std::span<const std::byte> config) {
        context = &ctx;
        auto result = environment.initialize(config);
        if (!result) return result;
        WindowConfig window;
        window.edge_to_edge = true;
        if (auto configured = ctx.window().configure(window); !configured) return configured;
        if (auto stopped = ctx.clock().set_period(0); !stopped) return stopped;
        return ctx.tasks().start(load());
    }
    Result<bool> on_event(Context&, const Event& event) {
        auto updated = environment.update(event);
        if (!updated || *updated) return updated;
        if (event.service == 4 && event.opcode == 0x8001) {
            auto tick = decode_clock_tick(event);
            if (!tick) return std::unexpected(tick.error());
            if (phase == Phase::running) sample(tick->timestamp_us);
            return true;
        }
        return false;
    }
    void on_controller(Context&, const input::ControllerState& state) {
        if (!controller.update(state)) return;
        if (controller.pressed(input::controller_a)) request(phase != Phase::running);
        if (controller.pressed(input::controller_x)) reset();
        if (controller.pressed(input::controller_y)) reset(true);
    }
    void on_foreground(Context&) { foreground = true; }
    void on_background(Context&) {
        foreground = false; controller.reset();
        ++control_generation; // A pending Clock.now must not restart us on resume.
        ready.set(true);
        if (phase != Phase::running) return;
        // Background Hosts may suspend completion delivery. Freeze the last
        // displayed sample synchronously, then save the checkpoint if possible.
        auto stopped = context->clock().set_period(0);
        if (!stopped) { on_error(*context, stopped.error()); return; }
        phase = Phase::paused;
        update_labels(); checkpoint();
    }
    void on_error(Context&, Error) {
        (void)context->clock().set_period(0);
        phase = Phase::paused; ready.set(true);
        update_labels(); storage_status.set("操作失败");
    }
    auto view() {
        return SafeArea(environment.display(), Column(
            Row(Text(@PXA_TITLE@).font(Font::title), Text(storage_status).font(Font::caption))
                .justify(Justify::space_between).fill(),
            Text(time_text).font(Font::headline).fill().text_align(TextAlign::center),
            Text(phase_text).font(Font::caption).fill().text_align(TextAlign::center),
            Progress(progress).height(4_dp).fill(),
            Row(Button(start_text).on_click([this] { request(phase != Phase::running); })
                    .enabled(ready).height(44_dp).grow().justify(Justify::center),
                Button("重置").on_click([this] { reset(); }).enabled(ready).height(44_dp).grow().justify(Justify::center),
                Button(mode_text).on_click([this] { reset(true); }).enabled(ready).height(44_dp).grow().justify(Justify::center))
                .gap(6_dp).fill()
        ).gap(4_dp).fill().fill_height().justify(Justify::center))
            .padding(6_dp).background(Color::background);
    }
};

PXA_APPLICATION(Timer)
