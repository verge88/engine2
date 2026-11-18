#include "engine2/CascadeMath.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <initializer_list>
#include <stdexcept>

namespace {

void check(bool good, const char* message) {
    if (!good) throw std::runtime_error(message);
}

// Verify that each camera-slice corner is actually in the CSM light-space
// frustum. This is a regression check on the CSM projection math.
void checkFrustumCorners(int count) {
    constexpr float cameraNear = 0.1f;
    constexpr float cameraFar = 150.0f;
    constexpr float shadowDistance = 95.0f;
    const glm::mat4 projection =
        glm::perspective(glm::radians(55.0f), 16.0f / 9.0f,
                         cameraNear, cameraFar);
    const glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 3.5f, 16.0f),
        glm::vec3(0.0f, 2.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 light = glm::normalize(
        glm::vec3(-0.55f, -1.0f, -0.35f));
    const auto cascades = engine2::computeSunCascades(
        view, projection, light, cameraNear, cameraFar, shadowDistance,
        count, 1536);
    const glm::mat4 inverseVP = glm::inverse(projection * view);
    float start = cameraNear;
    for (int i = 0; i < count; ++i) {
        const auto& cascade = cascades[static_cast<std::size_t>(i)];
        const float end = cascade.farDistance;
        check(end > start, "Cascade boundaries not increasing");
        for (float distance : {start, end}) {
            const float depthFraction =
                (distance - cameraNear) / (cameraFar - cameraNear);
            for (int x : {-1, 1}) {
                for (int y : {-1, 1}) {
                    const glm::vec4 a = inverseVP * glm::vec4(x, y, -1.0f, 1.0f);
                    const glm::vec4 b = inverseVP * glm::vec4(x, y, 1.0f, 1.0f);
                    const glm::vec3 p = glm::mix(
                        glm::vec3(a) / a.w, glm::vec3(b) / b.w, depthFraction);
                    const glm::vec4 clip = cascade.lightSpace *
                                            glm::vec4(p, 1.0f);
                    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                    check(std::abs(ndc.x) < 1.01f &&
                          std::abs(ndc.y) < 1.01f &&
                          std::abs(ndc.z) < 1.01f,
                          "Camera slice corner clipped by cascade matrix");
                }
            }
        }
        start = end;
    }
    check(std::abs(start - shadowDistance) < 0.001f,
          "Final cascade does not cover full configured distance");
}

} // namespace

int main() {
    checkFrustumCorners(1);
    checkFrustumCorners(3);
    bool rejected = false;
    try { (void)engine2::sunCascadeSplits(0.1f, 150.0f, 2); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Unsupported cascade count was accepted");
    return 0;
}
