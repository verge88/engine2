#pragma once

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cmath>

namespace engine2 {

struct Bounds {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

// Six world-space clip planes extracted from a standard OpenGL view-projection
// matrix. OBB support radius is computed from transformed local AABB axes,
// allowing rotated/non-uniformly scaled meshes to be culled conservatively.
class Frustum {
public:
    explicit Frustum(const glm::mat4& viewProjection) {
        const glm::vec4 r0(viewProjection[0][0], viewProjection[1][0],
                           viewProjection[2][0], viewProjection[3][0]);
        const glm::vec4 r1(viewProjection[0][1], viewProjection[1][1],
                           viewProjection[2][1], viewProjection[3][1]);
        const glm::vec4 r2(viewProjection[0][2], viewProjection[1][2],
                           viewProjection[2][2], viewProjection[3][2]);
        const glm::vec4 r3(viewProjection[0][3], viewProjection[1][3],
                           viewProjection[2][3], viewProjection[3][3]);
        planes_ = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r3 + r2, r3 - r2};
        for (auto& plane : planes_) {
            const float length = glm::length(glm::vec3(plane));
            if (length > 1.0e-7f) plane /= length;
        }
    }

    [[nodiscard]] bool intersects(const Bounds& bounds, const glm::mat4& model) const {
        const glm::vec3 localCenter = (bounds.min + bounds.max) * 0.5f;
        const glm::vec3 halfExtents = (bounds.max - bounds.min) * 0.5f;
        const glm::vec3 center = glm::vec3(model * glm::vec4(localCenter, 1.0f));
        const glm::vec3 xAxis = glm::vec3(model[0]) * halfExtents.x;
        const glm::vec3 yAxis = glm::vec3(model[1]) * halfExtents.y;
        const glm::vec3 zAxis = glm::vec3(model[2]) * halfExtents.z;
        for (const glm::vec4& plane : planes_) {
            const glm::vec3 n(plane);
            const float radius = std::abs(glm::dot(n, xAxis)) +
                                 std::abs(glm::dot(n, yAxis)) +
                                 std::abs(glm::dot(n, zAxis));
            if (glm::dot(n, center) + plane.w < -radius) return false;
        }
        return true;
    }

private:
    std::array<glm::vec4, 6> planes_{};
};

} // namespace engine2
