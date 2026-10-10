#include <pxa/net.hpp>

#include <algorithm>

namespace pxa {
namespace {

std::span<const std::byte> bytes(std::string_view text) noexcept {
    return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

bool name_valid(std::string_view name) noexcept {
    if (name.empty() || name.size() > 64) return false;
    for (const char character : name) {
        if ((character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') ||
            std::string_view("!#$%&'*+-.^_`|~").find(character) !=
                std::string_view::npos) continue;
        return false;
    }
    return true;
}

bool value_valid(std::string_view value) noexcept {
    if (value.size() > 256) return false;
    for (const unsigned char character : value)
        if (character != '\t' && (character < 0x20 || character > 0x7e))
            return false;
    return true;
}

bool forbidden(std::string_view name) noexcept {
    for (auto value : {"connection", "content-length", "host",
                       "proxy-connection", "te", "trailer",
                       "transfer-encoding", "upgrade"})
        if (name == value) return true;
    return false;
}

bool record_at(std::span<const std::byte> input, std::size_t& offset,
               std::uint16_t& raw_tag,
               std::span<const std::byte>& value) noexcept {
    if (input.size() - offset < 4) return false;
    const auto* data = input.data() + offset;
    raw_tag = wire::get16(data);
    const auto size = wire::get16(data + 2);
    if (!raw_tag || input.size() - offset - 4 < size) return false;
    value = input.subspan(offset + 4, size);
    offset += 4 + size;
    return true;
}

bool parse_header(std::span<const std::byte> nested,
                  std::string_view& name,
                  std::string_view& value) noexcept {
    std::size_t offset = 0;
    std::uint16_t tag = 0;
    std::span<const std::byte> field;
    if (!record_at(nested, offset, tag, field) || tag != 1) return false;
    name = {reinterpret_cast<const char*>(field.data()), field.size()};
    if (!name_valid(name) ||
        !record_at(nested, offset, tag, field) || tag != 2 ||
        offset != nested.size()) return false;
    value = {reinterpret_cast<const char*>(field.data()), field.size()};
    return value_valid(value);
}

} // namespace

Result<std::size_t> encode_net_request(const NetRequest& request,
    const Permission& permission, std::span<std::byte> packet) noexcept {
    const auto method = static_cast<std::uint16_t>(request.method);
    if (!permission || !(permission.handle() >> 32) ||
        request.url.empty() || request.url.size() > 512 ||
        !(request.url.starts_with("http://") ||
          request.url.starts_with("https://")) ||
        method < 1 || method > 6 ||
        !request.max_response_bytes || request.max_response_bytes > 262144 ||
        request.timeout_ms < 100 || request.timeout_ms > 60000 ||
        request.headers.size() > 8 || request.wanted_headers.size() > 8 ||
        request.body.size() > 2048 ||
        ((method == 1 || method == 2) && !request.body.empty()))
        return std::unexpected(Error::invalid_argument);
    for (const unsigned char c : request.url)
        if (c < 0x21 || c > 0x7e)
            return std::unexpected(Error::invalid_argument);
    if (packet.size() > wire::max_control_bytes)
        packet = packet.first(wire::max_control_bytes);
    if (packet.size() < wire::header_bytes)
        return std::unexpected(Error::resource_limit);
    // Request descriptors and their text/body must survive every record write.
    // Reject destructive aliasing before touching caller storage; no scratch
    // copy proportional to a URL, HTTP body or header set is needed.
    if (wire::overlaps(std::as_bytes(std::span{&request, 1}), packet) ||
        wire::overlaps(bytes(request.url), packet) ||
        wire::overlaps(request.body, packet) ||
        wire::overlaps(std::as_bytes(request.headers), packet) ||
        wire::overlaps(std::as_bytes(request.wanted_headers), packet) ||
        wire::overlaps(std::as_bytes(std::span{&permission, 1}), packet))
        return std::unexpected(Error::invalid_argument);
    for (const auto& header : request.headers)
        if (wire::overlaps(bytes(header.name), packet) ||
            wire::overlaps(bytes(header.value), packet))
            return std::unexpected(Error::invalid_argument);
    for (auto name : request.wanted_headers)
        if (wire::overlaps(bytes(name), packet))
            return std::unexpected(Error::invalid_argument);
    wire::Writer writer(packet.subspan(wire::header_bytes));
    std::array<std::byte, 8> scalar{};
    if (!wire::record(writer, 1, bytes(request.url)))
        return std::unexpected(Error::resource_limit);
    wire::put16(scalar.data(), method);
    if (!wire::record(writer, 2, std::span{scalar}.first(2)))
        return std::unexpected(Error::resource_limit);
    wire::put64(scalar.data(), permission.handle());
    if (!wire::record(writer, 3, scalar))
        return std::unexpected(Error::resource_limit);
    wire::put32(scalar.data(), request.max_response_bytes);
    if (!wire::record(writer, 4, std::span{scalar}.first(4)))
        return std::unexpected(Error::resource_limit);
    wire::put32(scalar.data(), request.timeout_ms);
    if (!wire::record(writer, 8, std::span{scalar}.first(4)))
        return std::unexpected(Error::resource_limit);
    std::size_t header_bytes = 0;
    for (std::size_t index = 0; index < request.headers.size(); ++index) {
        const auto& header = request.headers[index];
        const auto size = 12 + header.name.size() + header.value.size();
        if (!name_valid(header.name) || !value_valid(header.value) ||
            forbidden(header.name) || size > 2048 - header_bytes)
            return std::unexpected(Error::invalid_argument);
        for (std::size_t previous = 0; previous < index; ++previous)
            if (request.headers[previous].name == header.name)
                return std::unexpected(Error::invalid_argument);
        if (writer.remaining() < size ||
            !writer.u16(9) || !writer.u16(static_cast<std::uint16_t>(size - 4)) ||
            !wire::record(writer, 1, bytes(header.name)) ||
            !wire::record(writer, 2, bytes(header.value)))
            return std::unexpected(Error::resource_limit);
        header_bytes += size;
    }
    if (!request.body.empty() && !wire::record(writer, 10, request.body))
        return std::unexpected(Error::resource_limit);
    for (std::size_t index = 0; index < request.wanted_headers.size(); ++index) {
        const auto name = request.wanted_headers[index];
        if (!name_valid(name)) return std::unexpected(Error::invalid_argument);
        for (std::size_t previous = 0; previous < index; ++previous)
            if (request.wanted_headers[previous] == name)
                return std::unexpected(Error::invalid_argument);
        if (!wire::record(writer, 11, bytes(name)))
            return std::unexpected(Error::resource_limit);
    }
    return wire::header_bytes + writer.size();
}

std::uint64_t net_late_body_handle(
    std::span<const std::byte> payload) noexcept {
    if (payload.size() < 4 || wire::get32(payload.data()) != 0) return 0;
    std::size_t offset = 4;
    while (offset < payload.size()) {
        std::uint16_t tag = 0;
        std::span<const std::byte> value;
        if (!record_at(payload, offset, tag, value)) return 0;
        if (tag == 7 && value.size() == 8) {
            const auto handle = wire::get64(value.data());
            return handle >> 32 ? handle : 0;
        }
    }
    return 0;
}

Result<NetResponse> decode_net_response(Transport& transport,
    std::span<const std::byte> payload, std::span<NetHeader> headers,
    std::uint32_t max_response_bytes) noexcept {
    Resource<NetBodyTag> pending(transport, net_late_body_handle(payload));
    auto body = wire::result_body(payload);
    if (!body) return std::unexpected(body.error());
    std::array<std::span<const std::byte>, 8> header_fields{};
    std::size_t header_count = 0;
    std::size_t header_bytes = 0;
    std::uint16_t previous = 0;
    std::uint32_t seen = 0;
    std::uint64_t handle = 0;
    NetResponse result;
    std::size_t offset = 4;
    while (offset < payload.size()) {
        std::uint16_t tag = 0;
        std::span<const std::byte> value;
        if (!record_at(payload, offset, tag, value) || tag < previous)
            return std::unexpected(Error::protocol_error);
        previous = tag;
        if (tag & 0x8000) continue;
        switch (tag) {
        case 5:
            if ((seen & 1) || value.size() != 2)
                return std::unexpected(Error::protocol_error);
            result.status_code = wire::get16(value.data());
            seen |= 1;
            break;
        case 6:
            if ((seen & 2) || value.size() > 96)
                return std::unexpected(Error::protocol_error);
            for (std::size_t i = 0; i < value.size(); ++i) {
                const auto c = std::to_integer<unsigned>(value[i]);
                if (c < 0x20 || c > 0x7e)
                    return std::unexpected(Error::protocol_error);
                result.content_type[i] = static_cast<char>(c);
            }
            result.content_type_size = static_cast<std::uint16_t>(value.size());
            seen |= 2;
            break;
        case 7:
            if ((seen & 4) || value.size() != 8 ||
                !(wire::get64(value.data()) >> 32))
                return std::unexpected(Error::protocol_error);
            handle = wire::get64(value.data());
            seen |= 4;
            break;
        case 9: {
            if (header_count == 8 || value.size() + 4 > 2048 - header_bytes)
                return std::unexpected(Error::protocol_error);
            std::string_view name, text;
            if (!parse_header(value, name, text))
                return std::unexpected(Error::protocol_error);
            for (std::size_t i = 0; i < header_count; ++i) {
                std::string_view prior, ignored;
                (void)parse_header(header_fields[i], prior, ignored);
                if (prior == name)
                    return std::unexpected(Error::protocol_error);
            }
            header_fields[header_count++] = value;
            header_bytes += value.size() + 4;
            break;
        }
        case 12:
            if ((seen & 8) || value.size() != 8)
                return std::unexpected(Error::protocol_error);
            result.body_length = wire::get64(value.data());
            seen |= 8;
            break;
        case 13:
            if ((seen & 16) || value.size() != 4)
                return std::unexpected(Error::protocol_error);
            result.flags = wire::get32(value.data());
            seen |= 16;
            break;
        default:
            return std::unexpected(Error::protocol_error);
        }
    }
    if ((seen & 19) != 19 || result.status_code < 100 ||
        result.status_code > 599 || (result.flags & ~3u) ||
        ((result.flags & 1u) != 0) != ((seen & 4u) != 0) ||
        ((result.flags & 2u) != 0) != ((seen & 8u) != 0) ||
        (!(result.flags & 1u) && result.body_length != 0) ||
        ((result.flags & 2u) && result.body_length > max_response_bytes))
        return std::unexpected(Error::protocol_error);
    if (header_count > headers.size())
        return std::unexpected(Error::resource_limit);
    for (std::size_t i = 0; i < header_count; ++i) {
        std::string_view name, value;
        (void)parse_header(header_fields[i], name, value);
        std::copy(name.begin(), name.end(), headers[i].name.begin());
        std::copy(value.begin(), value.end(), headers[i].value.begin());
        headers[i].name_size = static_cast<std::uint16_t>(name.size());
        headers[i].value_size = static_cast<std::uint16_t>(value.size());
    }
    result.headers = headers.first(header_count);
    if (handle) result.body = NetBody(transport, pending.release());
    return result;
}

} // namespace pxa
