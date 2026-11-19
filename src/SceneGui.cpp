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

bool differentMatrix(const glm::mat4& a, const glm::mat4& b) {
    for (int column=0; column<4; ++column)
        for (int row=0; row<4; ++row)
            if (std::abs(a[column][row]-b[column][row]) > 1.0e-5f)
                return true;
    return false;
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
    marqueeStart_.reset();
    activeAxis_ = -1;
    activeAxisCheckpointed_ = false;
    glfwSetInputMode(window_, GLFW_CURSOR,
                     visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
}

void SceneGui::setGridStep(float value) {
    if (!std::isfinite(value) || value < 0.05f || value > 20.0f)
        throw std::invalid_argument("Gizmo grid step must be 0.05..20");
    gridStep_ = value;
}

void SceneGui::select(Scene& scene, Entity entity, bool additive) {
    selection_.pick(scene, entity, additive);
    selected_ = selection_.primary();
    nameBuffer_.fill('\0');
    if (scene.contains(selected_)) {
        std::snprintf(nameBuffer_.data(), nameBuffer_.size(),
                      "%s", scene.name(selected_).c_str());
    }
    activeAxis_ = -1;
}

void SceneGui::choose(Scene& scene, Entity entity) {
    select(scene, entity, false);
}

void SceneGui::selectIndex(const Scene& scene, std::size_t index) {
    const auto handles = scene.entities();
    if (index >= handles.size())
        throw std::invalid_argument("GUI selected entity index is out of range");
    selection_.pick(scene,handles[index],false);
    selected_ = selection_.primary();
    nameBuffer_.fill('\0');
    std::snprintf(nameBuffer_.data(), nameBuffer_.size(),
                  "%s", scene.name(selected_).c_str());
}

void SceneGui::addIndex(const Scene& scene, std::size_t index) {
    const auto handles = scene.entities();
    if (index >= handles.size())
        throw std::invalid_argument("GUI additive entity index is out of range");
    selection_.pick(scene,handles[index],true);
    selected_ = selection_.primary();
    nameBuffer_.fill('\0');
    if (scene.contains(selected_))
        std::snprintf(nameBuffer_.data(), nameBuffer_.size(),
                      "%s", scene.name(selected_).c_str());
}

Entity SceneGui::pickAt(Scene& scene, const Camera& camera,
                         glm::vec2 pixel, glm::vec2 viewport, bool additive) {
    if (viewport.x <= 0.0f || viewport.y <= 0.0f)
        throw std::invalid_argument("Scene pick viewport is invalid");
    const glm::mat4 vp = camera.projectionMatrix(viewport.x/viewport.y) *
                         camera.viewMatrix();
    const auto ray = viewportPickRay(vp,pixel,viewport);
    if (!ray) throw std::invalid_argument("Scene picking pixel is outside viewport");
    const auto hit = precisePicking_ ? pickSceneTriangles(scene,*ray)
                                     : pickSceneBounds(scene,*ray);
    select(scene,hit ? hit->entity : Entity{},additive);
    return hit ? hit->entity : Entity{};
}

std::size_t SceneGui::marqueeAt(
    Scene& scene, const Camera& camera, glm::vec2 from,
    glm::vec2 to, glm::vec2 viewport, bool additive) {
    if (viewport.x<=0 || viewport.y<=0)
        throw std::invalid_argument("Marquee viewport is invalid");
    const glm::mat4 vp=camera.projectionMatrix(viewport.x/viewport.y) *
                       camera.viewMatrix();
    const auto hits=pickSceneRectangle(scene,vp,viewport,from,to);
    selection_.selectMany(scene,hits,additive);
    selected_=selection_.primary();
    nameBuffer_.fill('\0');
    if (scene.contains(selected_))
        std::snprintf(nameBuffer_.data(),nameBuffer_.size(),
                      "%s",scene.name(selected_).c_str());
    activeAxis_=-1;
    return hits.size();
}

void SceneGui::undo(Scene& scene) {
    if (history_.undo(scene)) {
        selection_.prune(scene);
        selected_=selection_.primary();
        if (scene.contains(selected_)) {
            nameBuffer_.fill('\0');
            std::snprintf(nameBuffer_.data(),nameBuffer_.size(),
                          "%s",scene.name(selected_).c_str());
        }
        edited_ = true;
        status_ = "Undo";
    }
}

void SceneGui::redo(Scene& scene) {
    if (history_.redo(scene)) {
        selection_.prune(scene);
        selected_=selection_.primary();
        if (scene.contains(selected_)) {
            nameBuffer_.fill('\0');
            std::snprintf(nameBuffer_.data(),nameBuffer_.size(),
                          "%s",scene.name(selected_).c_str());
        }
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
            if (selection_.contains(entity)) flags |= ImGuiTreeNodeFlags_Selected;
            if (!hasChildren || depth >= 48)
                flags |= ImGuiTreeNodeFlags_Leaf |
                         ImGuiTreeNodeFlags_NoTreePushOnOpen;
            const bool expanded = ImGui::TreeNodeEx(
                scene.name(entity).empty() ? "(unnamed entity)"
                                           : scene.name(entity).c_str(),
                flags);
            if (ImGui::IsItemClicked()) {
                const ImGuiIO& io = ImGui::GetIO();
                select(scene,entity,io.KeyCtrl || io.KeyShift);
            }
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
    if (ImGui::Button(selection_.size()>1 ? "Delete selected entities"
                                          : "Delete selected entity")) {
        const auto roots = selection_.roots(scene);
        history_.perform(scene,[&](Scene& working) {
            for (Entity root : roots) working.destroyEntity(root);
        });
        select(scene,{},false);
        edited_=true;
        status_="Deleted selected entities and descendants";
    }
}

void SceneGui::drawGizmo(Scene& scene, const Camera& camera) {
    gizmoHovering_=false;
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
    const auto origin=projectGizmoPoint(vp,center,screen);
    if (!origin || origin->x < -30 || origin->x > screen.x+30 ||
        origin->y < -30 || origin->y > screen.y+30) {
        activeAxis_=-1;
        return;
    }

    const std::array<glm::vec3,3> axes{
        glm::vec3(1,0,0),glm::vec3(0,1,0),glm::vec3(0,0,1)
    };
    const std::array<ImU32,3> colors{
        IM_COL32(240,72,72,255),IM_COL32(88,218,115,255),
        IM_COL32(90,159,250,255)
    };
    const char* labels[]={"X","Y","Z"};
    const GizmoMode mode=static_cast<GizmoMode>(gizmoMode_);
    const ImVec2 mouse=io.MousePos;
    const glm::vec2 pointer(mouse.x,mouse.y);
    ImDrawList* list=ImGui::GetForegroundDrawList();
    std::array<std::optional<GizmoAxisProjection>,3> projected;
    int hovered=-1;
    float nearest=10.0f;

    if (mode == GizmoMode::Rotate) {
        // Three true 3D rings (planes perpendicular to world X/Y/Z),
        // projected through the stable camera matrix.
        constexpr int segments=64;
        for (int axis=0; axis<3; ++axis) {
            std::optional<glm::vec2> previous;
            float minimum=1.0e6f;
            for (int j=0;j<=segments;++j) {
                const float angle=glm::two_pi<float>() *
                                  static_cast<float>(j) / segments;
                const auto sample=projectGizmoPoint(
                    vp,gizmoRingPoint(center,axis,angle,worldLength*0.80f),
                    screen);
                if (sample && previous) {
                    minimum=std::min(minimum,gizmoSegmentDistance(
                        pointer,{*previous,*sample,1.0f}));
                }
                previous=sample;
            }
            if (minimum<nearest) {
                hovered=axis;
                nearest=minimum;
            }
        }
        for (int axis=0;axis<3;++axis) {
            const ImU32 color=(axis==activeAxis_ || axis==hovered)
                ? IM_COL32(255,235,110,255) : colors[static_cast<std::size_t>(axis)];
            std::optional<glm::vec2> previous;
            for (int j=0;j<=segments;++j) {
                const float angle=glm::two_pi<float>() *
                                  static_cast<float>(j) / segments;
                const auto sample=projectGizmoPoint(
                    vp,gizmoRingPoint(center,axis,angle,worldLength*0.80f),
                    screen);
                if (sample && previous) {
                    list->AddLine(ImVec2(previous->x,previous->y),
                                  ImVec2(sample->x,sample->y),color,
                                  axis==hovered || axis==activeAxis_ ? 3.8f : 2.5f);
                }
                previous=sample;
            }
        }
    } else {
        for(int axis=0;axis<3;++axis) {
            auto& projection=projected[static_cast<std::size_t>(axis)];
            projection=projectGizmoAxis(
                vp,center,axes[static_cast<std::size_t>(axis)],screen,worldLength);
            if (!projection) continue;
            const float delta=gizmoSegmentDistance(pointer,*projection);
            if (delta<nearest) {
                hovered=axis;
                nearest=delta;
            }
        }
        for(int axis=0;axis<3;++axis) {
            const auto& projection=projected[static_cast<std::size_t>(axis)];
            if (!projection) continue;
            const ImVec2 from(projection->origin.x,projection->origin.y);
            const ImVec2 to(projection->tip.x,projection->tip.y);
            const bool selected=axis==activeAxis_ || axis==hovered;
            const ImU32 color=selected ? IM_COL32(255,235,110,255) :
                colors[static_cast<std::size_t>(axis)];
            list->AddLine(from,to,color,selected ? 5.0f : 3.5f);
            if (mode==GizmoMode::Scale)
                list->AddRectFilled(ImVec2(to.x-6,to.y-6),
                                    ImVec2(to.x+6,to.y+6),color);
            else
                list->AddCircleFilled(to,selected ? 8.0f : 6.0f,color,16);
            list->AddText(ImVec2(to.x+9,to.y-8),color,labels[axis]);
        }
    }

    list->AddCircleFilled(ImVec2(origin->x,origin->y),4,
                          IM_COL32(240,240,240,255),16);

    gizmoHovering_ = hovered>=0 || activeAxis_>=0;
    bool started=false;
    if (activeAxis_<0 && hovered>=0 &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) {
        activeAxis_=hovered;
        activeAxisCheckpointed_=false;
        dragStartLocal_=scene.localTransform(selected_);
        dragParentWorld_=scene.parent(selected_)
            ? scene.worldTransform(scene.parent(selected_)) : glm::mat4(1.0f);
        dragTargets_.clear();
        for (Entity root : selection_.roots(scene)) {
            dragTargets_.push_back({
                root,scene.localTransform(root),
                scene.parent(root) ? scene.worldTransform(scene.parent(root))
                                   : glm::mat4(1.0f)
            });
        }
        dragMouseStart_=pointer;
        dragMousePrevious_=pointer;
        dragCenterScreen_=*origin;
        dragAngle_=0.0f;
        if (mode!=GizmoMode::Rotate && projected[static_cast<std::size_t>(hovered)])
            dragProjection_=*projected[static_cast<std::size_t>(hovered)];
        started=true;
    }

    if (activeAxis_>=0) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            activeAxis_=-1;
            activeAxisCheckpointed_=false;
        } else if (!started) {
            float drag=0.0f;
            if (mode==GizmoMode::Rotate) {
                dragAngle_+=gizmoPointerAngleDelta(dragCenterScreen_,
                                                   dragMousePrevious_,pointer);
                dragAngle_=glm::clamp(dragAngle_,-glm::two_pi<float>()*8.0f,
                                      glm::two_pi<float>()*8.0f);
                drag=dragAngle_;
            } else {
                drag=gizmoFullAxisDrag(dragProjection_,pointer-dragMouseStart_);
                if (mode==GizmoMode::Scale)
                    drag/=std::max(dragProjection_.worldLength,0.01f);
            }
            try {
                std::vector<std::pair<Entity,glm::mat4>> pending;
                for (const DragTarget& target : dragTargets_) {
                    const glm::mat4 next=applyGizmoTransform(
                        target.local,target.parentWorld,mode,activeAxis_,drag,
                        snapping_,gridStep_);
                    if (differentMatrix(next,scene.localTransform(target.entity)))
                        pending.emplace_back(target.entity,next);
                }
                if (!pending.empty()) {
                    if (!activeAxisCheckpointed_) {
                        history_.checkpoint(scene);
                        activeAxisCheckpointed_=true;
                    }
                    for (const auto& [entity,transform] : pending)
                        scene.setLocalTransform(entity,transform);
                    edited_=true;
                }
            } catch(const std::exception& error) {
                status_=error.what();
                activeAxis_=-1;
            }
        }
    }
    dragMousePrevious_=pointer;
}

void SceneGui::drawSelectionOutlines(const Scene& scene,
                                      const Camera& camera) {
    const ImGuiIO& io=ImGui::GetIO();
    const glm::vec2 screen(io.DisplaySize.x,io.DisplaySize.y);
    if (screen.x<128.0f || screen.y<128.0f) return;
    const glm::mat4 vp=camera.projectionMatrix(screen.x/screen.y) *
                       camera.viewMatrix();
    ImDrawList* overlay=ImGui::GetForegroundDrawList();

    for (Entity entity : selection_.entities()) {
        if (!scene.contains(entity) || !pickableVisible(scene,entity)) continue;
        const auto* renderer=scene.rendererComponent(entity);
        if (!renderer || !renderer->mesh) continue;
        const Bounds bounds=renderer->mesh->bounds();
        const glm::mat4 world=scene.worldTransform(entity);
        glm::vec2 minScreen(1.0e9f);
        glm::vec2 maxScreen(-1.0e9f);
        int visibleCorners=0;
        for (int corner=0;corner<8;++corner) {
            const glm::vec3 local(
                corner & 1 ? bounds.max.x : bounds.min.x,
                corner & 2 ? bounds.max.y : bounds.min.y,
                corner & 4 ? bounds.max.z : bounds.min.z);
            const glm::vec3 worldCorner(world*glm::vec4(local,1.0f));
            const auto pixel=projectGizmoPoint(vp,worldCorner,screen);
            if (!pixel) continue;
            minScreen=glm::min(minScreen,*pixel);
            maxScreen=glm::max(maxScreen,*pixel);
            ++visibleCorners;
        }
        if (visibleCorners<2) continue;
        if (maxScreen.x<0 || maxScreen.y<0 ||
            minScreen.x>screen.x || minScreen.y>screen.y) continue;
        minScreen=glm::clamp(minScreen,glm::vec2(0),screen);
        maxScreen=glm::clamp(maxScreen,glm::vec2(0),screen);
        if (maxScreen.x-minScreen.x<2 || maxScreen.y-minScreen.y<2)
            continue;
        const ImU32 color=entity==selected_
            ? IM_COL32(255,224,91,255)
            : IM_COL32(94,212,246,235);
        overlay->AddRect(ImVec2(minScreen.x,minScreen.y),
                         ImVec2(maxScreen.x,maxScreen.y),color,
                         0.0f,0,2.0f);
    }
}

void SceneGui::draw(Scene& scene, const SceneAssetRegistry& assets,
                    const Camera& camera) {
    if (!visible_) return;
    selection_.prune(scene);
    selected_=selection_.primary();
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
        ImGui::TextDisabled("Click: select | Ctrl/Shift+click: toggle");
        ImGui::TextDisabled("Drag empty viewport: rectangular selection");
        ImGui::Checkbox("Precise triangle picking",&precisePicking_);
        ImGui::Text("Selected: %zu   Primary: %s",selection_.size(),
                    scene.contains(selected_) ? scene.name(selected_).c_str() : "(none)");
        ImGui::TextDisabled("F1: camera   Move / Rotate / Scale X,Y,Z");
        ImGui::BeginDisabled(!history_.canUndo());
        if (ImGui::Button("Undo (Ctrl+Z)")) undo(scene);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!history_.canRedo());
        if (ImGui::Button("Redo (Ctrl+Y)")) redo(scene);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%zu / %zu",history_.undoCount(),history_.redoCount());
        ImGui::Checkbox("Viewport gizmo",&gizmoEnabled_);
        const int previousMode=gizmoMode_;
        ImGui::RadioButton("Move",&gizmoMode_,0);
        ImGui::SameLine();
        ImGui::RadioButton("Rotate",&gizmoMode_,1);
        ImGui::SameLine();
        ImGui::RadioButton("Scale",&gizmoMode_,2);
        if (gizmoMode_!=previousMode) activeAxis_=-1;
        ImGui::Checkbox("Snap (grid / 15 deg / 0.25 scale)",&snapping_);
        if (snapping_) {
            ImGui::SetNextItemWidth(145.0f);
            if (ImGui::DragFloat("Grid step", &gridStep_,
                                 0.05f, 0.05f, 20.0f, "%.2f"))
                gridStep_=glm::clamp(gridStep_,0.05f,20.0f);
        }

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
        if (ImGui::Button("Deselect")) select(scene,{},false);
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
    // Gizmo gets first refusal: clicking a handle must not also pick the
    // mesh behind it. ImGui windows consume clicks before viewport picking.
    if (!gizmoHovering_ && activeAxis_<0 &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) {
        const glm::vec2 pixel(io.MousePos.x,io.MousePos.y);
        const glm::vec2 size(io.DisplaySize.x,io.DisplaySize.y);
        if (pixel.x>=0 && pixel.y>=0 &&
            pixel.x<size.x && pixel.y<size.y) {
            const Entity hit=pickAt(
                scene,camera,pixel,size,io.KeyCtrl||io.KeyShift);
            // Start marquee only from an unoccupied part of the viewport.
            if (!hit) {
                marqueeStart_=pixel;
                marqueeCurrent_=pixel;
                marqueeAdditive_=io.KeyCtrl||io.KeyShift;
            }
        }
    }

    if (marqueeStart_) {
        marqueeCurrent_={io.MousePos.x,io.MousePos.y};
        const glm::vec2 size(io.DisplaySize.x,io.DisplaySize.y);
        marqueeCurrent_=glm::clamp(marqueeCurrent_,glm::vec2(0),
                                  glm::max(size-glm::vec2(0.01f),glm::vec2(0)));
        const glm::vec2 from=glm::min(*marqueeStart_,marqueeCurrent_);
        const glm::vec2 to=glm::max(*marqueeStart_,marqueeCurrent_);
        if (glm::length(marqueeCurrent_-*marqueeStart_)>=6.0f) {
            ImDrawList* layer=ImGui::GetForegroundDrawList();
            layer->AddRectFilled(ImVec2(from.x,from.y),ImVec2(to.x,to.y),
                                 IM_COL32(90,162,234,35));
            layer->AddRect(ImVec2(from.x,from.y),ImVec2(to.x,to.y),
                           IM_COL32(90,178,255,220),0.0f,0,1.5f);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (glm::length(marqueeCurrent_-*marqueeStart_)>=6.0f)
                (void)marqueeAt(scene,camera,*marqueeStart_,
                                marqueeCurrent_,size,marqueeAdditive_);
            marqueeStart_.reset();
        }
    }
    drawSelectionOutlines(scene,camera);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (!open) setVisible(false);
}

} // namespace engine2
