#pragma once
#include "game.hpp"
#include <algorithm>
#include <cstring>

namespace pxa::game {
// Optional procedural-resource uploader. Scratch belongs to the caller and
// can be reused immediately after the synchronous Host copy; no hidden cache.
class Upload {
public:
    static constexpr std::size_t header_bytes = 20;
    Upload(Renderer& renderer, std::span<std::byte> scratch) noexcept
        : renderer_(renderer), scratch_(scratch) {}
    Result<void> texture(AtlasBinding slot, std::uint16_t width,
                         std::uint16_t height,
                         std::span<const std::byte> pixels) noexcept {
        if (!width || !height || width>256 || height>256 || pixels.size() != std::size_t(width)*height ||
            slot.slot >= 48 || (renderer_.info().max_textures &&
            slot.slot >= renderer_.info().max_textures) ||
            (renderer_.info().max_texture_dimension &&
             (width > renderer_.info().max_texture_dimension ||
              height > renderer_.info().max_texture_dimension)))
            return std::unexpected(Error::invalid_argument);
        if (slot.slot >= 16 && !renderer_.supports(RenderCapability::texture_slots_48))
            return std::unexpected(Error::unsupported);
        return send(2, slot.slot, width, height, pixels);
    }
    Result<void> palette(std::span<const std::uint16_t> colors,
                         std::uint16_t light_levels = 1) noexcept {
        if (!light_levels || light_levels > 256 ||
            colors.size() != std::size_t(light_levels)*256)
            return std::unexpected(Error::invalid_argument);
        if (scratch_.size() < header_bytes + colors.size()*2)
            return std::unexpected(Error::limit_exceeded);
        auto* payload=scratch_.data()+header_bytes;
        std::memmove(payload,colors.data(),colors.size()*2);
        for (std::size_t i=0;i<colors.size();++i) {
            std::uint16_t color;
            std::memcpy(&color,payload+2*i,2);
            wire::put16(payload+2*i,color);
        }
        return send(light_levels==1?1:3,0,256,light_levels,
                    scratch_.subspan(header_bytes,colors.size()*2));
    }
private:
    Result<void> send(std::uint8_t kind,std::uint8_t slot,std::uint16_t width,
                      std::uint16_t height,std::span<const std::byte> payload) noexcept {
        if (scratch_.size()<header_bytes+payload.size())
            return std::unexpected(Error::limit_exceeded);
        auto* out=scratch_.data();
        if (payload.data()!=out+header_bytes)
            std::memmove(out+header_bytes,payload.data(),payload.size());
        wire::put32(out,0x52555850);wire::put16(out+4,1);wire::put16(out+6,7);
        out[8]=std::byte(kind);out[9]=std::byte(slot);wire::put16(out+10,0);
        wire::put16(out+12,width);wire::put16(out+14,height);
        wire::put32(out+16,static_cast<std::uint32_t>(payload.size()));
        const auto size=header_bytes+payload.size();
        auto result=renderer_.transport().io(renderer_.handle(),0x100,scratch_.first(size));
        if(!result)return std::unexpected(result.error());
        return *result==size?Result<void>{}:Result<void>{std::unexpected(Error::protocol_error)};
    }
    Renderer& renderer_;
    std::span<std::byte> scratch_;
};
} // namespace pxa::game
