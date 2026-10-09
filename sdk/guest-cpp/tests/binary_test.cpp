#include <pxa/binary.hpp>
#include <pxa/pixels.hpp>
#include <pxa/service_wire.hpp>
#include <cassert>
#include <cstdio>

using namespace pxa;

extern "C" std::int32_t pxa_submit(const std::uint8_t*, std::uint32_t) { assert(false); return -1; }
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { assert(false); return -1; }

static_assert(!binary::Scalar<std::array<int, 2>>);
static_assert(binary::encode(std::uint32_t{0x12345678}) ==
    std::array{std::byte{0x78}, std::byte{0x56}, std::byte{0x34}, std::byte{0x12}});
static_assert(*binary::decode<std::int32_t>(binary::encode(std::int32_t{-123})) == -123);
static_assert(std::same_as<FixedText<12>, wire::OwnedText<12>>);

int main() {
    std::array<std::byte, 32> buffer{};
    binary::Writer out(buffer);
    assert(out.write(std::uint16_t{0x1234}));
    assert(out.write(std::int64_t{INT64_MIN}));
    assert(out.write(-1.25f));
    assert(out.write(-0.0));
    assert(out.write(true));
    assert(out.size() == 23);
    binary::Reader in(out.written());
    assert(*in.read<std::uint16_t>() == 0x1234);
    assert(*in.read<std::int64_t>() == INT64_MIN);
    assert(*in.read<float>() == -1.25f);
    assert(std::bit_cast<std::uint64_t>(*in.read<double>()) == UINT64_C(0x8000000000000000));
    assert(*in.read<bool>() && in.finish());
    assert(!in.read<std::uint8_t>() && in.position() == 23);
    assert(!binary::read<std::uint32_t>(buffer, SIZE_MAX));
    assert(!binary::write(buffer, std::uint32_t{1}, SIZE_MAX));
    assert(!binary::decode<std::uint16_t>(buffer));
    const std::array invalid_bool{std::byte{2}};
    binary::Reader bad(invalid_bool);
    assert(!bad.read<bool>() && bad.position() == 0);
    assert(!bad.finish());
    std::array<std::byte, 3> small{std::byte{9},std::byte{9},std::byte{9}};
    binary::Writer full(small);
    assert(!full.write(std::uint32_t{1}) && full.size() == 0);
    assert(small[0] == std::byte{9} && small[2] == std::byte{9});
    assert(!full.bytes(buffer) && full.size() == 0);

    FixedText<6> text;
    assert(text.set("中国") && text.view() == "中国");
    assert(!text.set("中国人") && text.view() == "中国");
    assert(!text.set("\xc0\x80") && text.view() == "中国");
    assert(!text.set(std::string_view{"a\0b", 3}));

    // Odd address and padded stride must not rely on native uint16_t stores.
    std::array<std::byte, 17> pixels;
    pixels.fill(std::byte{0x55});
    auto view = Rgb565Pixels::from_bytes(std::span{pixels}.subspan(1), 3, 2, 8);
    assert(view && view->width() == 3 && view->height() == 2);
    view->fill(0x1234);
    assert(pixels[0] == std::byte{0x55} && pixels[1] == std::byte{0x34});
    assert(pixels[2] == std::byte{0x12} && pixels[7] == std::byte{0x55});
    assert(pixels[8] == std::byte{0x55} && pixels[15] == std::byte{0x55});
    auto row = *view->row(1);
    row[2] = 0xabcd;
    row[0] = row[2];
    assert(std::uint16_t(row[0]) == 0xabcd && pixels[13] == std::byte{0xcd});
    assert(!view->row(2));
    assert(!Rgb565Pixels::from_bytes(pixels, 3, 2, 5));
    assert(!Rgb565Pixels::from_bytes(pixels, 3, 2, UINT32_MAX));
    assert(!Rgb565Pixels::from_bytes(pixels, 0, 2, 8));
    puts("Binary/FixedText/RGB565: golden bytes, bounds, UTF-8, unaligned padded pixels OK");
}
