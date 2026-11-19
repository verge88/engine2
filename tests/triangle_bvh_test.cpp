#include "engine2/TriangleBVH.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool near(float a, float b, float epsilon=0.0004f) {
    return std::abs(a-b) <= epsilon;
}
template<class Fn>
void mustReject(Fn&& fn,const char* message) {
    bool rejected = false;
    try { fn(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected,message);
}

} // namespace

int main() {
    using namespace engine2;

    // Realistic 128x128 tessellated plane, 32,768 indexed triangles.
    // More than enough to verify asymptotic acceleration WITHOUT relying on
    // wall-clock time, which is unstable on overloaded CI machines.
    constexpr std::uint32_t cells=128;
    std::vector<glm::vec3> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(static_cast<std::size_t>(cells+1)*(cells+1));
    indices.reserve(static_cast<std::size_t>(cells)*cells*6);
    for (std::uint32_t y=0;y<=cells;++y) {
        for (std::uint32_t x=0;x<=cells;++x)
            vertices.emplace_back(static_cast<float>(x) / cells * 20.0f - 10.0f,
                                  static_cast<float>(y) / cells * 20.0f - 10.0f,
                                  0.0f);
    }
    for (std::uint32_t y=0;y<cells;++y) {
        for (std::uint32_t x=0;x<cells;++x) {
            const std::uint32_t a=y*(cells+1)+x;
            const std::uint32_t b=a+cells+1;
            indices.insert(indices.end(),{a,a+1,b,a+1,b+1,b});
        }
    }
    const std::size_t totalTriangles=indices.size()/3;
    check(totalTriangles==32768,"Incorrect synthetic triangle count");
    TriangleBVH bvh(vertices,indices);
    check(bvh.triangleCount()==totalTriangles &&
          bvh.nodeCount()>100,"BVH was not recursively constructed");

    std::uint64_t trianglesTested=0;
    std::uint64_t boundsTested=0;
    std::uint64_t rays=0;
    for (int iy=0; iy<7; ++iy) {
        for (int ix=0; ix<7; ++ix) {
            const float x=-9.3f+ix*2.7f;
            const float y=-9.2f+iy*2.65f;
            const glm::vec3 origin(x,y,8.0f);
            const glm::vec3 direction(0,0,-1);
            TriangleBVHStats stats{};
            const auto accelerated=bvh.raycast(
                vertices,indices,origin,direction,100.0f,&stats);
            const auto reference=intersectPickTriangles(
                vertices,indices,origin,direction,100.0f);
            check(accelerated.has_value()==reference.has_value(),
                  "BVH hit/miss must match brute-force triangle search");
            if (reference)
                check(near(*accelerated,*reference),
                      "BVH chose incorrect nearest triangle");
            check(stats.boundsTests>=stats.nodesVisited &&
                  stats.trianglesTested<=totalTriangles,
                  "Invalid BVH traversal counters");
            trianglesTested+=stats.trianglesTested;
            boundsTested+=stats.boundsTests;
            ++rays;
        }
    }
    // BVH reduces ray-triangle intersection work by at least 50x.
    check(trianglesTested*50 < totalTriangles*rays,
          "BVH failed to reduce triangle intersections by 50x");

    // Axes, origin, nonuniform transform and ray-distance preservation.
    const glm::mat4 parent=
        glm::translate(glm::mat4(1.0f),glm::vec3(1.0f,3.0f,-5.0f)) *
        glm::rotate(glm::mat4(1.0f),glm::radians(32.0f),glm::vec3(0,1,0)) *
        glm::scale(glm::mat4(1.0f),glm::vec3(2.0f,0.5f,1.4f));
    const glm::vec3 target(parent*glm::vec4(1.0f,2.0f,0.0f,1.0f));
    const glm::vec3 facing=glm::normalize(
        glm::vec3(parent*glm::vec4(0,0,1,0)));
    const glm::vec3 worldOrigin=target+facing*6.0f;
    const glm::mat4 inverse=glm::inverse(parent);
    const glm::vec3 localOrigin(inverse*glm::vec4(worldOrigin,1));
    const glm::vec3 localDirection(inverse*glm::vec4(-facing,0));
    const auto transformed=bvh.raycast(vertices,indices,localOrigin,localDirection);
    check(transformed && near(*transformed,6.0f,0.002f),
          "Nonuniform scale and rotated parent changed world ray t");

    TriangleBVHStats missStats{};
    const auto miss=bvh.raycast(vertices,indices,{30,0,5},{0,0,-1},100,&missStats);
    check(!miss && missStats.trianglesTested==0 &&
          missStats.boundsTests==1,
          "BVH broadphase should reject an off-axis ray without triangle tests");
    const auto tooShort=bvh.raycast(vertices,indices,{0,0,5},{0,0,-1},4.0f);
    check(!tooShort,"Maximum distance must bound triangle intersections");

    // Closest of multiple overlapping projected surfaces must still win.
    const std::vector<glm::vec3> layerPoints{
        {-1,-1,-2},{1,-1,-2},{-1,1,-2},
        {-1,-1,0},{1,-1,0},{-1,1,0}
    };
    const std::vector<std::uint32_t> layerIndices{0,1,2,3,4,5};
    TriangleBVH layered(layerPoints,layerIndices);
    const auto closest=layered.raycast(layerPoints,layerIndices,
                                       {-0.5f,-0.5f,4.0f},{0,0,-1});
    check(closest && near(*closest,4.0f),"BVH returned farther triangle");

    // Move-construction/move-assignment mirrors Mesh's existing resource
    // lifetime path. Moved BVH nodes retain correct primitive reordering.
    TriangleBVH moved(std::move(layered));
    check(moved.raycast(layerPoints,layerIndices,
                        {-0.5f,-0.5f,4},{0,0,-1})==closest,
          "BVH move construction dropped geometry");
    TriangleBVH assigned;
    assigned=std::move(moved);
    check(assigned.raycast(layerPoints,layerIndices,
                           {-0.5f,-0.5f,4},{0,0,-1})==closest,
          "BVH move assignment dropped nodes");
    TriangleBVH empty;
    TriangleBVHStats zero{};
    check(!empty.raycast({}, {}, {0,0,2},{0,0,-1},100,&zero) &&
          zero.nodesVisited==0,"Empty BVH should not visit nodes");

    mustReject([&]{TriangleBVH x(vertices,{0,1});},
               "BVH accepts incomplete triangle index list");
    mustReject([&]{TriangleBVH x(vertices,{0,1,999999});},
               "BVH accepts out-of-range triangle index");
    auto badVertices=vertices;
    badVertices[0].x=std::numeric_limits<float>::quiet_NaN();
    mustReject([&]{TriangleBVH x(badVertices,indices);},
               "BVH accepts non-finite vertex position");
    mustReject([&]{(void)bvh.raycast(vertices,indices,
                                   {0,0,1},{0,0,-1},-1);},
               "BVH accepts negative maximum ray distance");

    std::cout << "BVH rays=" << rays
              << " trianglesTotal=" << totalTriangles
              << " bruteForceIntersections=" << totalTriangles*rays
              << " bvhTriangleIntersections=" << trianglesTested
              << " bvhBoundsTests=" << boundsTested << '\n';
    return 0;
}
