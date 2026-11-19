#pragma once

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace engine2 {

// Two-sided Moller-Trumbore triangle raycast. Ray directions may be transformed
// by inverse non-uniform scale: t is still expressed in the INPUT ray units.
// A degenerate triangle or a hit behind the near-ray origin is rejected.
inline std::optional<float> intersectPickTriangle(
    glm::vec3 origin, glm::vec3 direction,
    glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    constexpr float epsilon = 1.0e-7f;
    const glm::vec3 e1 = b - a;
    const glm::vec3 e2 = c - a;
    const glm::vec3 p = glm::cross(direction, e2);
    const float determinant = glm::dot(e1, p);
    if (!std::isfinite(determinant) || std::abs(determinant) < epsilon)
        return std::nullopt;
    const float inverse = 1.0f / determinant;
    const glm::vec3 t = origin - a;
    const float u = glm::dot(t, p) * inverse;
    if (!std::isfinite(u) || u < -epsilon || u > 1.0f + epsilon)
        return std::nullopt;
    const glm::vec3 q = glm::cross(t,e1);
    const float v = glm::dot(direction,q) * inverse;
    if (!std::isfinite(v) || v < -epsilon || u+v > 1.0f + epsilon)
        return std::nullopt;
    const float distance = glm::dot(e2,q) * inverse;
    if (!std::isfinite(distance) || distance < 0.0f)
        return std::nullopt;
    return distance;
}

// Indexed CPU positions retained alongside the GPU geometry. The caller
// first uses a cheap mesh-AABB slab test, then tests actual triangles.
// No GL calls occur, so this function can be unit-tested without a context.
inline std::optional<float> intersectPickTriangles(
    const std::vector<glm::vec3>& positions,
    const std::vector<std::uint32_t>& indices,
    glm::vec3 rayOrigin, glm::vec3 rayDirection,
    float maximumDistance = std::numeric_limits<float>::infinity()) {
    if (indices.size() % 3 != 0)
        throw std::invalid_argument("Triangle index count must be divisible by 3");
    if (!(maximumDistance >= 0.0f))
        throw std::invalid_argument("Ray limit must be nonnegative");
    std::optional<float> nearest;
    float limit = maximumDistance;
    for (std::size_t i = 0; i < indices.size(); i+=3) {
        const auto a = indices[i], b = indices[i+1], c = indices[i+2];
        if (a >= positions.size() || b >= positions.size() || c >= positions.size())
            throw std::invalid_argument("Triangle index is out of range");
        const auto t = intersectPickTriangle(
            rayOrigin, rayDirection, positions[a], positions[b], positions[c]);
        if (t && *t <= limit) {
            nearest = t;
            limit = *t;
        }
    }
    return nearest;
}

} // namespace engine2
