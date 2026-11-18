#include "engine2/Frustum.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main() {
    const glm::mat4 projection = glm::perspective(
        glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    const engine2::Frustum frustum(projection);
    const engine2::Bounds unit{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};

    auto at = [](glm::vec3 position) {
        return glm::translate(glm::mat4(1.0f), position);
    };
    require(frustum.intersects(unit, at({0.0f, 0.0f, -3.0f})),
            "Visible object wrongly culled");
    require(!frustum.intersects(unit, at({0.0f, 0.0f, 3.0f})),
            "Object behind camera should be culled");
    require(!frustum.intersects(unit, at({100.0f, 0.0f, -3.0f})),
            "Object far outside FOV should be culled");
    require(!frustum.intersects(unit, at({0.0f, 0.0f, -150.0f})),
            "Object beyond far plane should be culled");
    require(frustum.intersects(unit, at({0.0f, 0.0f, -0.4f})),
            "Object overlapping near plane must remain visible");

    // Rotated and scaled bounds must conservatively intersect the camera frustum.
    const auto large = glm::scale(at({12.0f, 0.0f, -10.0f}),
                                  glm::vec3(12.0f, 1.0f, 1.0f));
    require(frustum.intersects(unit, large), "Large overlapping OBB wrongly culled");
    const auto rotated = glm::rotate(at({0.0f, 0.0f, -5.0f}),
                                     glm::pi<float>() * 0.25f,
                                     glm::vec3(0.0f, 1.0f, 0.0f));
    require(frustum.intersects(unit, rotated), "Rotated OBB wrongly culled");
    return 0;
}
