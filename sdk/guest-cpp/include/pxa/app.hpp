#pragma once

#include "ui.hpp"
#include "input.hpp"
#include "ui_theme.hpp"
#include "navigation.hpp"
#include "task.hpp"
#include "game_service.hpp"
#include "window.hpp"
#include "assets.hpp"
#include "storage.hpp"
#include "fs.hpp"
#include "permission.hpp"
#include "audio.hpp"
#include "clock.hpp"
#include "log.hpp"
#include "device.hpp"
#include "sensor.hpp"
#include "haptics.hpp"
#include "net.hpp"
#include "http.hpp"
#include "websocket.hpp"
#include "audio_capture.hpp"
#include "ipc.hpp"
#include "work.hpp"
#include "surface.hpp"
#include "lease.hpp"
#include "resource.hpp"
#include "event_port.hpp"
#include "store_installer.hpp"

#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace pxa {

enum class StopReason : std::uint32_t {};

class Context {
public:
    Transport& transport() noexcept { return transport_; }
    TaskScope& tasks() noexcept { return tasks_; }
    TaskScope& foreground_tasks() noexcept { return foreground_tasks_; }
    Response request(std::uint16_t service, std::uint16_t opcode,
                     std::span<const std::byte> payload = {},
                     bool close_late_handle = false) noexcept {
        return Response(transport_, requests_, service, opcode, payload,
                        close_late_handle);
    }
    game::Service game() noexcept { return {transport_, requests_}; }
    WindowService window() noexcept { return {transport_, requests_}; }
    AssetService assets() noexcept { return {transport_, requests_}; }
    StorageService storage() noexcept { return {transport_, requests_}; }
    FilesystemService fs() noexcept { return {transport_, requests_}; }
    PermissionService permissions() noexcept { return {transport_, requests_}; }
    AudioService audio() noexcept { return {transport_, requests_}; }
    ui::ThemeService theme() noexcept { return {transport_, requests_}; }
    ClockService clock() noexcept { return {transport_, requests_}; }
    LogService log() noexcept { return LogService(transport_); }
    DeviceService device() noexcept { return {transport_, requests_}; }
    SensorService sensors() noexcept { return {transport_, requests_}; }
    HapticsService haptics() noexcept { return {transport_, requests_}; }
    NetService net() noexcept { return {transport_, requests_}; }
    HttpClient http() noexcept { return {transport_, requests_}; }
    SocketClient sockets() noexcept { return {transport_, requests_}; }
    WebSocketClient websockets() noexcept { return {transport_, requests_}; }
    AudioCaptureService capture() noexcept { return {transport_, requests_}; }
    IpcService ipc() noexcept { return {transport_, requests_}; }
    WorkService work() noexcept { return {transport_, requests_}; }
    SurfaceService surface() noexcept { return {transport_, requests_}; }
    LeaseService leases() noexcept { return {transport_, requests_}; }
    ResourceService resources() noexcept { return {transport_, requests_}; }
    EventPortService event_ports() noexcept { return {transport_, requests_}; }
    StoreInstallerService store_installer() noexcept { return {transport_, requests_}; }
    bool foreground() const noexcept { return foreground_; }

private:
    template<class> friend class AppRuntime;
    Transport transport_;
    RequestTable requests_;
    TaskScope tasks_;
    TaskScope foreground_tasks_;
    bool foreground_ = true;
};

namespace detail {
// Detection is compile-time: apps without input callbacks add no input page.
template<class App> struct AppInput {
    static constexpr bool controller = requires(App& app, Context& ctx, const input::ControllerState& e) {
        app.on_controller(ctx, e);
    };
    static constexpr bool pointer = requires(App& app, Context& ctx, const input::Pointer& e) {
        app.on_pointer(ctx, e);
    };
    static constexpr bool volume = requires(App& app, Context& ctx, const input::VolumeKeyEvent& e) {
        app.on_volume_key(ctx, e);
    };
    static constexpr std::uint64_t mask = [] {
        std::uint64_t value = 0;
        if constexpr (controller) {
            static_assert(requires(App& app, Context& ctx, const input::ControllerState& e) {
                { app.on_controller(ctx, e) } -> std::same_as<void>;
            }, "on_controller must return void; start asynchronous work explicitly through Context tasks");
            value |= ui::protocol::event_mask_controller;
        }
        if constexpr (pointer) {
            static_assert(requires(App& app, Context& ctx, const input::Pointer& e) {
                { app.on_pointer(ctx, e) } -> std::same_as<void>;
            }, "on_pointer must return void; start asynchronous work explicitly through Context tasks");
            value |= ui::protocol::event_mask_pointer;
        }
        if constexpr (volume) {
            static_assert(requires(App& app, Context& ctx, const input::VolumeKeyEvent& e) {
                { app.on_volume_key(ctx, e) } -> std::same_as<void>;
            }, "on_volume_key must return void; start asynchronous work explicitly through Context tasks");
            value |= UINT64_C(1) << 4;
        }
        return value;
    }();
};

} // namespace detail

template<class App, bool HasView = requires(App& app) { app.view(); },
         bool HasNavigation = requires(App& app) { app.navigation(); }>
class UiSlot {
public:
    static constexpr bool has_gestures = false;
    Result<void> mount(App&, Transport& transport) noexcept {
        if constexpr (detail::AppInput<App>::mask != 0) {
            // Pure input uses only a root and a generation, never a UI Page.
            const auto generation = transport.next_ui_generation();
            if (!generation) return std::unexpected(Error::limit_exceeded);
            ui::Transaction tx(transport, generation, 1, ui::protocol::replace_surface);
            if (!tx.create(1, 0, ui::protocol::root) ||
                !tx.u8(1, ui::protocol::layout, ui::protocol::column) ||
                !tx.rgba(1, ui::protocol::background, 0) ||
                !tx.u64(1, ui::protocol::event_mask, detail::AppInput<App>::mask))
                return std::unexpected(tx.error());
            auto result = tx.commit();
            if (!result) return result;
            generation_ = generation;
        }
        return {};
    }
    bool handle(const Event&) noexcept { return false; }
    std::uint32_t generation() const noexcept {
        if constexpr (detail::AppInput<App>::mask != 0) return generation_;
        else return 0;
    }
    Result<void> flush() noexcept { return {}; }
    void reset() noexcept {
        if constexpr (detail::AppInput<App>::mask != 0) generation_ = 0;
    }
private:
    struct NoInput {};
    [[no_unique_address]] std::conditional_t<detail::AppInput<App>::mask != 0,
        std::uint32_t, NoInput> generation_{};
};

template<class App>
class UiSlot<App, true, false> {
    using View = decltype(std::declval<App&>().view());
public:
    static constexpr bool has_gestures = ui::capacity_of<View>.gestures > 0;
    void gesture_tick(std::uint64_t now) { if(page_)page_->gesture_tick(now); }
    std::uint64_t gesture_deadline() const noexcept { return page_?page_->gesture_deadline():0; }
    void gesture_cancel() noexcept { if(page_)page_->gesture_cancel(); }
    Result<void> mount(App& app, Transport& transport) {
        page_.emplace(transport, app.view(), detail::AppInput<App>::mask);
        auto result = page_->mount();
        if (!result) page_.reset();
        return result;
    }
    bool handle(const Event& event) noexcept {
        return page_ && page_->handle(event);
    }
    std::uint32_t generation() const noexcept { return page_ ? page_->generation() : 0; }
    Result<void> flush() noexcept {
        return page_ ? page_->flush() : Result<void>{};
    }
    void reset() noexcept { page_.reset(); }
private:
    std::optional<ui::Page<View>> page_;
};

template<class App, bool HasView>
class UiSlot<App, HasView, true> {
    using Navigation = std::remove_reference_t<decltype(std::declval<App&>().navigation())>;
public:
    static constexpr bool has_gestures = true; // Navigator routes are selected at run time.
    void gesture_tick(std::uint64_t now) { if(navigation_)navigation_->gesture_tick(now); }
    std::uint64_t gesture_deadline() const noexcept { return navigation_?navigation_->gesture_deadline():0; }
    void gesture_cancel() noexcept { if(navigation_)navigation_->gesture_cancel(); }
    Result<void> mount(App& app, Transport& transport) noexcept {
        navigation_ = &app.navigation();
        if constexpr (detail::AppInput<App>::mask != 0)
            return navigation_->attach(transport, detail::AppInput<App>::mask);
        else return navigation_->attach(transport);
    }
    bool handle(const Event& event) noexcept {
        return navigation_ && navigation_->handle(event);
    }
    std::uint32_t generation() const noexcept { return navigation_ ? navigation_->generation() : 0; }
    Result<void> flush() noexcept {
        return navigation_ ? navigation_->flush() : Result<void>{};
    }
    void reset() noexcept {
        if (navigation_) navigation_->reset();
        navigation_ = nullptr;
    }
private:
    Navigation* navigation_ = nullptr;
};

template<class App>
class AppRuntime {
public:
    static std::int32_t start(const std::uint8_t* config,
                              std::uint32_t length) noexcept {
        if (live_ || (config == nullptr && length != 0))
            return static_cast<std::int32_t>(Error::bad_state);
        context_.transport_.phase(Phase::start);
        context_.foreground_ = true;
        if constexpr(UiSlot<App>::has_gestures) gesture_period_=0;
        app_ = std::construct_at(reinterpret_cast<App*>(storage_));
        live_ = true;
        context_.tasks_.on_error(nullptr, nullptr);
        context_.foreground_tasks_.on_error(nullptr, nullptr);
        if constexpr (requires(App& app, Context& ctx, Error error) {
                          app.on_error(ctx, error);
                      }) {
            auto report = [](void*, Error error) noexcept {
                app_->on_error(context_, error);
            };
            context_.tasks_.on_error(nullptr, report);
            context_.foreground_tasks_.on_error(nullptr, report);
        }
        if constexpr (requires(App& app, Context& ctx,
                               std::span<const std::byte> bytes) {
                          { app.on_start(ctx, bytes) } ->
                              std::same_as<Result<void>>;
                      }) {
            auto result = app_->on_start(
                context_, {reinterpret_cast<const std::byte*>(config), length});
            if (!result) {
                auto error = result.error();
                context_.transport_.phase(Phase::stopped);
                context_.tasks_.cancel();
                context_.foreground_tasks_.cancel();
                context_.requests_.clear();
                std::destroy_at(app_);
                app_ = nullptr;
                live_ = false;
                return static_cast<std::int32_t>(error);
            }
        } else if constexpr (requires(App& app, Context& ctx) {
                                 { app.on_start(ctx) } ->
                                     std::same_as<Result<void>>;
                             }) {
            auto result = app_->on_start(context_);
            if (!result) {
                auto error = result.error();
                context_.transport_.phase(Phase::stopped);
                context_.tasks_.cancel();
                context_.foreground_tasks_.cancel();
                context_.requests_.clear();
                std::destroy_at(app_);
                app_ = nullptr;
                live_ = false;
                return static_cast<std::int32_t>(error);
            }
        }
        auto mounted = ui_.mount(*app_, context_.transport_);
        if (!mounted) {
            auto error = mounted.error();
            context_.transport_.phase(Phase::stopped);
            context_.tasks_.cancel();
            context_.foreground_tasks_.cancel();
            context_.requests_.clear();
            ui_.reset();
            std::destroy_at(app_);
            app_ = nullptr;
            live_ = false;
            return static_cast<std::int32_t>(error);
        }
        if constexpr (requires(App& app, Context& ctx,
                               game::FrameTick tick) {
                          app.on_frame(ctx, tick);
                      }) {
            auto period = context_.clock().set_period(loop_options_.frame_period_ms);
            if (!period) {
                auto error = period.error();
                context_.transport_.phase(Phase::stopped);
                context_.tasks_.cancel();
                context_.foreground_tasks_.cancel();
                context_.requests_.clear();
                ui_.reset();
                std::destroy_at(app_);
                app_ = nullptr;
                live_ = false;
                return static_cast<std::int32_t>(error);
            }
            stepper_.reset();
            if constexpr(UiSlot<App>::has_gestures)gesture_period_=loop_options_.frame_period_ms;
        }
        context_.transport_.phase(Phase::inactive);
        return 0;
    }

    static std::int32_t event(const std::uint8_t* bytes,
                              std::uint32_t length) noexcept {
        if (!live_ || (bytes == nullptr && length != 0))
            return static_cast<std::int32_t>(Error::bad_state);
        auto parsed = parse_event(
            {reinterpret_cast<const std::byte*>(bytes), length});
        if (!parsed) return static_cast<std::int32_t>(parsed.error());
        context_.transport_.phase(Phase::event);
        if (parsed->service == 17 && parsed->opcode == 0x8005 &&
            parsed->token == 0 && parsed->payload.size() == 1) {
            auto state = std::to_integer<unsigned>(parsed->payload[0]);
            if (state > 1) {
                context_.transport_.phase(Phase::inactive);
                return static_cast<std::int32_t>(Error::protocol_error);
            }
            context_.foreground_ = state != 0;
            if constexpr(UiSlot<App>::has_gestures)ui_.gesture_cancel();
            if (context_.foreground_) {
                if constexpr (requires(App& app, Context& ctx,
                                       game::FrameTick tick) {
                                  app.on_frame(ctx, tick);
                              }) {
                    stepper_.reset();
                    auto period = context_.clock().set_period(loop_options_.frame_period_ms);
                    if (!period) {
                        context_.transport_.phase(Phase::inactive);
                        return static_cast<std::int32_t>(period.error());
                    }
                    if constexpr(UiSlot<App>::has_gestures)gesture_period_=loop_options_.frame_period_ms;
                }
                if constexpr (requires(App& app, Context& ctx) {
                                  app.on_foreground(ctx);
                              }) app_->on_foreground(context_);
            } else {
                context_.foreground_tasks_.cancel();
                if constexpr (requires(App& app, Context& ctx,
                                       game::FrameTick tick) {
                                  app.on_frame(ctx, tick);
                              }) {
                    stepper_.reset();
                    auto period = context_.clock().set_period(0);
                    if (!period) {
                        context_.transport_.phase(Phase::inactive);
                        return static_cast<std::int32_t>(period.error());
                    }
                    if constexpr(UiSlot<App>::has_gestures)gesture_period_=0;
                }
                if constexpr (requires(App& app, Context& ctx) {
                                  app.on_background(ctx);
                              }) app_->on_background(context_);
            }
            auto flushed = flush_ui();
            context_.transport_.phase(Phase::inactive);
            return flushed ? 1 : static_cast<std::int32_t>(flushed.error());
        }
        if constexpr (has_frame_ || UiSlot<App>::has_gestures) {
        if (parsed->service == 4 && parsed->opcode == 0x8001 &&
            parsed->token == 0) {
            if (parsed->payload.size() != 8) {
                context_.transport_.phase(Phase::inactive);
                return static_cast<std::int32_t>(Error::protocol_error);
            }
            if (context_.foreground_) {
                if constexpr(UiSlot<App>::has_gestures)
                    ui_.gesture_tick(wire::get64(parsed->payload.data()));
                if constexpr (requires(App& app, Context& ctx,
                                       game::FrameTick tick) {
                                  app.on_frame(ctx, tick);
                              }) {
                    auto timestamp = wire::get64(parsed->payload.data());
                    auto steps = stepper_.advance(timestamp);
                    for (std::uint8_t i = 0; i < steps.count; ++i) {
                        if constexpr (requires(App& app, Context& ctx,
                                               std::uint32_t delta_us) {
                                          app.on_update(ctx, delta_us);
                                      }) app_->on_update(context_, steps.step_us);
                    }
                    app_->on_frame(context_, {timestamp, steps.step_us,
                                              steps.frame_delta_us,
                                              steps.count});
                }
            }
            auto flushed = flush_ui();
            context_.transport_.phase(Phase::inactive);
            return flushed ? 1 : static_cast<std::int32_t>(flushed.error());
        }
        }
        if (parsed->service == 2 && parsed->opcode == 0x8001 &&
            parsed->token == 0) {
            auto metrics = decode_window_metrics(parsed->payload);
            if (!metrics) {
                context_.transport_.phase(Phase::inactive);
                return static_cast<std::int32_t>(metrics.error());
            }
            if constexpr (requires(App& app, const WindowMetrics& value) {
                              app.on_window_changed(value);
                          }) app_->on_window_changed(*metrics);
            auto flushed = flush_ui();
            context_.transport_.phase(Phase::inactive);
            return flushed ? 1 : static_cast<std::int32_t>(flushed.error());
        }
        if (parsed->service == 2 && parsed->opcode == 0x8002 &&
            parsed->token == 0) {
            if (!parsed->payload.empty()) {
                context_.transport_.phase(Phase::inactive);
                return static_cast<std::int32_t>(Error::protocol_error);
            }
            BackAction action = BackAction::close;
            if constexpr (requires(App& app) {
                              { app.on_back() } -> std::same_as<BackAction>;
                          }) action = app_->on_back();
            auto flushed = flush_ui();
            context_.transport_.phase(Phase::inactive);
            return flushed ? static_cast<std::int32_t>(action)
                           : static_cast<std::int32_t>(flushed.error());
        }
        std::int32_t result = context_.requests_.dispatch(*parsed) ? 1 : 0;
        context_.tasks_.reap();
        context_.foreground_tasks_.reap();
        if (result == 0) {
            auto input = handle_input(*parsed);
            if (!input) result = static_cast<std::int32_t>(input.error());
            else if (*input) result = 1;
        }
        if (result == 0 && ui_.handle(*parsed)) result = 1;
        if constexpr (requires(App& app, Context& ctx, const Event& value) {
                          { app.on_event(ctx, value) } ->
                              std::same_as<Result<bool>>;
                      }) {
            if (result == 0) {
                auto handled = app_->on_event(context_, *parsed);
                result = handled ? static_cast<std::int32_t>(*handled)
                                 : static_cast<std::int32_t>(handled.error());
            }
        }
        auto flushed = flush_ui();
        if (!flushed && result >= 0)
            result = static_cast<std::int32_t>(flushed.error());
        context_.transport_.phase(Phase::inactive);
        return result;
    }

    static void stop(std::uint32_t reason) noexcept {
        if (!live_) return;
        context_.transport_.phase(Phase::stopped);
        context_.tasks_.cancel();
        context_.foreground_tasks_.cancel();
        context_.requests_.clear();
        ui_.reset();
        context_.tasks_.on_error(nullptr, nullptr);
        context_.foreground_tasks_.on_error(nullptr, nullptr);
        if constexpr (requires(App& app, StopReason value) {
                          app.on_stop(value);
                      }) app_->on_stop(static_cast<StopReason>(reason));
        std::destroy_at(app_);
        app_ = nullptr;
        live_ = false;
    }

private:
    static Result<bool> handle_input(const Event& event) noexcept {
        using Inputs = detail::AppInput<App>;
        if constexpr (Inputs::mask != 0) {
            if (event.service != ui::protocol::service || event.opcode != 0x8001 || event.token ||
                event.payload.size() < 24 || wire::get32(event.payload.data()) != 1 ||
                wire::get32(event.payload.data() + 4) != 1 ||
                wire::get32(event.payload.data() + 8) != ui_.generation()) return false;
            const auto kind = wire::get16(event.payload.data() + 12);
            if constexpr (Inputs::controller) {
                if (kind == ui::protocol::event_controller_kind) {
                    auto state = input::decode_controller(event);
                    if (!state) return std::unexpected(state.error());
                    if (context_.foreground()) app_->on_controller(context_, *state);
                    return true;
                }
            }
            if constexpr (Inputs::pointer) {
                if (kind == ui::protocol::event_pointer_kind) {
                    auto pointer = input::decode_pointer(event);
                    if (!pointer) return std::unexpected(pointer.error());
                    if (context_.foreground()) app_->on_pointer(context_, *pointer);
                    return true;
                }
            }
            if constexpr (Inputs::volume) {
                if (kind == 5) {
                    auto key = input::decode_volume_key(event);
                    if (!key) return std::unexpected(key.error());
                    if (context_.foreground()) app_->on_volume_key(context_, *key);
                    return true;
                }
            }
        }
        return false;
    }

    static Result<void> flush_ui() noexcept {
        auto result = ui_.flush();
        if constexpr(UiSlot<App>::has_gestures) {
            const std::uint16_t base=context_.foreground_ && has_frame_?loop_options_.frame_period_ms:0;
            const std::uint16_t period=context_.foreground_ && ui_.gesture_deadline()
                ? (base?std::min<std::uint16_t>(base,16):16) : base;
            if(period!=gesture_period_) {
                auto changed=context_.clock().set_period(period);
                if(!changed)return changed;
                gesture_period_=period;
            }
        }
        if (result || result.error() == Error::protocol_error) return result;
        if constexpr (requires(App& app, Context& ctx, Error error) {
                          app.on_error(ctx, error);
                      }) app_->on_error(context_, result.error());
        else detail::report_error("PXA UI update failed: ", result.error());
        return {};
    }

    inline static Context context_{};
    static constexpr bool has_frame_=requires(App& app,Context& ctx,game::FrameTick tick){app.on_frame(ctx,tick);};
    inline static std::uint16_t gesture_period_=0;
    inline static UiSlot<App> ui_{};
    static constexpr game::LoopOptions loop_options_ = [] {
        if constexpr (requires { App::loop_options; }) return App::loop_options;
        else return game::LoopOptions{};
    }();
    static_assert(loop_options_.simulation_step_us >= 1000 &&
                  loop_options_.simulation_step_us <= 1000000 &&
                  loop_options_.frame_period_ms >= 16 &&
                  loop_options_.frame_period_ms <= 1000 &&
                  loop_options_.maximum_updates >= 1 &&
                  loop_options_.maximum_updates <= 16);
    inline static FixedStepper stepper_{loop_options_.simulation_step_us,
                                       loop_options_.maximum_updates};
    alignas(App) inline static std::byte storage_[sizeof(App)]{};
    inline static App* app_ = nullptr;
    inline static bool live_ = false;
};

} // namespace pxa

#define PXA_APPLICATION(AppType)                                         \
    extern "C" std::int32_t pxa_app_start(const std::uint8_t* config,       \
                                            std::uint32_t length) {         \
        return ::pxa::AppRuntime<AppType>::start(config, length);           \
    }                                                                       \
    extern "C" std::int32_t pxa_app_on_event(const std::uint8_t* bytes,    \
                                               std::uint32_t length) {      \
        return ::pxa::AppRuntime<AppType>::event(bytes, length);            \
    }                                                                       \
    extern "C" void pxa_app_stop(std::uint32_t reason) {                  \
        ::pxa::AppRuntime<AppType>::stop(reason);                           \
    }

#define PXA_SERVICE(ServiceType) PXA_APPLICATION(ServiceType)
#define PXA_JOB(JobType) PXA_APPLICATION(JobType)
#define PXA_GAME(GameType) PXA_APPLICATION(GameType)
