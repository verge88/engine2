#pragma once

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace engine2 {

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
    // Representative world-space contact point. Single-point contacts are
    // intentional; clipped multi-point OBB manifolds remain a later stage.
    glm::vec3 point{0.0f};
};

struct OrientedBox {
    glm::vec3 center{0.0f};
    glm::mat3 rotation{1.0f}; // orthonormal body axes in columns
    glm::vec3 halfExtents{0.5f};
};

inline glm::vec3 colliderHalfExtents(const ColliderGeometry& geometry) {
    return geometry.shape == ColliderShape::Box
        ? geometry.halfExtents : glm::vec3(geometry.radius);
}

inline glm::vec3 orientedBoxLocalPoint(
    const OrientedBox& box,glm::vec3 worldPoint) {
    const glm::vec3 offset=worldPoint-box.center;
    return {
        glm::dot(offset,box.rotation[0]),
        glm::dot(offset,box.rotation[1]),
        glm::dot(offset,box.rotation[2])
    };
}

inline glm::vec3 orientedBoxWorldPoint(
    const OrientedBox& box,glm::vec3 localPoint) {
    return box.center +
        box.rotation[0]*localPoint.x +
        box.rotation[1]*localPoint.y +
        box.rotation[2]*localPoint.z;
}

inline glm::vec3 orientedBoxFeatureSupport(
    const OrientedBox& box,glm::vec3 direction) {
    glm::vec3 result=box.center;
    constexpr float featureEpsilon=1.0e-6f;
    for (int axis=0;axis<3;++axis) {
        const float projection=glm::dot(direction,box.rotation[axis]);
        if (projection>featureEpsilon)
            result+=box.rotation[axis]*box.halfExtents[axis];
        else if (projection<-featureEpsilon)
            result-=box.rotation[axis]*box.halfExtents[axis];
    }
    return result;
}

inline float orientedBoxProjectedRadius(
    const OrientedBox& box,glm::vec3 axis) {
    return
        box.halfExtents.x*std::abs(glm::dot(axis,box.rotation[0])) +
        box.halfExtents.y*std::abs(glm::dot(axis,box.rotation[1])) +
        box.halfExtents.z*std::abs(glm::dot(axis,box.rotation[2]));
}

inline Contact sphereSphereContact(glm::vec3 a, float radiusA,
                                   glm::vec3 b, float radiusB) {
    const glm::vec3 offset = b - a;
    const float distanceSquared = glm::dot(offset, offset);
    const float radius = radiusA + radiusB;
    if (distanceSquared >= radius * radius) return {};
    const float distance = std::sqrt(distanceSquared);
    const glm::vec3 normal=distance > 1.0e-6f
        ? offset / distance : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 point=0.5f*
        ((a+normal*radiusA)+(b-normal*radiusB));
    return {true,normal,radius-distance,point};
}

// Normal points from the SPHERE toward the BOX so the impulse routine can
// always use one convention. The inside-box branch selects the nearest face
// and uses the physical face point even though the correction normal points
// in the opposite direction.
inline Contact sphereOrientedBoxContact(
    glm::vec3 sphere,float radius,const OrientedBox& box) {
    const glm::vec3 local=orientedBoxLocalPoint(box,sphere);
    const glm::vec3 closest=glm::clamp(
        local,-box.halfExtents,box.halfExtents);
    const glm::vec3 boxPoint=orientedBoxWorldPoint(box,closest);
    const glm::vec3 delta=boxPoint-sphere;
    const float distanceSquared=glm::dot(delta,delta);
    if (distanceSquared>1.0e-12f) {
        if (distanceSquared>=radius*radius) return {};
        const float distance=std::sqrt(distanceSquared);
        const glm::vec3 normal=delta/distance;
        const glm::vec3 spherePoint=sphere+normal*radius;
        return {true,normal,radius-distance,
                0.5f*(spherePoint+boxPoint)};
    }

    const glm::vec3 faceDistance=box.halfExtents-glm::abs(local);
    int axis=0;
    if (faceDistance.y<faceDistance.x) axis=1;
    if (faceDistance.z<faceDistance[axis]) axis=2;
    const float side=local[axis]>=0.0f ? 1.0f : -1.0f;
    glm::vec3 faceLocal=local;
    faceLocal[axis]=side*box.halfExtents[axis];
    const glm::vec3 facePoint=orientedBoxWorldPoint(box,faceLocal);
    const glm::vec3 normal=-side*box.rotation[axis];
    const glm::vec3 spherePoint=sphere-normal*radius;
    return {true,normal,radius+faceDistance[axis],
            0.5f*(spherePoint+facePoint)};
}

inline Contact sphereBoxContact(glm::vec3 sphere, float radius,
                                glm::vec3 box, glm::vec3 halfExtents) {
    return sphereOrientedBoxContact(
        sphere,radius,{box,glm::mat3(1.0f),halfExtents});
}

inline Contact orientedBoxContact(
    const OrientedBox& a,const OrientedBox& b) {
    const glm::vec3 delta=b.center-a.center;
    float minimum=std::numeric_limits<float>::infinity();
    glm::vec3 bestAxis(1.0f,0.0f,0.0f);

    const auto consider=[&](glm::vec3 rawAxis) {
        const float squared=glm::dot(rawAxis,rawAxis);
        if (squared<1.0e-10f) return true;
        const glm::vec3 axis=rawAxis/std::sqrt(squared);
        const float radiusA=orientedBoxProjectedRadius(a,axis);
        const float radiusB=orientedBoxProjectedRadius(b,axis);
        const float separation=std::abs(glm::dot(delta,axis));
        const float overlap=radiusA+radiusB-separation;
        if (overlap<=0.0f) return false;
        if (overlap<minimum) {
            minimum=overlap;
            bestAxis=glm::dot(delta,axis)>=0.0f ? axis : -axis;
        }
        return true;
    };

    for (int axis=0;axis<3;++axis)
        if (!consider(a.rotation[axis])) return {};
    for (int axis=0;axis<3;++axis)
        if (!consider(b.rotation[axis])) return {};
    for (int i=0;i<3;++i)
        for (int j=0;j<3;++j)
            if (!consider(glm::cross(a.rotation[i],b.rotation[j])))
                return {};

    const glm::vec3 pointA=orientedBoxFeatureSupport(a,bestAxis);
    const glm::vec3 pointB=orientedBoxFeatureSupport(b,-bestAxis);
    return {true,bestAxis,minimum,0.5f*(pointA+pointB)};
}

inline Contact boxBoxContact(glm::vec3 a, glm::vec3 halfA,
                             glm::vec3 b, glm::vec3 halfB) {
    return orientedBoxContact(
        {a,glm::mat3(1.0f),halfA},
        {b,glm::mat3(1.0f),halfB});
}

inline Contact colliderContactOriented(
    const ColliderGeometry& a,glm::vec3 positionA,const glm::mat3& rotationA,
    const ColliderGeometry& b,glm::vec3 positionB,const glm::mat3& rotationB) {
    if (a.shape==ColliderShape::Sphere && b.shape==ColliderShape::Sphere)
        return sphereSphereContact(positionA,a.radius,positionB,b.radius);
    if (a.shape==ColliderShape::Sphere && b.shape==ColliderShape::Box)
        return sphereOrientedBoxContact(
            positionA,a.radius,{positionB,rotationB,b.halfExtents});
    if (a.shape==ColliderShape::Box && b.shape==ColliderShape::Sphere) {
        Contact contact=sphereOrientedBoxContact(
            positionB,b.radius,{positionA,rotationA,a.halfExtents});
        contact.normal=-contact.normal;
        return contact;
    }
    return orientedBoxContact(
        {positionA,rotationA,a.halfExtents},
        {positionB,rotationB,b.halfExtents});
}

inline Contact colliderContact(const ColliderGeometry& a, glm::vec3 positionA,
                               const ColliderGeometry& b, glm::vec3 positionB) {
    return colliderContactOriented(
        a,positionA,glm::mat3(1.0f),
        b,positionB,glm::mat3(1.0f));
}

// Compatibility helper for the sphere-only angular stages and CCD path.
// Discrete OBB contacts use Contact::point directly for both lever arms.
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
        if (centerInside) direction = -direction;
    }
    return normal * (direction * body.radius);
}

} // namespace engine2
