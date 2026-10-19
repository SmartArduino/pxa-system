#include <pxa/game_spans_wire_generated.hpp>
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
namespace g = pxa::game::wire_spans;
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
    roundtrip<g::pxa_game_render_spans_header_wire_t,72>(g::pxa_game_render_spans_header_decode,g::pxa_game_render_spans_header_encode);
    roundtrip<g::pxa_game_render_projection_wire_t,80>(g::pxa_game_render_projection_decode,g::pxa_game_render_projection_encode);
    roundtrip<g::pxa_game_render_span_wire_t,24>(g::pxa_game_render_span_decode,g::pxa_game_render_span_encode);
    roundtrip<g::pxa_game_render_blit_wire_t,64>(g::pxa_game_render_blit_decode,g::pxa_game_render_blit_encode);
    roundtrip<g::pxa_game_render_frame_header_wire_t,16>(g::pxa_game_render_frame_header_decode,g::pxa_game_render_frame_header_encode);
    roundtrip<g::pxa_game_render_row_wire_t,8>(g::pxa_game_render_row_decode,g::pxa_game_render_row_encode);
    std::array<std::byte,56> expected;
    std::array<g::pxa_game_render_row_wire_t,7> rows;
    for(std::size_t i=0;i<expected.size();++i)expected[i]=std::byte((i*37+159)&255);
    for(unsigned i=0;i<rows.size();++i)assert(g::pxa_game_render_row_decode(expected.data()+i*8,8,&rows[i]));
    std::array<std::byte,72> output;output.fill(std::byte{0xa5});
    assert(!g::pxa_game_render_rows_encode(output.data()+1,55,rows.data(),rows.size()));
    for(auto b:output)assert(b==std::byte{0xa5});
    assert(g::pxa_game_render_rows_encode(output.data()+1,56,rows.data(),rows.size()));
    assert(!std::memcmp(output.data()+1,expected.data(),56));
    assert(output[0]==std::byte{0xa5}&&output[57]==std::byte{0xa5});
    assert(g::pxa_game_render_rows_encode(nullptr,0,nullptr,0));
    std::puts("PASS: WASM GameRender encoders preserve every wire byte, six records, odd offsets, short buffers and contiguous rows");
}
