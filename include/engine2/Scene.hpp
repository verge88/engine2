#pragma once

#include "engine2/Renderer.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace engine2 {

// Generation-stamped handles reject stale references after object deletion
// and slot reuse. Entity{} represents an absent parent / invalid entity.
struct Entity {
    static constexpr std::uint32_t invalidIndex =
        std::numeric_limits<std::uint32_t>::max();
    std::uint32_t index = invalidIndex;
    std::uint32_t generation = 0;

    [[nodiscard]] explicit operator bool() const {
        return index != invalidIndex;
    }
    bool operator==(const Entity&) const = default;
};

// Mesh/texture lifetimes are owned by the application's asset storage
// (procedural Mesh or LoadedScene). Scene components hold non-owning pointers.
struct MeshRendererComponent {
    const Mesh* mesh = nullptr;
    Material material{};
};

// A first, deliberately limited, physics building block: unparented bodies
// integrate gravity on a fixed timestep and collide with a horizontal plane.
// Full rigid-body/world collisions and a broadphase are later milestones.
struct RigidBodyComponent {
    glm::vec3 velocity{0.0f};
    bool gravity = true;
    float radius = 0.5f;          // world-space approximate bounding sphere
    float groundHeight = -1.0f;  // world-space collision plane
    float restitution = 0.35f;   // 0: no bounce, 1: full elastic bounce
};

struct SceneStats {
    std::size_t entities = 0;
    std::size_t renderables = 0;
    std::size_t pointLights = 0;
    std::size_t rigidBodies = 0;
    std::uint64_t fixedSteps = 0;
};

// A small runtime object/component world rather than a data-oriented archetype
// ECS. Supports parent-child transforms, renderers, lights and fixed-step
// rigid-body motion; independent of a GL context until Mesh::draw().
class Scene {
public:
    static constexpr float fixedDeltaSeconds = 1.0f / 60.0f;

    Entity createEntity(std::string name = {});
    void destroyEntity(Entity entity);
    [[nodiscard]] bool contains(Entity entity) const;

    [[nodiscard]] const std::string& name(Entity entity) const;
    void setParent(Entity child, Entity parent = {}, bool keepWorld = false);
    [[nodiscard]] Entity parent(Entity entity) const;

    void setLocalTransform(Entity entity, const glm::mat4& matrix);
    [[nodiscard]] const glm::mat4& localTransform(Entity entity) const;
    [[nodiscard]] glm::mat4 worldTransform(Entity entity) const;
    void setEnabled(Entity entity, bool enabled);
    [[nodiscard]] bool enabled(Entity entity) const;

    void setRenderer(Entity entity, MeshRendererComponent component);
    void setPointLight(Entity entity, const PointLight& light);
    void setRigidBody(Entity entity, const RigidBodyComponent& body);
    [[nodiscard]] const RigidBodyComponent& rigidBody(Entity entity) const;

    [[nodiscard]] std::vector<RenderItem> renderItems() const;
    [[nodiscard]] std::vector<PointLight> pointLights() const;
    [[nodiscard]] SceneStats stats() const;

    // Returns executed fixed 1/60-second steps, with at most 8 per frame.
    // Large frame stalls drop extra accumulated time to avoid a spiral of
    // death. Identical time chunks produce identical deterministic physics.
    [[nodiscard]] std::uint32_t advance(float deltaSeconds);

private:
    struct Slot {
        std::uint32_t generation = 1;
        bool alive = false;
        bool enabled = true;
        std::string name;
        Entity parent{};
        glm::mat4 local{1.0f};
        std::optional<MeshRendererComponent> renderer;
        std::optional<PointLight> light;
        std::optional<RigidBodyComponent> body;
    };

    [[nodiscard]] const Slot& checked(Entity entity) const;
    [[nodiscard]] Slot& checked(Entity entity);
    [[nodiscard]] bool isVisible(Entity entity) const;
    void fixedStep();

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> freeSlots_;
    double accumulator_ = 0.0;
    std::uint64_t totalSteps_ = 0;
};

} // namespace engine2
