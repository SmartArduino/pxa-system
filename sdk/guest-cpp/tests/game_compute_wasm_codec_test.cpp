#include <pxa/game_compute_wire_generated.hpp>
#include <pxa/game_tiles.hpp>
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
namespace g = pxa::game::wire_compute;
template<class T, std::size_t N, class Decode, class Encode>
static void roundtrip(Decode decode, Encode encode) {
    for (unsigned alignment=0;alignment<8;++alignment) {
        std::array<std::byte,N> expected;
        for(std::size_t i=0;i<N;++i)expected[i]=std::byte((i*73+alignment*19)&255);
        T value;
        assert(decode(expected.data(),N,&value));
        std::array<std::byte,N+16> output;output.fill(std::byte{0xa5});
        assert(!encode(output.data()+alignment,N-1,&value));
        for(auto b:output)assert(b==std::byte{0xa5});
        assert(encode(output.data()+alignment,N,&value));
        assert(!std::memcmp(output.data()+alignment,expected.data(),N));
        for(std::size_t i=0;i<alignment;++i)assert(output[i]==std::byte{0xa5});
        for(std::size_t i=alignment+N;i<output.size();++i)assert(output[i]==std::byte{0xa5});
    }
}
int main() {
    // Snapshot unaligned caller data with either codec selection, including
    // sources that overlap the newly reserved area of the same packet.
    for (unsigned alignment = 0; alignment < 8; ++alignment) {
        std::array<std::byte, 1024> storage{};
        auto bytes = std::span(storage).subspan(alignment);
        pxa::game::PriorityTileBatch batch(bytes, {4096, 8192, 1024, 32});
        std::array<std::byte, 64> expected;
        for (unsigned i = 0; i < expected.size(); ++i) expected[i] = std::byte(i * 13);
        // This source overlaps the first destination by 56..59 bytes.
        auto source = bytes.subspan(bytes.size() - 72, 64);
        std::memcpy(source.data(), expected.data(), expected.size());
        auto offset = batch.copy_source(source);
        assert(offset && !std::memcmp(bytes.data() + *offset, expected.data(), 64));
        assert((reinterpret_cast<std::uintptr_t>(bytes.data() + *offset) & 3u) == 0);
        auto second = batch.copy_source(bytes.subspan(*offset, 64));
        assert(second && !std::memcmp(bytes.data() + *second, expected.data(), 64));
    }
    roundtrip<g::pxa_game_render_tiles_header_wire_t,32>(g::pxa_game_render_tiles_header_decode,g::pxa_game_render_tiles_header_encode);
    roundtrip<g::pxa_game_render_tile_wire_t,24>(g::pxa_game_render_tile_decode,g::pxa_game_render_tile_encode);
    roundtrip<g::pxa_game_render_blit_wire_t,64>(g::pxa_game_render_blit_decode,g::pxa_game_render_blit_encode);
    std::puts("PASS: WASM compute codecs preserve wire bytes, odd offsets and short buffers");
}
