#pragma once

#include "assets.hpp"

namespace pxa {

struct ImageAnimationLimits {
    // Zero uses the ordinary decode ceiling. Only sequence codecs apply these
    // limits, so a smaller GIF budget does not reduce PNG/JPEG resolution.
    std::uint32_t max_pixels = 0, max_encoded_bytes = 0;
};
struct ImageFrameInfo {
    std::uint32_t delay_ms = 0, iteration = 0;
    bool last = true;
};

// Owns the current immutable image and its optional Host decoder. Static images
// and final frames retain pixels only. No Guest pixel/encoded buffer or timer.
// Keep this object alive and unmoved while advance() is pending, just as a
// borrowed I/O buffer must outlive its operation. Cancel before destroying it.
class DecodedImage {
public:
    DecodedImage(const DecodedImage &) = delete;
    DecodedImage &operator=(const DecodedImage &) = delete;
    DecodedImage(DecodedImage &&) noexcept = default;
    DecodedImage &operator=(DecodedImage &&) noexcept = default;
    const Asset &image() const & noexcept { return image_; }
    const Asset &image() const && = delete;
    ImageFrameInfo frame_info() const noexcept { return frame_; }

    // One requested advance, with no automatic catch-up queue. On success the
    // previous handle closes; existing UI bindings still own their old pixels.
    // A cancelled advance may have completed on the Host: discard/reload this
    // sequence before resuming rather than assuming its old handle has state.
    Task<bool> advance() & {
        if (!image_ || advancing_) co_return std::unexpected(Error::bad_state);
        if (frame_.last) co_return false;
        advancing_ = true;
        struct Reset { bool &value; ~Reset() { value = false; } } reset{advancing_};
        std::array<std::byte, 8> packet;
        wire::put64(packet.data(), image_.handle());
        auto event = co_await Response(*transport_, *requests_, 21, 7, packet, true);
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        auto next = AssetService::decode_image_result(*transport_, *body, options_, 48);
        if (!next) co_return std::unexpected(next.error());
        auto frame = read_frame(*body);
        if (!frame) co_return std::unexpected(frame.error());
        const auto &before = image_.descriptor(), &after = next->descriptor();
        if (before.source_width != after.source_width || before.source_height != after.source_height ||
            before.source_bytes != after.source_bytes || before.width != after.width ||
            before.height != after.height || frame->iteration < frame_.iteration)
            co_return std::unexpected(Error::protocol_error);
        image_ = std::move(*next);
        frame_ = *frame;
        co_return true;
    }
    Task<bool> advance() && = delete;

private:
    friend class ImageDecodeService;
    DecodedImage(Transport &transport, RequestTable &requests, Asset image,
        ImageDecodeOptions options, ImageFrameInfo frame) noexcept
        : image_(std::move(image)), transport_(&transport), requests_(&requests),
          options_(options), frame_(frame) {}
    static Result<ImageFrameInfo> read_frame(std::span<const std::byte> body) noexcept {
        const auto flags = wire::get32(body.data() + 44);
        if (flags & ~1u) return std::unexpected(Error::protocol_error);
        return ImageFrameInfo{wire::get32(body.data() + 36), wire::get32(body.data() + 40), bool(flags & 1)};
    }
    Asset image_;
    Transport *transport_;
    RequestTable *requests_;
    ImageDecodeOptions options_;
    ImageFrameInfo frame_;
    bool advancing_ = false;
};

class ImageDecodeService {
public:
    ImageDecodeService(Transport &transport, RequestTable &requests) noexcept
        : transport_(transport), requests_(requests) {}
    template<class Source>
        requires requires(const Source &source) {
            { source.readable() } -> std::same_as<ReadableResource>;
        }
    Task<DecodedImage> decode(Source source, ImageDecodeOptions options = {},
        ImageAnimationLimits animation = {}) const {
        if (!AssetService::valid_image_options(options) || animation.max_pixels > 65536 ||
            animation.max_encoded_bytes > 524288)
            return Task<DecodedImage>::failed(Error::invalid_argument);
        const auto view = source.readable();
        if (view.transport != &transport_ || !view.handle || (view.type != 2 && view.type != 3))
            return Task<DecodedImage>::failed(Error::bad_state);
        return decode_impl(transport_, requests_, std::move(source), options, animation);
    }
private:
    template<class Source>
    static Task<DecodedImage> decode_impl(Transport &transport, RequestTable &requests,
        Source source, ImageDecodeOptions options, ImageAnimationLimits animation) {
        const auto view = source.readable();
        std::array<std::byte, 32> packet{};
        wire::put64(packet.data(), view.handle);
        wire::put16(packet.data() + 8, view.type);
        wire::put16(packet.data() + 10, 256 | std::to_underlying(options.formats));
        wire::put16(packet.data() + 12, options.max_width);
        wire::put16(packet.data() + 14, options.max_height);
        wire::put32(packet.data() + 16, options.max_pixels);
        wire::put32(packet.data() + 20, options.max_encoded_bytes);
        wire::put32(packet.data() + 24, animation.max_pixels);
        wire::put32(packet.data() + 28, animation.max_encoded_bytes);
        auto event = co_await Response(transport, requests, 21, 6, packet, true);
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        auto image = AssetService::decode_image_result(transport, *body, options, 48);
        if (!image) co_return std::unexpected(image.error());
        auto frame = DecodedImage::read_frame(*body);
        if (!frame) co_return std::unexpected(frame.error());
        co_return DecodedImage(transport, requests, std::move(*image), options, *frame);
    }
    Transport &transport_;
    RequestTable &requests_;
};

inline ImageDecodeService AssetService::images() noexcept { return {transport_, requests_}; }

} // namespace pxa
