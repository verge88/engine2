#pragma once

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace engine2 {

inline constexpr int maxSunCascades = 3;

struct CascadeTransform {
    glm::mat4 lightSpace{1.0f};
    float farDistance = 0.0f;
};

// Practical split scheme: blend linear and logarithmic camera-depth slices.
// The last split is always at the configured shadow distance.
inline std::array<float, maxSunCascades> sunCascadeSplits(
    float cameraNear, float shadowFar, int count, float logarithmicWeight = 0.6f) {
    if ((count != 1 && count != maxSunCascades) ||
        !(cameraNear > 0.0f) || !(shadowFar > cameraNear)) {
        throw std::invalid_argument("Invalid cascaded shadow parameters");
    }
    std::array<float, maxSunCascades> splits{};
    for (int i = 0; i < count; ++i) {
        const float fraction = static_cast<float>(i + 1) / count;
        const float linear = cameraNear + (shadowFar - cameraNear) * fraction;
        const float logarithmic =
            cameraNear * std::pow(shadowFar / cameraNear, fraction);
        splits[static_cast<std::size_t>(i)] =
            glm::mix(linear, logarithmic, glm::clamp(logarithmicWeight, 0.0f, 1.0f));
    }
    splits[static_cast<std::size_t>(count - 1)] = shadowFar;
    return splits;
}

// Fit a stable light-space orthographic projection to each world-space camera
// frustum slice. Use the enclosing sphere instead of a tightly fitted AABB to
// limit shimmering when the camera rotates. Shadow-caster padding includes
// offscreen occluders in front of the visible camera slice.
inline std::array<CascadeTransform, maxSunCascades> computeSunCascades(
    const glm::mat4& view, const glm::mat4& projection,
    glm::vec3 directionToGround, float cameraNear, float cameraFar,
    float shadowDistance, int count, int mapResolution) {
    if (mapResolution <= 0 || !(cameraFar > cameraNear))
        throw std::invalid_argument("Invalid camera or shadow resolution");

    const float far = glm::min(shadowDistance, cameraFar);
    const auto splits = sunCascadeSplits(cameraNear, far, count);
    const glm::mat4 inverseViewProjection = glm::inverse(projection * view);

    std::array<glm::vec3, 4> nearCorners{};
    std::array<glm::vec3, 4> farCorners{};
    int cornerIndex = 0;
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            const glm::vec2 xy(x ? 1.0f : -1.0f, y ? 1.0f : -1.0f);
            const glm::vec4 a = inverseViewProjection *
                                glm::vec4(xy, -1.0f, 1.0f);
            const glm::vec4 b = inverseViewProjection *
                                glm::vec4(xy,  1.0f, 1.0f);
            nearCorners[static_cast<std::size_t>(cornerIndex)] =
                glm::vec3(a) / a.w;
            farCorners[static_cast<std::size_t>(cornerIndex)] =
                glm::vec3(b) / b.w;
            ++cornerIndex;
        }
    }

    directionToGround = glm::normalize(directionToGround);
    std::array<CascadeTransform, maxSunCascades> output{};
    float start = cameraNear;
    for (int cascade = 0; cascade < count; ++cascade) {
        const float end = splits[static_cast<std::size_t>(cascade)];
        const float nearRatio = (start - cameraNear) / (cameraFar - cameraNear);
        const float farRatio = (end - cameraNear) / (cameraFar - cameraNear);
        std::array<glm::vec3, 8> corners{};
        glm::vec3 center(0.0f);
        for (std::size_t i = 0; i < 4; ++i) {
            const glm::vec3 edge = farCorners[i] - nearCorners[i];
            corners[i] = nearCorners[i] + edge * nearRatio;
            corners[i + 4] = nearCorners[i] + edge * farRatio;
            center += corners[i] + corners[i + 4];
        }
        center /= 8.0f;

        float radius = 0.0f;
        for (const glm::vec3& corner : corners)
            radius = glm::max(radius, glm::length(corner - center));
        // Round up by 1/16 world unit to avoid small projection scale changes.
        radius = std::max(std::ceil(radius * 16.0f) / 16.0f, 1.0f);
        constexpr float casterPadding = 25.0f;
        const glm::vec3 eye =
            center - directionToGround * (radius + casterPadding);
        const glm::mat4 lightView = glm::lookAt(
            eye, center, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 lightProjection = glm::ortho(
            -radius, radius, -radius, radius, 0.1f,
            2.0f * (radius + casterPadding));

        // Align the projection to whole texels for moving cameras.
        const glm::mat4 unrounded = lightProjection * lightView;
        const glm::vec4 origin = unrounded * glm::vec4(0, 0, 0, 1);
        const float texelScale = static_cast<float>(mapResolution) * 0.5f;
        const glm::vec2 originTexels = glm::vec2(origin) * texelScale;
        const glm::vec2 delta = glm::round(originTexels) - originTexels;
        lightProjection[3][0] += delta.x / texelScale;
        lightProjection[3][1] += delta.y / texelScale;

        output[static_cast<std::size_t>(cascade)] =
            {lightProjection * lightView, end};
        start = end;
    }
    return output;
}

} // namespace engine2
