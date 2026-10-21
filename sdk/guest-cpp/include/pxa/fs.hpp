#pragma once

#include "task.hpp"
#include "service_wire.hpp"
#include "permission.hpp"

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
    return Task<T>::failed(error);
}
template<std::size_t Capacity> using Payload = wire::RequestPacket<Capacity>;
struct BorrowedPayload {
    std::span<std::byte> bytes;
    std::span<std::byte> packet() noexcept { return bytes; }
};
template<std::size_t N>
inline void path_record(Payload<N>& payload, std::uint16_t tag,
                         std::string_view path) noexcept {
    auto* out = payload.bytes.data() + payload.size;
    wire::put16(out, tag);
    wire::put16(out + 2, static_cast<std::uint16_t>(path.size()));
    for (std::size_t i = 0; i < path.size(); ++i) out[4 + i] = std::byte(path[i]);
    payload.size += 4 + path.size();
}
template<std::size_t N>
Task<void> status(Transport& transport, RequestTable& requests,
                   std::uint16_t opcode, Payload<N> payload) {
    auto event = co_await Response(transport, requests, 5, opcode,
                                   payload.packet());
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
// Caller-owned storage; the wire header stays private and result spans borrow
// this buffer until its next read. No heap, coroutine or payload copy.
template<std::size_t Capacity>
class FileReadBuffer {
    static_assert(Capacity>0 && Capacity<=4096);
    friend class File;
    std::array<std::byte,24+Capacity> packet_{};
public:
    static constexpr std::size_t capacity=Capacity;
};
class File {
public:
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&&) noexcept = default;
    File& operator=(File&&) noexcept = default;
    std::uint64_t handle() const noexcept { return resource_.handle(); }
    ReadableResource readable() const & noexcept {
        return {transport_, directory_ ? 0 : resource_.handle(), 3};
    }
    explicit operator bool() const noexcept { return static_cast<bool>(resource_); }
    Result<void> close() noexcept { return resource_.close(); }
    Result<std::uint32_t> read(std::span<std::byte> buffer) noexcept {
        return transfer(1, buffer);
    }
    std::uint64_t features() const noexcept { return features_; }
    bool supports_read_at() const noexcept { return (features_&1)!=0; }
    template<std::size_t Capacity>
    Result<std::span<const std::byte>> read_at(std::uint64_t offset,std::uint64_t cookie,
        FileReadBuffer<Capacity>& buffer,std::size_t requested=Capacity) noexcept {
        if(!requested || requested>Capacity)return std::unexpected(Error::invalid_argument);
        auto result=read_at(offset,cookie,std::span{buffer.packet_}.first(24+requested));
        if(!result)return std::unexpected(result.error());
        return std::span<const std::byte>{buffer.packet_}.subspan(24,*result);
    }
    // Borrow header+data storage; result bytes begin at packet.subspan(24).
    Result<std::uint32_t> read_at(std::uint64_t offset,std::uint64_t cookie,
                                std::span<std::byte> packet) noexcept {
        if(!resource_ || directory_)return std::unexpected(Error::bad_state);
        if(!cookie || packet.size()<=24 || packet.size()>24+4096 ||
            offset>UINT64_MAX-(packet.size()-24))return std::unexpected(Error::invalid_argument);
        wire::put64(packet.data(),offset);wire::put64(packet.data()+8,cookie);
        wire::put32(packet.data()+16,static_cast<std::uint32_t>(packet.size()-24));
        wire::put32(packet.data()+20,0);
        auto result=transport_->io(handle(),16,packet);
        if(result && *result>packet.size()-24)return std::unexpected(Error::protocol_error);
        return result;
    }
    Result<void> cancel_read_at(std::uint64_t cookie) noexcept {
        if(!resource_ || directory_)return std::unexpected(Error::bad_state);
        if(!cookie)return std::unexpected(Error::invalid_argument);
        std::array<std::byte,8> packet{};wire::put64(packet.data(),cookie);
        auto result=transport_->io(handle(),17,packet);
        if(!result)return std::unexpected(result.error());
        if(*result)return std::unexpected(Error::protocol_error);
        return {};
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
    friend class Directory;
    File(Transport& transport, RequestTable& requests,
         std::uint64_t handle, bool directory, std::uint64_t features=0) noexcept
        : resource_(transport, handle), transport_(&transport),
          requests_(&requests), directory_(directory),features_(features) {}
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
    std::uint64_t features_=0;
};

struct RootInfo {
    std::array<std::byte,16> volume_id{};
    std::uint64_t mount_epoch=0,capabilities=0,total_bytes=0,free_bytes=0,quota_bytes=0;
    std::uint32_t rights=0,max_read_at_bytes=0;
    std::uint16_t max_path_bytes=0,max_segment_bytes=0,space_known=0;
    std::uint8_t naming=0,state=0;
};
class Directory {
public:
    Directory()=default;
    std::uint64_t handle() const noexcept { return file_.handle(); }
    explicit operator bool() const noexcept { return bool(file_); }
    Result<void> close() noexcept { return file_.close(); }
    Task<File> open(std::string_view path,OpenMode mode=OpenMode::read) const {
        if(!file_ || path.empty() || path.find('\0')!=path.npos ||
            !fs_detail::valid_mode(mode)) return Task<File>::failed(Error::invalid_argument);
        if(path.size()>255) return Task<File>::failed(Error::limit_exceeded);
        fs_detail::Payload<269> payload;
        wire::put64(payload.bytes.data(),handle());
        wire::put32(payload.bytes.data()+8,static_cast<std::uint32_t>(mode));
        wire::put16(payload.bytes.data()+12,static_cast<std::uint16_t>(path.size()));
        for(std::size_t i=0;i<path.size();++i) payload.bytes[14+i]=std::byte(path[i]);
        payload.size=14+path.size();
        return open_impl(*transport_,*requests_,payload,
            (static_cast<std::uint32_t>(mode)&64)!=0);
    }
    Task<RootInfo> info() const {
        if(!file_) return Task<RootInfo>::failed(Error::bad_state);
        return info_impl(*transport_,*requests_,handle());
    }
    // Long paths use caller-owned storage instead of enlarging every coroutine
    // slot. Keep packet unchanged and alive through completion/cancellation.
    Task<File> open(std::string_view path,OpenMode mode,std::span<std::byte> packet) const {
        if(!file_ || path.empty() || path.size()>1024 || path.find('\0')!=path.npos ||
            !fs_detail::valid_mode(mode) || packet.size()<14+path.size())
            return Task<File>::failed(Error::invalid_argument);
        wire::put64(packet.data(),handle());
        wire::put32(packet.data()+8,static_cast<std::uint32_t>(mode));
        wire::put16(packet.data()+12,static_cast<std::uint16_t>(path.size()));
        for(std::size_t i=0;i<path.size();++i) packet[14+i]=std::byte(path[i]);
        return open_impl(*transport_,*requests_,fs_detail::BorrowedPayload{
            packet.first(14+path.size())},(static_cast<std::uint32_t>(mode)&64)!=0);
    }
private:
    friend class FilesystemService;
    Directory(Transport& t,RequestTable& r,std::uint64_t h):file_(t,r,h,true),transport_(&t),requests_(&r){}
    template<class Packet> static Task<File> open_impl(Transport& t,RequestTable& r,
        Packet payload,bool directory) {
        auto event=co_await Response(t,r,5,11,payload.packet());
        if(!event) co_return std::unexpected(event.error());
        auto body=wire::result_body(event->payload);
        if(!body) co_return std::unexpected(body.error());
        auto pending=wire::result_resource<FileTag>(t,*body);
        if(body->size()!=28 || !(pending.handle()>>32) ||
            (*body)[8]!=(directory?std::byte{2}:std::byte{1}) ||
            (*body)[9]!=std::byte{} || (*body)[10]!=std::byte{} || (*body)[11]!=std::byte{})
            co_return std::unexpected(Error::protocol_error);
        co_return File(t,r,pending.release(),directory,wire::get64(body->data()+12));
    }
    static Task<RootInfo> info_impl(Transport& t,RequestTable& r,std::uint64_t h) {
        std::array<std::byte,8> payload{}; wire::put64(payload.data(),h);
        auto event=co_await Response(t,r,5,17,payload);
        if(!event) co_return std::unexpected(event.error());
        auto body=wire::result_body(event->payload);
        if(!body) co_return std::unexpected(body.error());
        if(body->size()!=72) co_return std::unexpected(Error::protocol_error);
        RootInfo info; const auto *p=body->data();
        for(unsigned i=0;i<16;++i) info.volume_id[i]=p[i];
        info.mount_epoch=wire::get64(p+16); info.rights=wire::get32(p+24);
        info.capabilities=wire::get64(p+28); info.max_path_bytes=wire::get16(p+36);
        info.max_segment_bytes=wire::get16(p+38); info.max_read_at_bytes=wire::get32(p+40);
        info.naming=std::to_integer<std::uint8_t>(p[44]); info.state=std::to_integer<std::uint8_t>(p[45]);
        info.space_known=wire::get16(p+46); info.total_bytes=wire::get64(p+48);
        info.free_bytes=wire::get64(p+56); info.quota_bytes=wire::get64(p+64);
        if(!info.mount_epoch || !info.max_path_bytes || !info.max_segment_bytes)
            co_return std::unexpected(Error::protocol_error);
        co_return info;
    }
    File file_;
    Transport *transport_=nullptr; RequestTable *requests_=nullptr;
};
struct DirectoryGrant {
    Directory root;
    std::array<std::byte,16> id{};
    std::uint32_t rights=0;
};

class FilesystemService {
public:
    FilesystemService(Transport& transport, RequestTable& requests) noexcept
        : transport_(transport), requests_(requests) {}

    // As with other service resources, keep permission alive until ALL derived
    // directories/files close. Persistent IDs are safe to store; handles are not.
    Task<DirectoryGrant> request_directory(const Permission& permission,
        std::string_view purpose,bool persistent=true) {
        if(!permission || purpose.empty() || purpose.size()>256 || purpose.find('\0')!=purpose.npos)
            return Task<DirectoryGrant>::failed(Error::invalid_argument);
        fs_detail::Payload<274> p;
        wire::put64(p.bytes.data(),permission.handle()); wire::put32(p.bytes.data()+8,3);
        wire::put32(p.bytes.data()+12,persistent?1:0); wire::put16(p.bytes.data()+16,purpose.size());
        for(std::size_t i=0;i<purpose.size();++i) p.bytes[18+i]=std::byte(purpose[i]);
        p.size=18+purpose.size();
        return directory_request(transport_,requests_,p);
    }
    // Host binds this fixed root to the verified app identity and storage
    // placement. No filesystem permission dialog or directory picker.
    // Filesystem 0.4. Display-only path, available even without mounted media.
    Task<FixedText<255>> app_media_path() { return media_path_request(transport_,requests_); }
    Task<Directory> app_media() {
        fs_detail::Payload<28> p; p.bytes[0]=std::byte{5}; p.size=28;
        return grant_request(transport_,requests_,p);
    }
    Task<Directory> open_grant(const Permission& permission,const std::array<std::byte,16>& id) {
        if(!permission) return Task<Directory>::failed(Error::invalid_argument);
        fs_detail::Payload<28> p; p.bytes[0]=std::byte{4};
        wire::put64(p.bytes.data()+4,permission.handle());
        for(unsigned i=0;i<16;++i) p.bytes[12+i]=id[i]; p.size=28;
        return grant_request(transport_,requests_,p);
    }

    Task<File> open(std::string_view path, OpenMode mode = OpenMode::read) {
        if (!fs_detail::valid_path(path) || !fs_detail::valid_mode(mode))
            return fs_detail::failure<File>(Error::invalid_argument);
        fs_detail::Payload<267> payload;
        fs_detail::path_record(payload, 1, path);
        auto* out = payload.bytes.data() + payload.size;
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
        return move_path(4,source,destination);
    }
    // UI/fs Host 0.2: atomically publish a completed regular file, preserving
    // the prior version on failure. Does not replace directories or open files.
    Task<void> replace(std::string_view source, std::string_view destination) {
        return move_path(8,source,destination);
    }
    Task<void> move_path(std::uint16_t opcode, std::string_view source,
                         std::string_view destination) {
        if (!fs_detail::valid_path(source) || !fs_detail::valid_path(destination) ||
            source == destination)
            return fs_detail::failure<void>(Error::invalid_argument);
        fs_detail::Payload<518> payload;
        fs_detail::path_record(payload, 1, source);
        fs_detail::path_record(payload, 2, destination);
        return fs_detail::status(transport_, requests_, opcode, payload);
    }
    Task<FileInfo> stat(std::string_view path) {
        if (!fs_detail::valid_path(path))
            return fs_detail::failure<FileInfo>(Error::invalid_argument);
        fs_detail::Payload<259> payload;
        fs_detail::path_record(payload, 1, path);
        return stat_request(transport_, requests_, payload);
    }
private:
    static Task<FixedText<255>> media_path_request(Transport& t,RequestTable& r) {
        auto event=co_await Response(t,r,5,18,{});
        if(!event)co_return std::unexpected(event.error());
        auto body=wire::result_body(event->payload);
        if(!body)co_return std::unexpected(body.error());
        if(body->size()<3)co_return std::unexpected(Error::protocol_error);
        auto length=wire::get16(body->data());FixedText<255> text;
        if(!length || body->size()!=2u+length || !text.assign(body->subspan(2)))
            co_return std::unexpected(Error::protocol_error);
        co_return text;
    }
    static Task<DirectoryGrant> directory_request(Transport& t,RequestTable& r,
        fs_detail::Payload<274> p) {
        auto event=co_await Response(t,r,5,10,p.packet());
        if(!event) co_return std::unexpected(event.error());
        auto body=wire::result_body(event->payload);
        if(!body) co_return std::unexpected(body.error());
        auto pending=wire::result_resource<FileTag>(t,*body);
        if(body->size()!=28 || !(pending.handle()>>32)) co_return std::unexpected(Error::protocol_error);
        auto rights=wire::get32(body->data()+8);
        if(!rights || (rights&~31u)) co_return std::unexpected(Error::protocol_error);
        DirectoryGrant grant{Directory(t,r,pending.release()),{},rights};
        for(unsigned i=0;i<16;++i) grant.id[i]=(*body)[12+i];
        co_return std::move(grant);
    }
    static Task<Directory> grant_request(Transport& t,RequestTable& r,
        fs_detail::Payload<28> p) {
        auto event=co_await Response(t,r,5,9,p.packet());
        if(!event) co_return std::unexpected(event.error());
        auto body=wire::result_body(event->payload);
        if(!body) co_return std::unexpected(body.error());
        auto pending=wire::result_resource<FileTag>(t,*body);
        if(body->size()!=12 || !(pending.handle()>>32)) co_return std::unexpected(Error::protocol_error);
        auto rights=wire::get32(body->data()+8);
        if(!rights || (rights&~31u)) co_return std::unexpected(Error::protocol_error);
        co_return Directory(t,r,pending.release());
    }
    Task<void> path_status(std::uint16_t opcode, std::string_view path) {
        if (!fs_detail::valid_path(path))
            return fs_detail::failure<void>(Error::invalid_argument);
        fs_detail::Payload<259> payload;
        fs_detail::path_record(payload, 1, path);
        return fs_detail::status(transport_, requests_, opcode, payload);
    }
    static Task<File> open_request(Transport& transport, RequestTable& requests,
                                    fs_detail::Payload<267> payload, bool directory) {
        auto event = co_await Response(transport, requests, 5, 1,
            payload.packet(), true);
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        auto pending = wire::result_resource<FileTag>(transport, *body);
        if (body->size() != 8 || !(pending.handle() >> 32))
            co_return std::unexpected(Error::protocol_error);
        co_return File(transport, requests, pending.release(), directory);
    }
    static Task<FileInfo> stat_request(Transport& transport, RequestTable& requests,
                                        fs_detail::Payload<259> payload) {
        auto event = co_await Response(transport, requests, 5, 5,
            payload.packet());
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
