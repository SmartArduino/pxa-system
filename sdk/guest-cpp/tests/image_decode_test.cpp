#include "../../testing/pxa_control_capture.h"
#include <cassert>
#include <pxa/app.hpp>
#include <vector>

static std::uint64_t token;
static std::vector<std::uint64_t> closed;
static int mode, requests, cancellations;
static bool finished;
static pxa::RequestTable source_requests;
extern "C" std::int32_t pxa_test_control(const std::uint8_t *data, std::uint32_t length) {
    const auto *p = reinterpret_cast<const std::byte *>(data);
    assert(length >= 20);
    if (pxa::wire::get16(p) == 21) {
        ++requests;
        token = pxa::wire::get64(p + 4);
        assert(pxa::wire::get16(p + 2) == 6 && length == 44);
        assert(pxa::wire::get64(p + 20) == 700 && pxa::wire::get16(p + 28) == 2);
        assert(pxa::wire::get16(p + 30) == 0 && pxa::wire::get16(p + 32) == 320);
        assert(pxa::wire::get32(p + 36) == 32768 && pxa::wire::get32(p + 40) == 131072);
    } else {
        assert(pxa::wire::get16(p) == 1);
        if (pxa::wire::get16(p + 2) == 1)
            ++cancellations;
        else {
            assert(pxa::wire::get16(p + 2) == 2);
            closed.push_back(pxa::wire::get64(p + 20));
        }
    }
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t *, std::uint32_t) {
    assert(!"Image loading must not transfer encoded bytes through Guest I/O");
    return -11;
}
struct ImageApp {
    pxa::Task<void> run(pxa::Context &context) {
        pxa::HttpExchange invalid(context.transport(), source_requests, 701);
        auto bad = co_await context.assets().decode_image(std::move(invalid), {.max_width = 0});
        assert(!bad && bad.error() == pxa::Error::invalid_argument && !invalid);
        pxa::HttpExchange source(context.transport(), source_requests, 700);
        auto pending = context.assets().decode_image(std::move(source));
        assert(!source); // Captured immediately, even before starting the lazy task.
        auto image = co_await std::move(pending);
        if (mode >= 2)
            assert(!image);
        else {
            assert(image && image->handle() == 800);
            const auto &info = image->descriptor();
            assert(info.kind == pxa::AssetKind::image && info.width == 64 && info.height == 36);
            assert(info.source_width == 320 && info.source_height == 180 &&
                   info.source_bytes == 1234);
        }
        finished = true;
        co_return pxa::Result<void>{};
    }
    pxa::Result<void> on_start(pxa::Context &context) {
        return context.foreground_tasks().start(run(context));
    }
};
PXA_APPLICATION(ImageApp)
static void deliver(std::span<const std::byte> payload) {
    std::array<std::byte, 80> p{};
    pxa::wire::put16(p.data(), 21);
    pxa::wire::put16(p.data() + 2, 6);
    pxa::wire::put64(p.data() + 4, token);
    pxa::wire::put32(p.data() + 12, payload.size());
    std::copy(payload.begin(), payload.end(), p.begin() + 20);
    assert(pxa_app_on_event(reinterpret_cast<const std::uint8_t *>(p.data()),
                            20 + payload.size()) == 1);
}
static void background() {
    std::array<std::byte, 21> p{};
    pxa::wire::put16(p.data(), 17);
    pxa::wire::put16(p.data() + 2, 0x8005);
    pxa::wire::put32(p.data() + 12, 1);
    assert(pxa_app_on_event(reinterpret_cast<const std::uint8_t *>(p.data()), p.size()) == 1);
}
int main() {
    for (mode = 0; mode < 5; ++mode) {
        closed.clear();
        requests = cancellations = 0;
        finished = false;
        assert(pxa_app_start(nullptr, 0) == 0 && requests == 1);
        std::array<std::byte, 40> decoded{};
        pxa::wire::put64(decoded.data() + 4, 800);
        decoded[12] = std::byte{4};
        decoded[13] = std::byte{7};
        pxa::wire::put16(decoded.data() + 14, 1);
        pxa::wire::put16(decoded.data() + 16, 64);
        pxa::wire::put16(decoded.data() + 18, 36);
        pxa::wire::put32(decoded.data() + 20, 9248);
        pxa::wire::put32(decoded.data() + 24, 9216);
        pxa::wire::put32(decoded.data() + 28, 9343);
        pxa::wire::put16(decoded.data() + 32, 320);
        pxa::wire::put16(decoded.data() + 34, 180);
        pxa::wire::put32(decoded.data() + 36, 1234);
        if (mode == 1)
            background();
        if (mode == 2)
            deliver(std::span{decoded}.first(39));
        else if (mode == 3) {
            pxa::wire::put16(decoded.data() + 32, 0);
            deliver(decoded);
        } else if (mode == 4) {
            pxa::wire::put32(decoded.data(), std::uint32_t(-9));
            deliver(std::span{decoded}.first(4));
        } else
            deliver(decoded);
        assert(finished == (mode != 1) && cancellations == (mode == 1));
        assert(std::count(closed.begin(), closed.end(), 700) == 1);
        assert(std::count(closed.begin(), closed.end(), 701) == 1);
        assert(std::count(closed.begin(), closed.end(), 800) == (mode != 4));
        const auto count = closed.size();
        pxa_app_stop(0);
        assert(closed.size() == count);
    }
}
