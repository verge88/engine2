#include "engine2/SceneGui.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/common.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace engine2 {
namespace {

glm::vec3 positionOf(const glm::mat4& matrix) {
    return glm::vec3(matrix[3]);
}

void textVec3(const char* title, const glm::vec3& value) {
    ImGui::Text("%s: %.2f, %.2f, %.2f", title, value.x, value.y, value.z);
}

} // namespace

SceneGui::SceneGui(GLFWwindow* window) : window_(window) {
    if (!window_) throw std::invalid_argument("SceneGui requires a GLFW window");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplGlfw_InitForOpenGL(window_, true)) {
        ImGui::DestroyContext();
        throw std::runtime_error("Could not initialize Dear ImGui GLFW backend");
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        throw std::runtime_error("Could not initialize Dear ImGui OpenGL3 backend");
    }
    std::snprintf(savePath_.data(), savePath_.size(), "%s", "scene-edited.json");
}

SceneGui::~SceneGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void SceneGui::setVisible(bool visible) {
    if (visible_ == visible) return;
    visible_ = visible;
    glfwSetInputMode(window_, GLFW_CURSOR,
                     visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
}

void SceneGui::choose(Scene& scene, Entity entity) {
    selected_ = entity;
    nameBuffer_.fill('\0');
    if (scene.contains(entity)) {
        const std::string& name = scene.name(entity);
        std::snprintf(nameBuffer_.data(), nameBuffer_.size(), "%s", name.c_str());
    }
}

void SceneGui::selectIndex(const Scene& scene, std::size_t index) {
    const auto handles = scene.entities();
    if (index >= handles.size())
        throw std::invalid_argument("GUI selected entity index is out of range");
    selected_ = handles[index];
    nameBuffer_.fill('\0');
    std::snprintf(nameBuffer_.data(), nameBuffer_.size(),
                  "%s", scene.name(selected_).c_str());
}

void SceneGui::drawHierarchy(Scene& scene) {
    const auto all = scene.entities();
    // Build a real parent-child tree, rather than a flat numbered list.
    // Scene::setParent guards cycles, but cap display depth to avoid
    // runaway recursion for imported, excessively deep hierarchies.
    std::function<void(Entity, int)> tree = [&](Entity parent, int depth) {
        for (Entity entity : all) {
            if (scene.parent(entity) != parent) continue;
            if (!scene.contains(entity)) continue;
            const bool hasChildren = std::any_of(
                all.begin(), all.end(),
                [&](Entity other) { return scene.parent(other) == entity; });
            ImGui::PushID(static_cast<int>(entity.index));
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                       ImGuiTreeNodeFlags_SpanAvailWidth |
                                       ImGuiTreeNodeFlags_DefaultOpen;
            if (selected_ == entity) flags |= ImGuiTreeNodeFlags_Selected;
            if (!hasChildren || depth >= 48)
                flags |= ImGuiTreeNodeFlags_Leaf |
                         ImGuiTreeNodeFlags_NoTreePushOnOpen;
            const bool expanded = ImGui::TreeNodeEx(
                scene.name(entity).empty() ? "(unnamed entity)"
                                           : scene.name(entity).c_str(),
                flags);
            if (ImGui::IsItemClicked()) choose(scene, entity);
            if (expanded && hasChildren && depth < 48) {
                tree(entity, depth + 1);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    };
    ImGui::BeginChild("Entity hierarchy", ImVec2(0, 180), true);
    tree({}, 0);
    ImGui::EndChild();
}

void SceneGui::drawProperties(Scene& scene) {
    if (!scene.contains(selected_)) {
        selected_ = {};
        ImGui::TextDisabled("Select an entity in the hierarchy.");
        return;
    }

    const Entity entity = selected_;
    ImGui::SeparatorText("Entity");
    if (ImGui::InputText("Name", nameBuffer_.data(), nameBuffer_.size(),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        try {
            scene.setName(entity, std::string(nameBuffer_.data()));
            edited_ = true;
            status_ = "Renamed entity";
        } catch (const std::exception& e) {
            status_ = e.what();
        }
    }

    bool enabled = scene.enabled(entity);
    if (ImGui::Checkbox("Enabled", &enabled)) {
        scene.setEnabled(entity, enabled);
        edited_ = true;
    }

    glm::mat4 local = scene.localTransform(entity);
    glm::vec3 localPosition = positionOf(local);
    float localValues[3]{localPosition.x, localPosition.y, localPosition.z};
    if (ImGui::DragFloat3("Local position", localValues, 0.05f, -10000.0f, 10000.0f)) {
        local[3] = glm::vec4(localValues[0], localValues[1], localValues[2], 1.0f);
        scene.setLocalTransform(entity, local);
        edited_ = true;
    }
    textVec3("World", positionOf(scene.worldTransform(entity)));

    bool keepWorld = true;
    // A reparent must preserve the current world transform by default.
    if (ImGui::BeginCombo("Parent", scene.parent(entity)
        ? scene.name(scene.parent(entity)).c_str() : "(root)")) {
        if (ImGui::Selectable("(root)", !scene.parent(entity))) {
            try {
                scene.setParent(entity, {}, keepWorld);
                edited_ = true;
            } catch (const std::exception& error) { status_ = error.what(); }
        }
        for (Entity candidate : scene.entities()) {
            if (candidate == entity) continue;
            ImGui::PushID(static_cast<int>(candidate.index));
            const bool selected = candidate == scene.parent(entity);
            if (ImGui::Selectable(scene.name(candidate).c_str(), selected)) {
                try {
                    scene.setParent(entity, candidate, keepWorld);
                    edited_ = true;
                } catch (const std::exception& error) { status_ = error.what(); }
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    if (const auto* renderer = scene.rendererComponent(entity)) {
        ImGui::SeparatorText("MeshRenderer");
        MeshRendererComponent component = *renderer;
        float color[3]{component.material.albedo.r,
                       component.material.albedo.g,
                       component.material.albedo.b};
        if (ImGui::ColorEdit3("Albedo", color)) {
            component.material.albedo = glm::vec3(color[0], color[1], color[2]);
            scene.setRenderer(entity, component);
            edited_ = true;
        }
        // Both sliders must render every frame; short-circuit || would hide
        // the roughness widget while the metallic control is being dragged.
        bool editMaterial = ImGui::SliderFloat(
            "Metallic", &component.material.metallic, 0.0f, 1.0f);
        editMaterial |= ImGui::SliderFloat(
            "Roughness", &component.material.roughness, 0.04f, 1.0f);
        if (editMaterial) {
            scene.setRenderer(entity, component);
            edited_ = true;
        }
    }

    if (const PointLight* light = scene.pointLightComponent(entity)) {
        ImGui::SeparatorText("PointLight");
        PointLight changed = *light;
        bool edit = ImGui::DragFloat("Intensity", &changed.intensity,
                                    0.3f, 0.0f, 10000.0f);
        edit |= ImGui::DragFloat("Range", &changed.range,
                                 0.1f, 0.1f, 1000.0f);
        edit |= ImGui::ColorEdit3("Light color", &changed.color.x);
        if (edit) {
            scene.setPointLight(entity, changed);
            edited_ = true;
        }
    }

    if (const RigidBodyComponent* body = scene.rigidBodyComponent(entity)) {
        ImGui::SeparatorText("RigidBody");
        RigidBodyComponent changed = *body;
        bool edit = ImGui::DragFloat3("Velocity", &changed.velocity.x, 0.02f);
        edit |= ImGui::DragFloat("Mass", &changed.mass, 0.05f, 0.01f, 10000.0f);
        edit |= ImGui::SliderFloat("Restitution", &changed.restitution, 0.0f, 1.0f);
        edit |= ImGui::Checkbox("Gravity", &changed.gravity);
        edit |= ImGui::Checkbox("Static", &changed.isStatic);
        ImGui::Text("Collider: %s", changed.shape == ColliderShape::Box ? "AABB Box" : "Sphere");
        if (edit) {
            try {
                scene.setRigidBody(entity, changed);
                edited_ = true;
            } catch (const std::exception& error) { status_ = error.what(); }
        }
    }
    ImGui::Separator();
    if (ImGui::Button("Delete selected entity")) {
        scene.destroyEntity(entity);
        selected_ = {};
        edited_ = true;
        status_ = "Deleted entity (and descendants)";
    }
}

void SceneGui::draw(Scene& scene, const SceneAssetRegistry& assets) {
    if (!visible_) return;
    if (selected_ && !scene.contains(selected_)) selected_ = {};
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(18, 24), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(470, 650), ImGuiCond_FirstUseEver);
    bool open = true;
    if (ImGui::Begin("engine2 - Scene Inspector (F1)", &open)) {
        const SceneStats stats = scene.stats();
        ImGui::Text("Entities: %zu   Meshes: %zu   Lights: %zu   Bodies: %zu",
                    stats.entities, stats.renderables, stats.pointLights,
                    stats.rigidBodies);
        ImGui::TextDisabled("F1: return to camera   Tab: navigate fields");
        drawHierarchy(scene);
        if (ImGui::Button("Create empty entity")) {
            choose(scene, scene.createEntity("New Entity"));
            edited_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Deselect")) {
            selected_ = {};
        }
        drawProperties(scene);
        ImGui::SeparatorText("Save Scene");
        ImGui::InputText("JSON path", savePath_.data(), savePath_.size());
        if (ImGui::Button("Save scene JSON")) {
            try {
                if (savePath_[0] == '\0')
                    throw std::invalid_argument("Scene save path is empty");
                saveScene(scene, assets, savePath_.data());
                edited_ = false;
                status_ = std::string("Saved: ") + savePath_.data();
            } catch (const std::exception& error) {
                status_ = error.what();
            }
        }
        if (edited_) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), "Unsaved changes");
        }
        if (!status_.empty()) ImGui::TextWrapped("%s", status_.c_str());
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (!open) setVisible(false);
}

} // namespace engine2
