#pragma once

#include "task.hpp"
#include "service_wire.hpp"

#include <array>
#include <concepts>
#include <string_view>
#include <utility>

namespace pxa {

enum class AssetKind : std::uint8_t {
    texture = 1, palette = 2, audio = 3, image = 4, blob = 5
};

enum class AssetState : std::uint8_t { absent, queued, loading, ready, failed };
struct AssetStatus {
    AssetState state;
    std::int32_t load_status;
};

struct AssetDescriptor {
    AssetKind kind = AssetKind::texture;
    std::uint8_t encoding = 0;
    std::uint16_t format_version = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint32_t stored_bytes = 0;
    std::uint32_t decoded_bytes = 0;
    std::uint32_t resident_bytes = 0;
    std::uint32_t source_bytes = 0;
    // Available for decoded images; prepared dimensions above may be smaller.
    std::uint16_t source_width = 0, source_height = 0;
};

struct AssetTag {};

class Asset {
public:
    Asset(Transport& transport, std::uint64_t handle,
          AssetDescriptor descriptor) noexcept
        : handle_(transport, handle), descriptor_(descriptor) {}
    Asset(const Asset&) = delete;
    Asset& operator=(const Asset&) = delete;
    Asset(Asset&&) noexcept = default;
    Asset& operator=(Asset&&) noexcept = default;

    std::uint64_t handle() const noexcept { return handle_.handle(); }
    explicit operator bool() const noexcept { return bool(handle_); }
    Result<void> close() noexcept { return handle_.close(); }
    const AssetDescriptor& descriptor() const & noexcept { return descriptor_; }
    AssetDescriptor descriptor() const && noexcept { return descriptor_; }
private:
    Resource<AssetTag> handle_;
    AssetDescriptor descriptor_;
};

struct AssetRead {
    std::uint32_t offset = 0;
    std::uint32_t total_bytes = 0;
    std::uint32_t bytes = 0;
    bool eof() const noexcept { return offset + bytes == total_bytes; }
};

enum class ImageFormats : std::uint16_t { any = 0, png = 1, jpeg = 2, gif = 4 };
constexpr ImageFormats operator|(ImageFormats a, ImageFormats b) noexcept {
    return static_cast<ImageFormats>(std::to_underlying(a) | std::to_underlying(b));
}
struct ImageDecodeOptions {
    std::uint16_t max_width = 320, max_height = 320;
    std::uint32_t max_pixels = 32768, max_encoded_bytes = 131072;
    ImageFormats formats = ImageFormats::any;
};
class ImageDecodeService;
class DecodedImage;
class AssetService {
    using Packet = wire::RequestPacket<267>;
public:
    AssetService(Transport& transport, RequestTable& requests) noexcept
        : transport_(transport), requests_(requests) {}

    // Consume a File/HttpExchange (or another readable resource) immediately.
    // Core reads it on the Host: no encoded-data transfer through Guest memory.
    // Source and decode request close together on completion/error/cancellation.
    template <class Source>
        requires requires(const Source &source) {
            { source.readable() } -> std::same_as<ReadableResource>;
        }
    Task<Asset> decode_image(Source source, ImageDecodeOptions options = {}) {
        const auto view = source.readable();
        if (!valid_image_options(options))
            return Task<Asset>::failed(Error::invalid_argument);
        if (view.transport != &transport_ || !view.handle || (view.type != 2 && view.type != 3))
            return Task<Asset>::failed(Error::bad_state);
        return decode_image_impl(transport_, requests_, std::move(source), options);
    }

    // Optional frame support: include <pxa/image_frames.hpp> and require the
    // Assets image-frames feature. No state is added to Context or AssetService.
    ImageDecodeService images() noexcept;

    Task<Asset> load(AssetKind kind, std::string_view path) {
        if (!valid_path(path) || !valid_kind(kind))
            return Task<Asset>::failed(Error::invalid_argument);
        auto packet = path_packet(path, 4);
        packet.bytes[2] = std::byte(kind);
        return load_impl(transport_, requests_, kind, std::move(packet));
    }

    Task<AssetDescriptor> query(std::string_view path) {
        if (!valid_path(path))
            return Task<AssetDescriptor>::failed(Error::invalid_argument);
        return query_impl(transport_, requests_, path_packet(path, 4), 1);
    }

    // Warms an evictable cache entry; load() is still required for ownership.
    Task<AssetDescriptor> prefetch(AssetKind kind, std::string_view path) {
        if (!valid_path(path) || !valid_kind(kind))
            return Task<AssetDescriptor>::failed(Error::invalid_argument);
        auto packet = path_packet(path, 4);
        packet.bytes[2] = std::byte(kind);
        return query_impl(transport_, requests_, std::move(packet), 3);
    }

    Task<AssetStatus> status(std::string_view path) {
        if (!valid_path(path)) return Task<AssetStatus>::failed(Error::invalid_argument);
        return status_impl(transport_, requests_, path_packet(path, 4));
    }

    Task<AssetRead> read(std::string_view path, std::uint32_t offset,
                         std::span<std::byte> output) {
        if (!valid_path(path) || output.empty() || output.size() > 4064)
            return Task<AssetRead>::failed(Error::invalid_argument);
        auto packet = path_packet(path, 12);
        auto* payload = packet.bytes.data();
        wire::put32(payload + 4, offset);
        wire::put32(payload + 8, static_cast<std::uint32_t>(output.size()));
        return read_impl(transport_, requests_, std::move(packet), offset, output);
    }

private:
    friend class ImageDecodeService;
    friend class DecodedImage;
    static bool valid_image_options(ImageDecodeOptions options) noexcept {
        return options.max_width && options.max_width <= 2048 && options.max_height &&
            options.max_height <= 2048 && options.max_pixels && options.max_pixels <= 1048576 &&
            options.max_encoded_bytes && options.max_encoded_bytes <= 524288 &&
            !(std::to_underlying(options.formats) & ~7u);
    }
    static Result<Asset> decode_image_result(Transport &transport,
        std::span<const std::byte> body, ImageDecodeOptions options, std::size_t size = 36) {
        // Own any recognizable handle before validating the rest of the reply.
        auto pending = wire::result_resource<AssetTag>(transport, body);
        if (body.size() != size || !pending)
            return std::unexpected(Error::protocol_error);
        auto descriptor = decode_descriptor(body.subspan(8, 20));
        if (!descriptor || descriptor->kind != AssetKind::image)
            return std::unexpected(Error::protocol_error);
        descriptor->source_width = wire::get16(body.data() + 28);
        descriptor->source_height = wire::get16(body.data() + 30);
        descriptor->source_bytes = wire::get32(body.data() + 32);
        if (!descriptor->source_width || !descriptor->source_height ||
            !descriptor->width || !descriptor->height ||
            descriptor->width > options.max_width || descriptor->height > options.max_height ||
            std::uint32_t(descriptor->width) * descriptor->height > options.max_pixels ||
            !descriptor->source_bytes || descriptor->source_bytes > options.max_encoded_bytes)
            return std::unexpected(Error::protocol_error);
        return Asset(transport, pending.release(), *descriptor);
    }
    template <class Source>
    static Task<Asset> decode_image_impl(Transport &transport, RequestTable &requests, Source source,
                                         ImageDecodeOptions options) {
        const auto view = source.readable();
        std::array<std::byte, 24> packet{};
        wire::put64(packet.data(), view.handle);
        wire::put16(packet.data() + 8, view.type);
        wire::put16(packet.data() + 10, std::to_underlying(options.formats));
        wire::put16(packet.data() + 12, options.max_width);
        wire::put16(packet.data() + 14, options.max_height);
        wire::put32(packet.data() + 16, options.max_pixels);
        wire::put32(packet.data() + 20, options.max_encoded_bytes);
        auto event = co_await Response(transport, requests, 21, 6, packet, true);
        if (!event)
            co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body)
            co_return std::unexpected(body.error());
        co_return decode_image_result(transport, *body, options);
    }
    static Packet path_packet(std::string_view path, std::size_t prefix) noexcept {
        Packet packet;
        auto *payload = packet.bytes.data();
        wire::put16(payload, static_cast<std::uint16_t>(path.size()));
        for (std::size_t i = 0; i < path.size(); ++i)
            payload[prefix + i] = std::byte(path[i]);
        packet.size = prefix + path.size();
        return packet;
    }

    static Task<Asset> load_impl(Transport& transport, RequestTable& requests,
                                 AssetKind kind, Packet packet) {
        auto event = co_await Response(
            transport, requests, 21, 2,
            packet.packet(), true);
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        auto pending = wire::result_resource<AssetTag>(transport, *body);
        if (body->size() != 28 || !pending)
            co_return std::unexpected(Error::protocol_error);
        auto descriptor = decode_descriptor(event->payload.subspan(12));
        if (!descriptor || descriptor->kind != kind)
            co_return std::unexpected(Error::protocol_error);
        co_return Asset(transport, pending.release(), *descriptor);
    }

    static Task<AssetDescriptor> query_impl(Transport& transport,
        RequestTable& requests, Packet packet, std::uint16_t opcode) {
        auto event = co_await Response(
            transport, requests, 21, opcode, packet.packet());
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        if (event->payload.size() != 24)
            co_return std::unexpected(Error::protocol_error);
        auto descriptor = decode_descriptor(*body);
        if (descriptor && opcode == 3 &&
            std::byte(descriptor->kind) != packet.bytes[2])
            co_return std::unexpected(Error::protocol_error);
        co_return descriptor;
    }

    static Task<AssetStatus> status_impl(Transport& transport,
        RequestTable& requests, Packet packet) {
        auto event = co_await Response(transport, requests, 21, 4, packet.packet());
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        if (body->size() != 8 || std::to_integer<unsigned>((*body)[0]) > 4 ||
            (*body)[1] != std::byte{} || (*body)[2] != std::byte{} ||
            (*body)[3] != std::byte{})
            co_return std::unexpected(Error::protocol_error);
        co_return AssetStatus{static_cast<AssetState>((*body)[0]),
            static_cast<std::int32_t>(wire::get32(body->data() + 4))};
    }

    static Task<AssetRead> read_impl(Transport& transport, RequestTable& requests,
                                     Packet packet, std::uint32_t offset,
                                     std::span<std::byte> output) {
        auto event = co_await Response(
            transport, requests, 21, 5, packet.packet());
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        if (event->payload.size() < 12 ||
            event->payload.size() > 12 + output.size())
            co_return std::unexpected(Error::protocol_error);
        AssetRead result{wire::get32(event->payload.data() + 4),
                         wire::get32(event->payload.data() + 8),
                         static_cast<std::uint32_t>(event->payload.size() - 12)};
        if (result.offset != offset ||
            result.offset > result.total_bytes ||
            result.bytes > result.total_bytes - result.offset ||
            (!result.bytes && !result.eof()))
            co_return std::unexpected(Error::protocol_error);
        for (std::size_t i = 0; i < result.bytes; ++i)
            output[i] = event->payload[12 + i];
        co_return result;
    }

private:
    static bool valid_kind(AssetKind kind) noexcept {
        return kind >= AssetKind::texture && kind <= AssetKind::image;
    }
    static bool valid_path(std::string_view path) noexcept {
        return !path.empty() && path.size() <= 255 &&
               path.find('\0') == std::string_view::npos;
    }
    static Result<AssetDescriptor> decode_descriptor(
        std::span<const std::byte> payload) noexcept {
        if (payload.size() != 20)
            return std::unexpected(Error::protocol_error);
        auto kind = static_cast<AssetKind>(
            std::to_integer<std::uint8_t>(payload[0]));
        if (!valid_kind(kind) && kind != AssetKind::blob)
            return std::unexpected(Error::protocol_error);
        return AssetDescriptor{
            kind,
            std::to_integer<std::uint8_t>(payload[1]),
            wire::get16(payload.data() + 2),
            wire::get16(payload.data() + 4),
            wire::get16(payload.data() + 6),
            wire::get32(payload.data() + 8),
            wire::get32(payload.data() + 12),
            wire::get32(payload.data() + 16)};
    }

    Transport& transport_;
    RequestTable& requests_;
};

} // namespace pxa
