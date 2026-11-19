#pragma once

#include "engine2/Scene.hpp"
#include "engine2/Mesh.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace engine2 {

// Picking uses the REAL local mesh bounds, not the physics collider. Rays
// are transformed into object space so rotated/nonuniformly-scaled assets
// can be selected. The bounds are an approximation, not per-triangle hits.
struct PickRay {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f}; // unit-length world direction
};

inline std::optional<PickRay> viewportPickRay(
    const glm::mat4& viewProjection, glm::vec2 pixel, glm::vec2 viewport) {
    if (viewport.x <= 0.0f || viewport.y <= 0.0f ||
        pixel.x < 0.0f || pixel.y < 0.0f ||
        pixel.x >= viewport.x || pixel.y >= viewport.y ||
        !std::isfinite(pixel.x) || !std::isfinite(pixel.y))
        return std::nullopt;
    const float determinant = glm::determinant(viewProjection);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-10f)
        return std::nullopt;
    const glm::mat4 inverse = glm::inverse(viewProjection);
    const glm::vec2 ndc{2.0f * pixel.x / viewport.x - 1.0f,
                         1.0f - 2.0f * pixel.y / viewport.y};
    const glm::vec4 nearWorld = inverse * glm::vec4(ndc, -1.0f, 1.0f);
    const glm::vec4 farWorld = inverse * glm::vec4(ndc, 1.0f, 1.0f);
    if (std::abs(nearWorld.w) < 1.0e-8f ||
        std::abs(farWorld.w) < 1.0e-8f) return std::nullopt;
    const glm::vec3 nearPoint = glm::vec3(nearWorld) / nearWorld.w;
    const glm::vec3 farPoint = glm::vec3(farWorld) / farWorld.w;
    const glm::vec3 delta = farPoint - nearPoint;
    const float length = glm::length(delta);
    if (!std::isfinite(length) || length < 1.0e-5f) return std::nullopt;
    return PickRay{nearPoint, delta / length};
}

inline std::optional<float> intersectPickBounds(
    const PickRay& ray, const Bounds& bounds, const glm::mat4& localToWorld) {
    const float determinant = glm::determinant(localToWorld);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-10f)
        return std::nullopt;
    const glm::mat4 worldToLocal = glm::inverse(localToWorld);
    const glm::vec3 origin(worldToLocal * glm::vec4(ray.origin,1.0f));
    const glm::vec3 direction(worldToLocal * glm::vec4(ray.direction,0.0f));
    float entry = 0.0f;
    float exit = std::numeric_limits<float>::infinity();
    for (int axis=0;axis<3;++axis) {
        if (!std::isfinite(origin[axis]) || !std::isfinite(direction[axis]))
            return std::nullopt;
        if (std::abs(direction[axis]) < 1.0e-8f) {
            if (origin[axis] < bounds.min[axis] ||
                origin[axis] > bounds.max[axis]) return std::nullopt;
            continue;
        }
        float nearT = (bounds.min[axis] - origin[axis]) / direction[axis];
        float farT = (bounds.max[axis] - origin[axis]) / direction[axis];
        if (nearT > farT) std::swap(nearT,farT);
        entry = std::max(entry,nearT);
        exit = std::min(exit,farT);
        if (entry > exit) return std::nullopt;
    }
    if (!std::isfinite(entry) || entry < 0.0f || exit < 0.0f)
        return std::nullopt;
    return entry; // world-ray parameter is unchanged by affine transform
}

inline bool pickableVisible(const Scene& scene, Entity entity) {
    for (Entity current = entity; current; current = scene.parent(current))
        if (!scene.enabled(current)) return false;
    return true;
}

struct ScenePickHit {
    Entity entity{};
    float distance = std::numeric_limits<float>::infinity();
};

inline std::optional<ScenePickHit> pickSceneBounds(
    const Scene& scene, const PickRay& ray) {
    std::optional<ScenePickHit> best;
    for (Entity entity : scene.entities()) {
        if (!pickableVisible(scene,entity)) continue;
        const auto* renderer = scene.rendererComponent(entity);
        if (!renderer || !renderer->mesh) continue;
        const auto t = intersectPickBounds(
            ray, renderer->mesh->bounds(), scene.worldTransform(entity));
        if (t && (!best || *t < best->distance - 1.0e-5f))
            best = ScenePickHit{entity,*t};
    }
    return best;
}

// Generation-safe multi-selection. Clicking normally replaces the selection;
// Ctrl/Shift toggles a member without duplicating it. The last picked member
// is the primary inspector/gizmo target. Roots exclude entities with another
// selected ancestor, so parent+child group drags never double-transform.
class SceneSelection {
public:
    void prune(const Scene& scene) {
        selected_.erase(
            std::remove_if(selected_.begin(),selected_.end(),
                           [&](Entity e){return !scene.contains(e);}),
            selected_.end());
        if (!scene.contains(primary_) ||
            !contains(primary_))
            primary_ = selected_.empty() ? Entity{} : selected_.back();
    }

    void pick(const Scene& scene, Entity entity, bool toggle = false) {
        prune(scene);
        if (entity && !scene.contains(entity))
            throw std::invalid_argument("Cannot select stale scene entity");
        if (!toggle) {
            selected_.clear();
            if (entity) selected_.push_back(entity);
        } else if (entity) {
            const auto it = std::find(selected_.begin(),selected_.end(),entity);
            if (it != selected_.end()) selected_.erase(it);
            else selected_.push_back(entity);
        }
        primary_ = selected_.empty() ? Entity{} : selected_.back();
    }

    [[nodiscard]] bool contains(Entity e) const {
        return std::find(selected_.begin(),selected_.end(),e) != selected_.end();
    }
    [[nodiscard]] Entity primary() const { return primary_; }
    [[nodiscard]] const std::vector<Entity>& entities() const { return selected_; }
    [[nodiscard]] std::size_t size() const { return selected_.size(); }

    [[nodiscard]] std::vector<Entity> roots(const Scene& scene) const {
        std::vector<Entity> result;
        for (Entity entity : selected_) {
            if (!scene.contains(entity)) continue;
            bool selectedAncestor = false;
            for (Entity parent = scene.parent(entity); parent;
                 parent = scene.parent(parent)) {
                if (contains(parent)) {
                    selectedAncestor = true;
                    break;
                }
            }
            if (!selectedAncestor) result.push_back(entity);
        }
        return result;
    }

private:
    std::vector<Entity> selected_;
    Entity primary_{};
};

} // namespace engine2
