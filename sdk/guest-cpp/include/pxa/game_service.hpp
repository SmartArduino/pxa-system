#pragma once

#include "game.hpp"
#include "task.hpp"

namespace pxa::game {

struct RenderOptions {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint8_t buffers = 2;
    std::uint8_t scale = 0;
    Scratch scratch = Scratch::none;
    bool direct_scanout = false;
    std::uint32_t max_draw_bytes = 4096;
};

class Service {
public:
    Service(Transport& transport, RequestTable& requests) noexcept
        : transport_(transport), requests_(requests) {}

    Task<Renderer> create(RenderOptions options = {}) {
        const bool automatic = options.width == 0 && options.height == 0;
        if ((!automatic && (!options.width || !options.height ||
                            options.scale != 0)) ||
            options.buffers < 2 || options.buffers > 3 ||
            options.scale > 4 ||
            static_cast<unsigned>(options.scratch) > 2 ||
            (options.max_draw_bytes &&
             (options.max_draw_bytes < 32 || options.max_draw_bytes > 49152)))
            co_return std::unexpected(Error::invalid_argument);

        std::array<std::byte, 12> payload{};
        wire::put16(payload.data(), options.width);
        wire::put16(payload.data() + 2, options.height);
        payload[4] = std::byte(options.buffers);
        payload[5] = std::byte(options.direct_scanout ? 1 : 0);
        payload[6] = std::byte(options.scale);
        payload[7] = std::byte(static_cast<std::uint8_t>(options.scratch));
        wire::put32(payload.data() + 8, options.max_draw_bytes);
        const std::uint16_t opcode = automatic ? 2 : 1;
        auto event = co_await Response(
            transport_, requests_, 18, opcode,
            {payload.data(), options.max_draw_bytes ? 12u : 8u}, true);
        if (!event) co_return std::unexpected(event.error());
        if (event->payload.size() < 4)
            co_return std::unexpected(Error::protocol_error);
        auto status = static_cast<std::int32_t>(
            wire::get32(event->payload.data()));
        if (status != 0) {
            if (event->payload.size() != 4)
                co_return std::unexpected(Error::protocol_error);
            co_return std::unexpected(static_cast<Error>(status));
        }
        if (event->payload.size() != (automatic ? 36u : 24u))
            co_return std::unexpected(Error::protocol_error);
        auto handle = wire::get64(event->payload.data() + 4);
        if (!handle) co_return std::unexpected(Error::protocol_error);
        struct PendingContext {};
        Resource<PendingContext> pending(transport_, handle);
        RenderInfo info;
        info.capabilities = wire::get32(event->payload.data() + 12);
        info.max_draw_bytes = wire::get32(event->payload.data() + 16);
        info.max_texture_dimension = wire::get16(event->payload.data() + 20);
        info.max_textures = std::to_integer<std::uint8_t>(event->payload[22]);
        if (event->payload[23] != std::byte{})
            co_return std::unexpected(Error::protocol_error);
        if (automatic) {
            info.display_width = wire::get16(event->payload.data() + 24);
            info.display_height = wire::get16(event->payload.data() + 26);
            info.render_width = wire::get16(event->payload.data() + 28);
            info.render_height = wire::get16(event->payload.data() + 30);
            info.render_scale = std::to_integer<std::uint8_t>(
                event->payload[32]);
            info.supported_scale_mask = std::to_integer<std::uint8_t>(
                event->payload[33]);
            if (event->payload[34] != std::byte{} ||
                event->payload[35] != std::byte{})
                co_return std::unexpected(Error::protocol_error);
        } else {
            info.render_width = options.width;
            info.render_height = options.height;
        }
        co_return Renderer(transport_, pending.release(), info);
    }

private:
    Transport& transport_;
    RequestTable& requests_;
};

} // namespace pxa::game
