#pragma once

#include "device_runtime_info_generated.hpp"
#include "permission.hpp"

namespace pxa {

enum class MacKind : std::uint16_t {
    wifi_station_hardware = 1, wifi_softap_hardware = 2,
    bluetooth_hardware = 3, ethernet_hardware = 4, wifi_station_current = 5
};
enum class MacFlags : std::uint32_t {
    hardware = 1, current = 2, locally_administered = 4
};
struct DeviceMac {
    MacKind kind;
    std::array<std::byte, 6> address{};
    std::uint32_t flags = 0;
    bool has(MacFlags flag) const noexcept {
        return (flags & static_cast<std::uint32_t>(flag)) != 0;
    }
};

class DeviceService {
public:
    DeviceService(Transport& transport, RequestTable& requests) noexcept
        : transport_(transport), requests_(requests) {}

    Task<DeviceRuntimeInfo> runtime_info(this DeviceService self) {
        auto response = co_await Response(self.transport_, self.requests_, 15, 2,
            std::span<const std::byte>{});
        if (!response) co_return std::unexpected(response.error());
        co_return decode_device_runtime_info(response->payload);
    }

    Task<DeviceMac> mac(MacKind kind, const Permission& permission) {
        const auto value = static_cast<std::uint16_t>(kind);
        if (value < 1 || value > 5 || !(permission.handle() >> 32))
            return Task<DeviceMac>::failed(Error::invalid_argument);
        wire::RequestPacket<18> packet;
        wire::Writer writer(std::span(packet.bytes).subspan(wire::header_bytes));
        writer.u16(1); writer.u16(2); writer.u16(value);
        writer.u16(2); writer.u16(8); writer.u64(permission.handle());
        packet.size = writer.size();
        return mac_impl(*this, packet, kind);
    }

private:
    static Task<DeviceMac> mac_impl(DeviceService self,
        wire::RequestPacket<18> packet, MacKind kind) {
        auto response = co_await Response(self.transport_, self.requests_, 15, 1,
            Response::PrebuiltPacket{packet.packet()});
        if (!response) co_return std::unexpected(response.error());
        auto body = wire::result_body(response->payload);
        if (!body) co_return std::unexpected(body.error());
        wire::Records records(*body);
        auto encoded_kind = records.take(1, 2);
        auto address = records.take(2, 6);
        auto flags = records.take(3, 4);
        if (!encoded_kind || !address || !flags || !records.empty() ||
            wire::get16(encoded_kind->data()) != static_cast<std::uint16_t>(kind))
            co_return std::unexpected(Error::protocol_error);
        DeviceMac output{kind};
        for (std::size_t i = 0; i < 6; ++i) output.address[i] = (*address)[i];
        output.flags = wire::get32(flags->data());
        co_return output;
    }
    Transport& transport_;
    RequestTable& requests_;
};

} // namespace pxa
