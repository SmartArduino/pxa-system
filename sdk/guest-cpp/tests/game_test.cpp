#include <pxa/game.hpp>
#include <pxa/game3d.hpp>

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>

static std::array<std::byte, 512> last_draw{};
static std::uint32_t last_draw_size = 0;
static int submits = 0;
static int binds = 0;
static int bind_result = 0;
static int allocations = 0;

void* operator new(std::size_t size) {
    ++allocations;
    if (void* pointer = std::malloc(size)) return pointer;
    std::abort();
}
void* operator new[](std::size_t size) {
    ++allocations;
    if (void* pointer = std::malloc(size)) return pointer;
    std::abort();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

extern "C" std::int32_t pxa_submit(const std::uint8_t*, std::uint32_t) {
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t handle, std::uint32_t operation,
                                  std::uint8_t* data, std::uint32_t size) {
    assert(handle == 77);
    if (operation == 0x103) {
        ++binds;
        assert(size == 28 || size == 16);
        assert(pxa::wire::get16(reinterpret_cast<std::byte*>(data)) == (size == 28 ? 2 : 1));
        if (bind_result) return bind_result;
        return size;
    }
    assert(operation == 0x101);
    ++submits;
    last_draw_size = size;
    assert(size <= last_draw.size());
    for (std::uint32_t i = 0; i < size; ++i)
        last_draw[i] = std::byte(data[i]);
    return size;
}

int main() {
    // Compare the AOT-friendly quantizer with the standard half-away rule,
    // including adjacent floats at positive/negative tie boundaries.
    for (int i=-32768; i<=32767; ++i) {
        for (float offset : {0.0f, 0.5f}) {
            const float value = i + offset;
            for (float sample : {value, std::nextafter(value, -INFINITY), std::nextafter(value, INFINITY)})
                assert(pxa::game3d::detail::round_wire_value(sample) == std::lround(sample));
        }
    }
    std::uint32_t random=123456789;
    for (int i=0;i<8192;++i) {
        random=random*1664525+1013904223;
        const float value=(static_cast<float>(random & 0xffffffu)/16777215.0f)*131072-65536;
        assert(pxa::game3d::detail::round_wire_value(value)==std::lround(value));
    }

    pxa::Transport transport;
    transport.phase(pxa::Phase::event);
    pxa::game::Renderer renderer(transport, 77, 1 | 2 | 8 | 16 | 32);
    std::array<std::uint64_t, 1> textures{11};
    assert(renderer.bind_assets(textures, 12));
    assert(binds == 1);
    {
        pxa::Asset texture(transport, 11, {.kind = pxa::AssetKind::texture});
        pxa::Asset palette(transport, 12, {.kind = pxa::AssetKind::palette});
        pxa::Asset image(transport, 13, {.kind = pxa::AssetKind::image});
        pxa::Asset absent(transport, 0, {.kind = pxa::AssetKind::texture});
        assert(renderer.bind_asset(texture));
        assert(renderer.bind_asset(palette));
        assert(!renderer.bind_asset(image) && !renderer.bind_asset(absent));
        assert(!renderer.bind_asset(palette, {1}));
        assert(!renderer.bind_asset(texture, {16}));
        assert(binds == 3);
        bind_result = 15;
        assert(renderer.bind_asset(texture).error() == pxa::Error::protocol_error);
        bind_result = -11;
        assert(!renderer.bind_asset(texture));
        bind_result = 0;
    }

    pxa::game::DrawBuffer<512> buffer;
    std::array<pxa::game::Sprite, 1> sprites{{
        {.x = 10, .y = 20, .width = 16, .height = 16,
         .source_width = 16, .source_height = 16}}};
    std::array<pxa::game::Vertex, 4> quad_vertices{{
        {.x_q4 = 0, .y_q4 = 0},
        {.x_q4 = 160, .y_q4 = 0, .u_q4 = 16},
        {.x_q4 = 160, .y_q4 = 160, .u_q4 = 16, .v_q4 = 16},
        {.x_q4 = 0, .y_q4 = 160, .v_q4 = 16}}};
    std::array<pxa::game::Vertex, 3> triangle_vertices{{
        {.x_q4 = 0, .y_q4 = 0},
        {.x_q4 = 160, .y_q4 = 0},
        {.x_q4 = 0, .y_q4 = 160}}};
    auto frame = renderer.frame(buffer);
    frame.clear(pxa::game::Color565::black());
    frame.quad({0, 0, 16, 0, 16, 16, 0, 16}, {0xffff});
    frame.sprites({0}, sprites);
    frame.textured_quad({0}, quad_vertices, {.affine_uv = true});
    frame.triangles({0}, triangle_vertices);
    frame.solid_triangles(triangle_vertices, {0xf800});
    assert(frame.submit());
    assert(submits == 1 && last_draw_size == 32 + 8 + 24 + 28 + 56 + 48 + 48);
    assert(pxa::wire::get32(last_draw.data()) == 0x4c525850);
    assert(pxa::wire::get32(last_draw.data() + 12) == 59);
    assert(pxa::wire::get32(last_draw.data() + 16) == 6);
    assert(pxa::wire::get64(last_draw.data() + 20) == 1);
    assert(last_draw[92] == std::byte{3} && last_draw[93] == std::byte{2});
    assert(pxa::wire::get16(last_draw.data() + 94) == 56);
    assert(pxa::wire::get16(last_draw.data() + 100 + 12) == 160);
    assert(pxa::wire::get16(last_draw.data() + 100 + 12 + 4) == 16);
    assert(pxa::wire::get16(last_draw.data() + 100 + 10) == 256);
    assert(last_draw[148] == std::byte{6});
    assert(pxa::wire::get16(last_draw.data() + 156) == 1);
    assert(last_draw[196] == std::byte{6} && last_draw[197] == std::byte{1});
    assert(pxa::wire::get16(last_draw.data() + 202) == 0xf800);
    auto next = renderer.frame(buffer);
    next.clear(pxa::game::Color565::black());
    assert(next.submit());
    assert(pxa::wire::get64(last_draw.data() + 20) == 2);

    pxa::game::Renderer limited(transport, 77, 2 | 16);
    auto unsupported = limited.frame(buffer);
    unsupported.textured_quad({0}, quad_vertices, {.affine_uv = true});
    auto unsupported_result = unsupported.submit();
    assert(!unsupported_result &&
           unsupported_result.error() == pxa::Error::unsupported);

    auto bad_options = renderer.frame(buffer);
    bad_options.textured_quad({0}, quad_vertices,
                              {.coverage_mask = true});
    auto invalid = bad_options.submit();
    assert(!invalid && invalid.error() == pxa::Error::invalid_argument);

    auto bad_batch = renderer.frame(buffer);
    bad_batch.triangles({0}, std::span{triangle_vertices}.first(2));
    invalid = bad_batch.submit();
    assert(!invalid && invalid.error() == pxa::Error::invalid_argument);

    auto missing_sprite_feature = renderer.frame(buffer);
    missing_sprite_feature.sprites(
        {0}, sprites, {.solid_color = true, .additive = true,
                       .color = {0xffff}});
    auto missing = missing_sprite_feature.submit();
    assert(!missing && missing.error() == pxa::Error::unsupported);

    auto missing_slots = renderer.frame(buffer);
    missing_slots.sprites({16}, sprites);
    missing = missing_slots.submit();
    assert(!missing && missing.error() == pxa::Error::unsupported);

    pxa::game::DrawBuffer<64> small_buffer;
    auto full = renderer.frame(small_buffer);
    full.clear(pxa::game::Color565::black());
    full.solid_triangles(triangle_vertices, {0xffff});
    auto overflow = full.submit();
    assert(!overflow && overflow.error() == pxa::Error::limit_exceeded);
    assert(submits == 2);

    const auto before = allocations;
    for (int i = 0; i < 16; ++i) {
        auto steady = renderer.frame(buffer);
        steady.clear({0}).sprites({0}, sprites)
              .solid_triangles(triangle_vertices, {0xf800})
              .solid_depth_quad(quad_vertices, {0x07e0});
        assert(steady.submit());
    }
    assert(submits == 18 && allocations == before);
    // Solid depth quads need no texture capabilities or bound assets.
    pxa::game::Renderer flat_renderer(transport, 77, 2);
    auto flat = flat_renderer.frame(buffer);
    flat.solid_depth_quad(quad_vertices, {0x1234});
    assert(flat.submit());
    assert(last_draw_size == 32 + 56);
    assert(pxa::wire::get32(last_draw.data() + 12) == 2);
    assert(last_draw[33] == std::byte{1});
    assert(pxa::wire::get16(last_draw.data() + 38) == 0x1234);
    assert(pxa::wire::get16(last_draw.data() + 32 + 8 + 10) == 256);
    pxa::game::Renderer no_quads(transport, 77, 16);
    auto missing_quad = no_quads.frame(buffer);
    missing_quad.solid_depth_quad(quad_vertices, {0});
    assert(missing_quad.submit().error() == pxa::Error::unsupported);
    auto full_quad = flat_renderer.frame(small_buffer);
    full_quad.solid_depth_quad(quad_vertices, {0});
    assert(full_quad.submit().error() == pxa::Error::limit_exceeded);
    assert(allocations == before);

    // Clipped convex perimeters retain the scanline depth path. Complete
    // polygon reservations fail before partially encoding a triangle fan.
    const std::array<pxa::game::Vertex, 5> pentagon{
        quad_vertices[0], quad_vertices[1],
        pxa::game::Vertex{.x_q4=200, .y_q4=80}, quad_vertices[2], quad_vertices[3]};
    pxa::game::Renderer polygon_renderer(transport, 77, 2 | 128 | 256 | 32768);
    auto polygon = polygon_renderer.frame(buffer);
    polygon.textured_depth_polygon({0}, pentagon, {.transparent_index0 = true});
    assert(polygon.submit() && last_draw_size == 32 + 3 * 56);
    for (unsigned offset = 32; offset < last_draw_size; offset += 56) {
        assert(last_draw[offset] == std::byte{3});
        assert(last_draw[offset + 1] == std::byte{4 | 8 | 16});
        assert(std::memcmp(last_draw.data() + offset + 8 + 2 * 12,
                           last_draw.data() + offset + 8 + 3 * 12, 12) == 0);
    }
    auto short_polygon = polygon_renderer.frame(buffer);
    short_polygon.solid_depth_polygon(std::span{quad_vertices}.first(3), {0x1234});
    assert(short_polygon.submit() && last_draw_size == 32 + 56);
    assert(last_draw[33] == std::byte{1 | 4 | 16});
    const std::array<pxa::game::Vertex, 5> rounded_perimeter{
        quad_vertices[0], quad_vertices[1], quad_vertices[1], quad_vertices[2], quad_vertices[3]};
    auto rounded = polygon_renderer.frame(buffer);
    rounded.solid_depth_polygon(rounded_perimeter, {0});
    assert(rounded.submit() && last_draw_size == 32 + 2 * 56);
    const std::array<pxa::game::Vertex, 4> collinear{{
        {.x_q4=0}, {.x_q4=16}, {.x_q4=32}, {.x_q4=48}}};
    auto invisible = polygon_renderer.frame(buffer);
    invisible.clear({0}).solid_depth_polygon(collinear, {0});
    assert(invisible.submit() && last_draw_size == 40);
    auto invalid_polygon = renderer.frame(buffer);
    invalid_polygon.solid_depth_polygon(std::span{quad_vertices}.first(2), {0});
    assert(invalid_polygon.submit().error() == pxa::Error::invalid_argument);
    auto bounded_polygon = renderer.frame(small_buffer);
    const auto used_before = bounded_polygon.bytes_used();
    bounded_polygon.solid_depth_polygon(pentagon, {0});
    assert(bounded_polygon.bytes_used() == used_before);
    assert(bounded_polygon.submit().error() == pxa::Error::limit_exceeded);
    auto unsupported_polygon = flat_renderer.frame(buffer);
    unsupported_polygon.solid_depth_polygon(std::span{quad_vertices}.first(3), {0});
    assert(unsupported_polygon.submit().error() == pxa::Error::unsupported);
    assert(allocations == before);

    auto projection = pxa::game3d::Projector::create(320, 240, 1.2f, 0.25f, 32.0f);
    assert(projection);
    std::array<pxa::game::Vertex, 21> projected{};
    const std::array<pxa::game3d::MeshVertex, 3> face{{
        {{-0.5f, -0.5f, 2.0f}},
        {{0.5f, -0.5f, 2.0f}},
        {{0.0f, 0.5f, 2.0f}}}};
    const auto visible = projection->project_triangle(face, projected);
    assert(visible && *visible == 3);
    assert(projected[0].depth_q8 == 512);
    assert(projected[0].x_q4 < projected[1].x_q4);
    const auto hidden = projection->project_triangle(
        {face[0], face[2], face[1]}, projected);
    assert(hidden && *hidden == 0);
    auto near_face = face;
    near_face[0].position.z = 0.1f;
    const auto clipped = projection->project_triangle(near_face, projected);
    assert(clipped && *clipped == 6);
    for (std::size_t i = 0; i < *clipped; ++i) {
        assert(projected[i].depth_q8 >= 64);
        assert(projected[i].x_q4 >= 0 && projected[i].x_q4 <= 320 * 16);
        assert(projected[i].y_q4 >= 0 && projected[i].y_q4 <= 240 * 16);
    }
    const auto too_small = projection->project_triangle(
        near_face, std::span{projected}.first(3));
    assert(!too_small && too_small.error() == pxa::Error::limit_exceeded);
    auto outside = face;
    for (auto& vertex : outside) vertex.position.x += 30;
    const auto culled = projection->project_triangle(outside, projected);
    assert(culled && *culled == 0);
    auto partial = face;
    partial[0].position.x = -5.0f;
    const auto edge = projection->project_triangle(
        partial, projected, pxa::game3d::FrontFace::both);
    assert(edge && *edge >= 3);
    for (std::size_t i = 0; i < *edge; ++i)
        assert(projected[i].x_q4 >= 0 && projected[i].x_q4 <= 320 * 16);
    auto transform = pxa::game3d::Transform::rotation_y(0.0f);
    transform.origin.z = 1.0f;
    assert(transform.apply({0.0f, 0.0f, 1.0f}).z == 2.0f);
    assert(allocations == before);

    const std::array<pxa::game3d::MeshVertex, 4> wide_quad{{
        {{-4.0f, -0.5f, 2.0f}, 0, 0},
        {{0.5f, -0.5f, 2.0f}, 16, 0},
        {{0.5f, 0.5f, 2.0f}, 16, 16},
        {{-4.0f, 0.5f, 2.0f}, 0, 16}}};
    std::array<pxa::game::Vertex, pxa::game3d::Projector::max_polygon_vertices> perimeter{};
    auto quad_count = projection->project_polygon(wide_quad, perimeter);
    assert(quad_count && *quad_count == 4);
    unsigned left = 0;
    for (std::size_t i = 0; i < *quad_count; ++i) {
        assert(perimeter[i].depth_q8 == 512); // Q8 camera depth, not a byte.
        if (perimeter[i].x_q4 == 0) {
            ++left;
            assert(perimeter[i].u_q4 > 0 && perimeter[i].u_q4 < 16 * 16);
        }
    }
    assert(left == 2); // Clipped UVs interpolate; clamping screen x cannot do this.
    auto clipped_quad = wide_quad;
    clipped_quad[0].position.z = 0.1f;
    clipped_quad[3].position.z = 0.1f;
    quad_count = projection->project_polygon(clipped_quad, perimeter);
    assert(quad_count && *quad_count >= 3);
    const auto no_space = projection->project_polygon(wide_quad, std::span{perimeter}.first(3));
    assert(!no_space && no_space.error() == pxa::Error::limit_exceeded);
    assert(pxa::game::painter_depth_from_z(2.0f) == 512);
    assert(pxa::game::painter_depth_from_z(0.0001f) == 1);
    assert(pxa::game::painter_depth_from_z(1000.0f) == 65535);
    assert(pxa::game::painter_depth_from_z(-1.0f) == 0);
    assert(pxa::game::painter_depth_from_z(INFINITY) == 0);
    assert(allocations == before);

    // The integer attribute path must preserve all six clipping planes, UV
    // interpolation, and palette rows. Compare complete wire vertices against
    // the general projector for a varied set of partially visible polygons.
    for (unsigned sample = 0; sample < 1024; ++sample) {
        random = random * 1664525 + 1013904223;
        const float x = int(random & 255) / 16.0f - 8.0f;
        const float y = int((random >> 8) & 255) / 32.0f - 4.0f;
        const float z = int((random >> 16) & 255) / 8.0f;
        std::array<pxa::game3d::FixedMeshVertex, 4> fixed{{
            {{x, y, z}, -256, 512, 31},
            {{x + 3, y, z + 1}, 768, 512, 63},
            {{x + 3, y + 2, z + 2}, 768, -256, 95},
            {{x, y + 2, z + 1}, -256, -256, 127}}};
        std::array<pxa::game3d::MeshVertex, 4> general{};
        for (unsigned i = 0; i < 4; ++i)
            general[i] = {fixed[i].position, fixed[i].u_q4 / 16.0f,
                          fixed[i].v_q4 / 16.0f, float(fixed[i].light)};
        std::array<pxa::game::Vertex, 10> a{}, b{};
        const auto fa = projection->project_polygon_fixed(fixed, a);
        const auto fb = projection->project_polygon(general, b);
        assert(fa && fb && *fa == *fb);
        for (unsigned i = 0; i < *fa; ++i) {
            assert(a[i].x_q4 == b[i].x_q4 && a[i].y_q4 == b[i].y_q4);
            assert(a[i].u_q4 == b[i].u_q4 && a[i].v_q4 == b[i].v_q4);
            assert(a[i].depth_q8 == b[i].depth_q8 && a[i].light == b[i].light);
        }
    }
    std::array<pxa::game3d::FixedMeshVertex, 3> invalid_fixed{{
        {{0, 0, 2}}, {{1, 0, 2}}, {{0, 1, 2}}}};
    assert(projection->project_polygon_fixed(invalid_fixed, {}).error() ==
           pxa::Error::limit_exceeded);
    invalid_fixed[0].position.x = NAN;
    assert(projection->project_polygon_fixed(invalid_fixed, projected).error() ==
           pxa::Error::invalid_argument);
    assert(allocations == before);

}
