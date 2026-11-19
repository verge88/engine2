#pragma once

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace engine2 {

// Contact materials are dimensionless Coulomb coefficients. Defaults for
// legacy scenes are zero to preserve the pre-v2.1 frictionless simulation.
struct ContactMaterial {
    float restitution = 0.35f;
    float staticFriction = 0.0f;
    float dynamicFriction = 0.0f;
};

// One cache per broadphase pair for one fixed step. Holding the normal
// impulse across Gauss-Seidel passes prevents restitution from being
// erroneously cancelled by the next pass.
struct ContactImpulseState {
    float accumulatedNormal = 0.0f;
    glm::vec3 accumulatedTangent{0.0f};
    float reboundTarget = 0.0f;
    bool initialized = false;
    bool persistentEligible = false;
};

inline glm::vec3 contactPointVelocity(
    glm::vec3 velocity, glm::vec3 angularVelocity, glm::vec3 leverArm) {
    return velocity + glm::cross(angularVelocity,leverArm);
}

inline float directionalInverseMass(
    float invMassA, float invMassB,
    float invInertiaA, float invInertiaB,
    glm::vec3 leverArmA, glm::vec3 leverArmB,
    glm::vec3 direction) {
    const glm::vec3 angularA=glm::cross(leverArmA,direction);
    const glm::vec3 angularB=glm::cross(leverArmB,direction);
    return invMassA + invMassB +
        invInertiaA * glm::dot(angularA,angularA) +
        invInertiaB * glm::dot(angularB,angularB);
}

inline void applyContactImpulse(
    glm::vec3& velocityA, glm::vec3& angularVelocityA,
    glm::vec3& velocityB, glm::vec3& angularVelocityB,
    float invMassA, float invMassB,
    float invInertiaA, float invInertiaB,
    glm::vec3 leverArmA, glm::vec3 leverArmB,
    glm::vec3 impulse) {

    velocityA -= impulse * invMassA;
    velocityB += impulse * invMassB;
    angularVelocityA -=
        glm::cross(leverArmA,impulse) * invInertiaA;
    angularVelocityB +=
        glm::cross(leverArmB,impulse) * invInertiaB;
}

inline void warmStartContactImpulseAngular(
    glm::vec3& velocityA, glm::vec3& angularVelocityA,
    glm::vec3& velocityB, glm::vec3& angularVelocityB,
    float invMassA, float invMassB,
    float invInertiaA, float invInertiaB,
    glm::vec3 leverArmA, glm::vec3 leverArmB,
    glm::vec3 normal, ContactImpulseState& state) {

    if (!state.initialized || state.accumulatedNormal <= 0.0f) return;
    const glm::vec3 tangent =
        state.accumulatedTangent -
        glm::dot(state.accumulatedTangent,normal) * normal;
    state.accumulatedTangent = tangent;
    const glm::vec3 impulse =
        state.accumulatedNormal * normal + tangent;
    applyContactImpulse(
        velocityA,angularVelocityA,velocityB,angularVelocityB,
        invMassA,invMassB,invInertiaA,invInertiaB,
        leverArmA,leverArmB,impulse);
}

inline void solveContactImpulseAngular(
    glm::vec3& velocityA, glm::vec3& angularVelocityA,
    glm::vec3& velocityB, glm::vec3& angularVelocityB,
    float invMassA, float invMassB,
    float invInertiaA, float invInertiaB,
    glm::vec3 leverArmA, glm::vec3 leverArmB,
    glm::vec3 normal,
    const ContactMaterial& materialA, const ContactMaterial& materialB,
    ContactImpulseState& state) {

    const float normalInvMass=directionalInverseMass(
        invMassA,invMassB,invInertiaA,invInertiaB,
        leverArmA,leverArmB,normal);
    if (normalInvMass <= 0.0f) return;
    const glm::vec3 pointA=contactPointVelocity(
        velocityA,angularVelocityA,leverArmA);
    const glm::vec3 pointB=contactPointVelocity(
        velocityB,angularVelocityB,leverArmB);
    if (!state.initialized) {
        const float initial=glm::dot(pointB-pointA,normal);
        // Resting contacts don't bounce under gravity-induced tiny speeds.
        const float restitution=std::min(
            materialA.restitution,materialB.restitution);
        state.reboundTarget=initial < -0.5f ? -initial*restitution : 0.0f;
        state.persistentEligible=initial >= -0.5f;
        state.initialized=true;
    }

    const glm::vec3 currentA=contactPointVelocity(
        velocityA,angularVelocityA,leverArmA);
    const glm::vec3 currentB=contactPointVelocity(
        velocityB,angularVelocityB,leverArmB);
    const float closing=glm::dot(currentB-currentA,normal);
    const float wanted=(state.reboundTarget-closing)/normalInvMass;
    const float newNormal=std::max(state.accumulatedNormal+wanted,0.0f);
    const float appliedNormal=newNormal-state.accumulatedNormal;
    state.accumulatedNormal=newNormal;
    applyContactImpulse(
        velocityA,angularVelocityA,velocityB,angularVelocityB,
        invMassA,invMassB,invInertiaA,invInertiaB,
        leverArmA,leverArmB,appliedNormal*normal);

    // Sequential Coulomb friction uses contact-point relative velocity. For
    // spherical rotational inertia the tangent-plane effective mass is
    // isotropic because each lever arm is parallel to the contact normal.
    const float staticMu=std::sqrt(
        materialA.staticFriction*materialB.staticFriction);
    const float dynamicMu=std::sqrt(
        materialA.dynamicFriction*materialB.dynamicFriction);
    if (staticMu == 0.0f && dynamicMu == 0.0f) return;
    const glm::vec3 frictionPointA=contactPointVelocity(
        velocityA,angularVelocityA,leverArmA);
    const glm::vec3 frictionPointB=contactPointVelocity(
        velocityB,angularVelocityB,leverArmB);
    const glm::vec3 rel=frictionPointB-frictionPointA;
    const glm::vec3 tangentSpeed=rel-glm::dot(rel,normal)*normal;
    const float tangentMagnitude=glm::length(tangentSpeed);
    if (tangentMagnitude < 1.0e-8f) return;
    const glm::vec3 tangentDirection=tangentSpeed/tangentMagnitude;
    const float tangentInvMass=directionalInverseMass(
        invMassA,invMassB,invInertiaA,invInertiaB,
        leverArmA,leverArmB,tangentDirection);
    if (tangentInvMass <= 0.0f) return;
    glm::vec3 proposed=
        state.accumulatedTangent-tangentSpeed/tangentInvMass;
    const float magnitude=glm::length(proposed);
    const float staticLimit=staticMu*newNormal;
    if (magnitude > staticLimit && magnitude > 1.0e-8f)
        proposed *= (dynamicMu*newNormal/magnitude);
    const glm::vec3 frictionImpulse=proposed-state.accumulatedTangent;
    state.accumulatedTangent=proposed;
    applyContactImpulse(
        velocityA,angularVelocityA,velocityB,angularVelocityB,
        invMassA,invMassB,invInertiaA,invInertiaB,
        leverArmA,leverArmB,frictionImpulse);
}

inline void warmStartContactImpulse(
    glm::vec3& velocityA, glm::vec3& velocityB,
    float invMassA, float invMassB, glm::vec3 normal,
    ContactImpulseState& state) {

    glm::vec3 angularA(0.0f),angularB(0.0f);
    warmStartContactImpulseAngular(
        velocityA,angularA,velocityB,angularB,
        invMassA,invMassB,0.0f,0.0f,
        glm::vec3(0.0f),glm::vec3(0.0f),normal,state);
}

inline void solveContactImpulse(
    glm::vec3& velocityA, glm::vec3& velocityB,
    float invMassA, float invMassB, glm::vec3 normal,
    const ContactMaterial& materialA, const ContactMaterial& materialB,
    ContactImpulseState& state) {

    glm::vec3 angularA(0.0f),angularB(0.0f);
    solveContactImpulseAngular(
        velocityA,angularA,velocityB,angularB,
        invMassA,invMassB,0.0f,0.0f,
        glm::vec3(0.0f),glm::vec3(0.0f),normal,
        materialA,materialB,state);
}

// A ground plane is an infinite-mass static collider. This helper resolves
// one downward impact including a Coulomb friction impulse on the XZ plane.
// Caller performs positional clamping to groundHeight + collider half-height.
inline void solveFloorImpact(glm::vec3& velocity,
                             const ContactMaterial& body) {
    if (velocity.y >= 0.0f) return;
    const float impactSpeed = -velocity.y;
    float rebound = impactSpeed * body.restitution;
    if (rebound < 0.25f) rebound = 0.0f;
    velocity.y = rebound;
    const float normalDeltaVelocity = impactSpeed + rebound;

    const float horizontalSpeed = std::sqrt(
        velocity.x*velocity.x + velocity.z*velocity.z);
    if (horizontalSpeed < 1.0e-8f) return;
    if (horizontalSpeed <= body.staticFriction * normalDeltaVelocity) {
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        return;
    }
    const float retained = std::max(
        horizontalSpeed - body.dynamicFriction * normalDeltaVelocity,0.0f);
    const float ratio = retained / horizontalSpeed;
    velocity.x *= ratio;
    velocity.z *= ratio;
}

} // namespace engine2
