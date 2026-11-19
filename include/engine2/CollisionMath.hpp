#pragma once

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <cstdint>

namespace engine2 {

// Translation-only collision geometry, in WORLD units. A box is always axis
// aligned; the visual mesh may be rotated, but its collider does not rotate.
enum class ColliderShape : std::uint8_t { Sphere = 0, Box = 1 };

struct ColliderGeometry {
    ColliderShape shape = ColliderShape::Sphere;
    float radius = 0.5f;
    glm::vec3 halfExtents{0.5f};
};

struct Contact {
    bool colliding = false;
    glm::vec3 normal{1.0f, 0.0f, 0.0f}; // unit vector from A toward B
    float penetration = 0.0f;
};

inline glm::vec3 colliderHalfExtents(const ColliderGeometry& geometry) {
    return geometry.shape == ColliderShape::Box
        ? geometry.halfExtents : glm::vec3(geometry.radius);
}

inline Contact sphereSphereContact(glm::vec3 a, float radiusA,
                                   glm::vec3 b, float radiusB) {
    const glm::vec3 offset = b - a;
    const float distanceSquared = glm::dot(offset, offset);
    const float radius = radiusA + radiusB;
    if (distanceSquared >= radius * radius) return {};
    const float distance = std::sqrt(distanceSquared);
    return {true, distance > 1.0e-6f ? offset / distance
                                   : glm::vec3(1.0f, 0.0f, 0.0f),
            radius - distance};
}

// Normal points from the SPHERE toward the BOX so the impulse routine can
// always use one convention. The inside-box branch selects the nearest face
// and pushes the sphere outward, avoiding a zero-length contact normal.
inline Contact sphereBoxContact(glm::vec3 sphere, float radius,
                                glm::vec3 box, glm::vec3 halfExtents) {
    const glm::vec3 local = sphere - box;
    const glm::vec3 closest = glm::clamp(
        local, -halfExtents, halfExtents);
    const glm::vec3 delta = closest - local;
    const float distanceSquared = glm::dot(delta, delta);
    if (distanceSquared > 1.0e-12f) {
        if (distanceSquared >= radius * radius) return {};
        const float distance = std::sqrt(distanceSquared);
        return {true, delta / distance, radius - distance};
    }

    // Center inside/on the box. The minimum-distance exit face is chosen
    // deterministically x, then y, then z on ties.
    const glm::vec3 faceDistance = halfExtents - glm::abs(local);
    int axis = 0;
    if (faceDistance.y < faceDistance.x) axis = 1;
    if (faceDistance.z < faceDistance[axis]) axis = 2;
    glm::vec3 normal(0.0f);
    normal[axis] = local[axis] >= 0.0f ? -1.0f : 1.0f;
    return {true, normal, radius + faceDistance[axis]};
}

inline Contact boxBoxContact(glm::vec3 a, glm::vec3 halfA,
                             glm::vec3 b, glm::vec3 halfB) {
    const glm::vec3 delta = b - a;
    const glm::vec3 overlap = halfA + halfB - glm::abs(delta);
    if (overlap.x <= 0.0f || overlap.y <= 0.0f || overlap.z <= 0.0f)
        return {};
    int axis = 0;
    if (overlap.y < overlap.x) axis = 1;
    if (overlap.z < overlap[axis]) axis = 2;
    glm::vec3 normal(0.0f);
    normal[axis] = delta[axis] >= 0.0f ? 1.0f : -1.0f;
    return {true, normal, overlap[axis]};
}

inline Contact colliderContact(const ColliderGeometry& a, glm::vec3 positionA,
                               const ColliderGeometry& b, glm::vec3 positionB) {
    if (a.shape == ColliderShape::Sphere && b.shape == ColliderShape::Sphere)
        return sphereSphereContact(positionA, a.radius, positionB, b.radius);
    if (a.shape == ColliderShape::Sphere && b.shape == ColliderShape::Box)
        return sphereBoxContact(positionA, a.radius, positionB, b.halfExtents);
    if (a.shape == ColliderShape::Box && b.shape == ColliderShape::Sphere) {
        Contact contact = sphereBoxContact(positionB, b.radius, positionA, a.halfExtents);
        contact.normal = -contact.normal;
        return contact;
    }
    return boxBoxContact(positionA, a.halfExtents, positionB, b.halfExtents);
}

// Lever arm from a spherical body's center to its physical contact point.
inline glm::vec3 colliderContactLeverArm(
    const ColliderGeometry& body, glm::vec3 position,
    const ColliderGeometry& other, glm::vec3 otherPosition,
    glm::vec3 normal, bool firstBody) {
    if (body.shape != ColliderShape::Sphere) return glm::vec3(0.0f);

    float direction = firstBody ? 1.0f : -1.0f;
    if (other.shape == ColliderShape::Box) {
        const glm::vec3 local = position - otherPosition;
        const bool centerInside =
            std::abs(local.x) <= other.halfExtents.x &&
            std::abs(local.y) <= other.halfExtents.y &&
            std::abs(local.z) <= other.halfExtents.z;
        // sphereBoxContact uses a correction normal for containment. The
        // nearest physical point on the sphere lies opposite that normal.
        if (centerInside) direction = -direction;
    }
    return normal * (direction * body.radius);
}

} // namespace engine2
