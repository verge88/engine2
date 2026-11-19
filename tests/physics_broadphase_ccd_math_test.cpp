#include "engine2/PhysicsBroadphase.hpp"
#include "engine2/ContinuousCollision.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
void check(bool yes,const char* why) {
    if (!yes) throw std::runtime_error(why);
}
bool near(float a,float b,float tolerance=0.0002f) {
    return std::abs(a-b)<tolerance;
}
template<class F>
void invalid(F&& fn,const char* why) {
    bool rejected=false;
    try { fn(); } catch(const std::invalid_argument&) { rejected=true; }
    check(rejected,why);
}
}

int main() {
    using namespace engine2;
    constexpr std::size_t bodyCount=300;
    std::vector<PhysicsBroadphaseProxy> proxies;
    proxies.reserve(bodyCount);
    // Same X coordinate, but far apart in Y/Z. The old X-sweep inspected
    // O(n²) pairs here; 3D occupancy should perform zero pair checks.
    for (std::size_t i=0;i<bodyCount;++i) {
        const glm::vec3 center(
            0.0f,static_cast<float>(i%20)*7.0f,
            static_cast<float>(i/20)*7.0f);
        proxies.push_back({center-glm::vec3(0.4f),
                           center+glm::vec3(0.4f),false});
    }
    const auto sparse=physicsGridBroadphase(proxies);
    check(sparse.pairs.empty() && sparse.candidateTests==0,
          "3D broadphase must reject widely separated Y/Z bodies");
    check(sparse.occupiedCells>=bodyCount,
          "3D broadphase did not populate expected spatial cells");

    // Multi-cell overlap creates duplicate cell-level matches; the public
    // pair stream must be deduplicated and ordered by stable object index.
    proxies.clear();
    proxies.push_back({{-1,-1,-1},{1,1,1},false});  // 0
    proxies.push_back({{-0.8f,-0.8f,-0.8f},{0.8f,0.8f,0.8f},false}); // 1
    proxies.push_back({{0.2f,0.2f,0.2f},{1.2f,1.2f,1.2f},true}); // 2
    proxies.push_back({{30,30,30},{31,31,31},false}); // 3
    const auto overlap=physicsGridBroadphase(proxies,0.5f);
    const std::vector<std::pair<std::size_t,std::size_t>> expected{
        {0,1},{0,2},{1,2}
    };
    check(overlap.pairs==expected && overlap.candidateTests>=3,
          "3D broadphase pair order, overlap or deduplication incorrect");

    // Giant static object cannot consume millions of cells: it is handled
    // by deterministic direct comparisons against the other proxies.
    proxies.push_back({{-100,-100,-100},{100,100,100},true});
    const auto giant=physicsGridBroadphase(proxies,0.5f);
    check(giant.oversizedProxies>=1 &&
          std::find(giant.pairs.begin(),giant.pairs.end(),
                    std::pair<std::size_t,std::size_t>(0,4))!=giant.pairs.end() &&
          std::find(giant.pairs.begin(),giant.pairs.end(),
                    std::pair<std::size_t,std::size_t>(2,4))==giant.pairs.end(),
          "Oversized static proxy must intersect dynamics but skip static/static");

    // Compare a deterministic pseudo-random 3D scene with brute-force AABB.
    proxies.clear();
    std::uint32_t seed=20261119U;
    auto randomCoord=[&] {
        seed=1664525U*seed+1013904223U;
        return static_cast<float>(seed%1000U)*0.05f-25.0f;
    };
    for (int i=0;i<240;++i) {
        const glm::vec3 center(randomCoord(),randomCoord(),randomCoord());
        const glm::vec3 half(0.3f+static_cast<float>(i%9)*0.13f);
        proxies.push_back({center-half,center+half,(i%6)==0});
    }
    const auto result=physicsGridBroadphase(proxies,2.0f);
    std::vector<std::pair<std::size_t,std::size_t>> brute;
    for (std::size_t i=0;i<proxies.size();++i)
        for (std::size_t j=i+1;j<proxies.size();++j)
            if (!(proxies[i].isStatic && proxies[j].isStatic) &&
                physicsBoundsOverlap(proxies[i],proxies[j]))
                brute.emplace_back(i,j);
    check(result.pairs==brute,
          "3D spatial grid must match exhaustive 3D AABB overlaps");

    invalid([&]{(void)physicsGridBroadphase({},0);},
            "Nonpositive cell size accepted");
    invalid([&]{(void)physicsGridBroadphase({
        {{1,0,0},{0,1,1},false}});},
        "Inverted 3D proxy bounds accepted");
    invalid([&]{(void)physicsGridBroadphase({
        {{std::numeric_limits<float>::infinity(),0,0},{1,1,1},false}});},
        "Infinite 3D proxy bounds accepted");

    const ColliderGeometry box{ColliderShape::Box,0.5f,glm::vec3(0.5f)};
    const ColliderGeometry sphere{ColliderShape::Sphere,0.5f,glm::vec3(0.5f)};
    const auto wall=sweepSphereStatic(
        {-2,0,0},{2,0,0},0.3f,box,{0,0,0});
    check(wall && near(wall->fraction,0.3f) &&
          wall->normal.x>0.999f,
          "Swept sphere must hit expanded static AABB face at correct TOI");
    const auto ball=sweepSphereStatic(
        {-3,0,0},{3,0,0},0.3f,sphere,{0,0,0});
    check(ball && near(ball->fraction,(3.0f-0.8f)/6.0f) &&
          ball->normal.x>0.999f,
          "Swept sphere/sphere TOI or contact normal is incorrect");
    check(!sweepSphereStatic(
        {-3,2,0},{3,2,0},0.3f,box,{0,0,0}),
          "Swept AABB missed an axis-separated trajectory");
    check(!sweepSphereStatic(
        {-3,2,0},{3,2,0},0.3f,sphere,{0,0,0}),
          "Swept sphere reported a distant false positive");
    check(!sweepSphereStatic(
        {0,0,0},{3,0,0},0.3f,box,{0,0,0}),
          "Initial penetration must go to discrete solver, not CCD");
    check(!sweepSphereStatic(
        {-3,0,0},{-3,0,0},0.3f,box,{0,0,0}),
          "Stationary sphere cannot generate a CCD impact");
    return 0;
}
