#include "engine2/Scene.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace engine2 {
namespace {

bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

bool finite(const glm::mat4& matrix) {
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[column][row])) return false;
    return true;
}

} // namespace

Entity Scene::createEntity(std::string name) {
    std::uint32_t index;
    if (freeSlots_.empty()) {
        index = static_cast<std::uint32_t>(slots_.size());
        if (slots_.size() >= Entity::invalidIndex)
            throw std::overflow_error("Scene entity index limit exceeded");
        slots_.emplace_back();
    } else {
        index = freeSlots_.back();
        freeSlots_.pop_back();
    }
    Slot& slot = slots_[index];
    slot.alive = true;
    slot.enabled = true;
    slot.name = std::move(name);
    slot.parent = {};
    slot.local = glm::mat4(1.0f);
    slot.renderer.reset();
    slot.light.reset();
    slot.body.reset();
    return {index, slot.generation};
}

bool Scene::contains(Entity entity) const {
    return entity.index < slots_.size() &&
           slots_[entity.index].alive &&
           slots_[entity.index].generation == entity.generation;
}

const Scene::Slot& Scene::checked(Entity entity) const {
    if (!contains(entity))
        throw std::invalid_argument("Invalid or destroyed scene entity handle");
    return slots_[entity.index];
}

Scene::Slot& Scene::checked(Entity entity) {
    return const_cast<Slot&>(std::as_const(*this).checked(entity));
}

const std::string& Scene::name(Entity entity) const {
    return checked(entity).name;
}

Entity Scene::parent(Entity entity) const {
    return checked(entity).parent;
}

void Scene::destroyEntity(Entity entity) {
    checked(entity);
    // Deleting a parent deletes its descendants as well. Collect handles
    // first: generations change during deletion and must not be reused.
    std::vector<Entity> children;
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].alive && slots_[i].parent == entity)
            children.push_back({static_cast<std::uint32_t>(i),
                                slots_[i].generation});
    }
    for (Entity child : children) destroyEntity(child);

    Slot& slot = checked(entity);
    slot.alive = false;
    slot.name.clear();
    slot.parent = {};
    slot.renderer.reset();
    slot.light.reset();
    slot.body.reset();
    if (++slot.generation == 0) ++slot.generation;
    freeSlots_.push_back(entity.index);
}

void Scene::setParent(Entity child, Entity newParent, bool keepWorld) {
    Slot& node = checked(child);
    if (newParent) checked(newParent);
    if (newParent == child)
        throw std::invalid_argument("Entity cannot parent itself");
    if (newParent && node.body)
        throw std::invalid_argument("Rigid bodies must be root entities");

    // Reject cycles without mutating the scene.
    for (Entity ancestor = newParent; ancestor; ancestor = checked(ancestor).parent) {
        if (ancestor == child)
            throw std::invalid_argument("Scene parent cycle detected");
    }

    glm::mat4 adjusted = node.local;
    if (keepWorld) {
        const glm::mat4 originalWorld = worldTransform(child);
        const glm::mat4 parentWorld =
            newParent ? worldTransform(newParent) : glm::mat4(1.0f);
        if (std::abs(glm::determinant(parentWorld)) < 1.0e-8f)
            throw std::invalid_argument("Cannot preserve transform under singular parent");
        adjusted = glm::inverse(parentWorld) * originalWorld;
    }
    node.parent = newParent;
    node.local = adjusted;
}

void Scene::setLocalTransform(Entity entity, const glm::mat4& matrix) {
    if (!finite(matrix))
        throw std::invalid_argument("Scene transform contains non-finite values");
    checked(entity).local = matrix;
}

const glm::mat4& Scene::localTransform(Entity entity) const {
    return checked(entity).local;
}

glm::mat4 Scene::worldTransform(Entity entity) const {
    checked(entity);
    // Evaluate the hierarchy in root -> leaf order. No caching means moving
    // parents always updates descendants without dirty propagation bugs.
    std::vector<Entity> chain;
    for (Entity current = entity; current; current = checked(current).parent)
        chain.push_back(current);
    glm::mat4 transform(1.0f);
    for (auto i = chain.rbegin(); i != chain.rend(); ++i)
        transform *= checked(*i).local;
    return transform;
}

void Scene::setEnabled(Entity entity, bool enabled) {
    checked(entity).enabled = enabled;
}

bool Scene::enabled(Entity entity) const {
    return checked(entity).enabled;
}

bool Scene::isVisible(Entity entity) const {
    for (Entity current = entity; current; current = checked(current).parent) {
        if (!checked(current).enabled) return false;
    }
    return true;
}

std::vector<Entity> Scene::entities() const {
    std::vector<Entity> result;
    result.reserve(slots_.size());
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].alive)
            result.push_back({static_cast<std::uint32_t>(i), slots_[i].generation});
    }
    return result;
}

const MeshRendererComponent* Scene::rendererComponent(Entity entity) const {
    const auto& component = checked(entity).renderer;
    return component ? &*component : nullptr;
}

const PointLight* Scene::pointLightComponent(Entity entity) const {
    const auto& component = checked(entity).light;
    return component ? &*component : nullptr;
}

const RigidBodyComponent* Scene::rigidBodyComponent(Entity entity) const {
    const auto& component = checked(entity).body;
    return component ? &*component : nullptr;
}

void Scene::setRenderer(Entity entity, MeshRendererComponent component) {
    checked(entity).renderer = std::move(component);
}

void Scene::setPointLight(Entity entity, const PointLight& light) {
    if (!finite(light.position) || !finite(light.color) ||
        !std::isfinite(light.intensity) || !std::isfinite(light.range) ||
        light.range <= 0.0f || light.intensity < 0.0f)
        throw std::invalid_argument("Invalid point light parameters");
    checked(entity).light = light;
}

void Scene::setRigidBody(Entity entity, const RigidBodyComponent& body) {
    Slot& slot = checked(entity);
    if (slot.parent)
        throw std::invalid_argument("Rigid bodies must be root entities");
    if (!finite(body.velocity) || !std::isfinite(body.radius) ||
        !std::isfinite(body.groundHeight) || !std::isfinite(body.restitution) ||
        !std::isfinite(body.mass) || body.mass <= 0.0f ||
        body.radius <= 0.0f || body.restitution < 0.0f || body.restitution > 1.0f)
        throw std::invalid_argument("Invalid rigid body parameters");
    slot.body = body;
}

const RigidBodyComponent& Scene::rigidBody(Entity entity) const {
    const Slot& slot = checked(entity);
    if (!slot.body)
        throw std::invalid_argument("Entity has no rigid body component");
    return *slot.body;
}

std::vector<RenderItem> Scene::renderItems() const {
    std::vector<RenderItem> items;
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        const Slot& slot = slots_[index];
        if (!slot.alive || !slot.renderer || !slot.renderer->mesh) continue;
        const Entity handle{static_cast<std::uint32_t>(index), slot.generation};
        if (!isVisible(handle)) continue;
        items.push_back({slot.renderer->mesh, worldTransform(handle),
                         slot.renderer->material});
    }
    return items;
}

std::vector<PointLight> Scene::pointLights() const {
    std::vector<PointLight> lights;
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        const Slot& slot = slots_[index];
        if (!slot.alive || !slot.light) continue;
        const Entity handle{static_cast<std::uint32_t>(index), slot.generation};
        if (!isVisible(handle)) continue;
        PointLight light = *slot.light;
        light.position = glm::vec3(worldTransform(handle) *
                                    glm::vec4(light.position, 1.0f));
        lights.push_back(light);
    }
    return lights;
}

SceneStats Scene::stats() const {
    SceneStats result;
    result.fixedSteps = totalSteps_;
    result.broadphasePairs = broadphasePairs_;
    result.sphereContacts = sphereContacts_;
    for (const Slot& slot : slots_) {
        if (!slot.alive) continue;
        ++result.entities;
        if (slot.renderer && slot.renderer->mesh) ++result.renderables;
        if (slot.light) ++result.pointLights;
        if (slot.body) ++result.rigidBodies;
    }
    return result;
}

void Scene::fixedStep() {
    struct Candidate {
        std::uint32_t slot;
        glm::vec3 position;
        float radius;
        float minX;
        float maxX;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(slots_.size());

    // Semi-implicit Euler: integrate velocity first, then translate. Fixed
    // 60 Hz makes frame grouping deterministic. Static spheres are infinite-
    // mass obstacles and never move in response to contacts.
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        Slot& slot = slots_[i];
        if (!slot.alive || !slot.enabled || !slot.body) continue;
        RigidBodyComponent& body = *slot.body;
        glm::vec3 position(slot.local[3]);
        if (!body.isStatic) {
            if (body.gravity)
                body.velocity.y -= 9.81f * fixedDeltaSeconds;
            position += body.velocity * fixedDeltaSeconds;

            const float minimumY = body.groundHeight + body.radius;
            if (position.y < minimumY) {
                position.y = minimumY;
                if (body.velocity.y < 0.0f) {
                    body.velocity.y = -body.velocity.y * body.restitution;
                    if (body.velocity.y < 0.25f) body.velocity.y = 0.0f;
                }
            }
            slot.local[3] = glm::vec4(position, 1.0f);
        }
        candidates.push_back({
            static_cast<std::uint32_t>(i), position, body.radius,
            position.x - body.radius, position.x + body.radius
        });
    }

    // Sweep-and-prune broadphase: only x-interval overlaps reach the
    // remaining y/z and narrowphase tests. Sort by x, then entity slot for a
    // deterministic order, even if the same positions are repeated.
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  if (a.minX != b.minX) return a.minX < b.minX;
                  return a.slot < b.slot;
              });

    for (std::size_t i = 0; i < candidates.size(); ++i) {
        Candidate& first = candidates[i];
        for (std::size_t j = i + 1; j < candidates.size(); ++j) {
            Candidate& second = candidates[j];
            if (second.minX > first.maxX) break;
            Slot& aSlot = slots_[first.slot];
            Slot& bSlot = slots_[second.slot];
            RigidBodyComponent& a = *aSlot.body;
            RigidBodyComponent& b = *bSlot.body;
            if (a.isStatic && b.isStatic) continue;
            ++broadphasePairs_;

            const float combinedRadius = a.radius + b.radius;
            const glm::vec3 delta = second.position - first.position;
            if (std::abs(delta.y) >= combinedRadius ||
                std::abs(delta.z) >= combinedRadius) continue;
            const float distanceSquared = glm::dot(delta, delta);
            if (distanceSquared >= combinedRadius * combinedRadius) continue;
            ++sphereContacts_;

            // Fully coincident centers are possible when loading a scene.
            // Choose a stable axis for the separation instead of dividing 0/0.
            const float distance = std::sqrt(distanceSquared);
            const glm::vec3 normal =
                distance > 1.0e-6f ? delta / distance
                                   : glm::vec3(1.0f, 0.0f, 0.0f);
            const float inverseA = a.isStatic ? 0.0f : 1.0f / a.mass;
            const float inverseB = b.isStatic ? 0.0f : 1.0f / b.mass;
            const float totalInverseMass = inverseA + inverseB;
            if (totalInverseMass <= 0.0f) continue;

            // Resolve the actual overlap, mass-weighted, before applying the
            // collision impulse. Keep an explicit 0.0001f slop to avoid
            // non-stop micro-corrections caused by float rounding.
            const float penetration = combinedRadius - distance;
            const float correction =
                std::max(penetration - 0.0001f, 0.0f) / totalInverseMass;
            first.position -= normal * correction * inverseA;
            second.position += normal * correction * inverseB;

            const glm::vec3 relativeVelocity =
                (b.isStatic ? glm::vec3(0.0f) : b.velocity) -
                (a.isStatic ? glm::vec3(0.0f) : a.velocity);
            const float closingSpeed = glm::dot(relativeVelocity, normal);
            if (closingSpeed < 0.0f) {
                const float restitution = std::min(a.restitution, b.restitution);
                const float impulseMagnitude =
                    -(1.0f + restitution) * closingSpeed / totalInverseMass;
                const glm::vec3 impulse = impulseMagnitude * normal;
                if (!a.isStatic) a.velocity -= impulse * inverseA;
                if (!b.isStatic) b.velocity += impulse * inverseB;
            }
            // Keep candidate centers synchronized with the correction. The
            // broadphase order is still based on pre-correction intervals.
            if (!a.isStatic) aSlot.local[3] = glm::vec4(first.position, 1.0f);
            if (!b.isStatic) bSlot.local[3] = glm::vec4(second.position, 1.0f);
        }
    }

    // Sphere separation may push a body below its ground plane. Project it
    // back above the plane after contact resolution to avoid floor tunneling.
    for (const Candidate& candidate : candidates) {
        Slot& slot = slots_[candidate.slot];
        RigidBodyComponent& body = *slot.body;
        if (body.isStatic) continue;
        glm::vec3 position(slot.local[3]);
        const float minimumY = body.groundHeight + body.radius;
        if (position.y < minimumY) {
            position.y = minimumY;
            if (body.velocity.y < 0.0f) body.velocity.y = 0.0f;
            slot.local[3] = glm::vec4(position, 1.0f);
        }
    }
    ++totalSteps_;
}

std::uint32_t Scene::advance(float deltaSeconds) {
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0f)
        throw std::invalid_argument("Scene frame duration must be finite and nonnegative");
    // At most eight fixed steps each frame to prevent runaway CPU load.
    accumulator_ += std::min(static_cast<double>(deltaSeconds), 0.25);
    std::uint32_t steps = 0;
    while (accumulator_ + 1.0e-8 >= fixedDeltaSeconds && steps < 8) {
        fixedStep();
        accumulator_ -= fixedDeltaSeconds;
        ++steps;
    }
    if (steps == 8 && accumulator_ >= fixedDeltaSeconds)
        accumulator_ = std::fmod(accumulator_, fixedDeltaSeconds);
    return steps;
}

} // namespace engine2
