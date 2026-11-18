#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

#include <cstdint>
#include <stdexcept>

namespace engine2 {

// Deterministic subpixel jitter: eight Halton(2,3) samples centered around the
// pixel. Starts from index 1 to avoid the unhelpful zero Halton sample.
inline float radicalInverse(std::uint32_t n, std::uint32_t base) {
    float result = 0.0f;
    float fraction = 1.0f / static_cast<float>(base);
    while (n != 0) {
        result += fraction * static_cast<float>(n % base);
        n /= base;
        fraction /= static_cast<float>(base);
    }
    return result;
}

inline glm::vec2 temporalJitter(std::uint64_t frame) {
    const auto index = static_cast<std::uint32_t>(frame % 8) + 1u;
    return glm::vec2(radicalInverse(index, 2u),
                     radicalInverse(index, 3u)) - 0.5f;
}

// GLM perspective matrices have negative view-space z and clip-space w=-z.
// Modifying column 2 therefore creates the required positive NDC shift.
inline glm::mat4 jitterProjection(glm::mat4 projection,
                                  glm::vec2 pixelJitter, int width, int height) {
    if (width <= 0 || height <= 0)
        throw std::invalid_argument("TAA projection requires a valid viewport");
    projection[2][0] -= 2.0f * pixelJitter.x / static_cast<float>(width);
    projection[2][1] -= 2.0f * pixelJitter.y / static_cast<float>(height);
    return projection;
}

} // namespace engine2
