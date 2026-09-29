#pragma once

#include "task.hpp"
#include "service_wire.hpp"

#include <optional>
#include <string_view>

namespace pxa {

enum class OpenMode : std::uint32_t {
    read = 1, write = 2, create = 4, exclusive = 8,
    truncate = 16, append = 32, directory = 64
};
constexpr OpenMode operator|(OpenMode a, OpenMode b) noexcept {
    return static_cast<OpenMode>(static_cast<std::uint32_t>(a) |
                                 static_cast<std::uint32_t>(b));
}
enum class FileKind : std::uint8_t { regular = 1, directory = 2 };
enum class SeekOrigin : std::uint8_t { start, current, end };
struct FileInfo { FileKind kind; std::uint64_t size; };
struct DirectoryEntry {
    std::array<char, 64> bytes{};
    std::uint8_t name_bytes = 0;
    FileInfo info{};
    std::string_view name() const noexcept { return {bytes.data(), name_bytes}; }
};

namespace fs_detail {
inline bool valid_path(std::string_view path) noexcept {
    if (path.empty() || path.size() > 255 || path.front() == '/' ||
        path.back() == '/') return false;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= path.size(); ++i) {
        if (i != path.size() && path[i] != '/') {
            const auto byte = static_cast<unsigned char>(path[i]);
            if (byte < 0x20 || byte == 0x7f || byte == '\\') return false;
            continue;
        }
        const auto segment = path.substr(start, i - start);
        if (segment.empty() || segment.size() > 64 || segment == "." ||
            segment == ".." || segment.starts_with(".pxa-")) return false;
        start = i + 1;
    }
    for (std::size_t i = 0; i < path.size();) {
        const auto first = static_cast<unsigned char>(path[i++]);
        if (first < 0x80) continue;
        unsigned code, minimum, count;
        if ((first & 0xe0) == 0xc0) { code = first & 31; minimum = 0x80; count = 1; }
        else if ((first & 0xf0) == 0xe0) { code = first & 15; minimum = 0x800; count = 2; }
        else if ((first & 0xf8) == 0xf0) { code = first & 7; minimum = 0x10000; count = 3; }
        else return false;
        if (count > path.size() - i) return false;
        while (count--) {
            const auto next = static_cast<unsigned char>(path[i++]);
            if ((next & 0xc0) != 0x80) return false;
            code = (code << 6) | (next & 63);
        }
        if (code < minimum || code > 0x10ffff ||
            (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    return true;
}

inline bool valid_mode(OpenMode mode) noexcept {
    const auto flags = static_cast<std::uint32_t>(mode);
    return flags && !(flags & ~127u) && (flags & 3) &&
        (!(flags & 64) || flags == 65) &&
        (!(flags & 8) || (flags & 4)) &&
        (!(flags & 48) || (flags & 2));
}
template<class T> Task<T> failure(Error error) {
    co_return std::unexpected(error);
}
template<std::size_t Capacity> struct Payload {
    std::array<std::byte, wire::header_bytes + Capacity> bytes{};
    std::size_t size = 0;
    std::span<const std::byte> view() const noexcept {
        return {bytes.data() + wire::header_bytes, size};
    }
    std::span<std::byte> packet() noexcept {
        return {bytes.data(), wire::header_bytes + size};
    }
};
template<std::size_t N>
inline void path_record(Payload<N>& payload, std::uint16_t tag,
                         std::string_view path) noexcept {
    auto* out = payload.bytes.data() + wire::header_bytes + payload.size;
    wire::put16(out, tag);
    wire::put16(out + 2, static_cast<std::uint16_t>(path.size()));
    for (std::size_t i = 0; i < path.size(); ++i) out[4 + i] = std::byte(path[i]);
    payload.size += 4 + path.size();
}
template<std::size_t N>
Task<void> status(Transport& transport, RequestTable& requests,
                   std::uint16_t opcode, Payload<N> payload) {
    auto event = co_await Response(transport, requests, 5, opcode,
                                   Response::PrebuiltPacket{payload.packet()});
    if (!event) co_return std::unexpected(event.error());
    auto body = wire::result_body(event->payload);
    if (!body) co_return std::unexpected(body.error());
    if (!body->empty()) co_return std::unexpected(Error::protocol_error);
    co_return Result<void>{};
}
inline Task<std::uint64_t> seek(Transport& transport, RequestTable& requests,
                                 std::uint64_t handle, std::int64_t offset,
                                 SeekOrigin origin) {
    std::array<std::byte, 17> payload{};
    wire::put64(payload.data(), handle);
    wire::put64(payload.data() + 8, static_cast<std::uint64_t>(offset));
    payload[16] = std::byte(origin);
    auto event = co_await Response(transport, requests, 5, 6, payload);
    if (!event) co_return std::unexpected(event.error());
    auto body = wire::result_body(event->payload);
    if (!body) co_return std::unexpected(body.error());
    if (body->size() != 8) co_return std::unexpected(Error::protocol_error);
    co_return wire::get64(body->data());
}
inline Task<std::optional<DirectoryEntry>> next(
    Transport& transport, RequestTable& requests, std::uint64_t handle) {
    std::array<std::byte, 8> payload{};
    wire::put64(payload.data(), handle);
    auto event = co_await Response(transport, requests, 5, 7, payload);
    if (!event) co_return std::unexpected(event.error());
    auto body = wire::result_body(event->payload);
    if (!body) co_return std::unexpected(body.error());
    if (body->size() == 4 && wire::get16(body->data()) == 7 &&
        wire::get16(body->data() + 2) == 0)
        co_return std::optional<DirectoryEntry>{};
    if (body->size() < 4) co_return std::unexpected(Error::protocol_error);
    const auto name_size = wire::get16(body->data() + 2);
    wire::Records records(*body);
    auto name = records.take(4, name_size);
    auto kind = records.take(5, 1);
    auto size = records.take(6, 8);
    if (!name || !kind || !size || !records.empty() || name_size > 64)
        co_return std::unexpected(Error::protocol_error);
    std::string_view text(reinterpret_cast<const char*>(name->data()), name_size);
    const auto type = std::to_integer<unsigned>((*kind)[0]);
    if (!valid_path(text) || text.find('/') != text.npos || (type != 1 && type != 2))
        co_return std::unexpected(Error::protocol_error);
    DirectoryEntry entry;
    entry.name_bytes = static_cast<std::uint8_t>(name_size);
    entry.info = {static_cast<FileKind>(type), wire::get64(size->data())};
    for (std::size_t i = 0; i < name_size; ++i) entry.bytes[i] = text[i];
    co_return std::optional<DirectoryEntry>{entry};
}
} // namespace fs_detail

struct FileTag {};
class File {
public:
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&&) noexcept = default;
    File& operator=(File&&) noexcept = default;
    std::uint64_t handle() const noexcept { return resource_.handle(); }
    explicit operator bool() const noexcept { return static_cast<bool>(resource_); }
    void close() noexcept { resource_.reset(); }
    Result<std::uint32_t> read(std::span<std::byte> buffer) noexcept {
        return transfer(1, buffer);
    }
    Result<std::uint32_t> write(std::span<const std::byte> buffer) noexcept {
        return transfer(2, {const_cast<std::byte*>(buffer.data()), buffer.size()});
    }
    Task<std::uint64_t> seek(std::int64_t offset,
                              SeekOrigin origin = SeekOrigin::start) {
        if (!resource_ || static_cast<unsigned>(origin) > 2)
            return fs_detail::failure<std::uint64_t>(Error::invalid_argument);
        return fs_detail::seek(*transport_, *requests_, handle(), offset, origin);
    }
    Task<std::optional<DirectoryEntry>> next() {
        if (!resource_ || !directory_)
            return fs_detail::failure<std::optional<DirectoryEntry>>(Error::bad_state);
        return fs_detail::next(*transport_, *requests_, handle());
    }
private:
    friend class FilesystemService;
    File(Transport& transport, RequestTable& requests,
         std::uint64_t handle, bool directory) noexcept
        : resource_(transport, handle), transport_(&transport),
          requests_(&requests), directory_(directory) {}
    Result<std::uint32_t> transfer(std::uint32_t operation,
                                   std::span<std::byte> buffer) noexcept {
        if (!resource_ || directory_)
            return std::unexpected(Error::bad_state);
        auto result = transport_->io(handle(), operation, buffer);
        if (result && *result > buffer.size())
            return std::unexpected(Error::protocol_error);
        return result;
    }
    Resource<FileTag> resource_;
    Transport* transport_ = nullptr;
    RequestTable* requests_ = nullptr;
    bool directory_ = false;
};

class FilesystemService {
public:
    FilesystemService(Transport& transport, RequestTable& requests) noexcept
        : transport_(transport), requests_(requests) {}

    Task<File> open(std::string_view path, OpenMode mode = OpenMode::read) {
        if (!fs_detail::valid_path(path) || !fs_detail::valid_mode(mode))
            return fs_detail::failure<File>(Error::invalid_argument);
        fs_detail::Payload<267> payload;
        fs_detail::path_record(payload, 1, path);
        auto* out = payload.bytes.data() + wire::header_bytes + payload.size;
        wire::put16(out, 3);
        wire::put16(out + 2, 4);
        wire::put32(out + 4, static_cast<std::uint32_t>(mode));
        payload.size += 8;
        return open_request(transport_, requests_, payload,
                            (static_cast<std::uint32_t>(mode) & 64) != 0);
    }
    Task<File> directory(std::string_view path) {
        return open(path, OpenMode::read | OpenMode::directory);
    }
    Task<void> make_directory(std::string_view path) { return path_status(2, path); }
    Task<void> remove(std::string_view path) { return path_status(3, path); }
    Task<void> rename(std::string_view source, std::string_view destination) {
        if (!fs_detail::valid_path(source) || !fs_detail::valid_path(destination) ||
            source == destination)
            return fs_detail::failure<void>(Error::invalid_argument);
        fs_detail::Payload<518> payload;
        fs_detail::path_record(payload, 1, source);
        fs_detail::path_record(payload, 2, destination);
        return fs_detail::status(transport_, requests_, 4, payload);
    }
    Task<FileInfo> stat(std::string_view path) {
        if (!fs_detail::valid_path(path))
            return fs_detail::failure<FileInfo>(Error::invalid_argument);
        fs_detail::Payload<259> payload;
        fs_detail::path_record(payload, 1, path);
        return stat_request(transport_, requests_, payload);
    }
private:
    Task<void> path_status(std::uint16_t opcode, std::string_view path) {
        if (!fs_detail::valid_path(path))
            return fs_detail::failure<void>(Error::invalid_argument);
        fs_detail::Payload<259> payload;
        fs_detail::path_record(payload, 1, path);
        return fs_detail::status(transport_, requests_, opcode, payload);
    }
    static Task<File> open_request(Transport& transport, RequestTable& requests,
                                    fs_detail::Payload<267> payload, bool directory) {
        auto event = co_await Response(transport, requests, 5, 1, payload.view(), true);
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        if (body->size() != 8 || !(wire::get64(body->data()) >> 32))
            co_return std::unexpected(Error::protocol_error);
        co_return File(transport, requests, wire::get64(body->data()), directory);
    }
    static Task<FileInfo> stat_request(Transport& transport, RequestTable& requests,
                                        fs_detail::Payload<259> payload) {
        auto event = co_await Response(transport, requests, 5, 5, payload.view());
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        if (body->size() != 9 || ((*body)[0] != std::byte{1} && (*body)[0] != std::byte{2}))
            co_return std::unexpected(Error::protocol_error);
        co_return FileInfo{static_cast<FileKind>(std::to_integer<unsigned>((*body)[0])),
                          wire::get64(body->data() + 1)};
    }
    Transport& transport_;
    RequestTable& requests_;
};

} // namespace pxa
