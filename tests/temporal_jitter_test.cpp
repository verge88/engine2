#include "engine2/TemporalAA.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include <array>
#include <cmath>
#include <stdexcept>

namespace {

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

}

int main() {
    std::array<glm::vec2, 8> jitters{};
    for (std::uint64_t frame = 0; frame < jitters.size(); ++frame) {
        const glm::vec2 sample = engine2::temporalJitter(frame);
        check(sample.x >= -0.5f && sample.x < 0.5f, "Jitter x out of range");
        check(sample.y >= -0.5f && sample.y < 0.5f, "Jitter y out of range");
        for (std::size_t j = 0; j < frame; ++j) {
            check(glm::length(sample - jitters[j]) > 0.01f,
                  "Repeated Halton pixel offset");
        }
        jitters[frame] = sample;
    }
    check(glm::length(jitters[0] - engine2::temporalJitter(8)) < 1.0e-6f,
          "Temporal sample pattern must repeat in eight frames");

    const glm::mat4 projection = glm::perspective(
        glm::radians(55.0f), 16.0f / 9.0f, 0.1f, 150.0f);
    const glm::vec2 jitter(0.25f, -0.375f);
    const glm::mat4 jittered =
        engine2::jitterProjection(projection, jitter, 1280, 720);
    const glm::vec4 vertex(1.0f, 2.0f, -6.0f, 1.0f);
    const glm::vec4 before = projection * vertex;
    const glm::vec4 after = jittered * vertex;
    const glm::vec2 shift = glm::vec2(after) / after.w -
                             glm::vec2(before) / before.w;
    check(std::abs(shift.x - 2.0f * jitter.x / 1280.0f) < 1.0e-6f,
          "Projection x pixel jitter incorrect");
    check(std::abs(shift.y - 2.0f * jitter.y / 720.0f) < 1.0e-6f,
          "Projection y pixel jitter incorrect");

    bool rejected = false;
    try {
        (void)engine2::jitterProjection(projection, jitter, 0, 720);
    } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Invalid framebuffer dimensions accepted");
}
