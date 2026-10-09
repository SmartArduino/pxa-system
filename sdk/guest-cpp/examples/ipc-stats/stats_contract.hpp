// Generated from a PXA IPC contract. Do not edit by hand.
#pragma once

#include <pxa/ipc.hpp>

namespace stats {

struct Get {
    static constexpr std::string_view endpoint = "stats.get.v1";
    static constexpr std::uint16_t major = 1;
    static constexpr std::uint16_t minor = 1;
    static constexpr std::size_t request_bytes = 28;
    static constexpr std::size_t response_bytes = 54;
    static constexpr std::size_t call_packet_bytes =
        pxa::wire::header_bytes + 8 + endpoint.size() + request_bytes;
    static constexpr std::size_t reply_packet_bytes =
        pxa::wire::header_bytes + 20 + response_bytes;
    struct Request {
        std::uint32_t seed{};
        pxa::FixedText<16> label{};
    };

    struct Response {
        std::uint32_t next{};
        bool valid{};
        pxa::FixedText<16> label{};
        std::optional<bool> cached{};
        std::optional<pxa::FixedText<12>> note{};
    };

    static pxa::Result<std::size_t> encode_request(
        const Request& value, std::span<std::byte> output) noexcept {
        pxa::wire::Writer writer(output);
        {
            std::array<std::byte, 4> bytes{};
            pxa::wire::put32(bytes.data(), static_cast<std::uint32_t>(value.seed));
            if (!pxa::wire::record(writer, 1, bytes))
                return std::unexpected(pxa::Error::resource_limit);
        }
        {
            const auto text = value.label.view();
            const auto bytes = std::as_bytes(
                std::span{text.data(), text.size()});
            if (bytes.empty() || !pxa::wire::valid_utf8(bytes))
                return std::unexpected(pxa::Error::invalid_argument);
            if (!pxa::wire::record(writer, 2, bytes))
                return std::unexpected(pxa::Error::resource_limit);
        }
        return writer.size();
    }

    static pxa::Result<Request> decode_request(
        std::span<const std::byte> input) noexcept {
        Request output{};
        pxa::wire::Records records(input);
        {
            auto bytes = records.take(1, 4);
            if (!bytes) return std::unexpected(bytes.error());
            output.seed = static_cast<std::uint32_t>(pxa::wire::get32((*bytes).data()));
        }
        {
            auto bytes = records.take(2);
            if (!bytes) return std::unexpected(bytes.error());
            auto assigned = output.label.assign((*bytes));
            if (!assigned) return std::unexpected(assigned.error());
        }
        auto finished = records.finish(2);
        if (!finished) return std::unexpected(finished.error());
        return output;
    }

    static pxa::Result<std::size_t> encode_response(
        const Response& value, std::span<std::byte> output) noexcept {
        pxa::wire::Writer writer(output);
        {
            std::array<std::byte, 4> bytes{};
            pxa::wire::put32(bytes.data(), static_cast<std::uint32_t>(value.next));
            if (!pxa::wire::record(writer, 1, bytes))
                return std::unexpected(pxa::Error::resource_limit);
        }
        {
            const std::array<std::byte, 1> bytes{std::byte(value.valid ? 1 : 0)};
            if (!pxa::wire::record(writer, 2, bytes))
                return std::unexpected(pxa::Error::resource_limit);
        }
        {
            const auto text = value.label.view();
            const auto bytes = std::as_bytes(
                std::span{text.data(), text.size()});
            if (bytes.empty() || !pxa::wire::valid_utf8(bytes))
                return std::unexpected(pxa::Error::invalid_argument);
            if (!pxa::wire::record(writer, 3, bytes))
                return std::unexpected(pxa::Error::resource_limit);
        }
        {
            if (value.cached) {
                const std::array<std::byte, 1> bytes{std::byte((*value.cached) ? 1 : 0)};
                if (!pxa::wire::record(writer, 32772, bytes))
                    return std::unexpected(pxa::Error::resource_limit);
            }
        }
        {
            if (value.note) {
                const auto text = (*value.note).view();
                const auto bytes = std::as_bytes(
                    std::span{text.data(), text.size()});
                if (bytes.empty() || !pxa::wire::valid_utf8(bytes))
                    return std::unexpected(pxa::Error::invalid_argument);
                if (!pxa::wire::record(writer, 32773, bytes))
                    return std::unexpected(pxa::Error::resource_limit);
            }
        }
        return writer.size();
    }

    static pxa::Result<Response> decode_response(
        std::span<const std::byte> input) noexcept {
        Response output{};
        pxa::wire::Records records(input);
        {
            auto bytes = records.take(1, 4);
            if (!bytes) return std::unexpected(bytes.error());
            output.next = static_cast<std::uint32_t>(pxa::wire::get32((*bytes).data()));
        }
        {
            auto bytes = records.take(2, 1);
            if (!bytes) return std::unexpected(bytes.error());
            if ((*bytes)[0] != std::byte{0} && (*bytes)[0] != std::byte{1})
                return std::unexpected(pxa::Error::protocol_error);
            output.valid = (*bytes)[0] == std::byte{1};
        }
        {
            auto bytes = records.take(3);
            if (!bytes) return std::unexpected(bytes.error());
            auto assigned = output.label.assign((*bytes));
            if (!assigned) return std::unexpected(assigned.error());
        }
        {
            auto bytes = records.take_optional(4);
            if (!bytes) return std::unexpected(bytes.error());
            if (*bytes) {
                if ((**bytes).size() != 1)
                    return std::unexpected(pxa::Error::protocol_error);
                if ((**bytes)[0] != std::byte{0} && (**bytes)[0] != std::byte{1})
                    return std::unexpected(pxa::Error::protocol_error);
                output.cached = (**bytes)[0] == std::byte{1};
            }
        }
        {
            auto bytes = records.take_optional(5);
            if (!bytes) return std::unexpected(bytes.error());
            if (*bytes) {
                auto assigned = output.note.emplace().assign((**bytes));
                if (!assigned) return std::unexpected(assigned.error());
            }
        }
        auto finished = records.finish(5);
        if (!finished) return std::unexpected(finished.error());
        return output;
    }

};

} // namespace stats
