#pragma once

#include "task.hpp"
#include "service_wire.hpp"

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
public:
    PermissionService(Transport& transport, RequestTable& requests) noexcept
        : transport_(transport), requests_(requests) {}

    Task<bool> check(this PermissionService self, std::string_view name,
                     std::span<const std::byte> scope,
                     std::span<std::byte> packet) {
        auto& [transport_, requests_] = self;
        auto size = encode(name, scope, packet);
        if (!size) co_return std::unexpected(size.error());
        auto event = co_await Response(transport_, requests_, 11, 1,
            Response::PrebuiltPacket{packet.first(*size)});
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        if (body->size() != 1 || std::to_integer<unsigned>((*body)[0]) > 1)
            co_return std::unexpected(Error::protocol_error);
        co_return (*body)[0] == std::byte{1};
    }

    Task<bool> check(this PermissionService self, std::string_view name,
                     std::span<const std::byte> scope = {}) {
        std::array<std::byte, 512> packet{};
        co_return co_await self.check(name, scope, packet);
    }

    Task<bool> check(std::string_view name, std::string_view scope) {
        return check(name, std::as_bytes(std::span{scope.data(), scope.size()}));
    }

    Task<Permission> acquire(this PermissionService self, std::string_view name,
                             std::span<const std::byte> scope,
                             std::span<std::byte> packet) {
        auto& [transport_, requests_] = self;
        auto size = encode(name, scope, packet);
        if (!size) co_return std::unexpected(size.error());
        auto event = co_await Response(transport_, requests_, 11, 2,
            Response::PrebuiltPacket{packet.first(*size)}, true);
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        if (body->size() != 8 || !(wire::get64(body->data()) >> 32))
            co_return std::unexpected(Error::protocol_error);
        co_return Permission(transport_, wire::get64(body->data()));
    }

    Task<Permission> acquire(this PermissionService self, std::string_view name,
                             std::span<const std::byte> scope = {}) {
        std::array<std::byte, 512> packet{};
        co_return co_await self.acquire(name, scope, packet);
    }

    Task<Permission> acquire(std::string_view name, std::string_view scope) {
        return acquire(name, std::as_bytes(std::span{scope.data(), scope.size()}));
    }

private:
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
        wire::Writer writer(packet.subspan(wire::header_bytes));
        wire::record(writer, 1, {reinterpret_cast<const std::byte*>(name.data()),
                                 name.size()});
        if (!scope.empty()) wire::record(writer, 2, scope);
        return size;
    }
    Transport& transport_;
    RequestTable& requests_;
};

} // namespace pxa
