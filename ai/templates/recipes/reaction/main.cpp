#include <pxa/app.hpp>
#include <pxa/ui_environment.hpp>
#include <pxa/ui_layout.hpp>

using namespace pxa;
using namespace pxa::ui;
using namespace pxa::ui::literals;

#ifdef PXA_RECIPE_REACTION_SECONDS
static_assert(PXA_RECIPE_REACTION_SECONDS >= 1 && PXA_RECIPE_REACTION_SECONDS <= 300);
#endif

struct Reaction {
    enum class Phase { idle, running, paused, won, lost };
    Environment<> environment;
    Context* context = nullptr;
    input::Controller controller;
    State<bool> ready{true}, playing{false}, compact{false};
    State<int> focus{0};
    State<bool> focused[4]{State<bool>{true}, State<bool>{false}, State<bool>{false}, State<bool>{false}};
    State<std::string> status{"准备"}, score_text{"0 / 10"}, time_text{"30秒"}, action_text{"开始"};
    State<std::string> labels[4]{State<std::string>{"·"}, State<std::string>{"·"},
                                  State<std::string>{"·"}, State<std::string>{"·"}};
    Phase phase = Phase::idle;
    bool foreground = true;
    int score = 0, target = 0;
    std::uint32_t random = 1;
    std::uint64_t remaining = duration(), deadline = 0, last_sample = 0, generation = 0;
    static constexpr std::uint64_t duration() {
#ifdef PXA_RECIPE_REACTION_SECONDS
        return std::uint64_t(PXA_RECIPE_REACTION_SECONDS) * 1000000;
#else
        return 30000000;
#endif
    }
    void select(int index) {
        focus.set(index);
        for (int i = 0; i < 4; ++i) focused[i].set(i == index);
    }
    void update_layout() {
        const auto& metrics = environment.display().get();
        const auto rect = safe_rectangle(metrics, GestureAvoidance::bottom);
        compact.set(std::uint64_t(rect.height) * 65536 < std::uint64_t(metrics.density_q16) * 170);
    }
    void refresh() {
        score_text.set(std::to_string(score) + " / 10");
        time_text.set(std::to_string((remaining + 999999) / 1000000) + "秒");
        playing.set(phase == Phase::running && ready.get());
        action_text.set(phase == Phase::running ? "暂停" : phase == Phase::paused ? "继续" : "开始");
        for (int i = 0; i < 4; ++i) labels[i].set(phase == Phase::running && i == target ? "点我" : "·");
    }
    void finish(Phase result, const char* message) {
        ++generation;
        (void)context->clock().set_period(0);
        phase = result; ready.set(true); status.set(message); refresh();
    }
    void sample(std::uint64_t now) {
        if (now < last_sample) return;
        last_sample = now;
        remaining = now >= deadline ? 0 : deadline - now;
        if (!remaining) finish(Phase::lost, "时间到");
        else refresh();
    }
    void next_target() {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        target = (target + 1 + int(random % 3)) % 4;
    }
    Task<void> begin(std::uint64_t requested) {
        auto now = co_await context->clock().now();
        if (requested != generation || !foreground) co_return Result<void>{};
        if (!now) { on_error(*context, now.error()); co_return Result<void>{}; }
        if (phase != Phase::paused) {
            score = 0; remaining = duration(); select(0);
            random = std::uint32_t(*now) | 1; next_target();
        }
        deadline = *now + remaining; last_sample = *now;
        auto tick = context->clock().set_period(100);
        if (!tick) { on_error(*context, tick.error()); co_return Result<void>{}; }
        phase = Phase::running; ready.set(true); status.set("进行中"); refresh();
        co_return Result<void>{};
    }
    void pause() {
        ++generation; (void)context->clock().set_period(0);
        if (phase == Phase::running) { phase = Phase::paused; status.set("已暂停"); }
        ready.set(true); refresh();
    }
    void toggle() {
        if (!ready.get() || !foreground) return;
        if (phase == Phase::running) { pause(); return; }
        ready.set(false); refresh();
        if (auto started = context->tasks().start(begin(++generation)); !started) on_error(*context, started.error());
    }
    Task<void> hit(int index, std::uint64_t requested) {
        auto now = co_await context->clock().now();
        if (requested != generation || phase != Phase::running) co_return Result<void>{};
        if (!now) { on_error(*context, now.error()); co_return Result<void>{}; }
        sample(*now); // An input arriving after the deadline cannot win between ticks.
        if (phase != Phase::running) co_return Result<void>{};
        if (index != target) finish(Phase::lost, "点错了");
        else if (++score == 10) finish(Phase::won, "完成");
        else { next_target(); ready.set(true); refresh(); }
        co_return Result<void>{};
    }
    void choose(int index) {
        if (!ready.get() || phase != Phase::running || index < 0 || index >= 4) return;
        ready.set(false); refresh();
        if (auto started = context->tasks().start(hit(index, generation)); !started) on_error(*context, started.error());
    }
    void reset() {
        ++generation; (void)context->clock().set_period(0);
        phase = Phase::idle; score = 0; remaining = duration(); ready.set(true); select(0);
        status.set("准备"); refresh();
    }
    Result<void> on_start(Context& ctx, std::span<const std::byte> config) {
        context = &ctx;
        auto initialized = environment.initialize(config);
        if (!initialized) return initialized;
        update_layout();
        WindowConfig window; window.edge_to_edge = true;
        if (auto configured = ctx.window().configure(window); !configured) return configured;
        refresh(); return ctx.clock().set_period(0);
    }
    Result<bool> on_event(Context&, const Event& event) {
        auto updated = environment.update(event);
        if (!updated) return updated;
        if (*updated) { update_layout(); return true; }
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
        int index = focus.get();
        if (controller.pressed(input::controller_left) || controller.pressed(input::controller_right)) index ^= 1;
        if (controller.pressed(input::controller_up) || controller.pressed(input::controller_down)) index ^= 2;
        select(index);
        if (controller.pressed(input::controller_a)) {
            if (phase == Phase::running) choose(index); else toggle();
        }
        if (controller.pressed(input::controller_start)) toggle();
        if (controller.pressed(input::controller_x)) reset();
    }
    void on_background(Context&) { foreground = false; controller.reset(); pause(); }
    void on_foreground(Context&) { foreground = true; }
    void on_error(Context&, Error) { pause(); status.set("操作失败"); }
    auto tile(int index, Dp height) {
        return Row(Text(">").font(Font::caption).width(10_dp).visible(focused[index]),
            Button(labels[index]).on_click([this, index] { choose(index); })
                .enabled(playing).height(height).grow().justify(Justify::center)).gap(2_dp).grow();
    }
    auto layout(bool small) {
        const Dp tile_height{small ? 28 : 36}, action_height{small ? 28 : 32};
        return Column(
            Row(Text(@PXA_TITLE@).font(small ? Font::body : Font::title), Text(status).font(Font::caption)).fill().justify(Justify::space_between),
            Row(Text(score_text).font(small ? Font::caption : Font::body), Text(time_text).font(small ? Font::caption : Font::body)).fill().justify(Justify::space_between),
            Row(tile(0, tile_height), tile(1, tile_height)).gap(4_dp).fill(),
            Row(tile(2, tile_height), tile(3, tile_height)).gap(4_dp).fill(),
            Row(Button(action_text).on_click([this] { toggle(); }).enabled(ready).height(action_height).grow().justify(Justify::center),
                Button("重开").on_click([this] { reset(); }).height(action_height).grow().justify(Justify::center)).gap(4_dp).fill()
        ).gap(Dp{small ? 1 : 2}).padding(Dp{small ? 2 : 4}).fill().fill_height().justify(Justify::center);
    }
    auto view() {
        return SafeArea(environment.display(), When(compact,
            [this] { return layout(true); }, [this] { return layout(false); }).fill().fill_height())
            .background(Color::background);
    }
};

PXA_APPLICATION(Reaction)
