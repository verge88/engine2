#include "engine2/Camera.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>

namespace engine2 {

Camera::Camera() = default;

glm::vec3 Camera::front() const {
    const float yaw = glm::radians(yaw_);
    const float pitch = glm::radians(pitch_);
    return glm::normalize(glm::vec3(
        std::cos(yaw) * std::cos(pitch),
        std::sin(pitch),
        std::sin(yaw) * std::cos(pitch)
    ));
}

void Camera::update(GLFWwindow* window, float deltaTime) {
    const float speed = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? 11.0f : 4.5f)
                      * glm::clamp(deltaTime, 0.0f, 0.05f);
    const glm::vec3 forward = front();
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) position_ += forward * speed;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) position_ -= forward * speed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) position_ += right * speed;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) position_ -= right * speed;
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) position_.y += speed;
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) position_.y -= speed;
}

void Camera::onMouseMove(double xpos, double ypos) {
    if (firstMouse_) {
        lastX_ = xpos;
        lastY_ = ypos;
        firstMouse_ = false;
        return;
    }

    constexpr float sensitivity = 0.10f;
    yaw_ += static_cast<float>(xpos - lastX_) * sensitivity;
    pitch_ -= static_cast<float>(ypos - lastY_) * sensitivity;
    pitch_ = glm::clamp(pitch_, -89.0f, 89.0f);
    lastX_ = xpos;
    lastY_ = ypos;
}

void Camera::onScroll(double yoffset) {
    fov_ = glm::clamp(fov_ - static_cast<float>(yoffset) * 2.0f, 30.0f, 90.0f);
}

void Camera::resetMouseTracking() {
    firstMouse_ = true;
}

glm::mat4 Camera::viewMatrix() const {
    return glm::lookAt(position_, position_ + front(), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::projectionMatrix(float aspect) const {
    return glm::perspective(glm::radians(fov_), aspect, 0.1f, 150.0f);
}

} // namespace engine2
