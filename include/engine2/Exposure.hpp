#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace engine2 {

// Convert the geometric mean of the linear HDR luminance into a key-value
// exposure multiplier. logLuminance is the mean of ln(max(Y, epsilon)), not
// the logarithm of an arithmetic average; this is robust to isolated lights.
inline float targetAutoExposure(float logLuminance, float keyValue,
                                float minimum = 0.05f, float maximum = 16.0f) {
    if (!std::isfinite(logLuminance) || !std::isfinite(keyValue) ||
        keyValue <= 0.0f || minimum <= 0.0f || maximum < minimum)
        throw std::invalid_argument("Invalid HDR metering parameters");
    const float luminance = std::exp(std::clamp(logLuminance, -14.0f, 14.0f));
    return std::clamp(keyValue / luminance, minimum, maximum);
}

// Framerate-independent, exponential approach to the metered scene exposure.
// Works at 30/60/144 Hz and freezes when deltaTime is zero.
inline float adaptAutoExposure(float current, float target,
                               float adaptationSpeed, float deltaTime) {
    if (!std::isfinite(current) || !std::isfinite(target) ||
        !std::isfinite(adaptationSpeed) || !std::isfinite(deltaTime) ||
        current <= 0.0f || target <= 0.0f ||
        adaptationSpeed < 0.0f || deltaTime < 0.0f)
        throw std::invalid_argument("Invalid auto-exposure adaptation parameters");
    const float alpha = 1.0f -
        std::exp(-adaptationSpeed * std::min(deltaTime, 1.0f));
    return current + (target - current) * alpha;
}

} // namespace engine2
