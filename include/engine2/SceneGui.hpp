#pragma once

#include "engine2/SceneIO.hpp"
#include "engine2/SceneHistory.hpp"
#include "engine2/ScenePicking.hpp"
#include "engine2/GizmoMath.hpp"
#include "engine2/Camera.hpp"

#include <GLFW/glfw3.h>

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

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
    void addIndex(const Scene& scene, std::size_t index);
    [[nodiscard]] Entity pickAt(Scene& scene, const Camera& camera,
                                 glm::vec2 pixel, glm::vec2 viewport,
                                 bool additive = false);
    [[nodiscard]] std::size_t selectedCount() const { return selection_.size(); }
    [[nodiscard]] Entity selectedEntity() const { return selected_; }
    void setGizmoEnabled(bool enabled) { gizmoEnabled_ = enabled; }
    [[nodiscard]] bool gizmoEnabled() const { return gizmoEnabled_; }
    void setGizmoMode(GizmoMode mode) {
        gizmoMode_ = static_cast<int>(mode);
        activeAxis_ = -1;
    }
    [[nodiscard]] GizmoMode gizmoMode() const {
        return static_cast<GizmoMode>(gizmoMode_);
    }
    void setSnapping(bool enabled) { snapping_ = enabled; }
    [[nodiscard]] bool snapping() const { return snapping_; }
    void setGridStep(float gridStep);
    [[nodiscard]] float gridStep() const { return gridStep_; }
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
    void drawSelectionOutlines(const Scene& scene, const Camera& camera);
    void select(Scene& scene, Entity entity, bool additive);
    void applyWidgetEdit(Scene& scene, bool changed,
                         const std::function<void(Scene&)>& mutation);
    void undo(Scene& scene);
    void redo(Scene& scene);

    GLFWwindow* window_ = nullptr;
    Entity selected_{};
    SceneSelection selection_{};
    bool gizmoHovering_ = false;
    struct DragTarget {
        Entity entity{};
        glm::mat4 local{1.0f};
        glm::mat4 parentWorld{1.0f};
    };
    std::vector<DragTarget> dragTargets_{};
    bool visible_ = false;
    bool edited_ = false;
    bool gizmoEnabled_ = true;
    int gizmoMode_ = 0; // 0 translate, 1 rotate, 2 scale
    bool snapping_ = false;
    float gridStep_ = 0.5f;
    int activeAxis_ = -1;
    bool activeAxisCheckpointed_ = false;
    glm::mat4 dragStartLocal_{1.0f};
    glm::mat4 dragParentWorld_{1.0f};
    GizmoAxisProjection dragProjection_{};
    glm::vec2 dragMouseStart_{0.0f};
    glm::vec2 dragMousePrevious_{0.0f};
    glm::vec2 dragCenterScreen_{0.0f};
    float dragAngle_ = 0.0f;
    std::optional<Scene> widgetBefore_;
    bool widgetChanged_ = false;
    SceneHistory history_{64};
    std::array<char, 1025> nameBuffer_{};
    std::array<char, 512> savePath_{};
    std::string status_;
};

} // namespace engine2
