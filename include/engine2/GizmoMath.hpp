#pragma once

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <cstdint>
#include <stdexcept>
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

enum class GizmoMode : std::uint8_t { Translate = 0, Rotate = 1, Scale = 2 };

inline glm::vec3 gizmoAxisVector(int axis) {
    if (axis < 0 || axis > 2)
        throw std::invalid_argument("Gizmo axis must be X, Y or Z");
    glm::vec3 result(0.0f);
    result[axis] = 1.0f;
    return result;
}

// World-space circle about one of the major axes. Used by rotation ring
// drawing and hit testing; it requires only GLM, not a graphics context.
inline glm::vec3 gizmoRingPoint(glm::vec3 center, int axis,
                                float angle, float radius) {
    if (!std::isfinite(angle) || !std::isfinite(radius) || radius <= 0.0f)
        throw std::invalid_argument("Invalid rotation ring parameters");
    const float c = std::cos(angle) * radius;
    const float s = std::sin(angle) * radius;
    if (axis == 0) return center + glm::vec3(0.0f, c, s);
    if (axis == 1) return center + glm::vec3(c, 0.0f, s);
    if (axis == 2) return center + glm::vec3(c, s, 0.0f);
    throw std::invalid_argument("Invalid rotation ring axis");
}

inline float gizmoPointerAngleDelta(const glm::vec2& origin,
                                    const glm::vec2& from,
                                    const glm::vec2& to) {
    const glm::vec2 a = from - origin;
    const glm::vec2 b = to - origin;
    if (glm::dot(a,a) < 16.0f || glm::dot(b,b) < 16.0f)
        return 0.0f;
    const float delta = std::atan2(b.y,b.x) - std::atan2(a.y,a.x);
    if (!std::isfinite(delta)) return 0.0f;
    float wrapped = delta;
    const float pi = glm::pi<float>();
    if (wrapped > pi) wrapped -= 2.0f * pi;
    if (wrapped < -pi) wrapped += 2.0f * pi;
    // Limit each frame to 45 degrees to avoid jumps due to pointer warp.
    return glm::clamp(wrapped, -glm::quarter_pi<float>(), glm::quarter_pi<float>());
}

inline float gizmoSnap(float value, float increment) {
    if (!std::isfinite(value) || !std::isfinite(increment) || increment <= 0.0f)
        throw std::invalid_argument("Invalid gizmo snapping input");
    return std::round(value / increment) * increment;
}

// Cumulative drag expressed as a world-unit distance for translation,
// radians for rotation, or a dimensionless scale offset (factor = 1+drag).
// Transform always starts from the DRAG-START local matrix, not the current
// accumulated frame; this prevents floating-point drift and snap jitter.
// Translation and rotation use world axes; scaling uses local basis axes.
// The parent is frozen at gesture start while the editor is paused.
inline glm::mat4 applyGizmoTransform(const glm::mat4& originalLocal,
                                    const glm::mat4& parentWorld,
                                    GizmoMode mode, int axis,
                                    float drag, bool snapping = false,
                                    float translationStep = 0.5f,
                                    float rotationStepRadians = glm::radians(15.0f),
                                    float scaleStep = 0.25f) {
    const glm::vec3 direction = gizmoAxisVector(axis);
    if (!std::isfinite(drag))
        throw std::invalid_argument("Gizmo drag must be finite");
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!std::isfinite(originalLocal[c][r]) ||
                !std::isfinite(parentWorld[c][r]))
                throw std::invalid_argument("Gizmo transform must be finite");

    glm::mat4 result = originalLocal;
    if (mode == GizmoMode::Scale) {
        const float raw = glm::clamp(1.0f + drag, 0.1f, 10.0f);
        // Keep minimum positive scaling after snapping (no singular meshes).
        const float factor = snapping ?
            glm::max(scaleStep, gizmoSnap(raw, scaleStep)) : raw;
        glm::vec3 amounts(1.0f);
        amounts[axis] = factor;
        return originalLocal * glm::scale(glm::mat4(1.0f), amounts);
    }

    if (std::abs(glm::determinant(parentWorld)) < 1.0e-8f)
        throw std::invalid_argument("Cannot edit world axes under singular parent");
    const glm::mat4 originalWorld = parentWorld * originalLocal;
    const glm::vec3 center(originalWorld[3]);
    if (mode == GizmoMode::Translate) {
        glm::vec3 destination = center + direction * drag;
        if (snapping) destination[axis] =
            gizmoSnap(destination[axis], translationStep);
        result[3] = glm::inverse(parentWorld) * glm::vec4(destination,1.0f);
        return result;
    }
    if (mode == GizmoMode::Rotate) {
        const float radians = snapping ?
            gizmoSnap(drag, rotationStepRadians) : drag;
        const glm::mat4 aroundCenter =
            glm::translate(glm::mat4(1.0f), center) *
            glm::rotate(glm::mat4(1.0f), radians, direction) *
            glm::translate(glm::mat4(1.0f), -center);
        return glm::inverse(parentWorld) * aroundCenter * originalWorld;
    }
    throw std::invalid_argument("Unsupported gizmo mode");
}

// No per-frame clamp here: this function maps the whole screen drag from
// its starting location, so the user can move farther than 0.5 world units.
inline float gizmoFullAxisDrag(const GizmoAxisProjection& axis,
                              const glm::vec2& screenMotion) {
    const glm::vec2 direction = axis.tip - axis.origin;
    const float length2 = glm::dot(direction,direction);
    if (length2 < 0.25f || !std::isfinite(length2)) return 0.0f;
    const float value = glm::dot(screenMotion,direction) /
                        length2 * axis.worldLength;
    return std::isfinite(value) ? glm::clamp(value,-100.0f,100.0f) : 0.0f;
}

} // namespace engine2
