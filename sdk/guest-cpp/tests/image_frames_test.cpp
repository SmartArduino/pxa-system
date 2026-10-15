#include "../../testing/pxa_control_capture.h"
#include <pxa/app.hpp>
#include <pxa/image_frames.hpp>
#include <algorithm>
#include <cassert>
#include <vector>

static std::uint64_t token;
static std::vector<std::uint64_t> closed;
static unsigned operation, requests, cancellations;
static int mode;
static bool finished, overlap_rejected;
static pxa::DecodedImage *current;
static pxa::Context *app_context;
static pxa::RequestTable source_requests;

extern "C" std::int32_t pxa_test_control(const std::uint8_t *data, std::uint32_t length) {
    const auto *p = reinterpret_cast<const std::byte *>(data);
    assert(length >= 20);
    const auto service = pxa::wire::get16(p);
    const auto opcode = pxa::wire::get16(p + 2);
    if (service == 21) {
        ++requests;
        token = pxa::wire::get64(p + 4);
        operation = opcode;
        if (opcode == 6) {
            assert(length == 52);
            assert(pxa::wire::get64(p + 20) == 700 && pxa::wire::get16(p + 28) == 2);
            assert(pxa::wire::get16(p + 30) == 259); // frames + PNG/JPEG, reject disguised GIF
            assert(pxa::wire::get16(p + 32) == 320 && pxa::wire::get16(p + 34) == 320);
            assert(pxa::wire::get32(p + 36) == 32768 && pxa::wire::get32(p + 40) == 131072);
            assert(pxa::wire::get32(p + 44) == 8192 && pxa::wire::get32(p + 48) == 65536);
        } else {
            assert(opcode == 7 && length == 28);
            assert(pxa::wire::get64(p + 20) == (requests == 2 ? 800 : 801));
        }
    } else {
        assert(service == 1);
        if (opcode == 1) ++cancellations;
        else {
            assert(opcode == 2);
            closed.push_back(pxa::wire::get64(p + 20));
        }
    }
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t *, std::uint32_t) {
    assert(!"Encoded images must stay on the Host");
    return -11;
}
static pxa::Task<void> overlap() {
    auto result = co_await current->advance();
    assert(!result && result.error() == pxa::Error::bad_state);
    overlap_rejected = true;
    co_return pxa::Result<void>{};
}
struct ImageApp {
    pxa::Task<void> run(pxa::Context &context) {
        pxa::HttpExchange invalid(context.transport(), source_requests, 701);
        auto bad = co_await context.assets().images().decode(std::move(invalid), {}, {.max_pixels=65537});
        assert(!bad && bad.error() == pxa::Error::invalid_argument && !invalid);
        pxa::HttpExchange source(context.transport(), source_requests, 700);
        auto pending = context.assets().images().decode(std::move(source),
            {.formats = pxa::ImageFormats::png | pxa::ImageFormats::jpeg}, {8192, 65536});
        assert(!source);
        auto image = co_await std::move(pending);
        if (mode >= 4 && mode <= 6) assert(!image);
        else {
            assert(image && image->image().handle() == 800);
            assert(image->frame_info().delay_ms == 30 && image->frame_info().iteration == 0);
            current = &*image;
            if (mode == 1) {
                assert(image->frame_info().last);
                auto result = co_await image->advance();
                assert(result && !*result && requests == 1);
            } else {
                auto next = co_await image->advance();
                if (mode >= 7) assert(!next);
                else {
                    assert(next && *next && image->image().handle() == 801);
                    assert(image->frame_info().delay_ms == 655350 && !image->frame_info().last);
                    auto final = co_await image->advance();
                    assert(final && *final && image->image().handle() == 802);
                    assert(image->frame_info().last);
                    auto eof = co_await image->advance();
                    assert(eof && !*eof && requests == 3);
                }
            }
        }
        finished = true;
        co_return pxa::Result<void>{};
    }
    pxa::Result<void> on_start(pxa::Context &context) {
        app_context = &context;
        return context.foreground_tasks().start(run(context));
    }
};
PXA_APPLICATION(ImageApp)

static void deliver(unsigned opcode, std::span<const std::byte> payload) {
    std::array<std::byte, 80> p{};
    pxa::wire::put16(p.data(), 21);
    pxa::wire::put16(p.data() + 2, opcode);
    pxa::wire::put64(p.data() + 4, token);
    pxa::wire::put32(p.data() + 12, payload.size());
    std::copy(payload.begin(), payload.end(), p.begin() + 20);
    assert(pxa_app_on_event(reinterpret_cast<const std::uint8_t *>(p.data()), 20 + payload.size()) == 1);
}
static void background() {
    std::array<std::byte, 21> p{};
    pxa::wire::put16(p.data(), 17);
    pxa::wire::put16(p.data() + 2, 0x8005);
    pxa::wire::put32(p.data() + 12, 1);
    assert(pxa_app_on_event(reinterpret_cast<const std::uint8_t *>(p.data()), p.size()) == 1);
}
static std::array<std::byte, 52> frame(std::uint64_t handle, bool last = false) {
    std::array<std::byte, 52> p{};
    pxa::wire::put64(p.data() + 4, handle);
    p[12] = std::byte{4}; p[13] = std::byte{7};
    pxa::wire::put16(p.data() + 14, 1);
    pxa::wire::put16(p.data() + 16, 64); pxa::wire::put16(p.data() + 18, 36);
    pxa::wire::put32(p.data() + 20, 9248); pxa::wire::put32(p.data() + 24, 9216);
    pxa::wire::put32(p.data() + 28, 9343);
    pxa::wire::put16(p.data() + 32, 320); pxa::wire::put16(p.data() + 34, 180);
    pxa::wire::put32(p.data() + 36, 1234);
    pxa::wire::put32(p.data() + 40, handle == 800 ? 30 : 655350);
    pxa::wire::put32(p.data() + 48, last);
    return p;
}
int main() {
    for (mode = 0; mode < 9; ++mode) {
        closed.clear(); requests = cancellations = 0;
        finished = overlap_rejected = false; current = nullptr;
        assert(pxa_app_start(nullptr, 0) == 0 && requests == 1 && operation == 6);
        auto first = frame(800, mode == 1);
        if (mode == 2) background();
        if (mode == 4) deliver(6, std::span{first}.first(51));
        else if (mode == 5) {
            pxa::wire::put32(first.data() + 48, 2); deliver(6, first);
        } else if (mode == 6) {
            pxa::wire::put32(first.data(), std::uint32_t(-9));
            deliver(6, std::span{first}.first(4));
        } else deliver(6, first);
        if (mode == 0 || mode == 3 || mode >= 7) {
            assert(requests == 2 && operation == 7);
            assert(app_context->foreground_tasks().start(overlap()) && overlap_rejected);
            if (mode == 3) background();
            auto second = frame(801);
            if (mode == 7) deliver(7, std::span{second}.first(51));
            else if (mode == 8) {
                pxa::wire::put32(second.data(), std::uint32_t(-9));
                deliver(7, std::span{second}.first(4));
            } else deliver(7, second);
            if (mode == 0) {
                assert(requests == 3 && operation == 7);
                deliver(7, frame(802, true));
            }
        }
        assert(finished == (mode != 2 && mode != 3));
        assert(cancellations == unsigned(mode == 2 || mode == 3));
        assert(std::count(closed.begin(), closed.end(), 700) == 1);
        assert(std::count(closed.begin(), closed.end(), 701) == 1);
        assert(std::count(closed.begin(), closed.end(), 800) == (mode != 6));
        assert(std::count(closed.begin(), closed.end(), 801) == (mode == 0 || mode == 3 || mode == 7));
        assert(std::count(closed.begin(), closed.end(), 802) == (mode == 0));
        auto count = closed.size(); pxa_app_stop(0); assert(count == closed.size());
    }
    assert(pxa::task_pool_stats().active_slots == 0);
}
