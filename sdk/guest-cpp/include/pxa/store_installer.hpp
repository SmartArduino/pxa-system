#pragma once

#include "task.hpp"
#include "service_wire.hpp"
#include <concepts>

namespace pxa {

struct StorePackage {
    std::string_view app_id, ticket, sha256;
    std::uint64_t size;
};
struct StoreInstalledApp {
    FixedText<64> app_id;
    FixedText<31> version;
    FixedText<129> identity;
    std::uint64_t sequence = 0;
    bool uninstallable = false;
};
struct StoreDownloadEntry { FixedText<19> filename; FixedText<64> app_id; };
struct StoreDownloadProgress { std::uint64_t request_id, received, total; };

inline Result<StoreDownloadProgress> decode_store_download_progress(const Event& event) noexcept {
    if (event.service != 20 || event.opcode != 0x8001 || event.token || event.payload.size() != 24)
        return std::unexpected(Error::protocol_error);
    const auto* p = event.payload.data();
    StoreDownloadProgress progress{wire::get64(p), wire::get64(p + 8), wire::get64(p + 16)};
    if (!progress.request_id || !progress.total || progress.received > progress.total)
        return std::unexpected(Error::protocol_error);
    return progress;
}

// Privileged service for the built-in Store. Installation still follows the
// Host's user confirmation. Packets capture input before returning lazy Tasks;
// list output is caller-owned until completion/cancellation, with no heap.
class StoreInstallerService {
    using Packet = wire::RequestPacket<362>;
public:
    StoreInstallerService(Transport& transport, RequestTable& requests) noexcept
        : transport_(transport), requests_(requests) {}

    Task<void> install(const StorePackage& package) {
        auto packet = encode(package);
        if (!packet) return Task<void>::failed(packet.error());
        return status_impl(*this, 1, std::move(*packet));
    }
    Task<FixedText<19>> download(const StorePackage& package) {
        auto packet = encode(package);
        if (!packet) return Task<FixedText<19>>::failed(packet.error());
        return download_impl(*this, std::move(*packet));
    }
    Task<void> install_file(std::string_view filename) { return manage(3, filename); }
    Task<void> remove_download(std::string_view filename) { return manage(6, filename); }
    Task<void> launch(std::string_view app_id) { return manage(7, app_id); }
    Task<void> uninstall(std::string_view app_id) { return manage(8, app_id); }
    Task<std::size_t> installed(std::span<StoreInstalledApp> output) {
        return list_impl(*this, output);
    }
    Task<std::size_t> downloads(std::span<StoreDownloadEntry> output) {
        return list_impl(*this, output);
    }
private:
    static bool valid_filename(std::string_view value) noexcept {
        if (value.size() != 19 || !value.starts_with(".store-") || !value.ends_with(".pxa")) return false;
        for (auto c : value.substr(7, 8))
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        return true;
    }
    static Result<Packet> encode(const StorePackage& package) noexcept {
        const auto& [id, ticket, digest, size] = package;
        if (id.empty() || id.size() > 64 || ticket.empty() || ticket.size() > 255 ||
            id.find('\0') != std::string_view::npos || ticket.find('\0') != std::string_view::npos ||
            digest.size() != 64 || !size) return std::unexpected(Error::invalid_argument);
        if (size > 4194304) return std::unexpected(Error::limit_exceeded);
        Packet packet;
        wire::Writer writer(packet.bytes);
        writer.u8(static_cast<std::uint8_t>(id.size()));
        writer.u16(static_cast<std::uint16_t>(ticket.size())); writer.u64(size);
        auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        for (std::size_t i = 0; i < 64; i += 2) {
            auto high = hex(digest[i]), low = hex(digest[i + 1]);
            if (high < 0 || low < 0) return std::unexpected(Error::invalid_argument);
            writer.u8(static_cast<std::uint8_t>((high << 4) | low));
        }
        writer.bytes(std::as_bytes(std::span(id)));
        writer.bytes(std::as_bytes(std::span(ticket)));
        packet.size = writer.size();
        return packet;
    }
    Task<void> manage(std::uint16_t opcode, std::string_view value) {
        if ((opcode == 3 || opcode == 6) ? !valid_filename(value) :
            (value.empty() || value.size() > 64 || value.find('\0') != std::string_view::npos))
            return Task<void>::failed(Error::invalid_argument);
        wire::RequestPacket<64> packet;
        std::memcpy(packet.bytes.data(), value.data(), value.size()); packet.size = value.size();
        return status_impl(*this, opcode, std::move(packet));
    }
    template<std::size_t Capacity>
    static Task<void> status_impl(StoreInstallerService self, std::uint16_t opcode,
        wire::RequestPacket<Capacity> packet) {
        auto event = co_await Response(self.transport_, self.requests_, 20, opcode, packet.packet());
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        if (!body->empty()) co_return std::unexpected(Error::protocol_error);
        co_return Result<void>{};
    }
    static Task<FixedText<19>> download_impl(StoreInstallerService self, Packet packet) {
        auto event = co_await Response(self.transport_, self.requests_, 20, 2, packet.packet());
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        FixedText<19> filename;
        if (!filename.assign(*body) || !valid_filename(filename.view()))
            co_return std::unexpected(Error::protocol_error);
        co_return filename;
    }
    template<class Entry>
    static Task<std::size_t> list_impl(StoreInstallerService self, std::span<Entry> output) {
        constexpr bool installed = std::same_as<Entry, StoreInstalledApp>;
        auto event = co_await Response(self.transport_, self.requests_, 20, installed ? 4 : 5, {});
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        if (body->empty()) co_return std::unexpected(Error::protocol_error);
        const auto count = std::to_integer<unsigned>((*body)[0]);
        if (count > (installed ? 12u : 8u)) co_return std::unexpected(Error::protocol_error);
        if (count > output.size()) co_return std::unexpected(Error::resource_limit);
        auto bytes = body->subspan(1);
        for (std::size_t i = 0; i < count; ++i) {
            Entry entry;
            if constexpr (installed) {
                if (bytes.size() < 12) co_return std::unexpected(Error::protocol_error);
                auto id = std::to_integer<unsigned>(bytes[0]), version = std::to_integer<unsigned>(bytes[1]),
                     identity = std::to_integer<unsigned>(bytes[2]), flags = std::to_integer<unsigned>(bytes[11]);
                if (bytes.size() < 12u + id + version + identity || (flags & ~1u) ||
                    !entry.app_id.assign(bytes.subspan(12, id)) ||
                    (version && !entry.version.assign(bytes.subspan(12 + id, version))) ||
                    !entry.identity.assign(bytes.subspan(12 + id + version, identity)))
                    co_return std::unexpected(Error::protocol_error);
                entry.sequence = wire::get64(bytes.data() + 3); entry.uninstallable = flags & 1;
                bytes = bytes.subspan(12 + id + version + identity);
            } else {
                if (bytes.size() < 20) co_return std::unexpected(Error::protocol_error);
                auto id = std::to_integer<unsigned>(bytes[0]);
                if (bytes.size() < 20u + id || !entry.filename.assign(bytes.subspan(1, 19)) ||
                    !valid_filename(entry.filename.view()) || !entry.app_id.assign(bytes.subspan(20, id)))
                    co_return std::unexpected(Error::protocol_error);
                bytes = bytes.subspan(20 + id);
            }
            output[i] = std::move(entry);
        }
        if (!bytes.empty()) co_return std::unexpected(Error::protocol_error);
        co_return count;
    }
    Transport& transport_;
    RequestTable& requests_;
};

} // namespace pxa
