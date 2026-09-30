#include <pxa/app.hpp>

#include <algorithm>
#include <optional>
#include <charconv>

struct Game {
    pxa::game::DrawBuffer<4096> commands;
    std::optional<pxa::game::Renderer> renderer;
    std::optional<pxa::Asset> texture;
    std::optional<pxa::Asset> palette;
    std::int16_t x = 0;
    std::uint32_t frames = 0;
    bool initializing = false;

    pxa::Task<void> initialize(pxa::Context& context) {
        (void)context.log().write(pxa::LogLevel::info, "Game create request");
        auto window = co_await context.window().snapshot();
        if (!window) {
            initializing = false;
            co_return std::unexpected(window.error());
        }
        if (window->pixel_width < 80 || window->pixel_height < 80 ||
            window->pixel_width / 2 > UINT16_MAX ||
            window->pixel_height / 2 > UINT16_MAX) {
            initializing = false;
            co_return std::unexpected(pxa::Error::unsupported);
        }
        pxa::game::RenderOptions options;
        options.width = static_cast<std::uint16_t>(
            std::min(window->pixel_width / 2, 320u));
        options.height = static_cast<std::uint16_t>(
            std::min(window->pixel_height / 2, 240u));
        options.scratch = pxa::game::Scratch::depth16;
        auto created = co_await context.game().create(options);
        if (!created) {
            char message[48] = "Game create failed: ";
            auto [end, error] = std::to_chars(
                message + 20, message + sizeof(message),
                static_cast<int>(created.error()));
            if (error == std::errc{})
                (void)context.log().write(
                    pxa::LogLevel::error,
                    {message, static_cast<std::size_t>(end - message)});
            initializing = false;
            co_return std::unexpected(created.error());
        }
        auto loaded_texture = co_await context.assets().load(
            pxa::AssetKind::texture, "assets/checker.pxr");
        if (!loaded_texture) {
            initializing = false;
            co_return std::unexpected(loaded_texture.error());
        }
        auto loaded_palette = co_await context.assets().load(
            pxa::AssetKind::palette, "assets/palette.pxr");
        if (!loaded_palette) {
            initializing = false;
            co_return std::unexpected(loaded_palette.error());
        }
        texture.emplace(std::move(*loaded_texture));
        palette.emplace(std::move(*loaded_palette));
        const std::array texture_handles{texture->handle()};
        auto bound = created->bind_assets(texture_handles, palette->handle());
        if (!bound) {
            texture.reset();
            palette.reset();
            initializing = false;
            co_return std::unexpected(bound.error());
        }
        (void)context.log().write(pxa::LogLevel::info,
                                  "Game context ready");
        renderer.emplace(std::move(*created));
        initializing = false;
        co_return pxa::Result<void>{};
    }

    void on_foreground(pxa::Context& context) {
        if (renderer || initializing) return;
        initializing = true;
        auto started = context.tasks().start(initialize(context));
        if (!started) {
            initializing = false;
            (void)context.log().write(pxa::LogLevel::error,
                                      "Game task start failed");
        }
    }

    void on_update(pxa::Context&, std::uint32_t) {
        if (!renderer) return;
        auto width = renderer->info().render_width;
        x = static_cast<std::int16_t>((x + 2) % (width > 32 ? width - 32 : 1));
    }

    void on_frame(pxa::Context& context, pxa::game::FrameTick) {
        if (!renderer) return;
        const auto width = renderer->info().render_width;
        const auto height = renderer->info().render_height;
        if (width < 40 || height < 40) return;
        auto frame = renderer->frame(commands);
        frame.clear({0x0841});
        const auto left = static_cast<std::int16_t>(x * 16);
        const auto right = static_cast<std::int16_t>((x + 32) * 16);
        const auto top = static_cast<std::int16_t>((height / 2 - 16) * 16);
        const auto bottom = static_cast<std::int16_t>((height / 2 + 16) * 16);
        frame.quad({left, top, right, top, right, bottom, left, bottom},
                   {0x07e0});
        if (renderer->supports(pxa::game::RenderCapability::textured_quad)) {
            const std::array<pxa::game::Vertex, 4> panel{{
                {.x_q4 = left, .y_q4 = top},
                {.x_q4 = right, .y_q4 = top, .u_q4 = 8 * 16},
                {.x_q4 = right, .y_q4 = bottom,
                 .u_q4 = 8 * 16, .v_q4 = 8 * 16},
                {.x_q4 = left, .y_q4 = bottom, .v_q4 = 8 * 16}}};
            frame.textured_quad({0}, panel, {.affine_uv = true});
        }
        if (renderer->supports(pxa::game::RenderCapability::triangle_batch)) {
            const std::array<pxa::game::Vertex, 3> face{{
                {.x_q4 = static_cast<std::int16_t>(left + 8 * 16),
                 .y_q4 = static_cast<std::int16_t>(top + 6 * 16),
                 .depth_q8 = 128},
                {.x_q4 = static_cast<std::int16_t>(right - 8 * 16),
                 .y_q4 = static_cast<std::int16_t>(top + 6 * 16),
                 .depth_q8 = 128},
                {.x_q4 = static_cast<std::int16_t>((left + right) / 2),
                 .y_q4 = static_cast<std::int16_t>(bottom - 6 * 16),
                 .depth_q8 = 128}}};
            frame.solid_triangles(face, {0xf800});
        }
        auto submitted = frame.submit();
        if (submitted) ++frames;
        else if (!frames)
            (void)context.log().write(pxa::LogLevel::error,
                                      "Game frame submit failed");
    }
};

PXA_GAME(Game)
