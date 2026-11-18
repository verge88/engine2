#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

struct GLFWwindow;

namespace engine2 {

class Camera {
public:
    static constexpr float nearPlane = 0.1f;
    static constexpr float farPlane = 150.0f;

    Camera();

    void update(GLFWwindow* window, float deltaTime);
    void onMouseMove(double xpos, double ypos);
    void onScroll(double yoffset);
    void resetMouseTracking();

    [[nodiscard]] glm::mat4 viewMatrix() const;
    [[nodiscard]] glm::mat4 projectionMatrix(float aspect) const;
    [[nodiscard]] const glm::vec3& position() const { return position_; }

private:
    [[nodiscard]] glm::vec3 front() const;

    glm::vec3 position_{0.0f, 3.5f, 16.0f};
    float yaw_ = -90.0f;
    float pitch_ = -10.0f;
    float fov_ = 55.0f;
    bool firstMouse_ = true;
    double lastX_ = 0.0;
    double lastY_ = 0.0;
};

} // namespace engine2
