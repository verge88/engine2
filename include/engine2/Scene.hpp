#pragma once

#include "engine2/Renderer.hpp"
#include "engine2/CollisionMath.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
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

// Unparented rigid-body state. Linear collision response remains based on
// sphere/AABB geometry in world units; dynamic spheres may additionally carry
// free-spin angular velocity because their collider is rotation-invariant.
// Static bodies have infinite mass and are moved by game/editor code.
struct RigidBodyComponent {
    glm::vec3 velocity{0.0f};
    bool gravity = true;
    float radius = 0.5f;          // world-space approximate bounding sphere
    float groundHeight = -1.0f;  // world-space collision plane
    float restitution = 0.35f;   // 0: no bounce, 1: full elastic bounce
    float staticFriction = 0.0f; // legacy scenes remain frictionless
    float dynamicFriction = 0.0f;// <= staticFriction, sliding resistance
    float mass = 1.0f;           // kilograms, strictly positive for dynamics
    bool isStatic = false;       // infinite mass, no integration
    bool continuous = false;     // opt-in translation CCD; moving peers also opt in
    ColliderShape shape = ColliderShape::Sphere;
    glm::vec3 halfExtents{0.5f}; // WORLD units, for AABB boxes only
    // Kept at the end to preserve older positional aggregate initializers.
    glm::vec3 angularVelocity{0.0f}; // radians/second; dynamic spheres only
};

struct SceneStats {
    std::size_t entities = 0;
    std::size_t renderables = 0;
    std::size_t pointLights = 0;
    std::size_t rigidBodies = 0;
    std::uint64_t fixedSteps = 0;
    std::uint64_t broadphasePairs = 0; // 3D spatial-grid overlapping pairs
    std::uint64_t broadphaseCells = 0; // accumulated occupied grid cells
    std::uint64_t broadphaseTests = 0; // accumulated cell pair checks
    std::uint64_t ccdHits = 0;         // all swept collider time-of-impact events
    std::uint64_t ccdDynamicHits = 0;  // TOIs between two moving CCD bodies
    std::uint64_t ccdBudgetExhaustions = 0; // remaining time after event limit
    std::uint64_t ccdBoxFaceHits = 0;   // exact sphere/box face impacts
    std::uint64_t ccdBoxEdgeHits = 0;   // exact sphere/box cylindrical-edge impacts
    std::uint64_t ccdBoxCornerHits = 0; // exact sphere/box spherical-corner impacts
    std::uint64_t sphereContacts = 0;  // accumulated resolved sphere-sphere contacts
    std::uint64_t boxContacts = 0;     // accumulated resolved contacts involving boxes
    std::size_t persistentContacts = 0;// current cross-step resting contact cache
    std::uint64_t contactWarmStarts = 0;// accumulated cache reuses across fixed steps
    std::uint64_t solverPasses = 0;    // accumulated sequential solver passes
};

// A small runtime object/component world rather than a data-oriented archetype
// ECS. Supports parent-child transforms, renderers, lights and fixed-step
// rigid-body motion with 3D spatial-grid broadphase, discrete sequential
// contacts, and bounded event-driven translation CCD for spheres/AABBs;
// independent of a GL context until Mesh::draw().
class Scene {
public:
    static constexpr float fixedDeltaSeconds = 1.0f / 60.0f;

    Entity createEntity(std::string name = {});
    void destroyEntity(Entity entity);
    [[nodiscard]] bool contains(Entity entity) const;

    [[nodiscard]] const std::string& name(Entity entity) const;
    void setName(Entity entity, std::string name);
    void setParent(Entity child, Entity parent = {}, bool keepWorld = false);
    [[nodiscard]] Entity parent(Entity entity) const;

    void setLocalTransform(Entity entity, const glm::mat4& matrix);
    [[nodiscard]] const glm::mat4& localTransform(Entity entity) const;
    [[nodiscard]] glm::mat4 worldTransform(Entity entity) const;
    void setEnabled(Entity entity, bool enabled);
    [[nodiscard]] bool enabled(Entity entity) const;

    // Serialization and editor/inspector access, including disabled entities.
    // Returned component pointers are invalidated if the Scene is mutated.
    [[nodiscard]] std::vector<Entity> entities() const;
    [[nodiscard]] const MeshRendererComponent* rendererComponent(Entity entity) const;
    [[nodiscard]] const PointLight* pointLightComponent(Entity entity) const;
    [[nodiscard]] const RigidBodyComponent* rigidBodyComponent(Entity entity) const;

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
    using ContactCacheKey = std::array<std::uint32_t,4>;

    struct PersistentContactState {
        float normalImpulse = 0.0f;
        glm::vec3 tangentImpulse{0.0f};
        glm::vec3 normal{1.0f,0.0f,0.0f};
        std::uint64_t lastSeenStep = 0;
    };

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
    void invalidateContacts(std::uint32_t slot);
    void fixedStep();

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> freeSlots_;
    std::map<ContactCacheKey,PersistentContactState> contactCache_;
    double accumulator_ = 0.0;
    std::uint64_t totalSteps_ = 0;
    std::uint64_t broadphasePairs_ = 0;
    std::uint64_t broadphaseCells_ = 0;
    std::uint64_t broadphaseTests_ = 0;
    std::uint64_t ccdHits_ = 0;
    std::uint64_t ccdDynamicHits_ = 0;
    std::uint64_t ccdBudgetExhaustions_ = 0;
    std::uint64_t ccdBoxFaceHits_ = 0;
    std::uint64_t ccdBoxEdgeHits_ = 0;
    std::uint64_t ccdBoxCornerHits_ = 0;
    std::uint64_t sphereContacts_ = 0;
    std::uint64_t boxContacts_ = 0;
    std::uint64_t contactWarmStarts_ = 0;
    std::uint64_t solverPasses_ = 0;
};

} // namespace engine2
