#pragma once

#include "permission.hpp"

#include <array>
#include <string_view>

namespace pxa {

enum class HttpMethod : std::uint16_t {
    get = 1, head = 2, post = 3, put = 4, patch = 5, delete_ = 6
};

struct NetHeaderView {
    std::string_view name;
    std::string_view value;
};

struct NetRequest {
    std::string_view url;
    HttpMethod method = HttpMethod::get;
    std::uint32_t max_response_bytes = 64 * 1024;
    std::uint32_t timeout_ms = 15'000;
    std::span<const NetHeaderView> headers{};
    std::span<const std::byte> body{};
    std::span<const std::string_view> wanted_headers{};
};

struct NetHeader {
    std::array<char, 65> name{};
    std::array<char, 257> value{};
    std::uint16_t name_size = 0;
    std::uint16_t value_size = 0;

    std::string_view name_view() const noexcept { return {name.data(), name_size}; }
    std::string_view value_view() const noexcept { return {value.data(), value_size}; }
};

struct NetBodyTag {};

class NetBody {
public:
    NetBody() = default;
    NetBody(Transport& transport, std::uint64_t handle) noexcept
        : transport_(&transport), handle_(transport, handle) {}
    NetBody(const NetBody&) = delete;
    NetBody& operator=(const NetBody&) = delete;
    NetBody(NetBody&&) noexcept = default;
    NetBody& operator=(NetBody&&) noexcept = default;

    explicit operator bool() const noexcept { return static_cast<bool>(handle_); }
    Result<std::uint32_t> read(std::span<std::byte> output) noexcept {
        if (!handle_ || !transport_ || output.empty())
            return std::unexpected(Error::invalid_argument);
        return transport_->io(handle_.handle(), 1, output);
    }
    void close() noexcept { handle_.reset(); }
private:
    Transport* transport_ = nullptr;
    Resource<NetBodyTag> handle_;
};

struct NetResponse {
    std::uint16_t status_code = 0;
    std::array<char, 97> content_type{};
    std::uint16_t content_type_size = 0;
    std::uint32_t flags = 0;
    std::uint64_t body_length = 0;
    std::span<NetHeader> headers{};
    NetBody body;

    std::string_view content_type_view() const noexcept {
        return {content_type.data(), content_type_size};
    }
    bool body_length_known() const noexcept { return (flags & 2) != 0; }
};

Result<std::size_t> encode_net_request(const NetRequest& request,
    const Permission& permission, std::span<std::byte> packet) noexcept;

std::uint64_t net_late_body_handle(
    std::span<const std::byte> payload) noexcept;

Result<NetResponse> decode_net_response(Transport& transport,
    std::span<const std::byte> payload, std::span<NetHeader> headers,
    std::uint32_t max_response_bytes) noexcept;

class NetService {
public:
    NetService(Transport& transport, RequestTable& requests) noexcept
        : transport_(transport), requests_(requests) {}

    // The packet and header storage must remain alive until this task completes.
    Task<NetResponse> request(const NetRequest& request,
        const Permission& permission, std::span<std::byte> packet,
        std::span<NetHeader> headers = {}) {
        auto size = encode_net_request(request, permission, packet);
        if (!size) return Task<NetResponse>::failed(size.error());
        return request_impl(transport_, requests_, packet.first(*size), headers,
                            request.max_response_bytes);
    }

private:
    static Task<NetResponse> request_impl(Transport& transport,
        RequestTable& requests, std::span<std::byte> packet,
        std::span<NetHeader> headers, std::uint32_t max_response_bytes) {
        auto event = co_await Response(transport, requests, 9, 2,
            Response::PrebuiltPacket{packet}, true, 4, net_late_body_handle);
        if (!event) co_return std::unexpected(event.error());
        co_return decode_net_response(transport, event->payload, headers,
                                      max_response_bytes);
    }

    Transport& transport_;
    RequestTable& requests_;
};

} // namespace pxa
