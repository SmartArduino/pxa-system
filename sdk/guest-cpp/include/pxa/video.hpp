// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "resource.hpp"
#include "task.hpp"
#include "request_packet.hpp"
namespace pxa {
enum class VideoCodec : std::uint16_t { h264=1, mjpeg=2 };
enum class VideoImplementation : std::uint16_t { software=1, hardware=2 };
struct VideoCapability {
    std::uint16_t count=0, index=0;
    VideoCodec codec{};
    VideoImplementation implementation{};
    std::uint32_t profiles=0;
    std::uint16_t recommended_width=0,recommended_height=0,max_width=0,max_height=0;
    std::uint32_t max_pixels=0,max_packet_bytes=0;
    std::uint16_t recommended_fps=0,max_level=0;
    std::uint32_t implementation_id=0;
    std::uint16_t audio_codecs=0;
    bool supports(std::uint16_t w,std::uint16_t h) const noexcept {
        return count && w && h && w<=max_width && h<=max_height && std::uint32_t(w)*h<=max_pixels;
    }
};
struct VideoFrameInfo {
    std::uint16_t width=0,height=0;
    std::uint32_t stride_bytes=0;
    std::uint64_t pts_us=0,decode_us=0;
    std::uint32_t frame_bytes=0;
};
struct VideoAudioInfo {
    std::uint32_t sample_rate=0,frame_bytes=0;
    std::uint16_t channels=0,bits_per_sample=0;
    std::uint64_t pts_us=0;
};
struct VideoDecoderTag {};
class VideoDecoder {
public:
    VideoDecoder()=default;
    VideoDecoder(Transport& t,std::uint64_t h) : resource_(t,h),transport_(&t) {}
    explicit operator bool() const noexcept { return bool(resource_); }
    std::uint64_t handle() const noexcept { return resource_.handle(); }
    Result<void> close() noexcept { return resource_.close(); }
    // The writable packet has a 16-byte prefix followed by one compressed AU.
    Result<void> decode(std::span<std::byte> packet,std::uint64_t pts_us,bool drain=false) noexcept {
        if (packet.size()<16 || packet.size()>UINT32_MAX || (drain && packet.size()!=16))
            return std::unexpected(Error::invalid_argument);
        wire::put64(packet.data(),pts_us); wire::put32(packet.data()+8,drain ? 1u : 0u);
        wire::put32(packet.data()+12,0);
        return command(256,packet);
    }
    Result<void> reset() noexcept { return command(258,{}); }
    // Video 0.2; RGB565 pixels follow the eight-byte request prefix.
    Result<void> read_scaled(std::span<std::byte> packet,std::uint16_t width,std::uint16_t height) noexcept {
        if (!width || !height || std::uint64_t(width)*height*2+8!=packet.size() || packet.size()>UINT32_MAX)
            return std::unexpected(Error::invalid_argument);
        wire::put16(packet.data(),width); wire::put16(packet.data()+2,height); wire::put32(packet.data()+4,0);
        return command(259,packet);
    }
    // Video 0.3: header-free output can directly fill an acquired Surface buffer.
    Result<void> configure_scaled(std::uint16_t width,std::uint16_t height) noexcept {
        if (!width || !height) return std::unexpected(Error::invalid_argument);
        std::array<std::byte,4> body{};
        wire::put16(body.data(),width); wire::put16(body.data()+2,height);
        return command(260,body);
    }
    Result<void> read_scaled_pixels(std::span<std::byte> pixels) noexcept {
        if (pixels.empty() || pixels.size()>UINT32_MAX) return std::unexpected(Error::invalid_argument);
        return command(261,pixels);
    }
    // Eight-byte timestamp prefix followed by exactly one AAC-LC ADTS frame.
    Result<void> decode_audio(std::span<std::byte> packet,std::uint64_t pts_us) noexcept {
        if (packet.size()<15 || packet.size()>8192) return std::unexpected(Error::invalid_argument);
        wire::put64(packet.data(),pts_us); return command(272,packet);
    }
    Result<VideoAudioInfo> audio_info() noexcept {
        std::array<std::byte,24> body{};
        auto result=command(273,body); if (!result) return std::unexpected(result.error());
        auto *p=body.data();
        VideoAudioInfo info{wire::get32(p),wire::get32(p+8),wire::get16(p+4),wire::get16(p+6),wire::get64(p+16)};
        if (!info.sample_rate || !info.channels || info.channels>2 || info.bits_per_sample!=16 ||
            info.frame_bytes>16384 || info.frame_bytes%(info.channels*2) || wire::get32(p+12))
            return std::unexpected(Error::protocol_error);
        return info;
    }
    Result<std::uint32_t> read_audio(std::span<std::byte> out) noexcept { return read_impl(274,out); }
    Result<VideoFrameInfo> frame_info() noexcept {
        std::array<std::byte,32> body{};
        auto result=command(257,body); if (!result) return std::unexpected(result.error());
        auto *p=body.data();
        VideoFrameInfo f{wire::get16(p),wire::get16(p+2),wire::get32(p+4),wire::get64(p+8),wire::get64(p+16),wire::get32(p+24)};
        if (!f.width || !f.height || f.stride_bytes!=std::uint32_t(f.width)*2 ||
            f.frame_bytes!=std::uint64_t(f.stride_bytes)*f.height || wire::get32(p+28))
            return std::unexpected(Error::protocol_error);
        return f;
    }
    Result<std::uint32_t> read(std::span<std::byte> out) noexcept {
        return read_impl(1,out);
    }
private:
    Result<std::uint32_t> read_impl(std::uint32_t operation,std::span<std::byte> out) noexcept {
        if (!resource_ || out.size()>UINT32_MAX) return std::unexpected(Error::invalid_argument);
        auto n=transport_->io(handle(),operation,out);
        if (!n) return std::unexpected(n.error());
        if (*n>out.size()) return std::unexpected(Error::protocol_error);
        return *n;
    }
    Result<void> command(std::uint32_t op,std::span<std::byte> body) noexcept {
        if (!resource_) return std::unexpected(Error::bad_state);
        auto n=transport_->io(handle(),op,body);
        if (!n) return std::unexpected(n.error());
        if (*n) return std::unexpected(Error::protocol_error);
        return {};
    }
    Resource<VideoDecoderTag> resource_;
    Transport *transport_=nullptr;
};
class VideoService {
public:
    VideoService(Transport& t,RequestTable& r) : transport_(t),requests_(r) {}
    Task<VideoCapability> capability(this VideoService self,std::uint16_t index=0) {
        wire::RequestPacket<2> packet; wire::put16(packet.bytes.data(),index); packet.size=2;
        auto event=co_await Response(self.transport_,self.requests_,24,1,packet.packet());
        if (!event) co_return std::unexpected(event.error());
        auto body=wire::result_body(event->payload); if (!body) co_return std::unexpected(body.error());
        if (body->size()!=40) co_return std::unexpected(Error::protocol_error);
        auto *p=body->data();
        VideoCapability c{wire::get16(p),wire::get16(p+2),VideoCodec(wire::get16(p+4)),VideoImplementation(wire::get16(p+6)),
            wire::get32(p+8),wire::get16(p+12),wire::get16(p+14),wire::get16(p+16),wire::get16(p+18),
            wire::get32(p+20),wire::get32(p+24),wire::get16(p+28),wire::get16(p+30),wire::get32(p+32),wire::get16(p+38)};
        if (c.index!=index || (c.count && (index>=c.count || wire::get16(p+36)!=1)) ||
            (!c.count && (index || wire::get16(p+36)))) co_return std::unexpected(Error::protocol_error);
        co_return c;
    }
    Task<VideoDecoder> open(this VideoService self,std::uint16_t index,std::uint16_t width,std::uint16_t height) {
        if (!width || !height) co_return std::unexpected(Error::invalid_argument);
        wire::RequestPacket<8> packet; auto *p=packet.bytes.data();
        wire::put16(p,index); wire::put16(p+2,width); wire::put16(p+4,height); wire::put16(p+6,0); packet.size=8;
        auto event=co_await Response(self.transport_,self.requests_,24,2,packet.packet(),true);
        if (!event) co_return std::unexpected(event.error());
        auto body=wire::result_body(event->payload); if (!body) co_return std::unexpected(body.error());
        auto pending=wire::result_resource<VideoDecoderTag>(self.transport_,*body);
        if (body->size()!=8 || !(pending.handle()>>32)) co_return std::unexpected(Error::protocol_error);
        co_return VideoDecoder(self.transport_,pending.release());
    }
private:
    Transport& transport_;
    RequestTable& requests_;
};
}
