// SPDX-License-Identifier: MIT
#pragma once
#include "task.hpp"
#include "service_wire.hpp"
#include <vector>

namespace pxa {
struct FontGlyph {
    std::uint16_t line_height=0, baseline=0, advance=0, width=0, height=0;
    std::int16_t offset_x=0, offset_y=0;
    bool placeholder=false;
    std::vector<std::byte> coverage; // packed A8, independent of the event
};
inline Result<FontGlyph> decode_font_glyph(std::span<const std::byte> body) {
    if (body.size()<16) return std::unexpected(Error::protocol_error);
    const auto* p=body.data();
    FontGlyph g;
    g.line_height=wire::get16(p);g.baseline=wire::get16(p+2);g.advance=wire::get16(p+4);
    g.width=wire::get16(p+6);g.height=wire::get16(p+8);
    g.offset_x=static_cast<std::int16_t>(wire::get16(p+10));
    g.offset_y=static_cast<std::int16_t>(wire::get16(p+12));
    const auto flags=wire::get16(p+14);
    const auto bytes=std::size_t(g.width)*g.height;
    if (!g.line_height || g.line_height>128 || g.baseline>g.line_height || g.advance>128 ||
        g.width>64 || g.height>64 || bytes>3072 || body.size()!=16+bytes ||
        g.offset_x < -128 || g.offset_x>128 || g.offset_y < -128 || g.offset_y>128 || (flags&~1u))
        return std::unexpected(Error::protocol_error);
    g.placeholder=flags&1;g.coverage.assign(body.begin()+16,body.end());return g;
}
class FontService {
public:
    FontService(Transport& transport,RequestTable& requests) noexcept : transport_(transport),requests_(requests) {}
    // UI 0.10 font-glyph. Unicode scalar, native pixel size 8..48; no kerning.
    Task<FontGlyph> glyph(this FontService self,char32_t codepoint,std::uint16_t pixels) {
        if (codepoint>0x10ffff || (codepoint>=0xd800 && codepoint<=0xdfff) || pixels<8 || pixels>48)
            co_return std::unexpected(Error::invalid_argument);
        std::array<std::byte,8> packet{};
        wire::put32(packet.data(),static_cast<std::uint32_t>(codepoint));wire::put16(packet.data()+4,pixels);
        auto event=co_await Response(self.transport_,self.requests_,3,13,packet);
        if (!event) co_return std::unexpected(event.error());
        auto body=wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        co_return decode_font_glyph(*body);
    }
private:
    Transport& transport_;
    RequestTable& requests_;
};
} // namespace pxa
