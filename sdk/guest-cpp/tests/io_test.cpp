#include <pxa/io.hpp>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>

using namespace pxa;
void* operator new(std::size_t) { std::abort(); }
void operator delete(void*) noexcept {}
void operator delete(void*, std::size_t) noexcept {}
extern "C" std::int32_t pxa_submit(const std::uint8_t*, std::uint32_t) { std::abort(); }
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { std::abort(); }

int main() {
    std::array<std::byte, 9> bytes{};
    ReadTransfer read(bytes);
    unsigned calls = 0;
    auto receive = [&](std::span<std::byte> output) -> Result<std::uint32_t> {
        ++calls;
        if (calls == 1) return std::unexpected(Error::would_block);
        if (calls == 3) return std::unexpected(Error::io_error);
        assert(output.size() == 4);
        output[0] = std::byte(calls);
        return 2;
    };
    assert(read.step_with(receive, 4)->state == TransferState::blocked);
    assert(read.transferred() == 0 && calls == 1);
    assert(read.step_with(receive, 4)->state == TransferState::progress);
    assert(read.transferred() == 2);
    auto error = read.step_with(receive, 4);
    assert(!error && error.error() == Error::io_error && read.transferred() == 2);
    assert(read.step_with(receive, 4)->bytes == 2);
    assert(bytes[0] == std::byte{2} && bytes[2] == std::byte{4});
    assert(!read.step_with(receive, 0) && calls == 4);
    assert(!read.step_with([](auto b) -> Result<std::uint32_t> { return b.size()+1; }));
    assert(read.transferred() == 4);
    auto moved = std::move(read);
    assert(read.complete() && moved.transferred() == 4);
    assert(moved.step_with([](auto) -> Result<std::uint32_t> { return 0; })->state == TransferState::end);
    assert(moved.ended() && !moved.complete());
    assert(moved.completed_bytes().size() == 4);
    assert(moved.step_with(receive)->state == TransferState::end && calls == 4);

    struct Writer {
        unsigned calls = 0;
        Result<std::uint32_t> write(std::span<const std::byte> input) {
            ++calls;
            if (calls == 1) return 0;
            return input.size();
        }
    } writer;
    WriteTransfer write(bytes);
    assert(write.step(writer, 3)->state == TransferState::blocked);
    assert(write.transferred() == 0);
    assert(write.step(writer, 3)->bytes == 3);
    assert(write.step(writer, 3)->state == TransferState::progress);
    assert(write.step(writer, 3)->state == TransferState::complete);
    assert(write.step(writer)->state == TransferState::complete && writer.calls == 4);
    assert(write.completed_bytes().size() == bytes.size());
    ReadTransfer empty(std::span<std::byte>{});
    assert(empty.step_with(receive, 0)->state == TransferState::complete && calls == 4);
    static_assert(!std::is_copy_constructible_v<ReadTransfer>);
    static_assert(!std::is_copy_constructible_v<WriteTransfer>);
    std::printf("Transfer: one IO/step, retry/EOF/zero-write/limits/move, zero heap; cursor=%zu B\n", sizeof(ReadTransfer));
}
