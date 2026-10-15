#include <pxa/http.hpp>

namespace pxa {
namespace {
std::span<const std::byte> bytes(std::string_view s) noexcept {
    return std::as_bytes(std::span{s.data(), s.size()});
}
bool name_valid(std::string_view name) noexcept {
    if (name.empty() || name.size() > 64)
        return false;
    for (unsigned char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              std::string_view("!#$%&'*+-.^_`|~").find(c) != std::string_view::npos))
            return false;
    return true;
}
bool value_valid(std::string_view value) noexcept {
    for (unsigned char c : value)
        if (c != '\t' && (c < 0x20 || c == 0x7f))
            return false;
    return true;
}
} // namespace

Result<HttpRequestPacket> encode_http_request(const HttpRequest &r,
                                              const Permission &permission) noexcept {
    auto method = static_cast<std::uint16_t>(r.method);
    auto upload = r.body.empty() ? r.upload_bytes : r.body.size();
    auto &o = r.options;
    if (!permission || !(permission.handle() >> 32) || r.url.empty() ||
        r.url.size() > 512 ||
        !(r.url.starts_with("http://") || r.url.starts_with("https://")) ||
        method < 1 || method > 6 || o.buffer_bytes < 512 || o.buffer_bytes > 65536 ||
        (o.buffer_bytes & (o.buffer_bytes - 1)) || o.header_bytes < 16 ||
        o.header_bytes > 4096 || !o.max_body_bytes || o.timeout_ms < 100 ||
        o.timeout_ms > 60000 || r.headers.size() > 8 || r.wanted_headers.size() > 8 ||
        upload > INT32_MAX ||
        r.body.size() > 2048 || r.body.size() > o.buffer_bytes / 2 ||
        (!r.body.empty() && r.upload_bytes && r.upload_bytes != upload) ||
        ((method == 1 || method == 2) && upload))
        return std::unexpected(Error::invalid_argument);
    for (unsigned char c : r.url)
        if (c < 0x21 || c > 0x7e)
            return std::unexpected(Error::invalid_argument);
    std::size_t size = 70 + r.url.size() + (r.body.empty() ? 0 : 4 + r.body.size());
    std::size_t header_bytes = 0;
    for (std::size_t i = 0; i < r.headers.size(); ++i) {
        auto [name, value] = r.headers[i];
        if (!name_valid(name) || !value_valid(value))
            return std::unexpected(Error::invalid_argument);
        for (auto forbidden :
             {"connection", "content-length", "host", "proxy-connection", "te",
              "trailer", "transfer-encoding", "upgrade", "expect"})
            if (http_header_equal(name, forbidden))
                return std::unexpected(Error::invalid_argument);
        for (std::size_t j = 0; j < i; ++j)
            if (http_header_equal(name, r.headers[j].name))
                return std::unexpected(Error::invalid_argument);
        if (value.size() > 2048 ||
            name.size() + value.size() + 12 > 2048 - header_bytes)
            return std::unexpected(Error::limit_exceeded);
        header_bytes += name.size() + value.size() + 12;
    }
    size += header_bytes;
    for (std::size_t i = 0; i < r.wanted_headers.size(); ++i) {
        const auto name = r.wanted_headers[i];
        if (!name_valid(name))
            return std::unexpected(Error::invalid_argument);
        for (std::size_t j = 0; j < i; ++j)
            if (http_header_equal(name, r.wanted_headers[j]))
                return std::unexpected(Error::invalid_argument);
        size += 4 + name.size();
    }
    if (size > wire::max_call_payload_bytes)
        return std::unexpected(Error::limit_exceeded);
    HttpRequestPacket packet{
        std::unique_ptr<std::byte[]>(new (std::nothrow) std::byte[size]), size};
    if (!packet.storage)
        return std::unexpected(Error::resource_limit);
    wire::Writer w({packet.storage.get(), size});
    std::array<std::byte, 8> scalar;
    auto u32 = [&](std::uint16_t tag, std::uint32_t v) {
        wire::put32(scalar.data(), v);
        return wire::record(w, tag, std::span{scalar}.first(4));
    };
    auto u64 = [&](std::uint16_t tag, std::uint64_t v) {
        wire::put64(scalar.data(), v);
        return wire::record(w, tag, scalar);
    };
    bool ok = wire::record(w, 1, bytes(r.url));
    wire::put16(scalar.data(), method);
    ok = ok && wire::record(w, 2, std::span{scalar}.first(2)) &&
         u64(3, permission.handle()) && u64(4, o.max_body_bytes) &&
         u32(8, o.timeout_ms);
    for (auto [name, value] : r.headers) {
        ok = ok && w.u16(9) &&
             w.u16(static_cast<std::uint16_t>(8 + name.size() + value.size())) &&
             wire::record(w, 1, bytes(name)) && wire::record(w, 2, bytes(value));
    }
    if (!r.body.empty())
        ok = ok && wire::record(w, 10, r.body);
    for (auto name : r.wanted_headers)
        ok = ok && wire::record(w, 11, bytes(name));
    ok = ok && u32(14, o.buffer_bytes) && u64(15, upload) && u32(16, o.header_bytes);
    if (!ok)
        return std::unexpected(Error::internal);
    packet.size = w.size();
    return packet;
}

Task<HttpExchange> HttpClient::open_impl(HttpClient self, HttpRequestPacket packet) {
    auto event =
        co_await Response(self.transport_, self.requests_, 9, 3, packet.bytes(), true);
    if (!event)
        co_return std::unexpected(event.error());
    auto bytes = wire::result_body(event->payload);
    if (!bytes)
        co_return std::unexpected(bytes.error());
    HttpExchange pending;
    if (bytes->size() >= 8 && wire::get64(bytes->data()) >> 32)
        pending =
            HttpExchange(self.transport_, self.requests_, wire::get64(bytes->data()));
    if (bytes->size() != 8 || !pending)
        co_return std::unexpected(Error::protocol_error);
    co_return std::move(pending);
}
Task<HttpResponse> HttpClient::get_impl(Task<HttpExchange> pending) {
    auto exchange = co_await std::move(pending);
    if (!exchange)
        co_return std::unexpected(exchange.error());
    co_return co_await std::move(*exchange).response();
}
Task<HttpCapabilities> HttpClient::capabilities_impl(HttpClient self) {
    auto event = co_await Response(self.transport_, self.requests_, 9, 4, {});
    if (!event)
        co_return std::unexpected(event.error());
    auto b = wire::result_body(event->payload);
    if (!b)
        co_return std::unexpected(b.error());
    if (b->size() != 32)
        co_return std::unexpected(Error::protocol_error);
    co_return HttpCapabilities{wire::get32(b->data()),      wire::get32(b->data() + 4),
                               wire::get32(b->data() + 8),  wire::get32(b->data() + 12),
                               wire::get32(b->data() + 16), wire::get64(b->data() + 20),
                               wire::get32(b->data() + 28)};
}
Task<HttpResponse> HttpExchange::response() && {
    return response_impl(std::move(*this));
}
Task<HttpResponse> HttpExchange::response_impl(HttpExchange self) {
    if (!self)
        co_return std::unexpected(Error::bad_state);
    auto size = co_await AsyncIo(*self.transport_, *self.requests_,
                                 self.resource_.handle(), 5, 4);
    if (!size)
        co_return std::unexpected(size.error());
    if (*size < 16 || *size > 4096)
        co_return std::unexpected(Error::protocol_error);
    auto storage = std::unique_ptr<std::byte[]>(new (std::nothrow) std::byte[*size]);
    if (!storage)
        co_return std::unexpected(Error::resource_limit);
    auto amount =
        self.transport_->io(self.resource_.handle(), 4, {storage.get(), *size});
    if (!amount)
        co_return std::unexpected(amount.error());
    if (*amount != *size)
        co_return std::unexpected(Error::protocol_error);
    auto block = std::span<const std::byte>{storage.get(), *size};
    auto status = wire::get16(block.data()), flags = wire::get16(block.data() + 2);
    if (status < 200 || status > 599 || (flags & ~1u) ||
        wire::get32(block.data() + 4) != *size - 16)
        co_return std::unexpected(Error::protocol_error);
    auto fields = block.subspan(16);
    for (auto remaining = fields; !remaining.empty();) {
        if (remaining.size() < 4)
            co_return std::unexpected(Error::protocol_error);
        auto n = wire::get16(remaining.data()), v = wire::get16(remaining.data() + 2);
        if (4u + n + v > remaining.size())
            co_return std::unexpected(Error::protocol_error);
        auto text = reinterpret_cast<const char *>(remaining.data() + 4);
        if (!name_valid({text, n}) || !value_valid({text + n, v}))
            co_return std::unexpected(Error::protocol_error);
        remaining = remaining.subspan(4u + n + v);
    }
    auto length = wire::get64(block.data() + 8);
    if (!(flags & 1u) && length != UINT64_MAX)
        co_return std::unexpected(Error::protocol_error);
    co_return HttpResponse{status, (flags & 1u) ? std::optional{length} : std::nullopt,
                           std::move(storage), HttpHeaders(fields), std::move(self)};
}
} // namespace pxa
