#pragma once

#include <glm/common.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace engine2 {

// Grid coordinates are computed from WORLD-space swept bounds. Each proxy
// can cover multiple cells; duplicate pairs are removed and sorted to make
// the solver independent of hash/container iteration order.
struct PhysicsBroadphaseProxy {
    glm::vec3 minimum{0.0f};
    glm::vec3 maximum{0.0f};
    bool isStatic = false;
};

struct PhysicsBroadphaseResult {
    std::vector<std::pair<std::size_t, std::size_t>> pairs;
    std::size_t occupiedCells = 0;
    std::uint64_t candidateTests = 0;
    std::size_t oversizedProxies = 0;
};

inline bool physicsBoundsOverlap(
    const PhysicsBroadphaseProxy& a, const PhysicsBroadphaseProxy& b) {
    for (int k=0;k<3;++k)
        if (a.minimum[k]>b.maximum[k] || b.minimum[k]>a.maximum[k])
            return false;
    return true;
}

inline PhysicsBroadphaseResult physicsGridBroadphase(
    const std::vector<PhysicsBroadphaseProxy>& bodies,
    float cellSize = 2.0f) {
    if (!(cellSize>0.0f) || !std::isfinite(cellSize))
        throw std::invalid_argument("Physics grid cell size must be positive");
    struct Cell {
        std::int64_t x, y, z;
        bool operator<(const Cell& other) const {
            if (x!=other.x) return x<other.x;
            if (y!=other.y) return y<other.y;
            return z<other.z;
        }
    };
    std::map<Cell, std::vector<std::size_t>> cells;
    std::vector<std::size_t> oversized;
    PhysicsBroadphaseResult result;
    constexpr std::uint64_t maximumCellsPerProxy = 256;

    auto coordinate=[&](float value) -> std::int64_t {
        if (!std::isfinite(value) ||
            std::abs(static_cast<double>(value)/cellSize)>9.0e15)
            throw std::invalid_argument("Physics grid position is out of range");
        return static_cast<std::int64_t>(
            std::floor(static_cast<double>(value)/cellSize));
    };
    for (std::size_t i=0;i<bodies.size();++i) {
        const auto& box=bodies[i];
        std::int64_t lo[3],hi[3];
        std::uint64_t count=1;
        for (int axis=0;axis<3;++axis) {
            if (box.minimum[axis]>box.maximum[axis])
                throw std::invalid_argument("Physics proxy bounds are inverted");
            lo[axis]=coordinate(box.minimum[axis]);
            hi[axis]=coordinate(box.maximum[axis]);
            const auto span=static_cast<std::uint64_t>(hi[axis]-lo[axis])+1;
            if (span>maximumCellsPerProxy ||
                count>maximumCellsPerProxy/span) {
                count=maximumCellsPerProxy+1;
                break;
            }
            count*=span;
        }
        if (count>maximumCellsPerProxy) {
            oversized.push_back(i);
            continue;
        }
        for (std::int64_t x=lo[0];x<=hi[0];++x)
            for (std::int64_t y=lo[1];y<=hi[1];++y)
                for (std::int64_t z=lo[2];z<=hi[2];++z)
                    cells[{x,y,z}].push_back(i);
    }
    result.occupiedCells=cells.size();
    result.oversizedProxies=oversized.size();

    auto testPair=[&](std::size_t a,std::size_t b) {
        if (a==b) return;
        if (a>b) std::swap(a,b);
        if (bodies[a].isStatic && bodies[b].isStatic) return;
        ++result.candidateTests;
        if (physicsBoundsOverlap(bodies[a],bodies[b]))
            result.pairs.emplace_back(a,b);
    };
    for (const auto& [cell,items] : cells) {
        (void)cell;
        for (std::size_t i=0;i<items.size();++i)
            for (std::size_t j=i+1;j<items.size();++j)
                testPair(items[i],items[j]);
    }
    for (std::size_t i : oversized)
        for (std::size_t j=0;j<bodies.size();++j)
            if (i!=j) testPair(i,j);
    std::sort(result.pairs.begin(),result.pairs.end());
    result.pairs.erase(
        std::unique(result.pairs.begin(),result.pairs.end()),
        result.pairs.end());
    return result;
}

} // namespace engine2
