#pragma once

#include "engine2/SceneIO.hpp"
#include "engine2/SceneHistory.hpp"
#include "engine2/GizmoMath.hpp"
#include "engine2/Camera.hpp"

#include <GLFW/glfw3.h>

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
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
    void setGizmoEnabled(bool enabled) { gizmoEnabled_ = enabled; }
    [[nodiscard]] bool gizmoEnabled() const { return gizmoEnabled_; }
    [[nodiscard]] std::size_t undoCount() const { return history_.undoCount(); }
    [[nodiscard]] std::size_t redoCount() const { return history_.redoCount(); }
    // Call after Renderer::render and before screenshot/glfwSwapBuffers.
    void draw(Scene& scene, const SceneAssetRegistry& assets,
              const Camera& camera);

private:
    void drawHierarchy(Scene& scene);
    void drawProperties(Scene& scene);
    void choose(Scene& scene, Entity entity);
    void drawGizmo(Scene& scene, const Camera& camera);
    void applyWidgetEdit(Scene& scene, bool changed,
                         const std::function<void(Scene&)>& mutation);
    void undo(Scene& scene);
    void redo(Scene& scene);

    GLFWwindow* window_ = nullptr;
    Entity selected_{};
    bool visible_ = false;
    bool edited_ = false;
    bool gizmoEnabled_ = true;
    int activeAxis_ = -1;
    bool activeAxisCheckpointed_ = false;
    std::optional<Scene> widgetBefore_;
    bool widgetChanged_ = false;
    SceneHistory history_{64};
    std::array<char, 1025> nameBuffer_{};
    std::array<char, 512> savePath_{};
    std::string status_;
};

} // namespace engine2
