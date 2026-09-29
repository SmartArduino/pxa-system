#include <pxa/app.hpp>

#include <array>
#include <cassert>
#include <string>

static std::uint64_t request_token;
static unsigned requests;
static unsigned cancels;
static unsigned closes;
static unsigned reads;
static bool done;
static bool cancellation_mode;

extern "C" std::int32_t pxa_submit(const std::uint8_t* data,
                                     std::uint32_t length) {
    const auto* bytes = reinterpret_cast<const std::byte*>(data);
    const auto service = pxa::wire::get16(bytes);
    const auto opcode = pxa::wire::get16(bytes + 2);
    if (service == 9) {
        assert(opcode == 2 && length > 20);
        ++requests;
        request_token = pxa::wire::get64(bytes + 4);
        assert(request_token);
        pxa::wire::Records records({bytes + 20, length - 20});
        auto url = records.take(1);
        auto method = records.take(2, 2);
        auto permission = records.take(3, 8);
        auto limit = records.take(4, 4);
        auto timeout = records.take(8, 4);
        assert(url && method && permission && limit && timeout);
        assert(std::string_view(reinterpret_cast<const char*>(url->data()),
                                url->size()) == "https://example.test/data");
        assert(pxa::wire::get16(method->data()) == 1);
        assert(pxa::wire::get64(permission->data()) == ((1ull << 32) | 7));
        assert(pxa::wire::get32(limit->data()) == 4096);
        assert(pxa::wire::get32(timeout->data()) == 15000);
        auto wanted = records.take(11);
        assert(wanted && records.empty());
        assert(std::string_view(reinterpret_cast<const char*>(wanted->data()),
                                wanted->size()) == "etag");
    } else {
        assert(service == 1 && length == 28);
        if (opcode == 1) {
            assert(pxa::wire::get64(bytes + 20) == request_token);
            ++cancels;
        } else {
            assert(opcode == 2);
            assert(pxa::wire::get64(bytes + 20) == ((2ull << 32) | 3));
            ++closes;
        }
    }
    return 0;
}

extern "C" std::int32_t pxa_io(std::uint64_t handle, std::uint32_t opcode,
                                 std::uint8_t* output, std::uint32_t length) {
    assert(handle == ((2ull << 32) | 3) && opcode == 1 && length >= 3);
    ++reads;
    if (reads > 1) return 0;
    output[0] = 'a'; output[1] = 'b'; output[2] = 'c';
    return 3;
}

struct NetApp {
    pxa::Permission permission;
    std::array<std::byte, 512> packet{};
    std::array<pxa::NetHeader, 1> headers{};

    pxa::Task<void> run(pxa::Context& context) {
        constexpr std::array<std::string_view, 1> wanted{"etag"};
        auto pending = [&] {
            std::string url = "https://example.test/data";
            pxa::NetRequest request;
            request.url = url;
            request.max_response_bytes = 4096;
            request.wanted_headers = wanted;
            return context.net().request(request, permission, packet, headers);
        }();
        auto reply = co_await std::move(pending);
        assert(reply && reply->status_code == 404);
        assert(reply->content_type_view() == "text/plain");
        assert(reply->body_length_known() && reply->body_length == 3);
        assert(reply->headers.size() == 1);
        assert(reply->headers[0].name_view() == "etag");
        assert(reply->headers[0].value_view() == "v1");
        std::array<std::byte, 4> output{};
        auto amount = reply->body.read(output);
        assert(amount && *amount == 3);
        assert(output[0] == std::byte{'a'} && output[2] == std::byte{'c'});
        amount = reply->body.read(output);
        assert(amount && *amount == 0);
        done = true;
        co_return pxa::Result<void>{};
    }

    pxa::Result<void> on_start(pxa::Context& context) {
        permission = pxa::Permission(context.transport(), (1ull << 32) | 7);
        if (cancellation_mode) {
            pxa::NetRequest request;
            request.url = "https://example.test/data";
            request.max_response_bytes = 4096;
            constexpr std::array<std::string_view, 1> wanted{"etag"};
            request.wanted_headers = wanted;
            return context.foreground_tasks().start(
                context.net().request(request, permission, packet, headers));
        }
        return context.tasks().start(run(context));
    }
};

PXA_APPLICATION(NetApp)

static void deliver(std::span<const std::byte> payload) {
    std::array<std::byte, 160> event{};
    pxa::wire::put16(event.data(), 9);
    pxa::wire::put16(event.data() + 2, 2);
    pxa::wire::put64(event.data() + 4, request_token);
    pxa::wire::put32(event.data() + 12, payload.size());
    for (std::size_t i = 0; i < payload.size(); ++i) event[20 + i] = payload[i];
    assert(pxa_app_on_event(reinterpret_cast<const std::uint8_t*>(event.data()),
                            20 + payload.size()) == 1);
}

static std::array<std::byte, 110> response(std::size_t& length) {
    std::array<std::byte, 110> payload{};
    pxa::wire::Writer writer(std::span{payload}.subspan(4));
    std::array<std::byte, 8> scalar{};
    pxa::wire::put16(scalar.data(), 404);
    assert(pxa::wire::record(writer, 5, std::span{scalar}.first(2)));
    const std::string_view type = "text/plain";
    assert(pxa::wire::record(writer, 6, std::as_bytes(std::span{type.data(), type.size()})));
    pxa::wire::put64(scalar.data(), (2ull << 32) | 3);
    assert(pxa::wire::record(writer, 7, scalar));
    std::array<std::byte, 32> nested{};
    pxa::wire::Writer nested_writer(nested);
    const std::string_view name = "etag", value = "v1";
    assert(pxa::wire::record(nested_writer, 1, std::as_bytes(std::span{name.data(), name.size()})));
    assert(pxa::wire::record(nested_writer, 2, std::as_bytes(std::span{value.data(), value.size()})));
    assert(pxa::wire::record(writer, 9, std::span{nested}.first(nested_writer.size())));
    pxa::wire::put64(scalar.data(), 3);
    assert(pxa::wire::record(writer, 12, scalar));
    pxa::wire::put32(scalar.data(), 3);
    assert(pxa::wire::record(writer, 13, std::span{scalar}.first(4)));
    length = 4 + writer.size();
    return payload;
}

int main() {
    assert(pxa_app_start(nullptr, 0) == 0 && requests == 1);
    std::size_t length = 0;
    auto payload = response(length);
    deliver(std::span{payload}.first(length));
    assert(done && reads == 2 && closes == 1);
    pxa_app_stop(0);

    cancellation_mode = true;
    assert(pxa_app_start(nullptr, 0) == 0 && requests == 2);
    std::array<std::byte, 21> background{};
    pxa::wire::put16(background.data(), 17);
    pxa::wire::put16(background.data() + 2, 0x8005);
    pxa::wire::put32(background.data() + 12, 1);
    assert(pxa_app_on_event(reinterpret_cast<const std::uint8_t*>(background.data()),
                            background.size()) == 1);
    assert(cancels == 1);
    deliver(std::span{payload}.first(length));
    assert(closes == 2);
    pxa_app_stop(0);

    pxa::Transport transport;
    transport.phase(pxa::Phase::event);
    payload[length - 1] = std::byte{0xff};
    assert(pxa::net_late_body_handle(std::span{payload}.first(length)) ==
           ((2ull << 32) | 3));
    std::array<pxa::NetHeader, 1> headers{};
    auto invalid = pxa::decode_net_response(transport,
        std::span{payload}.first(length), headers, 4096);
    assert(!invalid && invalid.error() == pxa::Error::protocol_error);
    assert(closes == 3);

    payload = response(length);
    auto no_headers = pxa::decode_net_response(transport,
        std::span{payload}.first(length), {}, 4096);
    assert(!no_headers && no_headers.error() == pxa::Error::resource_limit);
    assert(closes == 4);

    std::array<std::byte, 48> empty_body{};
    pxa::wire::Writer empty_writer(std::span{empty_body}.subspan(4));
    std::array<std::byte, 8> scalar{};
    pxa::wire::put16(scalar.data(), 204);
    assert(pxa::wire::record(empty_writer, 5, std::span{scalar}.first(2)));
    assert(pxa::wire::record(empty_writer, 6, {}));
    pxa::wire::put64(scalar.data(), 0);
    assert(pxa::wire::record(empty_writer, 12, scalar));
    pxa::wire::put32(scalar.data(), 2);
    assert(pxa::wire::record(empty_writer, 13, std::span{scalar}.first(4)));
    auto head = pxa::decode_net_response(transport,
        std::span{empty_body}.first(4 + empty_writer.size()), {}, 4096);
    assert(head && head->status_code == 204 && !head->body &&
           head->body_length_known() && head->body_length == 0);
    assert(closes == 4);

    pxa::Permission permission(transport, (1ull << 32) | 7);
    std::array<std::byte, 128> packet{};
    pxa::NetRequest request;
    request.url = "https://example.test/data";
    constexpr std::array<pxa::NetHeaderView, 1> forbidden{{{"host", "x"}}};
    request.headers = forbidden;
    auto encoded = pxa::encode_net_request(request, permission, packet);
    assert(!encoded && encoded.error() == pxa::Error::invalid_argument);
    request.headers = {};
    encoded = pxa::encode_net_request(request, permission,
                                      std::span{packet}.first(32));
    assert(!encoded && encoded.error() == pxa::Error::resource_limit);
    transport.phase(pxa::Phase::stopped);
}
