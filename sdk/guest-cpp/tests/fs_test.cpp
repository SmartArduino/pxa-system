#include <pxa/app.hpp>

#include <algorithm>
#include <cassert>
#include <cstring>

using namespace pxa;

static std::array<std::byte, 4096> submitted;
static std::uint32_t submitted_bytes;
static unsigned submits, closes, reads, writes, cancels;
static std::uint64_t token;
static std::uint16_t opcode;
static bool finished;
static bool cancel_mode;
static constexpr std::uint64_t file_handle = 0x100000001;
static constexpr std::uint64_t directory_handle = 0x200000002;
static std::optional<DirectoryEntry> retained;

extern "C" std::int32_t pxa_submit(const std::uint8_t* data, std::uint32_t size) {
    assert(size <= submitted.size());
    const auto* bytes = reinterpret_cast<const std::byte*>(data);
    if (wire::get16(bytes) == 1) {
        assert(size == 28);
        if (wire::get16(bytes + 2) == 1) {
            assert(wire::get64(bytes + 20) == token);
            ++cancels;
            return 0;
        }
        assert(wire::get16(bytes + 2) == 2);
        const auto handle = wire::get64(bytes + 20);
        assert(handle == file_handle || handle == directory_handle);
        ++closes;
        return 0;
    }
    std::memcpy(submitted.data(), data, size);
    submitted_bytes = size;
    assert(wire::get16(submitted.data()) == 5);
    opcode = wire::get16(submitted.data() + 2);
    token = wire::get64(submitted.data() + 4);
    assert(token && wire::get32(submitted.data() + 12) == size - 20);
    ++submits;
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t handle, std::uint32_t operation,
                                  std::uint8_t* data, std::uint32_t size) {
    assert(handle == file_handle || handle == directory_handle);
    assert(handle == file_handle);
    if (operation == 1) {
        ++reads;
        assert(size == 8);
        std::memcpy(data, "abc", 3);
        return 3;
    }
    assert(operation == 2 && size == 3 && std::memcmp(data, "abc", 3) == 0);
    ++writes;
    return 2;
}

Task<void> exercise_files(Context& ctx) {
    const auto previous = submits;
    for (auto path : {"", "../save", "/save", "a//b", "a/.pxa-tmp", "a\\b", "\xc0\x80"}) {
        auto result = co_await ctx.fs().open(path);
        assert(!result && result.error() == Error::invalid_argument);
    }
    assert(submits == previous);
    auto invalid = co_await ctx.fs().open("save", OpenMode::exclusive);
    assert(!invalid && invalid.error() == Error::invalid_argument);
    invalid = co_await ctx.fs().open("save", OpenMode::read | OpenMode::truncate);
    assert(!invalid && invalid.error() == Error::invalid_argument);

    // The path and temporary facade may disappear before the lazy task starts.
    auto pending = [&] {
        std::string path = "save.bin";
        return ctx.fs().open(path, OpenMode::read | OpenMode::write | OpenMode::create);
    }();
    auto opened = co_await std::move(pending);
    assert(opened);
    File file = std::move(*opened);
    assert(!*opened && file.handle() == file_handle);
    std::array<std::byte, 8> bytes{};
    auto read = file.read(bytes);
    assert(read && *read == 3 && bytes[0] == std::byte{'a'});
    auto written = file.write(std::span<const std::byte>(bytes).first(3));
    assert(written && *written == 2);
    auto position = co_await file.seek(-2, SeekOrigin::end);
    assert(position && *position == 1);
    auto info = co_await ctx.fs().stat("save.bin");
    assert(info && info->kind == FileKind::regular && info->size == 3);
    file.close();
    assert(!file && closes == 1);
    assert(!file.read(bytes));
    co_return Result<void>{};
}

Task<void> exercise_directory(Context& ctx) {
    std::array<std::byte, 8> bytes{};
    auto dir = co_await ctx.fs().directory("saves");
    assert(dir && dir->handle() == directory_handle);
    assert(!dir->read(bytes));
    auto entry = co_await dir->next();
    assert(entry && *entry && (**entry).name() == "save.bin");
    retained = **entry;
    auto end = co_await dir->next();
    assert(end && !*end && retained->name() == "save.bin");
    auto malformed = co_await dir->next();
    assert(!malformed && malformed.error() == Error::protocol_error);
    dir->close();
    co_return Result<void>{};
}

Task<void> exercise_paths(Context& ctx) {
    auto made = co_await ctx.fs().make_directory("saves");
    assert(made);
    const std::string source = std::string(64, 'a') + "/" + std::string(64, 'b') +
        "/" + std::string(64, 'c') + "/" + std::string(60, 'd');
    const std::string destination = source.substr(0, 254) + "e";
    auto renamed = co_await ctx.fs().rename(source, destination);
    assert(renamed);
    auto removed = co_await ctx.fs().remove("save.bin");
    assert(!removed && removed.error() == Error::not_found);
    finished = true;
    co_return Result<void>{};
}
Task<void> exercise(Context& ctx) {
    assert(co_await exercise_files(ctx));
    assert(co_await exercise_directory(ctx));
    assert(co_await exercise_paths(ctx));
    co_return Result<void>{};
}
Task<void> cancelled_open(Context& ctx) {
    auto result = co_await ctx.fs().open("cancelled.bin");
    (void)result;
    assert(false);
    co_return Result<void>{};
}
struct App {
    Result<void> on_start(Context& ctx) {
        return cancel_mode ? ctx.foreground_tasks().start(cancelled_open(ctx))
                           : ctx.tasks().start(exercise(ctx));
    }
};
PXA_APPLICATION(App)

static void deliver(std::span<const std::byte> payload) {
    std::array<std::byte, 128> event{};
    wire::put16(event.data(), 5);
    wire::put16(event.data() + 2, opcode);
    wire::put64(event.data() + 4, token);
    wire::put32(event.data() + 12, static_cast<std::uint32_t>(payload.size()));
    std::copy(payload.begin(), payload.end(), event.begin() + 20);
    assert(pxa_app_on_event(reinterpret_cast<const std::uint8_t*>(event.data()),
                            20 + payload.size()) == 1);
}
static void opened(std::uint64_t handle) {
    std::array<std::byte, 12> payload{};
    wire::put64(payload.data() + 4, handle);
    deliver(payload);
}

int main() {
    assert(pxa_app_start(nullptr, 0) == 0);
    assert(opcode == 1 && submitted_bytes == 40);
    assert(wire::get16(submitted.data() + 20) == 1);
    assert(wire::get16(submitted.data() + 22) == 8);
    assert(std::memcmp(submitted.data() + 24, "save.bin", 8) == 0);
    assert(wire::get16(submitted.data() + 32) == 3);
    assert(wire::get32(submitted.data() + 36) == 7);
    opened(file_handle);
    assert(opcode == 6 && submitted_bytes == 37);
    assert(wire::get64(submitted.data() + 20) == file_handle);
    assert(wire::get64(submitted.data() + 28) == static_cast<std::uint64_t>(-2));
    assert(submitted[36] == std::byte{2});
    std::array<std::byte, 12> seek{};
    wire::put64(seek.data() + 4, 1);
    deliver(seek);
    assert(opcode == 5);
    std::array<std::byte, 13> stat{};
    stat[4] = std::byte{1};
    wire::put64(stat.data() + 5, 3);
    deliver(stat);
    assert(opcode == 1);
    opened(directory_handle);
    assert(opcode == 7);
    std::array<std::byte, 33> entry{};
    wire::put16(entry.data() + 4, 4);
    wire::put16(entry.data() + 6, 8);
    std::memcpy(entry.data() + 8, "save.bin", 8);
    wire::put16(entry.data() + 16, 5);
    wire::put16(entry.data() + 18, 1);
    entry[20] = std::byte{1};
    wire::put16(entry.data() + 21, 6);
    wire::put16(entry.data() + 23, 8);
    wire::put64(entry.data() + 25, 3);
    deliver(entry);
    entry.fill(std::byte{0x55});
    assert(retained && retained->name() == "save.bin");
    std::array<std::byte, 8> end{};
    wire::put16(end.data() + 4, 7);
    deliver(end);
    assert(opcode == 7);
    deliver(entry);
    assert(opcode == 2);
    const std::array<std::byte, 4> success{};
    deliver(success);
    assert(opcode == 4 && submitted_bytes == 538);
    assert(wire::get16(submitted.data() + 22) == 255);
    assert(wire::get16(submitted.data() + 279) == 2);
    assert(wire::get16(submitted.data() + 281) == 255);
    deliver(success);
    assert(opcode == 3);
    std::array<std::byte, 4> missing{};
    wire::put32(missing.data(), static_cast<std::uint32_t>(Error::not_found));
    deliver(missing);
    assert(finished && closes == 2 && reads == 1 && writes == 1);
    pxa_app_stop(0);
    assert(closes == 2);

    cancel_mode = true;
    assert(pxa_app_start(nullptr, 0) == 0 && opcode == 1);
    std::array<std::byte, 21> background{};
    wire::put16(background.data(), 17);
    wire::put16(background.data() + 2, 0x8005);
    wire::put32(background.data() + 12, 1);
    assert(pxa_app_on_event(reinterpret_cast<const std::uint8_t*>(background.data()),
                            background.size()) == 1);
    opened(file_handle);
    assert(closes == 3 && cancels == 1);
    pxa_app_stop(0);
    assert(closes == 3);
}
