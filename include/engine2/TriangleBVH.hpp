#pragma once

#include "engine2/TrianglePicking.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace engine2 {

// Per-ray operation counts, independent of CPU clock speed and machine load.
struct TriangleBVHStats {
    std::uint64_t boundsTests = 0;
    std::uint64_t nodesVisited = 0;
    std::uint64_t trianglesTested = 0;
};

// Binary bounding-volume hierarchy built ONCE for immutable mesh geometry.
// Median centroid splitting is deterministic (triangle ordinal tie-break).
// Mesh positions/indices are owned by Mesh, not duplicated by the hierarchy.
// The resulting BVH is immutable and safe for concurrent read-only raycasts.
class TriangleBVH {
public:
    TriangleBVH() = default;

    TriangleBVH(const std::vector<glm::vec3>& positions,
                const std::vector<std::uint32_t>& indices) {
        rebuild(positions, indices);
    }

    void rebuild(const std::vector<glm::vec3>& positions,
                 const std::vector<std::uint32_t>& indices) {
        if (indices.size() % 3 != 0)
            throw std::invalid_argument("BVH requires triangle indices");
        if (indices.size() / 3 >
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            throw std::length_error("BVH has too many triangles");
        for (const glm::vec3& p : positions) {
            if (!std::isfinite(p.x) || !std::isfinite(p.y) ||
                !std::isfinite(p.z))
                throw std::invalid_argument("BVH vertex must be finite");
        }
        for (std::uint32_t index : indices) {
            if (index >= positions.size())
                throw std::invalid_argument("BVH triangle index is out of range");
        }
        triangleOrder_.resize(indices.size()/3);
        std::iota(triangleOrder_.begin(), triangleOrder_.end(), 0U);
        nodes_.clear();
        if (!triangleOrder_.empty()) {
            nodes_.reserve(triangleOrder_.size() * 2U);
            buildNode(positions, indices, 0,
                      static_cast<std::uint32_t>(triangleOrder_.size()), 0);
        }
    }

    [[nodiscard]] std::size_t triangleCount() const {
        return triangleOrder_.size();
    }
    [[nodiscard]] std::size_t nodeCount() const {
        return nodes_.size();
    }

    [[nodiscard]] std::optional<float> raycast(
        const std::vector<glm::vec3>& positions,
        const std::vector<std::uint32_t>& indices,
        const glm::vec3& origin, const glm::vec3& direction,
        float maximumDistance = std::numeric_limits<float>::infinity(),
        TriangleBVHStats* stats = nullptr) const {

        if (stats) *stats = {};
        if (indices.size()/3 != triangleOrder_.size() ||
            indices.size()%3 != 0)
            throw std::invalid_argument("BVH geometry differs from built index buffer");
        if (!(maximumDistance >= 0.0f) || std::isnan(maximumDistance))
            throw std::invalid_argument("BVH maximum distance must be nonnegative");
        if (!std::isfinite(origin.x) || !std::isfinite(origin.y) ||
            !std::isfinite(origin.z) || !std::isfinite(direction.x) ||
            !std::isfinite(direction.y) || !std::isfinite(direction.z))
            throw std::invalid_argument("BVH ray must be finite");
        if (nodes_.empty()) return std::nullopt;

        std::optional<float> best;
        float limit = maximumDistance;
        struct Pending { std::uint32_t node; float entry; };
        std::vector<Pending> stack;
        stack.reserve(48);
        const auto root = intersectBox(nodes_[0],origin,direction,limit,stats);
        if (!root) return std::nullopt;
        stack.push_back({0U,*root});

        while (!stack.empty()) {
            const Pending pending = stack.back();
            stack.pop_back();
            if (pending.entry > limit) continue;
            const Node& node = nodes_[pending.node];
            if (stats) ++stats->nodesVisited;
            if (node.count != 0) {
                const std::size_t end =
                    static_cast<std::size_t>(node.first) + node.count;
                for (std::size_t i=node.first; i<end; ++i) {
                    const std::size_t offset =
                        static_cast<std::size_t>(triangleOrder_[i]) * 3U;
                    if (stats) ++stats->trianglesTested;
                    const auto t = intersectPickTriangle(
                        origin, direction,
                        positions[indices[offset]],
                        positions[indices[offset+1]],
                        positions[indices[offset+2]]);
                    if (t && *t <= limit) {
                        best = t;
                        limit = *t;
                    }
                }
            } else {
                const auto l = intersectBox(
                    nodes_[node.left],origin,direction,limit,stats);
                const auto r = intersectBox(
                    nodes_[node.right],origin,direction,limit,stats);
                // LIFO stack: visit nearer child first, allowing an early
                // nearest-hit update to prune more distant bounding boxes.
                if (l && r) {
                    if (*l <= *r) {
                        stack.push_back({node.right,*r});
                        stack.push_back({node.left,*l});
                    } else {
                        stack.push_back({node.left,*l});
                        stack.push_back({node.right,*r});
                    }
                } else if (l) {
                    stack.push_back({node.left,*l});
                } else if (r) {
                    stack.push_back({node.right,*r});
                }
            }
        }
        return best;
    }

private:
    struct Node {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
        std::uint32_t first = 0;
        std::uint32_t count = 0; // 0 = internal, positive = leaf
        std::uint32_t left = 0;
        std::uint32_t right = 0;
    };
    struct TriangleBounds {
        glm::vec3 min;
        glm::vec3 max;
        glm::vec3 centroid;
    };

    [[nodiscard]] static TriangleBounds triangleBounds(
        const std::vector<glm::vec3>& positions,
        const std::vector<std::uint32_t>& indices,
        std::uint32_t tri) {
        const std::size_t offset = static_cast<std::size_t>(tri)*3U;
        const glm::vec3 a=positions[indices[offset]];
        const glm::vec3 b=positions[indices[offset+1]];
        const glm::vec3 c=positions[indices[offset+2]];
        const glm::vec3 minimum=glm::min(a,glm::min(b,c));
        const glm::vec3 maximum=glm::max(a,glm::max(b,c));
        return {minimum,maximum,(a+b+c)*(1.0f/3.0f)};
    }

    std::uint32_t buildNode(
        const std::vector<glm::vec3>& positions,
        const std::vector<std::uint32_t>& indices,
        std::uint32_t first, std::uint32_t count,
        unsigned int depth) {
        const std::uint32_t index=static_cast<std::uint32_t>(nodes_.size());
        nodes_.push_back(Node{});
        const float infinity=std::numeric_limits<float>::infinity();
        glm::vec3 minimum(infinity),maximum(-infinity);
        glm::vec3 centroidMin(infinity),centroidMax(-infinity);
        for (std::uint32_t i=first;i<first+count;++i) {
            const auto bounds=triangleBounds(
                positions,indices,triangleOrder_[i]);
            minimum=glm::min(minimum,bounds.min);
            maximum=glm::max(maximum,bounds.max);
            centroidMin=glm::min(centroidMin,bounds.centroid);
            centroidMax=glm::max(centroidMax,bounds.centroid);
        }
        nodes_[index].min=minimum;
        nodes_[index].max=maximum;

        const glm::vec3 extent=centroidMax-centroidMin;
        unsigned int axis=0;
        if (extent.y > extent.x) axis=1;
        if (extent.z > extent[axis]) axis=2;
        constexpr std::uint32_t leafLimit=8;
        if (count <= leafLimit || depth >= 48 || extent[axis] <= 1.0e-7f) {
            nodes_[index].first=first;
            nodes_[index].count=count;
            return index;
        }

        const std::uint32_t middle=first+count/2U;
        std::nth_element(
            triangleOrder_.begin()+first,triangleOrder_.begin()+middle,
            triangleOrder_.begin()+first+count,
            [&](std::uint32_t a,std::uint32_t b) {
                const float x=triangleBounds(positions,indices,a).centroid[axis];
                const float y=triangleBounds(positions,indices,b).centroid[axis];
                return x<y || (x==y && a<b);
            });
        const std::uint32_t left=buildNode(
            positions,indices,first,middle-first,depth+1);
        const std::uint32_t right=buildNode(
            positions,indices,middle,first+count-middle,depth+1);
        // Do not retain a Node& while recursing; vector growth may reallocate.
        nodes_[index].left=left;
        nodes_[index].right=right;
        return index;
    }

    [[nodiscard]] static std::optional<float> intersectBox(
        const Node& node, glm::vec3 origin, glm::vec3 direction,
        float limit, TriangleBVHStats* stats) {
        if (stats) ++stats->boundsTests;
        float nearT=0.0f;
        float farT=limit;
        for (int axis=0;axis<3;++axis) {
            if (std::abs(direction[axis]) < 1.0e-10f) {
                if (origin[axis] < node.min[axis] ||
                    origin[axis] > node.max[axis]) return std::nullopt;
                continue;
            }
            float a=(node.min[axis]-origin[axis])/direction[axis];
            float b=(node.max[axis]-origin[axis])/direction[axis];
            if (a>b) std::swap(a,b);
            nearT=std::max(nearT,a);
            farT=std::min(farT,b);
            if (nearT>farT) return std::nullopt;
        }
        return nearT;
    }

    std::vector<std::uint32_t> triangleOrder_;
    std::vector<Node> nodes_;
};

} // namespace engine2
