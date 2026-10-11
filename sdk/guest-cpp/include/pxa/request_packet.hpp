#pragma once

#include "core.hpp"

#include <cstring>

namespace pxa::detail {

// Owned service payload. Coroutine capture copies only initialized bytes.
template<std::size_t Capacity> struct OwnedRequestPacket {
    static_assert(Capacity > 0 && Capacity <= wire::max_call_payload_bytes);
    std::array<std::byte, Capacity> bytes;
    std::size_t size = 0; // Initialized service payload bytes.

    OwnedRequestPacket() noexcept = default;
    OwnedRequestPacket(const OwnedRequestPacket& other) noexcept : size(other.size) {
        if (size > 0)
            std::memcpy(bytes.data(),
                        other.bytes.data(),
                        size);
    }
    OwnedRequestPacket(OwnedRequestPacket&& other) noexcept : OwnedRequestPacket(other) {}
    OwnedRequestPacket& operator=(const OwnedRequestPacket&) = delete;
    std::span<std::byte> packet() noexcept { return std::span{bytes}.first(size); }
};

} // namespace pxa::detail
