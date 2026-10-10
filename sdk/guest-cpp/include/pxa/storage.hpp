#pragma once

#include "task.hpp"
#include "binary.hpp"
#include "codec.hpp"
#include "request_packet.hpp"

#include <array>
#include <cstring>
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

    // The key and value are owned by the task as soon as this call returns.
    // Small scalar operations use one coroutine and a precisely bounded packet,
    // without the generic set() overload's 512-byte temporary buffer.
    template<binary::Scalar T>
    Task<T> get_value(this StorageService self, std::string_view key) {
        KeyPayload payload;
        if (!encode_key(key, payload.bytes))
            return Task<T>::failed(Error::invalid_argument);
        payload.size = 4 + key.size();
        return get_value_impl<T>(self, payload);
    }

    template<binary::Scalar T>
    Task<void> set_value(this StorageService self, std::string_view key, T value) {
        Packet<wire::header_bytes + 8 + 64 + sizeof(T)> packet;
        const auto encoded = binary::encode(value);
        auto size = encode_set<false>(key, encoded, packet.bytes);
        if (!size) return Task<void>::failed(size.error());
        packet.size = *size;
        return set_owned(self, packet);
    }

    // Explicit custom schema, sharing the same one-coroutine request path.
    // The codec decodes directly from the response; there is no intermediate
    // max-size value buffer. Encoding snapshots the object during this call.
    template<binary::ValueCodec Codec>
    Task<typename Codec::value_type> get_value(this StorageService self,
                                               std::string_view key) {
        static_assert(Codec::max_bytes <= 2048, "Storage values are limited to 2048 bytes");
        KeyPayload payload;
        if (!encode_key(key, payload.bytes))
            return Task<typename Codec::value_type>::failed(Error::invalid_argument);
        payload.size = 4 + key.size();
        return get_codec_impl<Codec>(self, payload);
    }

    template<binary::ValueCodec Codec>
    Task<void> set_value(this StorageService self, std::string_view key,
                         const typename Codec::value_type& value) {
        static_assert(Codec::max_bytes <= 2048, "Storage values are limited to 2048 bytes");
        if (!valid_key(key)) return Task<void>::failed(Error::invalid_argument);
        Packet<wire::header_bytes + 8 + 64 + Codec::max_bytes> packet;
        auto* payload = packet.bytes.data() + wire::header_bytes;
        write_key(key, payload);
        auto* record = payload + 4 + key.size();
        auto size = binary::encode<Codec>(value,
            std::span{record + 4, std::size_t{Codec::max_bytes}});
        if (!size) return Task<void>::failed(size.error());
        wire::put16(record, 2);
        wire::put16(record + 2, static_cast<std::uint16_t>(*size));
        packet.size = wire::header_bytes + 8 + key.size() + *size;
        return set_owned(self, packet);
    }

    // Large codecs can encode into an explicitly borrowed packet without
    // reserving their max_bytes in a coroutine slot. The source object must
    // not alias packet; key is snapshotted before the codec writes any bytes.
    template<binary::ValueCodec Codec>
    Task<void> set_value(this StorageService self, std::string_view key,
                         const typename Codec::value_type& value,
                         std::span<std::byte> packet) {
        static_assert(Codec::max_bytes <= 2048, "Storage values are limited to 2048 bytes");
        if (!valid_key(key)) return Task<void>::failed(Error::invalid_argument);
        const auto prefix = wire::header_bytes + 8 + key.size();
        if (packet.size() < prefix)
            return Task<void>::failed(Error::resource_limit);
        std::array<std::byte,68> key_snapshot;
        write_key(key,key_snapshot.data());
        auto size = binary::encode<Codec>(value,packet.subspan(prefix));
        if (!size) return Task<void>::failed(size.error());
        std::memcpy(packet.data()+wire::header_bytes,key_snapshot.data(),4+key.size());
        auto* record = packet.data()+prefix-4;
        wire::put16(record,2);
        wire::put16(record+2,static_cast<std::uint16_t>(*size));
        return set_borrowed(self,packet.first(prefix+*size));
    }

    Task<std::size_t> get(this StorageService self, std::string_view key,
                          std::span<std::byte> output) {
        KeyPayload payload;
        if (!encode_key(key, payload.bytes))
            return Task<std::size_t>::failed(Error::invalid_argument);
        payload.size = 4 + key.size();
        return get_impl(self, payload, output);
    }

    // Inputs are encoded now. Only packet/output storage is borrowed until
    // completion or cancellation; do not reuse it for another pending request.
    Task<void> set(this StorageService self, std::string_view key,
                   std::span<const std::byte> value,
                   std::span<std::byte> packet) {
        auto size = encode_set<true>(key, value, packet);
        if (!size) return Task<void>::failed(size.error());
        return set_borrowed(self, packet.first(*size));
    }

    Task<void> set(this StorageService self, std::string_view key,
                   std::span<const std::byte> value) {
        Packet<512> packet;
        auto size = encode_set<false>(key, value, packet.bytes);
        if (!size) return Task<void>::failed(size.error());
        packet.size = *size;
        return set_owned(self, packet);
    }

    Task<void> remove(this StorageService self, std::string_view key) {
        KeyPayload payload;
        if (!encode_key(key, payload.bytes))
            return Task<void>::failed(Error::invalid_argument);
        payload.size = 4 + key.size();
        return remove_impl(self, payload);
    }

    Task<std::size_t> list(this StorageService self, std::string_view after,
                           std::span<StorageKey> output) {
        KeyPayload payload;
        if (!after.empty()) {
            if (!encode_key(after, payload.bytes))
                return Task<std::size_t>::failed(Error::invalid_argument);
            payload.size = 4 + after.size();
        }
        return list_impl(self, payload, output);
    }

private:
    struct KeyPayload {
        std::array<std::byte, 68> bytes{};
        std::size_t size = 0;
    };
    template<std::size_t Capacity> using Packet = detail::OwnedRequestPacket<Capacity>;

    static Task<std::size_t> list_impl(StorageService self, KeyPayload payload,
                                      std::span<StorageKey> output) {
        auto event = co_await Response(
            self.transport_, self.requests_, 6, 4,
            std::span{payload.bytes}.first(payload.size));
        if (!event) co_return std::unexpected(event.error());
        auto status = check_status(event->payload);
        if (!status) co_return std::unexpected(status.error());
        auto records = event->payload.subspan(4);
        std::size_t count = 0;
        std::string_view previous(reinterpret_cast<const char*>(payload.bytes.data() + 4),
                                  payload.size ? payload.size - 4 : 0);
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

    template<bool MayAlias>
    static Result<std::size_t> encode_set(std::string_view key,
        std::span<const std::byte> value, std::span<std::byte> packet) noexcept {
        if (value.size() > 2048 || !valid_key(key))
            return std::unexpected(Error::invalid_argument);
        const auto size = wire::header_bytes + 8 + key.size() + value.size();
        if (packet.size() < size) return std::unexpected(Error::resource_limit);
        auto* payload = packet.data() + wire::header_bytes;
        auto* record = payload + 4 + key.size();
        if constexpr (MayAlias) {
            // External inputs may overlap packet: snapshot the key first.
            std::array<std::byte, 68> encoded_key;
            write_key(key, encoded_key.data());
            if (!value.empty()) std::memmove(record + 4, value.data(), value.size());
            std::memcpy(payload, encoded_key.data(), 4 + key.size());
        } else {
            // Fresh owned storage cannot alias either input.
            write_key(key, payload);
            if (!value.empty()) std::memcpy(record + 4, value.data(), value.size());
        }
        wire::put16(record, 2);
        wire::put16(record + 2, static_cast<std::uint16_t>(value.size()));
        return size;
    }
    static bool encode_key(std::string_view key, std::span<std::byte> output) noexcept {
        if (!valid_key(key) || output.size() < 4 + key.size()) return false;
        write_key(key, output.data());
        return true;
    }
    static void write_key(std::string_view key, std::byte* output) noexcept {
        wire::put16(output, 1);
        wire::put16(output + 2, static_cast<std::uint16_t>(key.size()));
        for (std::size_t i = 0; i < key.size(); ++i) output[4 + i] = std::byte(key[i]);
    }
    template<binary::Scalar T>
    static Task<T> get_value_impl(StorageService self, KeyPayload payload) {
        auto event = co_await Response(self.transport_, self.requests_, 6, 1,
            std::span{payload.bytes}.first(payload.size));
        if (!event) co_return std::unexpected(event.error());
        auto value = decode_value(event->payload);
        if (!value) co_return std::unexpected(value.error());
        co_return binary::decode<T>(*value);
    }

    static Task<std::size_t> get_impl(StorageService self, KeyPayload payload,
                                     std::span<std::byte> output) {
        auto event = co_await Response(self.transport_, self.requests_, 6, 1,
            std::span{payload.bytes}.first(payload.size));
        if (!event) co_return std::unexpected(event.error());
        auto value = decode_value(event->payload);
        if (!value) co_return std::unexpected(value.error());
        if (output.size() < value->size()) co_return std::unexpected(Error::resource_limit);
        if (!value->empty()) std::memmove(output.data(), value->data(), value->size());
        co_return value->size();
    }

    template<binary::ValueCodec Codec>
    static Task<typename Codec::value_type> get_codec_impl(StorageService self,
                                                           KeyPayload payload) {
        auto event = co_await Response(self.transport_, self.requests_, 6, 1,
            std::span{payload.bytes}.first(payload.size));
        if (!event) co_return std::unexpected(event.error());
        auto value = decode_value(event->payload);
        if (!value) co_return std::unexpected(value.error());
        co_return binary::decode<Codec>(*value);
    }

    static Result<std::span<const std::byte>> decode_value(
        std::span<const std::byte> payload) noexcept {
        auto status = check_status(payload);
        if (!status) return std::unexpected(status.error());
        const auto records = payload.subspan(4);
        if (records.size() < 4 || wire::get16(records.data()) != 2)
            return std::unexpected(Error::protocol_error);
        const auto size = wire::get16(records.data() + 2);
        if (size > 2048 || records.size() != 4u + size)
            return std::unexpected(Error::protocol_error);
        return records.subspan(4);
    }

    static Task<void> remove_impl(StorageService self, KeyPayload payload) {
        auto event = co_await Response(self.transport_, self.requests_, 6, 3,
            std::span{payload.bytes}.first(payload.size));
        if (!event) co_return std::unexpected(event.error());
        co_return decode_status(event->payload);
    }

    static Task<void> set_borrowed(StorageService self, std::span<std::byte> packet) {
        auto event = co_await Response(self.transport_, self.requests_, 6, 2,
            Response::PrebuiltPacket{packet});
        co_return event ? decode_status(event->payload) : Result<void>{std::unexpected(event.error())};
    }

    template<std::size_t Capacity>
    static Task<void> set_owned(StorageService self, Packet<Capacity> packet) {
        auto event = co_await Response(self.transport_, self.requests_, 6, 2,
            Response::PrebuiltPacket{std::span{packet.bytes}.first(packet.size)});
        co_return event ? decode_status(event->payload) : Result<void>{std::unexpected(event.error())};
    }

    static Result<void> decode_status(std::span<const std::byte> payload) noexcept {
        auto status = check_status(payload);
        if (!status) return std::unexpected(status.error());
        if (payload.size() != 4) return std::unexpected(Error::protocol_error);
        return {};
    }

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
