#include "engine2/Scene.hpp"

#include <glm/common.hpp>
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
    for (Slot& slot : slots_) {
        if (!slot.alive || !slot.enabled || !slot.body) continue;
        RigidBodyComponent& body = *slot.body;
        if (body.gravity)
            body.velocity.y -= 9.81f * fixedDeltaSeconds;

        glm::vec3 position(slot.local[3]);
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
