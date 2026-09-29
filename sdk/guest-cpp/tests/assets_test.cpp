#include <pxa/app.hpp>

#include <array>
#include <algorithm>
#include <cassert>

static std::uint64_t token;
static std::uint16_t opcode;
static int requests;
static int closes;
static int cancels;
static bool finished;
static bool cancellation_mode;

extern "C" std::int32_t pxa_submit(const std::uint8_t* data,
                                      std::uint32_t length) {
    auto* bytes = reinterpret_cast<const std::byte*>(data);
    assert(length >= 20);
    auto service = pxa::wire::get16(bytes);
    if (service == 21) {
        ++requests;
        token = pxa::wire::get64(bytes + 4);
        opcode = pxa::wire::get16(bytes + 2);
        assert(token != 0);
        assert(opcode == (requests == 2 ? 5 : requests == 3 ? 1 : 2));
        const auto prefix = opcode == 5 ? 12u : 4u;
        const auto expected = opcode == 2 ? "textures/a.pxa" : "blob.dat";
        const auto path_size = pxa::wire::get16(bytes + 20);
        assert(std::string_view(reinterpret_cast<const char*>(bytes + 20 + prefix),
                                path_size) == expected);
    } else {
        assert(service == 1);
        if (pxa::wire::get16(bytes + 2) == 1) {
            assert(pxa::wire::get64(bytes + 20) == token);
            ++cancels;
        } else {
            assert(pxa::wire::get16(bytes + 2) == 2);
            assert(pxa::wire::get64(bytes + 20) == 77);
            ++closes;
        }
    }
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t,
                                  std::uint8_t*, std::uint32_t) {
    assert(false);
    return -1;
}

struct AssetApp {
    std::array<std::byte, 4> content{};

    pxa::Task<void> run(pxa::Context& context) {
        auto pending = [&] {
            std::string path = "textures/a.pxa";
            return context.assets().load(pxa::AssetKind::texture, path);
        }();
        auto asset = co_await std::move(pending);
        if (!asset) co_return std::unexpected(asset.error());
        assert(asset->handle() == 77);
        assert(asset->descriptor().kind == pxa::AssetKind::texture);
        auto pending_read = [&] {
            std::string path = "blob.dat";
            return context.assets().read(path, 0, content);
        }();
        auto chunk = co_await std::move(pending_read);
        if (!chunk) co_return std::unexpected(chunk.error());
        assert(chunk->bytes == 3 && chunk->eof());
        assert(content[0] == std::byte{'a'} &&
               content[1] == std::byte{'b'} &&
               content[2] == std::byte{'c'});
        auto pending_query = [&] {
            std::string path = "blob.dat";
            return context.assets().query(path);
        }();
        auto descriptor = co_await std::move(pending_query);
        assert(descriptor && descriptor->resident_bytes == 200);
        finished = true;
        co_return pxa::Result<void>{};
    }

    pxa::Result<void> on_start(pxa::Context& context) {
        if (cancellation_mode)
            return context.foreground_tasks().start(context.assets().load(
                pxa::AssetKind::texture, "textures/a.pxa"));
        return context.tasks().start(run(context));
    }
};

PXA_APPLICATION(AssetApp)

static void deliver(std::span<const std::byte> payload) {
    std::array<std::byte, 64> packet{};
    pxa::wire::put16(packet.data(), 21);
    pxa::wire::put16(packet.data() + 2, opcode);
    pxa::wire::put64(packet.data() + 4, token);
    pxa::wire::put32(packet.data() + 12, payload.size());
    for (std::size_t i = 0; i < payload.size(); ++i)
        packet[20 + i] = payload[i];
    assert(pxa_app_on_event(
               reinterpret_cast<const std::uint8_t*>(packet.data()),
               20 + payload.size()) == 1);
}

int main() {
    assert(pxa_app_start(nullptr, 0) == 0);
    assert(requests == 1);
    std::array<std::byte, 32> loaded{};
    pxa::wire::put64(loaded.data() + 4, 77);
    loaded[12] = std::byte{1};
    pxa::wire::put32(loaded.data() + 20, 100);
    pxa::wire::put32(loaded.data() + 24, 200);
    pxa::wire::put32(loaded.data() + 28, 200);
    deliver(loaded);
    assert(requests == 2 && opcode == 5);
    std::array<std::byte, 15> read{};
    pxa::wire::put32(read.data() + 8, 3);
    read[12] = std::byte{'a'};
    read[13] = std::byte{'b'};
    read[14] = std::byte{'c'};
    deliver(read);
    assert(requests == 3 && opcode == 1);
    std::array<std::byte, 24> descriptor{};
    std::copy(loaded.begin() + 12, loaded.end(), descriptor.begin() + 4);
    deliver(descriptor);
    assert(finished && closes == 1);
    pxa_app_stop(0);
    assert(closes == 1);

    cancellation_mode = true;
    assert(pxa_app_start(nullptr, 0) == 0);
    assert(requests == 4);
    std::array<std::byte, 21> background{};
    pxa::wire::put16(background.data(), 17);
    pxa::wire::put16(background.data() + 2, 0x8005);
    pxa::wire::put32(background.data() + 12, 1);
    assert(pxa_app_on_event(
               reinterpret_cast<const std::uint8_t*>(background.data()),
               background.size()) == 1);
    assert(cancels == 1);
    deliver(loaded);
    assert(closes == 2);
    pxa_app_stop(0);
}
