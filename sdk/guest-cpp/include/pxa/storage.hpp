#pragma once

#include "task.hpp"

#include <array>
#include <string_view>

namespace pxa {

struct StorageKey {
    std::array<char, 64> bytes{};
    std::uint8_t size = 0;
    std::string_view view() const noexcept { return {bytes.data(), size}; }
};

class StorageService {
public:
    StorageService(Transport& transport, RequestTable& requests) noexcept
        : transport_(transport), requests_(requests) {}

    Task<std::size_t> get(this StorageService self, std::string_view key,
                          std::span<std::byte> output) {
        auto& [transport_, requests_] = self;
        if (!valid_key(key))
            co_return std::unexpected(Error::invalid_argument);
        std::array<std::byte, 68> payload{};
        wire::put16(payload.data(), 1);
        wire::put16(payload.data() + 2,
                    static_cast<std::uint16_t>(key.size()));
        for (std::size_t i = 0; i < key.size(); ++i)
            payload[4 + i] = std::byte(key[i]);
        auto event = co_await Response(
            transport_, requests_, 6, 1, {payload.data(), 4 + key.size()});
        if (!event) co_return std::unexpected(event.error());
        auto status = check_status(event->payload);
        if (!status) co_return std::unexpected(status.error());
        const auto records = event->payload.subspan(4);
        if (records.size() < 4 || wire::get16(records.data()) != 2)
            co_return std::unexpected(Error::protocol_error);
        const auto size = wire::get16(records.data() + 2);
        if (size > 2048 || records.size() != 4u + size)
            co_return std::unexpected(Error::protocol_error);
        if (output.size() < size)
            co_return std::unexpected(Error::resource_limit);
        for (std::size_t i = 0; i < size; ++i)
            output[i] = records[4 + i];
        co_return size;
    }

    Task<void> set(this StorageService self, std::string_view key,
                   std::span<const std::byte> value,
                   std::span<std::byte> packet) {
        auto& [transport_, requests_] = self;
        if (!valid_key(key) || value.size() > 2048)
            co_return std::unexpected(Error::invalid_argument);
        const auto size = wire::header_bytes + 8 + key.size() + value.size();
        if (packet.size() < size)
            co_return std::unexpected(Error::resource_limit);
        if (size > wire::max_control_bytes)
            co_return std::unexpected(Error::invalid_argument);
        auto* payload = packet.data() + wire::header_bytes;
        wire::put16(payload, 1);
        wire::put16(payload + 2, static_cast<std::uint16_t>(key.size()));
        for (std::size_t i = 0; i < key.size(); ++i)
            payload[4 + i] = std::byte(key[i]);
        auto* record = payload + 4 + key.size();
        wire::put16(record, 2);
        wire::put16(record + 2, static_cast<std::uint16_t>(value.size()));
        for (std::size_t i = 0; i < value.size(); ++i)
            record[4 + i] = value[i];
        auto event = co_await Response(
            transport_, requests_, 6, 2,
            Response::PrebuiltPacket{packet.first(size)});
        if (!event) co_return std::unexpected(event.error());
        auto status = check_status(event->payload);
        if (!status) co_return std::unexpected(status.error());
        if (event->payload.size() != 4)
            co_return std::unexpected(Error::protocol_error);
        co_return Result<void>{};
    }

    Task<void> set(this StorageService self, std::string_view key,
                   std::span<const std::byte> value) {
        std::array<std::byte, 512> packet{};
        co_return co_await self.set(key, value, packet);
    }

    Task<void> remove(this StorageService self, std::string_view key) {
        auto& [transport_, requests_] = self;
        if (!valid_key(key))
            co_return std::unexpected(Error::invalid_argument);
        std::array<std::byte, 68> payload{};
        wire::put16(payload.data(), 1);
        wire::put16(payload.data() + 2,
                    static_cast<std::uint16_t>(key.size()));
        for (std::size_t i = 0; i < key.size(); ++i)
            payload[4 + i] = std::byte(key[i]);
        auto event = co_await Response(
            transport_, requests_, 6, 3, {payload.data(), 4 + key.size()});
        if (!event) co_return std::unexpected(event.error());
        auto status = check_status(event->payload);
        if (!status) co_return std::unexpected(status.error());
        if (event->payload.size() != 4)
            co_return std::unexpected(Error::protocol_error);
        co_return Result<void>{};
    }

    Task<std::size_t> list(this StorageService self, std::string_view after,
                           std::span<StorageKey> output) {
        auto& [transport_, requests_] = self;
        if (!after.empty() && !valid_key(after))
            co_return std::unexpected(Error::invalid_argument);
        std::array<std::byte, 68> payload{};
        if (!after.empty()) {
            wire::put16(payload.data(), 1);
            wire::put16(payload.data() + 2,
                        static_cast<std::uint16_t>(after.size()));
            for (std::size_t i = 0; i < after.size(); ++i)
                payload[4 + i] = std::byte(after[i]);
        }
        auto event = co_await Response(
            transport_, requests_, 6, 4,
            {payload.data(), after.empty() ? 0u : 4u + after.size()});
        if (!event) co_return std::unexpected(event.error());
        auto status = check_status(event->payload);
        if (!status) co_return std::unexpected(status.error());
        auto records = event->payload.subspan(4);
        std::size_t count = 0;
        std::string_view previous;
        for (std::size_t offset = 0; offset < records.size();) {
            if (records.size() - offset < 4 ||
                wire::get16(records.data() + offset) != 1)
                co_return std::unexpected(Error::protocol_error);
            const auto size = wire::get16(records.data() + offset + 2);
            if (records.size() - offset - 4 < size || ++count > 14)
                co_return std::unexpected(Error::protocol_error);
            std::string_view key(
                reinterpret_cast<const char*>(records.data() + offset + 4),
                size);
            if (!valid_key(key) ||
                (!previous.empty() && !(previous < key)))
                co_return std::unexpected(Error::protocol_error);
            previous = key;
            offset += 4 + size;
        }
        if (output.size() < count)
            co_return std::unexpected(Error::resource_limit);
        for (std::size_t offset = 0, index = 0; index < count; ++index) {
            const auto size = wire::get16(records.data() + offset + 2);
            output[index].size = static_cast<std::uint8_t>(size);
            for (std::size_t i = 0; i < size; ++i)
                output[index].bytes[i] = static_cast<char>(
                    std::to_integer<unsigned>(records[offset + 4 + i]));
            offset += 4 + size;
        }
        co_return count;
    }

private:
    static bool valid_key(std::string_view key) noexcept {
        if (key.empty() || key.size() > 64 ||
            !((key[0] >= 'A' && key[0] <= 'Z') ||
              (key[0] >= 'a' && key[0] <= 'z')))
            return false;
        for (std::size_t i = 1; i < key.size(); ++i) {
            const char ch = key[i];
            if (!((ch >= 'A' && ch <= 'Z') ||
                  (ch >= 'a' && ch <= 'z') ||
                  (ch >= '0' && ch <= '9') ||
                  ch == '.' || ch == '_' || ch == '-'))
                return false;
        }
        return true;
    }

    static Result<void> check_status(
        std::span<const std::byte> payload) noexcept {
        if (payload.size() < 4)
            return std::unexpected(Error::protocol_error);
        const auto status = static_cast<std::int32_t>(
            wire::get32(payload.data()));
        if (status == 0) return {};
        if (payload.size() != 4 || status > -1 || status < -16)
            return std::unexpected(Error::protocol_error);
        return std::unexpected(static_cast<Error>(status));
    }

    Transport& transport_;
    RequestTable& requests_;
};

} // namespace pxa
