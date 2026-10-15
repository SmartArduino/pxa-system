#pragma once

#include "net.hpp"
#include "async_io.hpp"

#include <algorithm>
#include <iterator>
#include <memory>
#include <ranges>

namespace pxa {

struct HttpOptions {
    std::uint32_t buffer_bytes = 4096;
    std::uint32_t header_bytes = 1024;
    std::uint64_t max_body_bytes = 1024 * 1024;
    std::uint32_t timeout_ms = 15'000;
};
struct HttpRequest {
    std::string_view url;
    HttpMethod method = HttpMethod::get;
    std::span<const NetHeaderView> headers{};
    // Retain only these response fields; empty retains all fields. Filtering
    // happens on the Host before the response-head budget is consumed.
    std::span<const std::string_view> wanted_headers{};
    // Small inline body is captured by open(); larger uploads use write_some.
    std::span<const std::byte> body{};
    std::uint64_t upload_bytes = 0;
    HttpOptions options{};
};
struct HttpCapabilities {
    std::uint32_t features;
    std::uint32_t max_buffer_bytes;
    std::uint32_t max_header_bytes;
    std::uint32_t max_request_header_bytes;
    std::uint32_t max_url_bytes;
    std::uint64_t max_upload_bytes;
    std::uint32_t max_concurrent_requests;
    bool streaming_download() const noexcept {
        return features & 1;
    }
    bool streaming_upload() const noexcept {
        return features & 2;
    }
};

constexpr bool http_header_equal(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; };
        if (lower(a[i]) != lower(b[i]))
            return false;
    }
    return true;
}

// Views borrow the response. Moving the response preserves their addresses;
// destroying it invalidates them. No fixed-size array per header.
class HttpHeaders : public std::ranges::view_interface<HttpHeaders> {
  public:
    class Iterator {
      public:
        using value_type = NetHeaderView;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::forward_iterator_tag;
        Iterator() = default;
        explicit Iterator(std::span<const std::byte> bytes) noexcept : bytes_(bytes) {}
        value_type operator*() const noexcept {
            const auto name = wire::get16(bytes_.data()),
                       value = wire::get16(bytes_.data() + 2);
            const auto *text = reinterpret_cast<const char *>(bytes_.data() + 4);
            return {{text, name}, {text + name, value}};
        }
        Iterator &operator++() noexcept {
            bytes_ = bytes_.subspan(4u + wire::get16(bytes_.data()) +
                                    wire::get16(bytes_.data() + 2));
            return *this;
        }
        Iterator operator++(int) noexcept {
            auto old = *this;
            ++*this;
            return old;
        }
        bool operator==(const Iterator &other) const noexcept {
            return bytes_.data() == other.bytes_.data() &&
                   bytes_.size() == other.bytes_.size();
        }
        bool operator==(std::default_sentinel_t) const noexcept {
            return bytes_.empty();
        }

      private:
        std::span<const std::byte> bytes_{};
    };
    HttpHeaders() = default;
    explicit HttpHeaders(std::span<const std::byte> bytes) noexcept : bytes_(bytes) {}
    Iterator begin() const noexcept {
        return Iterator(bytes_);
    }
    std::default_sentinel_t end() const noexcept {
        return {};
    }
    std::optional<std::string_view> first(std::string_view name) const noexcept {
        for (auto item : *this)
            if (http_header_equal(item.name, name))
                return item.value;
        return std::nullopt;
    }
    auto values(std::string_view name) const noexcept {
        // A header name is at most 64 bytes; own the query so temporary input
        // strings cannot dangle in the returned lazy range.
        std::array<char, 64> query{};
        const auto size = name.size();
        if (size <= query.size())
            std::copy(name.begin(), name.end(), query.begin());
        return *this | std::views::filter([query, size](NetHeaderView item) {
            return size <= query.size() &&
                   http_header_equal(item.name, {query.data(), size});
        }) | std::views::transform([](NetHeaderView item) { return item.value; });
    }

  private:
    std::span<const std::byte> bytes_{};
};

struct HttpResponse;
struct HttpExchangeTag {};
class HttpExchange {
  public:
    HttpExchange() = default;
    HttpExchange(Transport &transport, RequestTable &requests,
                 std::uint64_t handle) noexcept
        : transport_(&transport), requests_(&requests), resource_(transport, handle) {}
    HttpExchange(const HttpExchange &) = delete;
    HttpExchange &operator=(const HttpExchange &) = delete;
    HttpExchange(HttpExchange &&) noexcept = default;
    HttpExchange &operator=(HttpExchange &&) noexcept = default;
    explicit operator bool() const noexcept {
        return static_cast<bool>(resource_);
    }
    ReadableResource readable() const & noexcept {
        return {transport_, resource_.handle(), 2};
    }
    AsyncIo read_some(std::span<std::byte> output) & noexcept {
        return {transport_, requests_, resource_.handle(), 1, 1, output};
    }
    AsyncIo write_some(std::span<const std::byte> input) & noexcept {
        return {transport_,
                requests_,
                resource_.handle(),
                2,
                2,
                {const_cast<std::byte *>(input.data()), input.size()}};
    }
    Result<void> finish_upload() & noexcept {
        if (!resource_ || !transport_)
            return std::unexpected(Error::bad_state);
        auto result = transport_->io(resource_.handle(), 6, {});
        if (!result)
            return std::unexpected(result.error());
        if (*result)
            return std::unexpected(Error::protocol_error);
        return {};
    }
    // Consume ownership: the response keeps the same exchange alive.
    Task<HttpResponse> response() &&;
    Result<void> close() noexcept {
        return resource_.close();
    }

  private:
    static Task<HttpResponse> response_impl(HttpExchange self);
    Transport *transport_ = nullptr;
    RequestTable *requests_ = nullptr;
    Resource<HttpExchangeTag> resource_;
};

struct HttpResponse {
    std::uint16_t status_code = 0;
    std::optional<std::uint64_t> body_length;
    HttpHeaders headers;
    HttpExchange body;
    HttpResponse() = default;
    HttpResponse(std::uint16_t status, std::optional<std::uint64_t> length,
                 std::unique_ptr<std::byte[]> storage, HttpHeaders fields,
                 HttpExchange exchange) noexcept
        : status_code(status), body_length(length), headers(fields),
          body(std::move(exchange)), storage_(std::move(storage)) {}

  private:
    // Own only the actual encoded response head. No default heap allocation.
    std::unique_ptr<std::byte[]> storage_;
};

struct HttpRequestPacket {
    std::unique_ptr<std::byte[]> storage;
    std::size_t size = 0;
    std::span<const std::byte> bytes() const noexcept {
        return {storage.get(), size};
    }
};
Result<HttpRequestPacket> encode_http_request(const HttpRequest &request,
                                              const Permission &permission) noexcept;

class HttpClient {
  public:
    HttpClient(Transport &transport, RequestTable &requests) noexcept
        : transport_(transport), requests_(requests) {}
    // Input is captured synchronously, before returning the lazy task.
    Task<HttpExchange> open(const HttpRequest &request, const Permission &permission) {
        auto packet = encode_http_request(request, permission);
        if (!packet)
            return Task<HttpExchange>::failed(packet.error());
        return open_impl(*this, std::move(*packet));
    }
    Task<HttpResponse> get(std::string_view url, const Permission &permission,
                           HttpOptions options = {}) {
        return get_impl(open(HttpRequest{.url = url, .options = options}, permission));
    }
    Task<HttpCapabilities> capabilities() {
        return capabilities_impl(*this);
    }

  private:
    static Task<HttpExchange> open_impl(HttpClient self, HttpRequestPacket packet);
    static Task<HttpResponse> get_impl(Task<HttpExchange> pending);
    static Task<HttpCapabilities> capabilities_impl(HttpClient self);
    Transport &transport_;
    RequestTable &requests_;
};

} // namespace pxa
