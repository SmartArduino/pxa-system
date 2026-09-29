#include <pxa/app.hpp>
#include <charconv>

using namespace pxa::ui;
using namespace pxa::ui::literals;

struct DeviceSensorApp {
    State<std::string> target{"Loading"}, engine{""}, status{"Discovering sensors"};
    State<int> sample_value{0};
    std::array<pxa::SensorDescriptor, 4> descriptors{};
    pxa::SensorSubscription subscription;
    pxa::Permission permission;
    pxa::Context* context = nullptr;
    std::size_t sensor_count = 0;
    bool busy = true, monitoring = false;

    pxa::Task<void> discover() {
        auto info = co_await context->device().runtime_info();
        if (info) {
            target.set(std::string(info->target.view()));
            engine.set(std::string(info->engine_abi.view()));
        } else target.set("Device information unavailable");
        auto count = co_await context->sensors().list(descriptors);
        if (count) {
            sensor_count = *count;
            status.set(sensor_count ? std::string(descriptors[0].semantic.view()) : "No sensors");
        } else {
            sensor_count = 0;
            status.set(count.error() == pxa::Error::resource_limit ?
                "Sensor catalog exceeds capacity" : "Sensors unavailable");
        }
        busy = false;
        co_return pxa::Result<void>{};
    }
    pxa::Task<void> monitor(pxa::SensorDescriptor descriptor) {
        auto grant = co_await context->permissions().acquire(
            "sensor.read", descriptor.semantic.view());
        if (!grant) {
            std::array<char, 32> error{};
            constexpr std::string_view prefix = "Permission error ";
            prefix.copy(error.data(), prefix.size());
            auto formatted = std::to_chars(error.data() + prefix.size(), error.data() + error.size(),
                                          static_cast<int>(grant.error()));
            status.set(std::string(error.data(), formatted.ptr));
            busy = false;
            co_return pxa::Result<void>{};
        }
        permission = std::move(*grant);
        auto started = co_await context->sensors().subscribe(
            descriptor, descriptor.min_period_ms, permission);
        if (started) {
            subscription = std::move(*started);
            status.set(std::string(descriptor.semantic.view()));
        } else {
            permission.reset();
            status.set("Subscription failed");
        }
        busy = false;
        co_return pxa::Result<void>{};
    }
    void begin_monitoring() {
        if (busy || !sensor_count || subscription) return;
        busy = true;
        monitoring = true;
        auto started = context->foreground_tasks().start(monitor(descriptors[0]));
        if (!started) { busy = false; status.set("Task capacity exceeded"); }
    }
    pxa::Result<void> on_start(pxa::Context& ctx) {
        context = &ctx;
        return ctx.tasks().start(discover());
    }
    void on_background(pxa::Context&) {
        subscription.reset();
        permission.reset();
        busy = false;
    }
    void on_foreground(pxa::Context&) {
        if (monitoring) begin_monitoring();
    }
    pxa::Result<bool> on_event(pxa::Context&, const pxa::Event& event) {
        if (event.service == 8 && event.opcode == 0x8001) {
            auto sample = subscription.sample(event);
            if (sample) sample_value.set(sample->values[0]);
            else if (sample.error() != pxa::Error::not_found)
                return std::unexpected(sample.error());
            return true;
        }
        if (event.service == 11 && event.opcode == 0x8001) {
            auto revoked = pxa::decode_permission_revoked(event);
            if (!revoked) return std::unexpected(revoked.error());
            if (revoked->name == "sensor.read") {
                context->foreground_tasks().cancel();
                subscription.reset(); permission.reset();
                monitoring = false; busy = false;
                status.set("Sensor permission revoked");
            }
            return true;
        }
        return false;
    }
    auto view() {
        return Column(
            Text<"Device and Sensor">().font(Font::title),
            Text(target), Text(engine), Text(status), Text(sample_value),
            Row(
                Button("Monitor").on_click([this] { begin_monitoring(); }),
                Button("Stop").on_click([this] {
                    context->foreground_tasks().cancel();
                    subscription.reset(); permission.reset();
                    busy = false; monitoring = false; status.set("Stopped");
                })
            ).gap(8_dp)
        ).gap(10_dp).padding(16_dp);
    }
};

PXA_APPLICATION(DeviceSensorApp)
