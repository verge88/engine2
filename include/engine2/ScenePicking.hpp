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

// Picking first checks real mesh-local bounds then intersects GPU-equivalent
// CPU triangles. Rotated/non-uniformly-scaled meshes are transformed to local
// space. Triangle ray tests do not yet alpha-test textured cutouts.
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

// Precise triangle picking uses AABBs only for early rejection. Ordering is
// by closest triangle intersection, not by closest bounding-box face.
inline std::optional<ScenePickHit> pickSceneTriangles(
    const Scene& scene, const PickRay& ray) {
    std::optional<ScenePickHit> best;
    for (Entity entity : scene.entities()) {
        if (!pickableVisible(scene,entity)) continue;
        const auto* renderer = scene.rendererComponent(entity);
        if (!renderer || !renderer->mesh) continue;
        const glm::mat4 model = scene.worldTransform(entity);
        const auto coarse = intersectPickBounds(
            ray,renderer->mesh->bounds(),model);
        if (!coarse || (best && *coarse >= best->distance)) continue;
        const glm::mat4 inverse = glm::inverse(model);
        const glm::vec3 localOrigin(inverse*glm::vec4(ray.origin,1.0f));
        const glm::vec3 localDirection(inverse*glm::vec4(ray.direction,0.0f));
        const auto fine = renderer->mesh->raycastLocal(
            localOrigin, localDirection,
            best ? best->distance : std::numeric_limits<float>::infinity());
        if (fine && (!best || *fine < best->distance - 1.0e-5f))
            best = ScenePickHit{entity,*fine};
    }
    return best;
}

struct PickRectangle {
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};
};

// The screen-space rectangle around the eight corners of a mesh's bounding
// box. Conservative selection; when a triangle crosses the near plane, only
// corners in front of the camera contribute (may omit partially clipped mesh).
inline std::optional<PickRectangle> projectPickBounds(
    const Bounds& bounds, const glm::mat4& model,
    const glm::mat4& viewProjection, glm::vec2 viewport) {
    if (viewport.x <= 0 || viewport.y <= 0) return std::nullopt;
    PickRectangle projected{
        glm::vec2(std::numeric_limits<float>::infinity()),
        glm::vec2(-std::numeric_limits<float>::infinity())
    };
    int valid = 0;
    for (int c=0;c<8;++c) {
        const glm::vec3 local(
            (c & 1) ? bounds.max.x : bounds.min.x,
            (c & 2) ? bounds.max.y : bounds.min.y,
            (c & 4) ? bounds.max.z : bounds.min.z);
        const glm::vec4 clip=viewProjection*model*glm::vec4(local,1.0f);
        if (!(clip.w>1.0e-5f) || !std::isfinite(clip.x) ||
            !std::isfinite(clip.y)) continue;
        const glm::vec2 ndc=glm::vec2(clip)/clip.w;
        const glm::vec2 pixel(
            (ndc.x*0.5f+0.5f)*viewport.x,
            (0.5f-ndc.y*0.5f)*viewport.y);
        projected.min=glm::min(projected.min,pixel);
        projected.max=glm::max(projected.max,pixel);
        ++valid;
    }
    return valid ? std::optional<PickRectangle>(projected) : std::nullopt;
}

inline bool pickRectanglesOverlap(PickRectangle a, PickRectangle b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x &&
           a.min.y <= b.max.y && a.max.y >= b.min.y;
}

inline std::vector<Entity> pickSceneRectangle(
    const Scene& scene, const glm::mat4& viewProjection,
    glm::vec2 viewport, glm::vec2 from, glm::vec2 to) {
    if (viewport.x<=0 || viewport.y<=0 || !std::isfinite(from.x) ||
        !std::isfinite(from.y) || !std::isfinite(to.x) ||
        !std::isfinite(to.y) || from.x<0 || from.y<0 ||
        to.x<0 || to.y<0 || from.x>=viewport.x ||
        to.x>=viewport.x || from.y>=viewport.y || to.y>=viewport.y)
        throw std::invalid_argument("Marquee rectangle is outside viewport");
    PickRectangle region{glm::min(from,to),glm::max(from,to)};
    std::vector<Entity> result;
    for (Entity entity : scene.entities()) {
        if (!pickableVisible(scene,entity)) continue;
        const auto* renderer=scene.rendererComponent(entity);
        if (!renderer || !renderer->mesh) continue;
        const auto box=projectPickBounds(renderer->mesh->bounds(),
            scene.worldTransform(entity),viewProjection,viewport);
        if (box && pickRectanglesOverlap(region,*box))
            result.push_back(entity);
    }
    return result;
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

    // Marquee is additive union rather than Ctrl+click toggling.
    void selectMany(const Scene& scene, const std::vector<Entity>& entities,
                    bool additive = false) {
        prune(scene);
        if (!additive) selected_.clear();
        for (Entity entity : entities) {
            if (!scene.contains(entity))
                throw std::invalid_argument("Marquee contains a stale entity");
            if (!contains(entity)) selected_.push_back(entity);
        }
        primary_=selected_.empty() ? Entity{} : selected_.back();
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
