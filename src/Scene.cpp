#include "engine2/Scene.hpp"
#include "engine2/ContactSolver.hpp"
#include "engine2/PhysicsBroadphase.hpp"
#include "engine2/ContinuousCollision.hpp"
#include "engine2/AngularDynamics.hpp"

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

float inverseAngularInertia(const RigidBodyComponent& body) {
    if (body.isStatic || body.shape!=ColliderShape::Sphere) return 0.0f;
    // Solid sphere around any axis: I = 2/5 m r^2.
    return 2.5f/(body.mass*body.radius*body.radius);
}

glm::vec3 contactLeverArm(
    const RigidBodyComponent& body, glm::vec3 position,
    const RigidBodyComponent& other, glm::vec3 otherPosition,
    glm::vec3 normal, bool firstBody) {
    return colliderContactLeverArm(
        {body.shape,body.radius,body.halfExtents},position,
        {other.shape,other.radius,other.halfExtents},otherPosition,
        normal,firstBody);
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

void Scene::invalidateContacts(std::uint32_t slot) {
    for (auto it=contactCache_.begin();it!=contactCache_.end();) {
        if (it->first[0]==slot || it->first[2]==slot)
            it=contactCache_.erase(it);
        else
            ++it;
    }
}

const std::string& Scene::name(Entity entity) const {
    return checked(entity).name;
}

void Scene::setName(Entity entity, std::string name) {
    if (name.size() > 1024)
        throw std::invalid_argument("Scene entity name exceeds 1024 bytes");
    checked(entity).name = std::move(name);
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
    invalidateContacts(entity.index);
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
    invalidateContacts(entity.index);
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
    invalidateContacts(entity.index);
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
    if (!finite(body.velocity) || !finite(body.angularVelocity) ||
        !std::isfinite(body.radius) ||
        !std::isfinite(body.groundHeight) || !std::isfinite(body.restitution) ||
        !std::isfinite(body.mass) || body.mass <= 0.0f ||
        !finite(body.halfExtents) ||
        body.halfExtents.x <= 0.0f || body.halfExtents.y <= 0.0f ||
        body.halfExtents.z <= 0.0f ||
        (body.shape != ColliderShape::Sphere && body.shape != ColliderShape::Box) ||
        body.radius <= 0.0f || body.restitution < 0.0f || body.restitution > 1.0f ||
        !std::isfinite(body.staticFriction) ||
        !std::isfinite(body.dynamicFriction) ||
        body.staticFriction < 0.0f || body.staticFriction > 4.0f ||
        body.dynamicFriction < 0.0f ||
        body.dynamicFriction > body.staticFriction)
        throw std::invalid_argument("Invalid rigid body parameters");
    const bool spinning=glm::dot(body.angularVelocity,body.angularVelocity)>1.0e-12f;
    if (spinning && body.shape!=ColliderShape::Sphere)
        throw std::invalid_argument(
            "Angular velocity currently requires a spherical collider");
    if (spinning && body.isStatic)
        throw std::invalid_argument(
            "Static rigid bodies cannot have angular velocity");
    invalidateContacts(entity.index);
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
    result.broadphaseCells = broadphaseCells_;
    result.broadphaseTests = broadphaseTests_;
    result.ccdHits = ccdHits_;
    result.ccdDynamicHits = ccdDynamicHits_;
    result.ccdBudgetExhaustions = ccdBudgetExhaustions_;
    result.ccdBoxFaceHits = ccdBoxFaceHits_;
    result.ccdBoxEdgeHits = ccdBoxEdgeHits_;
    result.ccdBoxCornerHits = ccdBoxCornerHits_;
    result.sphereContacts = sphereContacts_;
    result.boxContacts = boxContacts_;
    result.persistentContacts = contactCache_.size();
    result.contactWarmStarts = contactWarmStarts_;
    result.solverPasses = solverPasses_;
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
        std::uint32_t slot = 0;
        glm::vec3 start{0.0f};
        glm::vec3 position{0.0f};
        glm::vec3 half{0.0f};
    };
    std::vector<Candidate> candidates;
    candidates.reserve(slots_.size());

    // Semi-implicit Euler at fixed 60 Hz. Static bodies never integrate.
    // Remember both endpoints: swept broadphase must not discard fast spheres
    // that cross a small obstacle entirely inside one fixed simulation step.
    for (std::size_t i=0;i<slots_.size();++i) {
        Slot& slot=slots_[i];
        if (!slot.alive || !slot.enabled || !slot.body) continue;
        RigidBodyComponent& body=*slot.body;
        const glm::vec3 half=colliderHalfExtents(
            {body.shape,body.radius,body.halfExtents});
        const glm::vec3 start(slot.local[3]);
        glm::vec3 position=start;
        if (!body.isStatic) {
            if (body.shape==ColliderShape::Sphere)
                slot.local=integrateAngularVelocity(
                    slot.local,body.angularVelocity,fixedDeltaSeconds);
            if (body.gravity) body.velocity.y-=9.81f*fixedDeltaSeconds;
            position+=body.velocity*fixedDeltaSeconds;
            const float floor=body.groundHeight+half.y;
            if (position.y<floor) {
                position.y=floor;
                solveFloorImpactAngular(
                    body.velocity,body.angularVelocity,
                    1.0f/body.mass,inverseAngularInertia(body),
                    body.shape==ColliderShape::Sphere ? body.radius : 0.0f,
                    {body.restitution,body.staticFriction,body.dynamicFriction});
            }
            slot.local[3]=glm::vec4(position,1.0f);
        }
        candidates.push_back({static_cast<std::uint32_t>(i),start,position,half});
    }

    // True 3D broadphase: map the swept world-space bounds to 2m cells. A
    // giant proxy uses fallback checks against all bodies rather than
    // allocating an unbounded number of grid cells. Deduplicated pairs are
    // sorted by stable candidate indices to preserve deterministic physics.
    constexpr float margin=0.02f;
    std::vector<PhysicsBroadphaseProxy> proxies;
    proxies.reserve(candidates.size());
    for (const Candidate& candidate : candidates) {
        const RigidBodyComponent& body=*slots_[candidate.slot].body;
        proxies.push_back({
            glm::min(candidate.start,candidate.position)-candidate.half-glm::vec3(margin),
            glm::max(candidate.start,candidate.position)+candidate.half+glm::vec3(margin),
            body.isStatic
        });
    }
    const PhysicsBroadphaseResult broadphase=physicsGridBroadphase(proxies);
    broadphasePairs_+=broadphase.pairs.size();
    broadphaseCells_+=broadphase.occupiedCells;
    broadphaseTests_+=broadphase.candidateTests;

    // Event-driven continuous collision detection. Opt-in dynamic spheres
    // and translation-only AABBs advance on the same CCD timeline. Targets
    // are static sphere/AABBs or another opt-in moving collider. Rebuild swept
    // 3D pairs after EVERY impact, because bouncing can send a body towards a
    // wall entirely outside its original swept AABB.
    std::vector<std::size_t> ccdActors;
    std::vector<std::size_t> ccdObjects;
    for (std::size_t i=0;i<candidates.size();++i) {
        const RigidBodyComponent& body=*slots_[candidates[i].slot].body;
        const bool actor=!body.isStatic && body.continuous;
        if (actor) {
            ccdActors.push_back(i);
            candidates[i].position=candidates[i].start;
        }
        if (actor || body.isStatic) ccdObjects.push_back(i);
    }

    if (!ccdActors.empty()) {
        float remaining=fixedDeltaSeconds;
        constexpr int maxCcdEvents=12;
        constexpr float separationEpsilon=0.0002f;
        int events=0;
        for (;events<maxCcdEvents && remaining>1.0e-8f;++events) {
            std::vector<PhysicsBroadphaseProxy> swept;
            swept.reserve(ccdObjects.size());
            for (std::size_t index:ccdObjects) {
                const Candidate& candidate=candidates[index];
                const RigidBodyComponent& body=*slots_[candidate.slot].body;
                const glm::vec3 next=body.isStatic
                    ? candidate.position
                    : candidate.position+body.velocity*remaining;
                swept.push_back({
                    glm::min(candidate.position,next)-candidate.half-glm::vec3(margin),
                    glm::max(candidate.position,next)+candidate.half+glm::vec3(margin),
                    body.isStatic
                });
            }
            const PhysicsBroadphaseResult grid=physicsGridBroadphase(swept);
            broadphasePairs_+=grid.pairs.size();
            broadphaseCells_+=grid.occupiedCells;
            broadphaseTests_+=grid.candidateTests;

            std::optional<SweptSphereHit> earliest;
            std::size_t impactA=0,impactB=0;
            for (const auto& [localA,localB]:grid.pairs) {
                const std::size_t ia=ccdObjects[localA];
                const std::size_t ib=ccdObjects[localB];
                const Candidate& first=candidates[ia];
                const Candidate& second=candidates[ib];
                const RigidBodyComponent& a=*slots_[first.slot].body;
                const RigidBodyComponent& b=*slots_[second.slot].body;
                const glm::vec3 endA=first.position+
                    (a.isStatic ? glm::vec3(0.0f) : a.velocity*remaining);
                const glm::vec3 endB=second.position+
                    (b.isStatic ? glm::vec3(0.0f) : b.velocity*remaining);
                const std::optional<SweptSphereHit> hit=sweepColliderMoving(
                    first.position,endA,
                    {a.shape,a.radius,a.halfExtents},
                    second.position,endB,
                    {b.shape,b.radius,b.halfExtents});
                // Grid pairs are sorted, so exact-time ties use stable order.
                if (hit && (!earliest ||
                            hit->fraction<earliest->fraction-1.0e-6f)) {
                    earliest=hit;
                    impactA=ia;
                    impactB=ib;
                }
            }

            const float elapsed=earliest
                ? remaining*earliest->fraction : remaining;
            for (std::size_t index:ccdActors) {
                Candidate& actor=candidates[index];
                const RigidBodyComponent& body=*slots_[actor.slot].body;
                actor.position+=body.velocity*elapsed;
            }
            remaining=std::max(0.0f,remaining-elapsed);
            if (!earliest) break;

            Candidate& first=candidates[impactA];
            Candidate& second=candidates[impactB];
            RigidBodyComponent& a=*slots_[first.slot].body;
            RigidBodyComponent& b=*slots_[second.slot].body;
            const float invA=a.isStatic ? 0.0f : 1.0f/a.mass;
            const float invB=b.isStatic ? 0.0f : 1.0f/b.mass;
            const float invTotal=invA+invB;
            glm::vec3 va=a.isStatic ? glm::vec3(0.0f) : a.velocity;
            glm::vec3 vb=b.isStatic ? glm::vec3(0.0f) : b.velocity;
            glm::vec3 wa=a.isStatic ? glm::vec3(0.0f) : a.angularVelocity;
            glm::vec3 wb=b.isStatic ? glm::vec3(0.0f) : b.angularVelocity;
            ContactImpulseState impulse;
            solveContactImpulseAngular(
                va,wa,vb,wb,invA,invB,
                inverseAngularInertia(a),inverseAngularInertia(b),
                contactLeverArm(
                    a,first.position,b,second.position,earliest->normal,true),
                contactLeverArm(
                    b,second.position,a,first.position,earliest->normal,false),
                earliest->normal,
                {a.restitution,a.staticFriction,a.dynamicFriction},
                {b.restitution,b.staticFriction,b.dynamicFriction},
                impulse);
            if (!a.isStatic) {
                a.velocity=va;
                a.angularVelocity=wa;
            }
            if (!b.isStatic) {
                b.velocity=vb;
                b.angularVelocity=wb;
            }
            // Stay just outside the just-hit surface to avoid re-detecting
            // the same event at fraction zero after a velocity reversal.
            if (invTotal>0.0f) {
                first.position-=earliest->normal*
                    (separationEpsilon*invA/invTotal);
                second.position+=earliest->normal*
                    (separationEpsilon*invB/invTotal);
            }
            ++ccdHits_;
            if (!a.isStatic && !b.isStatic) ++ccdDynamicHits_;
            switch (earliest->feature) {
                case SweptContactFeature::Face: ++ccdBoxFaceHits_; break;
                case SweptContactFeature::Edge: ++ccdBoxEdgeHits_; break;
                case SweptContactFeature::Corner: ++ccdBoxCornerHits_; break;
                case SweptContactFeature::Sphere: break;
            }
        }
        // Bound the time spent in a pathological corridor of tiny contacts.
        // Unsimulated remainder falls back to discrete resolution and is
        // surfaced via telemetry, not silently frozen.
        if (remaining>1.0e-8f) {
            ++ccdBudgetExhaustions_;
            for (std::size_t index:ccdActors) {
                Candidate& actor=candidates[index];
                const RigidBodyComponent& body=*slots_[actor.slot].body;
                actor.position+=body.velocity*remaining;
            }
        }
        for (std::size_t index:ccdActors) {
            Candidate& actor=candidates[index];
            RigidBodyComponent& body=*slots_[actor.slot].body;
            const float floor=body.groundHeight+actor.half.y;
            if (actor.position.y<floor) {
                actor.position.y=floor;
                solveFloorImpactAngular(
                    body.velocity,body.angularVelocity,
                    1.0f/body.mass,inverseAngularInertia(body),
                    body.shape==ColliderShape::Sphere ? body.radius : 0.0f,
                    {0.0f,body.staticFriction,body.dynamicFriction});
            }
        }
    }

    struct Constraint {
        std::size_t first=0;
        std::size_t second=0;
        ContactCacheKey key{};
        ContactImpulseState impulse;
        PersistentContactState cached;
        glm::vec3 normal{1.0f,0.0f,0.0f};
        bool hasCached=false;
        bool counted=false;
    };
    std::vector<Constraint> constraints;
    constraints.reserve(broadphase.pairs.size());
    for (const auto& [first,second] : broadphase.pairs) {
        const Candidate& a=candidates[first];
        const Candidate& b=candidates[second];
        const ContactCacheKey key{
            a.slot,slots_[a.slot].generation,
            b.slot,slots_[b.slot].generation
        };
        Constraint constraint;
        constraint.first=first;
        constraint.second=second;
        constraint.key=key;
        const auto cached=contactCache_.find(key);
        if (cached!=contactCache_.end() &&
            cached->second.lastSeenStep+1==totalSteps_) {
            constraint.cached=cached->second;
            constraint.hasCached=true;
        }
        constraints.push_back(constraint);
    }

    // Sequential impulses communicate forces through chains of contacts.
    // Resting constraints can reuse the previous fixed step's accumulated
    // normal/tangent impulses before the eight Gauss-Seidel passes. A cached
    // contact is rejected if its normal changed or a fresh high-speed impact
    // needs restitution instead of resting-contact warm starting.
    constexpr int iterations=8;
    constexpr float positionSlop=0.0001f;
    constexpr float positionFactor=0.8f;
    constexpr float warmNormalAlignment=0.98f;
    for (int pass=0;pass<iterations;++pass) {
        for (Constraint& c : constraints) {
            Candidate& first=candidates[c.first];
            Candidate& second=candidates[c.second];
            RigidBodyComponent& a=*slots_[first.slot].body;
            RigidBodyComponent& b=*slots_[second.slot].body;
            const Contact contact=colliderContact(
                {a.shape,a.radius,a.halfExtents},first.position,
                {b.shape,b.radius,b.halfExtents},second.position);
            if (!contact.colliding) continue;
            c.normal=contact.normal;
            if (!c.counted) {
                if (a.shape==ColliderShape::Sphere &&
                    b.shape==ColliderShape::Sphere) ++sphereContacts_;
                else ++boxContacts_;
                c.counted=true;
            }
            const float invA=a.isStatic ? 0.0f : 1.0f/a.mass;
            const float invB=b.isStatic ? 0.0f : 1.0f/b.mass;
            const float total=invA+invB;
            if (total<=0.0f) continue;
            glm::vec3 velocityA=a.isStatic ? glm::vec3(0.0f) : a.velocity;
            glm::vec3 velocityB=b.isStatic ? glm::vec3(0.0f) : b.velocity;
            glm::vec3 angularA=a.isStatic
                ? glm::vec3(0.0f) : a.angularVelocity;
            glm::vec3 angularB=b.isStatic
                ? glm::vec3(0.0f) : b.angularVelocity;
            const float invInertiaA=inverseAngularInertia(a);
            const float invInertiaB=inverseAngularInertia(b);
            const glm::vec3 leverA=contactLeverArm(
                a,first.position,b,second.position,contact.normal,true);
            const glm::vec3 leverB=contactLeverArm(
                b,second.position,a,first.position,contact.normal,false);

            if (pass==0 && c.hasCached &&
                glm::dot(c.cached.normal,contact.normal)>=warmNormalAlignment) {
                const glm::vec3 pointA=contactPointVelocity(
                    velocityA,angularA,leverA);
                const glm::vec3 pointB=contactPointVelocity(
                    velocityB,angularB,leverB);
                const float closing=glm::dot(pointB-pointA,contact.normal);
                if (closing>=-0.5f) {
                    c.impulse.accumulatedNormal=c.cached.normalImpulse;
                    c.impulse.accumulatedTangent=c.cached.tangentImpulse;
                    c.impulse.reboundTarget=0.0f;
                    c.impulse.initialized=true;
                    c.impulse.persistentEligible=true;
                    warmStartContactImpulseAngular(
                        velocityA,angularA,velocityB,angularB,
                        invA,invB,invInertiaA,invInertiaB,
                        leverA,leverB,contact.normal,c.impulse);
                    ++contactWarmStarts_;
                }
            }

            solveContactImpulseAngular(
                velocityA,angularA,velocityB,angularB,
                invA,invB,invInertiaA,invInertiaB,
                leverA,leverB,contact.normal,
                {a.restitution,a.staticFriction,a.dynamicFriction},
                {b.restitution,b.staticFriction,b.dynamicFriction},
                c.impulse);
            if (!a.isStatic) {
                a.velocity=velocityA;
                a.angularVelocity=angularA;
            }
            if (!b.isStatic) {
                b.velocity=velocityB;
                b.angularVelocity=angularB;
            }

            const float overlap=std::max(contact.penetration-positionSlop,0.0f);
            const float correction=positionFactor*overlap/total;
            first.position-=contact.normal*(correction*invA);
            second.position+=contact.normal*(correction*invB);
        }

        for (Candidate& candidate : candidates) {
            RigidBodyComponent& body=*slots_[candidate.slot].body;
            if (body.isStatic) continue;
            const float floor=body.groundHeight+candidate.half.y;
            if (candidate.position.y<floor) {
                candidate.position.y=floor;
                solveFloorImpactAngular(
                    body.velocity,body.angularVelocity,
                    1.0f/body.mass,inverseAngularInertia(body),
                    body.shape==ColliderShape::Sphere ? body.radius : 0.0f,
                    {0.0f,body.staticFriction,body.dynamicFriction});
            }
        }
    }

    // Persist only resting contacts. Impacts with a restitution target are
    // deliberately not cached, preventing a large bounce impulse from being
    // replayed on the next fixed step. Entries not seen this step are pruned.
    for (const Constraint& c : constraints) {
        if (c.counted && c.impulse.initialized &&
            c.impulse.persistentEligible &&
            c.impulse.accumulatedNormal>0.0f) {
            contactCache_[c.key]={
                c.impulse.accumulatedNormal,
                c.impulse.accumulatedTangent,
                c.normal,
                totalSteps_
            };
        } else {
            contactCache_.erase(c.key);
        }
    }
    for (auto it=contactCache_.begin();it!=contactCache_.end();) {
        if (it->second.lastSeenStep!=totalSteps_)
            it=contactCache_.erase(it);
        else
            ++it;
    }

    for (const Candidate& candidate : candidates) {
        Slot& slot=slots_[candidate.slot];
        if (!slot.body->isStatic)
            slot.local[3]=glm::vec4(candidate.position,1.0f);
    }
    solverPasses_+=iterations;
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
