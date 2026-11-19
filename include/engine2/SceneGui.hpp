#pragma once

#include "engine2/SceneIO.hpp"

#include <GLFW/glfw3.h>

#include <array>
#include <cstddef>
#include <string>

namespace engine2 {

// Dear ImGui overlay for interactive editing of the running Scene.
// Owns ImGui context and GLFW/OpenGL3 backends; must be destroyed before
// GLFW window/context and after the scene's GL frame has finished.
class SceneGui {
public:
    explicit SceneGui(GLFWwindow* window);
    ~SceneGui();

    SceneGui(const SceneGui&) = delete;
    SceneGui& operator=(const SceneGui&) = delete;

    void setVisible(bool visible);
    [[nodiscard]] bool visible() const { return visible_; }
    void selectIndex(const Scene& scene, std::size_t index);
    // Call after Renderer::render and before screenshot/glfwSwapBuffers.
    void draw(Scene& scene, const SceneAssetRegistry& assets);

private:
    void drawHierarchy(Scene& scene);
    void drawProperties(Scene& scene);
    void choose(Scene& scene, Entity entity);

    GLFWwindow* window_ = nullptr;
    Entity selected_{};
    bool visible_ = false;
    bool edited_ = false;
    std::array<char, 1025> nameBuffer_{};
    std::array<char, 512> savePath_{};
    std::string status_;
};

} // namespace engine2
