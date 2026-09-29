#pragma once

#include "ui.hpp"
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
#include "net.hpp"

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
                     std::span<const std::byte> payload = {}) noexcept {
        return Response(transport_, requests_, service, opcode, payload);
    }
    game::Service game() noexcept { return {transport_, requests_}; }
    WindowService window() noexcept { return {transport_, requests_}; }
    AssetService assets() noexcept { return {transport_, requests_}; }
    StorageService storage() noexcept { return {transport_, requests_}; }
    FilesystemService fs() noexcept { return {transport_, requests_}; }
    PermissionService permissions() noexcept { return {transport_, requests_}; }
    AudioService audio() noexcept { return {transport_, requests_}; }
    ClockService clock() noexcept { return {transport_, requests_}; }
    LogService log() noexcept { return LogService(transport_); }
    DeviceService device() noexcept { return {transport_, requests_}; }
    SensorService sensors() noexcept { return {transport_, requests_}; }
    NetService net() noexcept { return {transport_, requests_}; }
    bool foreground() const noexcept { return foreground_; }

private:
    template<class> friend class AppRuntime;
    Transport transport_;
    RequestTable requests_;
    TaskScope tasks_;
    TaskScope foreground_tasks_;
    bool foreground_ = true;
};

template<class App, bool HasView = requires(App& app) { app.view(); },
         bool HasNavigation = requires(App& app) { app.navigation(); }>
class UiSlot {
public:
    Result<void> mount(App&, Transport&) noexcept { return {}; }
    bool handle(const Event&) noexcept { return false; }
    Result<void> flush() noexcept { return {}; }
    void reset() noexcept {}
};

template<class App>
class UiSlot<App, true, false> {
    using View = decltype(std::declval<App&>().view());
public:
    Result<void> mount(App& app, Transport& transport) {
        page_.emplace(transport, app.view());
        auto result = page_->mount();
        if (!result) page_.reset();
        return result;
    }
    bool handle(const Event& event) noexcept {
        return page_ && page_->handle(event);
    }
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
    Result<void> mount(App& app, Transport& transport) noexcept {
        navigation_ = &app.navigation();
        return navigation_->attach(transport);
    }
    bool handle(const Event& event) noexcept {
        return navigation_ && navigation_->handle(event);
    }
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
        app_ = std::construct_at(reinterpret_cast<App*>(storage_));
        live_ = true;
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
            auto period = context_.clock().set_period(16);
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
            if (context_.foreground_) {
                if constexpr (requires(App& app, Context& ctx,
                                       game::FrameTick tick) {
                                  app.on_frame(ctx, tick);
                              }) {
                    stepper_.reset();
                    auto period = context_.clock().set_period(16);
                    if (!period) {
                        context_.transport_.phase(Phase::inactive);
                        return static_cast<std::int32_t>(period.error());
                    }
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
                }
                if constexpr (requires(App& app, Context& ctx) {
                                  app.on_background(ctx);
                              }) app_->on_background(context_);
            }
            auto flushed = flush_ui();
            context_.transport_.phase(Phase::inactive);
            return flushed ? 1 : static_cast<std::int32_t>(flushed.error());
        }
        if constexpr (requires(App& app, Context& ctx,
                               game::FrameTick tick) {
                          app.on_frame(ctx, tick);
                      }) {
        if (parsed->service == 4 && parsed->opcode == 0x8001 &&
            parsed->token == 0) {
            if (parsed->payload.size() != 8) {
                context_.transport_.phase(Phase::inactive);
                return static_cast<std::int32_t>(Error::protocol_error);
            }
            if (context_.foreground_) {
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
        if constexpr (requires(App& app, StopReason value) {
                          app.on_stop(value);
                      }) app_->on_stop(static_cast<StopReason>(reason));
        std::destroy_at(app_);
        app_ = nullptr;
        live_ = false;
    }

private:
    static Result<void> flush_ui() noexcept {
        auto result = ui_.flush();
        if (result || result.error() == Error::protocol_error) return result;
        if constexpr (requires(App& app, Context& ctx, Error error) {
                          app.on_error(ctx, error);
                      }) app_->on_error(context_, result.error());
        else std::fprintf(stderr, "PXA UI update failed: %d\n",
                          static_cast<int>(result.error()));
        return {};
    }

    inline static Context context_{};
    inline static UiSlot<App> ui_{};
    inline static FixedStepper stepper_{};
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
