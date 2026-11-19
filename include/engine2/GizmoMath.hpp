#pragma once

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <optional>

namespace engine2 {

// Pure CPU math behind the in-viewport world-axis translation gizmo.
// Screen coordinates are in GLFW/ImGui window pixels (top-left is origin).
struct GizmoAxisProjection {
    glm::vec2 origin{};
    glm::vec2 tip{};
    float worldLength = 1.0f;
};

inline std::optional<glm::vec2> projectGizmoPoint(
    const glm::mat4& viewProjection, const glm::vec3& world,
    const glm::vec2& screenSize) {
    if (screenSize.x <= 0 || screenSize.y <= 0) return std::nullopt;
    const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
    if (!(clip.w > 0.0001f) ||
        !std::isfinite(clip.x) || !std::isfinite(clip.y) ||
        !std::isfinite(clip.w)) return std::nullopt;
    const glm::vec2 ndc(clip.x / clip.w, clip.y / clip.w);
    if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y)) return std::nullopt;
    return glm::vec2((ndc.x * 0.5f + 0.5f) * screenSize.x,
                     (0.5f - ndc.y * 0.5f) * screenSize.y);
}

inline std::optional<GizmoAxisProjection> projectGizmoAxis(
    const glm::mat4& viewProjection, const glm::vec3& origin,
    const glm::vec3& axis, const glm::vec2& screenSize,
    float worldLength = 1.0f) {
    if (!std::isfinite(worldLength) || worldLength <= 0.0f) return std::nullopt;
    const auto screenOrigin = projectGizmoPoint(viewProjection, origin, screenSize);
    const auto screenTip =
        projectGizmoPoint(viewProjection, origin + worldLength * axis, screenSize);
    if (!screenOrigin || !screenTip) return std::nullopt;
    if (glm::length(*screenTip - *screenOrigin) < 0.5f) return std::nullopt;
    return GizmoAxisProjection{*screenOrigin, *screenTip, worldLength};
}

inline float gizmoAxisDragDistance(const GizmoAxisProjection& axis,
                                   const glm::vec2& screenMotion) {
    const glm::vec2 screenAxis = axis.tip - axis.origin;
    const float lengthSquared = glm::dot(screenAxis, screenAxis);
    if (lengthSquared < 0.25f || !std::isfinite(lengthSquared)) return 0.0f;
    const float worldDelta =
        glm::dot(screenMotion, screenAxis) / lengthSquared * axis.worldLength;
    return std::isfinite(worldDelta) ?
        glm::clamp(worldDelta, -0.5f, 0.5f) : 0.0f;
}

inline float gizmoSegmentDistance(const glm::vec2& pointer,
                                  const GizmoAxisProjection& axis) {
    const glm::vec2 segment = axis.tip - axis.origin;
    const float lengthSquared = glm::dot(segment, segment);
    if (lengthSquared < 0.25f) return 1.0e6f;
    const float t = glm::clamp(
        glm::dot(pointer - axis.origin, segment) / lengthSquared, 0.0f, 1.0f);
    return glm::length(pointer - (axis.origin + segment * t));
}

} // namespace engine2
