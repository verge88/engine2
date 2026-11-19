#include "engine2/SceneGui.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
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
    activeAxis_ = -1;
    activeAxisCheckpointed_ = false;
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

void SceneGui::undo(Scene& scene) {
    if (history_.undo(scene)) {
        if (scene.contains(selected_)) choose(scene, selected_);
        else selected_ = {};
        edited_ = true;
        status_ = "Undo";
    }
}

void SceneGui::redo(Scene& scene) {
    if (history_.redo(scene)) {
        if (scene.contains(selected_)) choose(scene, selected_);
        else selected_ = {};
        edited_ = true;
        status_ = "Redo";
    }
}

void SceneGui::applyWidgetEdit(
    Scene& scene, bool changed,
    const std::function<void(Scene&)>& mutation) {
    // The snapshot is created at the start of the GUI gesture, so edits
    // made on every drag frame become a SINGLE undoable action.
    if (ImGui::IsItemActivated()) {
        widgetBefore_ = scene;
        widgetChanged_ = false;
    }
    if (changed) {
        if (!widgetBefore_) widgetBefore_ = scene;
        try {
            mutation(scene);
            if (!widgetChanged_) history_.checkpoint(*widgetBefore_);
            widgetChanged_ = true;
            edited_ = true;
        } catch (const std::exception& e) {
            status_ = e.what();
        }
    }
    if (ImGui::IsItemDeactivated() && widgetBefore_) {
        widgetBefore_.reset();
        widgetChanged_ = false;
    }
}

void SceneGui::drawHierarchy(Scene& scene) {
    const auto all = scene.entities();
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
            history_.perform(scene,[&](Scene& working) {
                working.setName(entity, std::string(nameBuffer_.data()));
            });
            edited_ = true;
            status_ = "Renamed entity";
        } catch (const std::exception& e) {
            status_ = e.what();
        }
    }

    bool enabled = scene.enabled(entity);
    if (ImGui::Checkbox("Enabled", &enabled)) {
        history_.perform(scene,[&](Scene& working) {
            working.setEnabled(entity, enabled);
        });
        edited_ = true;
    }

    const glm::mat4 local = scene.localTransform(entity);
    const glm::vec3 localPosition = positionOf(local);
    float localValues[3]{localPosition.x, localPosition.y, localPosition.z};
    const bool changedPosition = ImGui::DragFloat3(
        "Local position", localValues, 0.05f, -10000.0f, 10000.0f);
    applyWidgetEdit(scene, changedPosition, [&](Scene& working) {
        glm::mat4 transform = working.localTransform(entity);
        transform[3] = glm::vec4(localValues[0],localValues[1],localValues[2],1.0f);
        working.setLocalTransform(entity, transform);
    });
    textVec3("World", positionOf(scene.worldTransform(entity)));

    if (ImGui::BeginCombo("Parent", scene.parent(entity)
        ? scene.name(scene.parent(entity)).c_str() : "(root)")) {
        if (ImGui::Selectable("(root)", !scene.parent(entity))) {
            try {
                history_.perform(scene,[&](Scene& working) {
                    working.setParent(entity, {}, true);
                });
                edited_ = true;
            } catch (const std::exception& e) { status_ = e.what(); }
        }
        for (Entity candidate : scene.entities()) {
            if (candidate == entity) continue;
            ImGui::PushID(static_cast<int>(candidate.index));
            if (ImGui::Selectable(scene.name(candidate).c_str(),
                                  candidate == scene.parent(entity))) {
                try {
                    history_.perform(scene,[&](Scene& working) {
                        working.setParent(entity,candidate,true);
                    });
                    edited_ = true;
                } catch (const std::exception& e) { status_ = e.what(); }
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    if (scene.rendererComponent(entity)) {
        ImGui::SeparatorText("MeshRenderer");
        const Material current = scene.rendererComponent(entity)->material;
        float color[3]{current.albedo.r,current.albedo.g,current.albedo.b};
        const bool changedColor=ImGui::ColorEdit3("Albedo",color);
        applyWidgetEdit(scene, changedColor, [&](Scene& working) {
            MeshRendererComponent modified = *working.rendererComponent(entity);
            modified.material.albedo = glm::vec3(color[0],color[1],color[2]);
            working.setRenderer(entity,modified);
        });

        float metallic=scene.rendererComponent(entity)->material.metallic;
        const bool changedMetallic=ImGui::SliderFloat(
            "Metallic", &metallic, 0.0f, 1.0f);
        applyWidgetEdit(scene, changedMetallic, [&](Scene& working) {
            MeshRendererComponent modified = *working.rendererComponent(entity);
            modified.material.metallic=metallic;
            working.setRenderer(entity,modified);
        });

        float roughness=scene.rendererComponent(entity)->material.roughness;
        const bool changedRoughness=ImGui::SliderFloat(
            "Roughness", &roughness, 0.04f, 1.0f);
        applyWidgetEdit(scene, changedRoughness, [&](Scene& working) {
            MeshRendererComponent modified = *working.rendererComponent(entity);
            modified.material.roughness=roughness;
            working.setRenderer(entity,modified);
        });
    }

    if (scene.pointLightComponent(entity)) {
        ImGui::SeparatorText("PointLight");
        float intensity = scene.pointLightComponent(entity)->intensity;
        const bool changedIntensity=ImGui::DragFloat(
            "Intensity", &intensity, 0.3f, 0.0f, 10000.0f);
        applyWidgetEdit(scene, changedIntensity, [&](Scene& working) {
            PointLight modified=*working.pointLightComponent(entity);
            modified.intensity=intensity;
            working.setPointLight(entity,modified);
        });

        float range = scene.pointLightComponent(entity)->range;
        const bool changedRange=ImGui::DragFloat(
            "Range", &range, 0.1f, 0.1f, 1000.0f);
        applyWidgetEdit(scene,changedRange,[&](Scene& working) {
            PointLight modified=*working.pointLightComponent(entity);
            modified.range=range;
            working.setPointLight(entity,modified);
        });

        const auto lightColor=scene.pointLightComponent(entity)->color;
        float values[3]{lightColor.r,lightColor.g,lightColor.b};
        const bool changedLightColor=ImGui::ColorEdit3("Light color",values);
        applyWidgetEdit(scene,changedLightColor,[&](Scene& working) {
            PointLight modified=*working.pointLightComponent(entity);
            modified.color=glm::vec3(values[0],values[1],values[2]);
            working.setPointLight(entity,modified);
        });
    }

    if (scene.rigidBodyComponent(entity)) {
        ImGui::SeparatorText("RigidBody");
        const RigidBodyComponent body = *scene.rigidBodyComponent(entity);
        float velocity[3]{body.velocity.x,body.velocity.y,body.velocity.z};
        const bool changedVelocity=ImGui::DragFloat3("Velocity",velocity,0.02f);
        applyWidgetEdit(scene,changedVelocity,[&](Scene& working) {
            RigidBodyComponent modified=*working.rigidBodyComponent(entity);
            modified.velocity=glm::vec3(velocity[0],velocity[1],velocity[2]);
            working.setRigidBody(entity,modified);
        });

        float mass=scene.rigidBodyComponent(entity)->mass;
        const bool changedMass=ImGui::DragFloat("Mass",&mass,0.05f,0.01f,10000.0f);
        applyWidgetEdit(scene,changedMass,[&](Scene& working) {
            RigidBodyComponent modified=*working.rigidBodyComponent(entity);
            modified.mass=mass;
            working.setRigidBody(entity,modified);
        });

        float restitution=scene.rigidBodyComponent(entity)->restitution;
        const bool changedRestitution=ImGui::SliderFloat(
            "Restitution",&restitution,0.0f,1.0f);
        applyWidgetEdit(scene,changedRestitution,[&](Scene& working) {
            RigidBodyComponent modified=*working.rigidBodyComponent(entity);
            modified.restitution=restitution;
            working.setRigidBody(entity,modified);
        });

        bool gravity=scene.rigidBodyComponent(entity)->gravity;
        if (ImGui::Checkbox("Gravity",&gravity)) {
            history_.perform(scene,[&](Scene& working) {
                auto modified=*working.rigidBodyComponent(entity);
                modified.gravity=gravity;
                working.setRigidBody(entity,modified);
            });
            edited_=true;
        }
        bool isStatic=scene.rigidBodyComponent(entity)->isStatic;
        if (ImGui::Checkbox("Static",&isStatic)) {
            history_.perform(scene,[&](Scene& working) {
                auto modified=*working.rigidBodyComponent(entity);
                modified.isStatic=isStatic;
                working.setRigidBody(entity,modified);
            });
            edited_=true;
        }
        ImGui::Text("Collider: %s",scene.rigidBodyComponent(entity)->shape ==
                    ColliderShape::Box ? "AABB Box" : "Sphere");
    }
    ImGui::Separator();
    if (ImGui::Button("Delete selected entity")) {
        history_.perform(scene,[&](Scene& working) {
            working.destroyEntity(entity);
        });
        selected_={};
        edited_=true;
        status_="Deleted entity (and descendants)";
    }
}

void SceneGui::drawGizmo(Scene& scene, const Camera& camera) {
    if (!gizmoEnabled_ || !scene.contains(selected_)) {
        activeAxis_=-1;
        activeAxisCheckpointed_=false;
        return;
    }

    ImGuiIO& io=ImGui::GetIO();
    const glm::vec2 screen(io.DisplaySize.x,io.DisplaySize.y);
    if (screen.x < 128 || screen.y < 128) return;
    const glm::vec3 center=positionOf(scene.worldTransform(selected_));
    const float distance=glm::length(camera.position()-center);
    const float worldLength=glm::clamp(distance*0.105f,0.65f,2.5f);
    const glm::mat4 vp=camera.projectionMatrix(screen.x/screen.y)*
                       camera.viewMatrix();
    const std::array<glm::vec3,3> axes{
        glm::vec3(1,0,0),glm::vec3(0,1,0),glm::vec3(0,0,1)
    };
    const std::array<ImU32,3> colors{
        IM_COL32(240,72,72,255),IM_COL32(88,218,115,255),
        IM_COL32(90,159,250,255)
    };
    const char* labels[]={"X","Y","Z"};
    std::array<std::optional<GizmoAxisProjection>,3> projected;
    const auto origin=projectGizmoPoint(vp,center,screen);
    if (!origin || origin->x < -30 || origin->x > screen.x+30 ||
        origin->y < -30 || origin->y > screen.y+30) {
        activeAxis_=-1;
        return;
    }

    const ImVec2 mouse=io.MousePos;
    const glm::vec2 pointer(mouse.x,mouse.y);
    int hovered=-1;
    float closest=11.0f;
    for (int i=0;i<3;++i) {
        projected[static_cast<std::size_t>(i)] =
            projectGizmoAxis(vp,center,axes[static_cast<std::size_t>(i)],
                             screen,worldLength);
        const auto& axis=projected[static_cast<std::size_t>(i)];
        if (!axis) continue;
        const float d=gizmoSegmentDistance(pointer,*axis);
        if (d < closest) {
            closest=d;
            hovered=i;
        }
    }

    bool startedDrag = false;
    if (activeAxis_ < 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) && hovered >= 0) {
        activeAxis_=hovered;
        activeAxisCheckpointed_=false;
        startedDrag = true;
    }

    if (activeAxis_ >= 0) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            activeAxis_=-1;
            activeAxisCheckpointed_=false;
        } else {
            const auto& axis=projected[static_cast<std::size_t>(activeAxis_)];
            if (axis) {
                const glm::vec2 delta(io.MouseDelta.x,io.MouseDelta.y);
                const float move=gizmoAxisDragDistance(*axis,delta);
                if (!startedDrag && std::abs(move) > 1.0e-7f) {
                    const glm::vec3 worldPosition=center+
                        axes[static_cast<std::size_t>(activeAxis_)]*move;
                    glm::mat4 parentWorld(1.0f);
                    if (const Entity parent=scene.parent(selected_))
                        parentWorld=scene.worldTransform(parent);
                    if (std::abs(glm::determinant(parentWorld)) < 1.0e-8f) {
                        status_="Cannot translate under a singular parent";
                        activeAxis_=-1;
                    } else {
                        if (!activeAxisCheckpointed_) {
                            history_.checkpoint(scene);
                            activeAxisCheckpointed_=true;
                        }
                        glm::mat4 local=scene.localTransform(selected_);
                        local[3]=glm::inverse(parentWorld)*
                                 glm::vec4(worldPosition,1.0f);
                        scene.setLocalTransform(selected_,local);
                        edited_=true;
                    }
                }
            }
        }
    }

    ImDrawList* list=ImGui::GetForegroundDrawList();
    for (int i=0;i<3;++i) {
        const auto& axis=projected[static_cast<std::size_t>(i)];
        if (!axis) continue;
        const ImVec2 a(axis->origin.x,axis->origin.y);
        const ImVec2 b(axis->tip.x,axis->tip.y);
        const bool highlighted=i==activeAxis_ || i==hovered;
        const ImU32 color=highlighted ? IM_COL32(255,235,110,255)
                                     : colors[static_cast<std::size_t>(i)];
        list->AddLine(a,b,color,highlighted ? 5.0f : 3.5f);
        list->AddCircleFilled(b,highlighted ? 8.0f : 6.0f,color,16);
        list->AddText(ImVec2(b.x+9,b.y-8),color,labels[i]);
    }
    list->AddCircleFilled(ImVec2(origin->x,origin->y),4,
                          IM_COL32(240,240,240,255),16);
}

void SceneGui::draw(Scene& scene, const SceneAssetRegistry& assets,
                    const Camera& camera) {
    if (!visible_) return;
    if (selected_ && !scene.contains(selected_)) selected_={};
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    const ImGuiIO& io=ImGui::GetIO();
    if (io.KeyCtrl && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z,false)) {
            if (io.KeyShift) redo(scene);
            else undo(scene);
        } else if (ImGui::IsKeyPressed(ImGuiKey_Y,false)) {
            redo(scene);
        }
    }

    ImGui::SetNextWindowPos(ImVec2(18,24),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(470,690),ImGuiCond_FirstUseEver);
    bool open=true;
    if (ImGui::Begin("engine2 - Scene Editor (F1)",&open)) {
        const SceneStats stats=scene.stats();
        ImGui::Text("Entities: %zu   Meshes: %zu   Lights: %zu   Bodies: %zu",
                    stats.entities,stats.renderables,stats.pointLights,
                    stats.rigidBodies);
        ImGui::TextDisabled("F1: camera   Drag X/Y/Z handles to move objects");
        ImGui::BeginDisabled(!history_.canUndo());
        if (ImGui::Button("Undo (Ctrl+Z)")) undo(scene);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!history_.canRedo());
        if (ImGui::Button("Redo (Ctrl+Y)")) redo(scene);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%zu / %zu",history_.undoCount(),history_.redoCount());
        ImGui::Checkbox("Viewport move gizmo",&gizmoEnabled_);

        drawHierarchy(scene);
        if (ImGui::Button("Create empty entity")) {
            Entity created{};
            history_.perform(scene,[&](Scene& working) {
                created=working.createEntity("New Entity");
            });
            choose(scene,created);
            edited_=true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Deselect")) selected_={};
        drawProperties(scene);
        ImGui::SeparatorText("Save Scene");
        ImGui::InputText("JSON path",savePath_.data(),savePath_.size());
        if (ImGui::Button("Save scene JSON")) {
            try {
                if (savePath_[0]=='\0')
                    throw std::invalid_argument("Scene save path is empty");
                saveScene(scene,assets,savePath_.data());
                edited_=false;
                status_=std::string("Saved: ")+savePath_.data();
            } catch (const std::exception& e) { status_=e.what(); }
        }
        if (edited_) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f,0.78f,0.25f,1.0f),
                               "Unsaved changes");
        }
        if (!status_.empty()) ImGui::TextWrapped("%s",status_.c_str());
    }
    ImGui::End();

    drawGizmo(scene,camera);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (!open) setVisible(false);
}

} // namespace engine2
