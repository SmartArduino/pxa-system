#include <pxa/game_utils.hpp>
#include <pxa/game3d_camera.hpp>
#include <pxa/game3d_material.hpp>
#include <cassert>
#include <cmath>
extern "C" std::int32_t pxa_submit(const std::uint8_t*,std::uint32_t) { return 0; }
extern "C" std::int32_t pxa_io(std::uint64_t,std::uint32_t,std::uint8_t*,std::uint32_t) { return 0; }
static bool close(float a,float b) { return std::fabs(a-b)<0.00002f; }
int main() {
    pxa::game::RecyclingPool<int,3> pool;
    pool.acquire()=1; pool.acquire()=2; pool.acquire()=3;
    assert(pool.items()[0]==1 && pool.items()[2]==3);
    auto& recycled=pool.acquire(); assert(recycled==0); recycled=4;
    assert(pool.items()[0]==4 && pool.items()[1]==2);
    static_assert(sizeof(pool)==16);
    pxa::game::StageStatistics<2> stats;
    assert(stats.record(1,100) && stats.record(1,200) && !stats.record(2,7));
    assert(stats.stages[1].mean_us()==150 && stats.stages[1].max_us==200);
    stats.reset(); assert(stats.stages[1].samples==0 && stats.stages[1].mean_us()==0);
    for(float yaw : {0.0f,0.7f,2.4f,-1.1f}) for(float pitch : {-1.0f,0.0f,0.9f}) {
        auto basis=pxa::game3d::CameraBasis::from_pose({3,4,5},yaw,pitch);
        auto f=basis.forward(); auto r=basis.right();
        auto projected=basis.to_view({3+f.x*8,4+f.y*8,5+f.z*8});
        assert(close(projected.x,0) && close(projected.y,0) && close(projected.z,8));
        auto right=basis.to_view({3+r.x,4+r.y,5+r.z});
        assert(close(right.x,1) && close(right.y,0) && close(right.z,0));
    }
    std::array<pxa::game::Vertex,4> q{{{.x_q4=0,.y_q4=0,.depth_q8=4096},
        {.x_q4=63,.y_q4=0,.depth_q8=4096},{.x_q4=63,.y_q4=64,.depth_q8=4096},
        {.x_q4=0,.y_q4=64,.depth_q8=4096}}};
    using namespace pxa::game3d;
    assert(choose_material_path(q,16,false)==MaterialPath::solid);
    assert(choose_material_path(q,16,true)==MaterialPath::affine);
    assert(choose_material_path(q,16,false,{.solid_extent_q4=0})==MaterialPath::affine);
    q[1].depth_q8=4607;
    assert(choose_material_path(q,16,true)==MaterialPath::perspective);
    auto projection=Projector::create(296,240,1.15f,0.25f,64);
    assert(projection);
    const auto focal=projection->focal_length();
    assert(projection->clip_range(0.25f,24));
    assert(!projection->clip_range(24,24) && !projection->clip_range(0.25f,INFINITY));
    assert(projection->focal_length()==focal);
    std::array<pxa::game::Vertex,Projector::max_polygon_vertices> out{};
    const std::array<pxa::game3d::MeshVertex,3> outside{{{{0,0,25}},{{1,0,25}},{{0,1,25}}}};
    auto count=projection->project_polygon(outside,out); assert(count && *count==0);
}
