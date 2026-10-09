#pragma once

#include "core.hpp"

#include <cstring>

namespace pxa::detail {

// Internal owned request storage. Copy only initialized payload bytes when a
// coroutine captures the packet. Transport writes the complete header before
// submission; neither the header nor spare capacity needs a preliminary copy.
template<std::size_t Capacity> struct OwnedRequestPacket {
    static_assert(Capacity >= wire::header_bytes && Capacity <= wire::max_control_bytes);
    std::array<std::byte, Capacity> bytes;
    std::size_t size = 0; // Total packet bytes, including the reserved header.

    OwnedRequestPacket() noexcept = default;
    OwnedRequestPacket(const OwnedRequestPacket& other) noexcept : size(other.size) {
        if (size > wire::header_bytes)
            std::memcpy(bytes.data() + wire::header_bytes,
                        other.bytes.data() + wire::header_bytes,
                        size - wire::header_bytes);
    }
    OwnedRequestPacket(OwnedRequestPacket&& other) noexcept : OwnedRequestPacket(other) {}
    OwnedRequestPacket& operator=(const OwnedRequestPacket&) = delete;
    std::span<std::byte> packet() noexcept { return std::span{bytes}.first(size); }
};

} // namespace pxa::detail
