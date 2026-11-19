#pragma once

#include "engine2/CollisionMath.hpp"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace engine2 {

// Fraction of the current fixed-step displacement [0,1], with a unit normal
// pointing FROM the moving sphere TOWARD its static obstacle.
// Swept sphere/AABB uses the expanded box slab test (conservative at corners).
// Sphere/sphere is analytically exact for stationary target centers.
struct SweptSphereHit {
    float fraction = 1.0f;
    glm::vec3 normal{1.0f, 0.0f, 0.0f};
};

inline std::optional<SweptSphereHit> sweepSphereStatic(
    glm::vec3 start, glm::vec3 end, float radius,
    const ColliderGeometry& obstacle, glm::vec3 center) {

    const ColliderGeometry moving{ColliderShape::Sphere,radius,glm::vec3(radius)};
    if (colliderContact(moving,start,obstacle,center).colliding)
        return std::nullopt; // initial penetration belongs to discrete solver

    const glm::vec3 travel=end-start;
    const float squared=glm::dot(travel,travel);
    if (!(squared>1.0e-12f) || !std::isfinite(squared))
        return std::nullopt;

    if (obstacle.shape==ColliderShape::Sphere) {
        const float combined=radius+obstacle.radius;
        const glm::vec3 offset=start-center;
        const float b=glm::dot(offset,travel);
        const float c=glm::dot(offset,offset)-combined*combined;
        const float discriminant=b*b-squared*c;
        if (discriminant<0.0f || b>=0.0f) return std::nullopt;
        const float time=(-b-std::sqrt(discriminant))/squared;
        if (!(time>=0.0f && time<=1.0f)) return std::nullopt;
        const glm::vec3 delta=center-(start+travel*time);
        const float length=glm::length(delta);
        if (!(length>1.0e-7f)) return std::nullopt;
        return SweptSphereHit{time,delta/length};
    }

    if (obstacle.shape==ColliderShape::Box) {
        const glm::vec3 half=obstacle.halfExtents+glm::vec3(radius);
        const glm::vec3 minimum=center-half;
        const glm::vec3 maximum=center+half;
        float entry=0.0f;
        float exit=1.0f;
        int impactAxis=-1;
        for (int axis=0;axis<3;++axis) {
            if (std::abs(travel[axis])<=1.0e-10f) {
                if (start[axis]<minimum[axis] || start[axis]>maximum[axis])
                    return std::nullopt;
                continue;
            }
            const float first=(minimum[axis]-start[axis])/travel[axis];
            const float second=(maximum[axis]-start[axis])/travel[axis];
            const float nearTime=std::min(first,second);
            const float farTime=std::max(first,second);
            if (nearTime>entry) {
                entry=nearTime;
                impactAxis=axis;
            }
            exit=std::min(exit,farTime);
            if (entry>exit) return std::nullopt;
        }
        // Starting within the expanded AABB but outside the actual rounded
        // shape is ambiguous at corners; avoid making up a face normal.
        if (impactAxis<0 || entry<0.0f || entry>1.0f)
            return std::nullopt;
        glm::vec3 normal(0.0f);
        normal[impactAxis]=travel[impactAxis]>0.0f ? 1.0f : -1.0f;
        return SweptSphereHit{entry,normal};
    }
    return std::nullopt;
}

} // namespace engine2
