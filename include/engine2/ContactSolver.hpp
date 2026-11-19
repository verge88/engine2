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
};

inline void warmStartContactImpulse(
    glm::vec3& velocityA, glm::vec3& velocityB,
    float invMassA, float invMassB, glm::vec3 normal,
    ContactImpulseState& state) {

    if (!state.initialized || state.accumulatedNormal <= 0.0f) return;
    const glm::vec3 tangent =
        state.accumulatedTangent -
        glm::dot(state.accumulatedTangent,normal) * normal;
    state.accumulatedTangent = tangent;
    const glm::vec3 impulse =
        state.accumulatedNormal * normal + tangent;
    velocityA -= impulse * invMassA;
    velocityB += impulse * invMassB;
}

inline void solveContactImpulse(
    glm::vec3& velocityA, glm::vec3& velocityB,
    float invMassA, float invMassB, glm::vec3 normal,
    const ContactMaterial& materialA, const ContactMaterial& materialB,
    ContactImpulseState& state) {

    const float invMass = invMassA + invMassB;
    if (invMass <= 0.0f) return;
    if (!state.initialized) {
        const float initial = glm::dot(velocityB - velocityA, normal);
        // Resting contacts don't bounce under gravity-induced tiny speeds.
        const float restitution = std::min(
            materialA.restitution,materialB.restitution);
        state.reboundTarget = initial < -0.5f ? -initial * restitution : 0.0f;
        state.initialized = true;
    }

    const float closing = glm::dot(velocityB - velocityA,normal);
    const float wanted = (state.reboundTarget - closing) / invMass;
    const float newNormal = std::max(state.accumulatedNormal + wanted,0.0f);
    const float appliedNormal = newNormal - state.accumulatedNormal;
    state.accumulatedNormal = newNormal;
    const glm::vec3 impulse = appliedNormal * normal;
    velocityA -= impulse * invMassA;
    velocityB += impulse * invMassB;

    // Sequential Coulomb friction: clamp accumulated tangential impulse to
    // static cone or a dynamic cone, not individual per-iteration impulses.
    const float staticMu = std::sqrt(
        materialA.staticFriction * materialB.staticFriction);
    const float dynamicMu = std::sqrt(
        materialA.dynamicFriction * materialB.dynamicFriction);
    if (staticMu == 0.0f && dynamicMu == 0.0f) return;
    const glm::vec3 rel = velocityB - velocityA;
    const glm::vec3 tangentSpeed = rel - glm::dot(rel,normal) * normal;
    glm::vec3 proposed = state.accumulatedTangent - tangentSpeed / invMass;
    const float magnitude = glm::length(proposed);
    const float staticLimit = staticMu * newNormal;
    if (magnitude > staticLimit && magnitude > 1.0e-8f)
        proposed *= (dynamicMu * newNormal / magnitude);
    const glm::vec3 frictionImpulse = proposed - state.accumulatedTangent;
    state.accumulatedTangent = proposed;
    velocityA -= frictionImpulse * invMassA;
    velocityB += frictionImpulse * invMassB;
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
