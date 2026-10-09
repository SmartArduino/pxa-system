#pragma once

#include "task.hpp"
#include "service_wire.hpp"
#include "request_packet.hpp"

#include <cstring>
#include <string_view>

namespace pxa {

struct PermissionTag {};
using Permission = Resource<PermissionTag>;

struct PermissionRevoked {
    std::string_view name;
    std::span<const std::byte> scope;
};

inline Result<PermissionRevoked> decode_permission_revoked(
    const Event& event) noexcept {
    if (event.service != 11 || event.opcode != 0x8001 || event.token ||
        event.payload.size() < 8 || wire::get16(event.payload.data()) != 1)
        return std::unexpected(Error::protocol_error);
    const auto name_size = wire::get16(event.payload.data() + 2);
    if (!name_size || name_size > 96 || event.payload.size() < 8u + name_size)
        return std::unexpected(Error::protocol_error);
    const std::string_view name(
        reinterpret_cast<const char*>(event.payload.data() + 4), name_size);
    if (name.find('\0') != std::string_view::npos)
        return std::unexpected(Error::protocol_error);
    auto scope_record = event.payload.subspan(4 + name_size);
    const auto scope_size = wire::get16(scope_record.data() + 2);
    if (wire::get16(scope_record.data()) != 2 || scope_size > 1024 ||
        scope_record.size() != 4u + scope_size)
        return std::unexpected(Error::protocol_error);
    return PermissionRevoked{name, scope_record.subspan(4)};
}

class PermissionService {
    using OwnedPacket = detail::OwnedRequestPacket<512>;
    struct BorrowedPacket {
        std::span<std::byte> bytes;
        std::span<std::byte> packet() noexcept { return bytes; }
    };
public:
    PermissionService(Transport& transport, RequestTable& requests) noexcept
        : transport_(transport), requests_(requests) {}

    // name/scope are encoded during this call. An external packet remains
    // borrowed until completion/cancellation and must not be reused meanwhile.
    Task<bool> check(this PermissionService self, std::string_view name,
                     std::span<const std::byte> scope,
                     std::span<std::byte> packet) {
        auto size = encode<true>(name, scope, packet);
        if (!size) return Task<bool>::failed(size.error());
        return check_impl(self, BorrowedPacket{packet.first(*size)});
    }

    Task<bool> check(this PermissionService self, std::string_view name,
                     std::span<const std::byte> scope = {}) {
        OwnedPacket packet;
        auto size = encode<false>(name, scope, packet.bytes);
        if (!size) return Task<bool>::failed(size.error());
        packet.size = *size;
        return check_impl(self, packet);
    }

    Task<bool> check(std::string_view name, std::string_view scope) {
        return check(name, std::as_bytes(std::span{scope.data(), scope.size()}));
    }

    Task<Permission> acquire(this PermissionService self, std::string_view name,
                             std::span<const std::byte> scope,
                             std::span<std::byte> packet) {
        auto size = encode<true>(name, scope, packet);
        if (!size) return Task<Permission>::failed(size.error());
        return acquire_impl(self, BorrowedPacket{packet.first(*size)});
    }

    Task<Permission> acquire(this PermissionService self, std::string_view name,
                             std::span<const std::byte> scope = {}) {
        OwnedPacket packet;
        auto size = encode<false>(name, scope, packet.bytes);
        if (!size) return Task<Permission>::failed(size.error());
        packet.size = *size;
        return acquire_impl(self, packet);
    }

    Task<Permission> acquire(std::string_view name, std::string_view scope) {
        return acquire(name, std::as_bytes(std::span{scope.data(), scope.size()}));
    }

private:
    template<class Packet>
    static Task<bool> check_impl(PermissionService self, Packet packet) {
        auto event = co_await Response(self.transport_, self.requests_, 11, 1,
            Response::PrebuiltPacket{packet.packet()});
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        if (body->size() != 1 || std::to_integer<unsigned>((*body)[0]) > 1)
            co_return std::unexpected(Error::protocol_error);
        co_return (*body)[0] == std::byte{1};
    }

    template<class Packet>
    static Task<Permission> acquire_impl(PermissionService self, Packet packet) {
        auto event = co_await Response(self.transport_, self.requests_, 11, 2,
            Response::PrebuiltPacket{packet.packet()}, true);
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        // Own a recognizable result handle before validating the exact length,
        // so a malformed success result cannot leak a granted permission.
        Permission pending;
        if (body->size() >= 8 && (wire::get64(body->data()) >> 32))
            pending = Permission(self.transport_, wire::get64(body->data()));
        if (body->size() != 8 || !pending)
            co_return std::unexpected(Error::protocol_error);
        co_return std::move(pending);
    }

    template<bool MayAlias>
    static Result<std::size_t> encode(
        std::string_view name, std::span<const std::byte> scope,
        std::span<std::byte> packet) noexcept {
        if (name.empty() || name.size() > 96 || scope.size() > 1024 ||
            name.find('\0') != std::string_view::npos)
            return std::unexpected(Error::invalid_argument);
        const auto size = wire::header_bytes + 4 + name.size() +
                          (scope.empty() ? 0 : 4 + scope.size());
        if (packet.size() < size)
            return std::unexpected(Error::resource_limit);
        auto* out = packet.data() + wire::header_bytes;
        auto* scope_record = out + 4 + name.size();
        if constexpr (MayAlias) {
            std::array<char, 96> owned_name;
            std::memcpy(owned_name.data(), name.data(), name.size());
            if (!scope.empty()) std::memmove(scope_record + 4, scope.data(), scope.size());
            std::memcpy(out + 4, owned_name.data(), name.size());
        } else {
            std::memcpy(out + 4, name.data(), name.size());
            if (!scope.empty()) std::memcpy(scope_record + 4, scope.data(), scope.size());
        }
        wire::put16(out, 1);
        wire::put16(out + 2, static_cast<std::uint16_t>(name.size()));
        if (!scope.empty()) {
            wire::put16(scope_record, 2);
            wire::put16(scope_record + 2, static_cast<std::uint16_t>(scope.size()));
        }
        return size;
    }
    Transport& transport_;
    RequestTable& requests_;
};

} // namespace pxa
