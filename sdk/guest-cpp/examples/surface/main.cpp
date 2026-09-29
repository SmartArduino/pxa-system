#include <pxa/app.hpp>

#include <algorithm>
#include <charconv>
#include <optional>
#include <string_view>
#include <utility>

struct SurfaceDemo {
    std::optional<pxa::Surface> surface;
    std::uint64_t next_frame = 1;
    std::uint16_t offset = 0;
    bool reported_error = false;
    bool initializing = false;

    void report(pxa::Context& context, std::string_view stage,
                pxa::Error error) {
        if (reported_error) return;
        reported_error = true;
        char message[64] = "Surface ";
        auto* number = std::copy(stage.begin(), stage.end(), message + 8);
        *number++ = ':';
        *number++ = ' ';
        auto [end, status] = std::to_chars(
            number, message + sizeof(message),
            static_cast<int>(error));
        if (status == std::errc{})
            (void)context.log().write(pxa::LogLevel::error,
                {message, static_cast<std::size_t>(end - message)});
    }

    pxa::Task<void> initialize(pxa::Context& context) {
        auto window = co_await context.window().snapshot();
        if (!window) {
            report(context, "window", window.error());
            initializing = false;
            co_return std::unexpected(window.error());
        }
        if (window->pixel_width < 80 || window->pixel_height < 80) {
            initializing = false;
            co_return std::unexpected(pxa::Error::unsupported);
        }
        pxa::SurfaceOptions options{
            .width = static_cast<std::uint16_t>(
                std::min(window->pixel_width / 2, 320u)),
            .height = static_cast<std::uint16_t>(
                std::min(window->pixel_height / 2, 240u)),
            .buffers = 2};
        auto created = co_await context.surface().create_mapped(options);
        if (!created) {
            report(context, "create", created.error());
            initializing = false;
            co_return std::unexpected(created.error());
        }
        surface.emplace(std::move(*created));
        auto configured = co_await surface->configure({
            .width = options.width, .height = options.height});
        if (!configured) {
            report(context, "configure", configured.error());
            surface.reset();
        }
        initializing = false;
        co_return configured;
    }

    void on_foreground(pxa::Context& context) {
        if (surface || initializing) return;
        initializing = true;
        reported_error = false;
        auto started = context.tasks().start(initialize(context));
        if (!started) {
            initializing = false;
            report(context, "task", started.error());
        }
    }

    void on_frame(pxa::Context& context, pxa::game::FrameTick) {
        if (!surface || !*surface) return;
        auto acquired = surface->acquire();
        if (!acquired) {
            if (acquired.error() != pxa::Error::would_block)
                report(context, "acquire", acquired.error());
            return;
        }
        auto pixels = acquired->pixels();
        const auto width = surface->width();
        const auto height = surface->height();
        const auto stride = surface->stride_bytes();
        for (std::uint32_t y = 0; y < height; ++y) {
            auto* row = pixels.data() + y * stride;
            for (std::uint32_t x = 0; x < width; ++x) {
                const bool stripe = ((x + offset) / 16) % 2 == 0;
                const std::uint16_t color = stripe
                    ? static_cast<std::uint16_t>(0x07e0 +
                          ((y * 7 / height) << 11))
                    : static_cast<std::uint16_t>(0x001f +
                          ((y * 15 / height) << 11));
                pxa::wire::put16(row + x * 2, color);
            }
        }
        auto presented = acquired->present(next_frame);
        if (presented) {
            ++next_frame;
            offset = static_cast<std::uint16_t>((offset + 1) % width);
        } else {
            report(context, "present", presented.error());
        }
    }
};

PXA_GAME(SurfaceDemo)
