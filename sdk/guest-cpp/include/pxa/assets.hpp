#pragma once

#include "task.hpp"
#include "service_wire.hpp"

#include <array>
#include <string_view>

namespace pxa {

enum class AssetKind : std::uint8_t {
    texture = 1, palette = 2, audio = 3, image = 4
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

class AssetService {
    using Packet = wire::RequestPacket<267>;
public:
    AssetService(Transport& transport, RequestTable& requests) noexcept
        : transport_(transport), requests_(requests) {}

    Task<Asset> load(AssetKind kind, std::string_view path) {
        if (!valid_path(path) || !valid_kind(kind))
            return Task<Asset>::failed(Error::invalid_argument);
        auto packet = path_packet(path, 4);
        packet.bytes[wire::header_bytes + 2] = std::byte(kind);
        return load_impl(transport_, requests_, kind, std::move(packet));
    }

    Task<AssetDescriptor> query(std::string_view path) {
        if (!valid_path(path))
            return Task<AssetDescriptor>::failed(Error::invalid_argument);
        return query_impl(transport_, requests_, path_packet(path, 4));
    }

    Task<AssetRead> read(std::string_view path, std::uint32_t offset,
                         std::span<std::byte> output) {
        if (!valid_path(path) || output.empty() || output.size() > 4064)
            return Task<AssetRead>::failed(Error::invalid_argument);
        auto packet = path_packet(path, 12);
        auto* payload = packet.bytes.data() + wire::header_bytes;
        wire::put32(payload + 4, offset);
        wire::put32(payload + 8, static_cast<std::uint32_t>(output.size()));
        return read_impl(transport_, requests_, std::move(packet), offset, output);
    }

private:
    static Packet path_packet(std::string_view path, std::size_t prefix) noexcept {
        Packet packet;
        auto* payload = packet.bytes.data() + wire::header_bytes;
        wire::put16(payload, static_cast<std::uint16_t>(path.size()));
        for (std::size_t i = 0; i < path.size(); ++i) payload[prefix + i] = std::byte(path[i]);
        packet.size = prefix + path.size();
        return packet;
    }

    static Task<Asset> load_impl(Transport& transport, RequestTable& requests,
                                 AssetKind kind, Packet packet) {
        auto event = co_await Response(
            transport, requests, 21, 2,
            Response::PrebuiltPacket{packet.packet()}, true);
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
                                            RequestTable& requests, Packet packet) {
        auto event = co_await Response(
            transport, requests, 21, 1, Response::PrebuiltPacket{packet.packet()});
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        if (event->payload.size() != 24)
            co_return std::unexpected(Error::protocol_error);
        co_return decode_descriptor(event->payload.subspan(4));
    }

    static Task<AssetRead> read_impl(Transport& transport, RequestTable& requests,
                                     Packet packet, std::uint32_t offset,
                                     std::span<std::byte> output) {
        auto event = co_await Response(
            transport, requests, 21, 5, Response::PrebuiltPacket{packet.packet()});
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
        if (!valid_kind(kind))
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
