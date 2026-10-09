// Generated from spec/draft/pxa-device.json. Do not edit by hand.
#pragma once
#include "service_wire.hpp"

namespace pxa {
struct DeviceRuntimeInfo {
    FixedText<31> target;
    FixedText<23> architecture;
    FixedText<23> engine;
    FixedText<79> engine_abi;
    std::uint32_t formats = 0;
};

inline Result<DeviceRuntimeInfo> decode_device_runtime_info(
    std::span<const std::byte> payload) noexcept {
    auto body = wire::result_body(payload);
    if (!body) return std::unexpected(body.error());
    wire::Records records(*body);
    DeviceRuntimeInfo output;
    auto target = records.take(1);
    if (!target || !output.target.assign(*target))
        return std::unexpected(Error::protocol_error);
    auto architecture = records.take(2);
    if (!architecture || !output.architecture.assign(*architecture))
        return std::unexpected(Error::protocol_error);
    auto engine = records.take(3);
    if (!engine || !output.engine.assign(*engine))
        return std::unexpected(Error::protocol_error);
    auto engine_abi = records.take(4);
    if (!engine_abi || !output.engine_abi.assign(*engine_abi))
        return std::unexpected(Error::protocol_error);
    auto formats = records.take(5, 4);
    if (!formats || !records.empty())
        return std::unexpected(Error::protocol_error);
    output.formats = wire::get32(formats->data());
    return output;
}
} // namespace pxa
