#include "../../testing/pxa_control_capture.h"
#include <pxa/app.hpp>

#include <cassert>
#include <string>
#include <vector>

static constexpr std::uint64_t permission_handle = (1ull << 32) | 7;
static constexpr std::uint64_t exchange_handle = (2ull << 32) | 3;
static std::uint64_t open_token, wait_token;
static unsigned waits, closes, cancels, reads;
static bool headers_ready, body_ready, finished;
static std::vector<std::byte> head;
static std::uint32_t wait_interest;

extern "C" std::int32_t pxa_test_control(const std::uint8_t *input,
                                         std::uint32_t size) {
    auto *b = reinterpret_cast<const std::byte *>(input);
    auto service = pxa::wire::get16(b), op = pxa::wire::get16(b + 2);
    auto token = pxa::wire::get64(b + 4);
    if (service == 9) {
        assert(op == 3);
        open_token = token;
        pxa::wire::Records r({b + 20, size - 20});
        auto url = r.take(1);
        auto method = r.take(2, 2);
        auto permission = r.take(3, 8);
        auto budget = r.take(4, 8);
        auto timeout = r.take(8, 4);
        auto buffer = r.take(14, 4);
        auto upload = r.take(15, 8);
        auto headers = r.take(16, 4);
        assert(url && method && permission && budget && timeout && buffer && upload &&
               headers && r.empty());
        assert(std::string_view(reinterpret_cast<const char *>(url->data()),
                                url->size()) == "https://example.test/stream");
        assert(pxa::wire::get64(budget->data()) == 2 * 1024 * 1024);
        assert(pxa::wire::get32(timeout->data()) == 86400000u);
    } else {
        assert(service == 1);
        if (op == 4) {
            assert(size == 32 && pxa::wire::get64(b + 20) == exchange_handle);
            wait_token = token;
            wait_interest = pxa::wire::get32(b + 28);
            ++waits;
        } else if (op == 2) {
            assert(pxa::wire::get64(b + 20) == exchange_handle);
            ++closes;
        } else {
            assert(op == 1 && pxa::wire::get64(b + 20) == wait_token);
            ++cancels;
        }
    }
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t handle, std::uint32_t op,
                               std::uint8_t *out, std::uint32_t size) {
    assert(handle == exchange_handle);
    if (op == 5)
        return headers_ready ? static_cast<std::int32_t>(head.size()) : -7;
    if (op == 4) {
        assert(size >= head.size());
        std::memcpy(out, head.data(), head.size());
        return head.size();
    }
    assert(op == 1 && size >= 3);
    if (!body_ready)
        return -7;
    ++reads;
    if (reads == 1) {
        std::memcpy(out, "abc", 3);
        body_ready = false;
        return 3;
    }
    return 0;
}

struct HttpApp {
    pxa::Permission permission;
    std::array<std::byte, 128> buffer;
    pxa::Task<void> run(pxa::Context &context) {
        auto pending = [&] {
            std::string temporary = "https://example.test/stream";
            return context.http().get(temporary, permission,
                                      {.max_body_bytes = 2 * 1024 * 1024,
                                       .timeout_ms = 86400000u});
        }();
        auto response = co_await std::move(pending);
        assert(response && response->status_code == 200 && !response->body_length);
        assert(response->headers.first("Content-Type") == "text/plain");
        unsigned cookies = 0;
        auto values = response->headers.values(std::string("SET-cookie"));
        for (auto value : values) {
            assert(value == "a=1" || value == std::string(600, 'x'));
            ++cookies;
        }
        assert(cookies == 2);
        assert(pxa::task_pool_stats().active_slots == 1);
        auto n = co_await response->body.read_some(buffer);
        assert(n && *n == 3 && buffer[0] == std::byte{'a'});
        assert(pxa::task_pool_stats().active_slots == 1);
        n = co_await response->body.read_some(buffer);
        assert(n && *n == 0);
        finished = true;
        co_return pxa::Result<void>{};
    }
    pxa::Result<void> on_start(pxa::Context &context) {
        permission = pxa::Permission(context.transport(), permission_handle);
        return context.foreground_tasks().start(run(context));
    }
};
PXA_APPLICATION(HttpApp)

static void deliver(std::uint16_t service, std::uint16_t op, std::uint64_t token,
                    std::span<const std::byte> payload) {
    std::array<std::byte, 64> e{};
    pxa::wire::put16(e.data(), service);
    pxa::wire::put16(e.data() + 2, op);
    pxa::wire::put64(e.data() + 4, token);
    pxa::wire::put32(e.data() + 12, payload.size());
    std::memcpy(e.data() + 20, payload.data(), payload.size());
    assert(pxa_app_on_event(reinterpret_cast<const std::uint8_t *>(e.data()),
                            20 + payload.size()) == 1);
}
static void wake(std::uint32_t flags) {
    std::array<std::byte, 8> payload{};
    pxa::wire::put32(payload.data() + 4, flags);
    deliver(1, 4, wait_token, payload);
}
static void open_reply() {
    std::array<std::byte, 12> payload{};
    pxa::wire::put64(payload.data() + 4, exchange_handle);
    deliver(9, 3, open_token, payload);
}
static void add_header(std::string_view name, std::string_view value) {
    auto at = head.size();
    head.resize(at + 4 + name.size() + value.size());
    pxa::wire::put16(head.data() + at, name.size());
    pxa::wire::put16(head.data() + at + 2, value.size());
    std::memcpy(head.data() + at + 4, name.data(), name.size());
    std::memcpy(head.data() + at + 4 + name.size(), value.data(), value.size());
}
int main() {
    head.resize(16);
    add_header("content-type", "text/plain");
    add_header("Set-Cookie", "a=1");
    add_header("set-cookie", std::string(600, 'x'));
    pxa::wire::put16(head.data(), 200);
    pxa::wire::put32(head.data() + 4, head.size() - 16);
    pxa::wire::put64(head.data() + 8, UINT64_MAX);
    assert(pxa_app_start(nullptr, 0) == 0);
    open_reply();
    assert(waits == 1 && wait_interest == 4);
    headers_ready = true;
    wake(4);
    assert(waits == 2 && wait_interest == 1 && !finished);
    // Readiness is advisory: a spurious wake rearms without resuming the app.
    wake(1);
    assert(waits == 3 && !finished);
    body_ready = true;
    wake(1);
    assert(waits == 4 && reads == 1 && !finished);
    body_ready = true;
    wake(9);
    assert(finished && closes == 1 && pxa::task_pool_stats().active_slots == 0);
    pxa_app_stop(0);

    headers_ready = false;
    body_ready = false;
    finished = false;
    reads = 0;
    assert(pxa_app_start(nullptr, 0) == 0);
    open_reply();
    headers_ready = true;
    wake(4);
    assert(wait_interest == 1);
    std::array<std::byte, 21> background{};
    pxa::wire::put16(background.data(), 17);
    pxa::wire::put16(background.data() + 2, 0x8005);
    pxa::wire::put32(background.data() + 12, 1);
    assert(pxa_app_on_event(reinterpret_cast<const std::uint8_t *>(background.data()),
                            background.size()) == 1);
    assert(cancels == 1 && closes == 2 && pxa::task_pool_stats().active_slots == 0);
    wake(9);
    assert(!finished && closes == 2);
    pxa_app_stop(0);

    pxa::HttpExchange empty;
    auto invalid = empty.read_some({});
    assert(invalid.await_ready());
    assert(!invalid.await_resume() &&
           invalid.await_resume().error() == pxa::Error::bad_state);
    pxa::Transport transport;
    transport.phase(pxa::Phase::event);
    pxa::Permission permission(transport, permission_handle);
    pxa::HttpRequest request{.url = "https://example.test/stream"};
    std::array<pxa::NetHeaderView, 1> fields{{{"HOST", "x"}}};
    request.headers = fields;
    assert(!pxa::encode_http_request(request, permission));
    request.headers = {};
    request.options.buffer_bytes = 513;
    assert(!pxa::encode_http_request(request, permission));
    request.options.buffer_bytes = 4096;
    std::string selected = "Content-Type";
    std::array<std::string_view, 2> wanted{selected, "location"};
    request.wanted_headers = wanted;
    auto packet = pxa::encode_http_request(request, permission);
    assert(packet);
    selected.assign(selected.size(), 'x'); // Encoded names must own their bytes.
    pxa::wire::Records records(packet->bytes());
    assert(records.take(1) && records.take(2, 2) && records.take(3, 8) &&
           records.take(4, 8) && records.take(8, 4));
    for (auto expected : {"Content-Type", "location"}) {
        auto encoded = records.take(11);
        assert(encoded && std::string_view(reinterpret_cast<const char *>(encoded->data()),
                                           encoded->size()) == expected);
    }
    assert(records.take(14, 4) && records.take(15, 8) && records.take(16, 4) &&
           records.empty());
    wanted = {"Content-Type", "content-type"};
    assert(!pxa::encode_http_request(request, permission));
    for (auto invalid_name : {"", "content type", "name\r\nInjected", "名称"}) {
        wanted = {invalid_name, "location"};
        assert(!pxa::encode_http_request(request, permission));
    }
    selected.assign(64, 'x');
    wanted = {selected, "location"};
    assert(pxa::encode_http_request(request, permission));
    selected.push_back('x');
    wanted = {selected, "location"};
    assert(!pxa::encode_http_request(request, permission));
    std::array<std::string_view, 9> too_many{};
    request.wanted_headers = too_many;
    assert(!pxa::encode_http_request(request, permission));
    permission.release();
    transport.phase(pxa::Phase::stopped);
}
