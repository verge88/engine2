#pragma once

#include "engine2/CollisionMath.hpp"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace engine2 {

// Fraction of the current fixed-step displacement [0,1], with a unit normal
// pointing FROM the moving sphere TOWARD its static obstacle.
// A moving sphere's center is swept against a rounded AABB (the exact
// Minkowski sum of an axis-aligned box and a sphere). Sphere/sphere is
// analytically exact for stationary or moving centers.
enum class SweptContactFeature : std::uint8_t {
    Sphere, Face, Edge, Corner
};

struct SweptSphereHit {
    float fraction = 1.0f;
    glm::vec3 normal{1.0f, 0.0f, 0.0f};
    SweptContactFeature feature = SweptContactFeature::Sphere;
};

// Exact first time of impact for the ray-like motion of a sphere center
// against an axis-aligned BOX + radius Minkowski sum. That boundary is made
// of 6 planar faces, 12 cylindrical edges and 8 spherical corners. A simple
// expanded-AABB slab test is NOT exact for a sphere near an edge/corner.
//
// Enumerate roots for all three types and only accept a candidate lying on
// the actual rounded volume (distance to nearest box point == radius) and
// approached by the motion. Double precision here avoids losing tangential
// roots to float cancellation near corners at ordinary world coordinates.
// Normal points FROM moving sphere TO static box, like sphereBoxContact().
inline std::optional<SweptSphereHit> sweepSphereRoundedBox(
    glm::vec3 start, glm::vec3 end, float radius,
    glm::vec3 center, glm::vec3 halfExtents) {
    if (!(radius>0.0f) || !std::isfinite(radius))
        return std::nullopt;
    const glm::dvec3 origin=glm::dvec3(start)-glm::dvec3(center);
    const glm::dvec3 travel=glm::dvec3(end)-glm::dvec3(start);
    const glm::dvec3 half(halfExtents);
    const double r=static_cast<double>(radius);
    if (glm::dot(travel,travel)<1.0e-18)
        return std::nullopt;
    const glm::dvec3 initialClosest=glm::clamp(origin,-half,half);
    if (glm::length(initialClosest-origin)<r-1.0e-9)
        return std::nullopt; // initial penetration goes to discrete solver

    std::optional<SweptSphereHit> best;
    const auto consider=[&](double rawTime,SweptContactFeature feature) {
        constexpr double epsilonTime=1.0e-8;
        if (!std::isfinite(rawTime) || rawTime < -epsilonTime ||
            rawTime > 1.0+epsilonTime) return;
        const double t=std::clamp(rawTime,0.0,1.0);
        const glm::dvec3 point=origin+travel*t;
        const glm::dvec3 nearest=glm::clamp(point,-half,half);
        const glm::dvec3 toBox=nearest-point;
        const double distance=glm::length(toBox);
        if (!(distance>1.0e-10) ||
            std::abs(distance-r)>std::max(2.0e-7,r*2.0e-7) ||
            glm::dot(travel,toBox)<=1.0e-10) return;
        if (!best || t<static_cast<double>(best->fraction)-1.0e-8)
            best=SweptSphereHit{
                static_cast<float>(t),glm::vec3(toBox/distance),feature
            };
    };

    const auto solveSurface=[&](double a,double b,double c,
                                SweptContactFeature feature) {
        if (!(a>1.0e-18)) return;
        const double discriminant=b*b-a*c;
        const double tolerance=1.0e-12*std::max(1.0,b*b+std::abs(a*c));
        if (discriminant < -tolerance) return;
        const double root=std::sqrt(std::max(0.0,discriminant));
        consider((-b-root)/a,feature);
        consider((-b+root)/a,feature);
    };

    // Planar face patches: cross x/y/z at +/- (half + radius).
    for (int axis=0;axis<3;++axis) {
        if (std::abs(travel[axis])<1.0e-14) continue;
        for (double side : {-1.0,1.0})
            consider((side*(half[axis]+r)-origin[axis])/travel[axis],
                     SweptContactFeature::Face);
    }

    // Four edge cylinders per free axis, 12 in total.
    for (int freeAxis=0;freeAxis<3;++freeAxis) {
        const int i=(freeAxis+1)%3;
        const int j=(freeAxis+2)%3;
        for (double signI : {-1.0,1.0}) {
            for (double signJ : {-1.0,1.0}) {
                const double di=origin[i]-signI*half[i];
                const double dj=origin[j]-signJ*half[j];
                const double a=travel[i]*travel[i]+travel[j]*travel[j];
                const double b=di*travel[i]+dj*travel[j];
                const double c=di*di+dj*dj-r*r;
                solveSurface(a,b,c,SweptContactFeature::Edge);
            }
        }
    }

    // Eight corner spheres. False feature extensions from planes/cylinders
    // are rejected above using the exact point-to-box distance.
    for (double sx : {-1.0,1.0}) {
        for (double sy : {-1.0,1.0}) {
            for (double sz : {-1.0,1.0}) {
                const glm::dvec3 diff=origin-glm::dvec3(
                    sx*half.x,sy*half.y,sz*half.z);
                solveSurface(glm::dot(travel,travel),
                             glm::dot(diff,travel),
                             glm::dot(diff,diff)-r*r,
                             SweptContactFeature::Corner);
            }
        }
    }
    return best;
}

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

    if (obstacle.shape==ColliderShape::Box)
        return sweepSphereRoundedBox(
            start,end,radius,center,obstacle.halfExtents);
    return std::nullopt;
}


 // Analytic relative-motion sweep between two MOVING spheres. Both centers
 // travel linearly over the same fixed-step remainder, and the returned
 // normal always points from A toward B at the earliest point of contact.
 // Already intersecting starts are handled by the ordinary contact solver.
inline std::optional<SweptSphereHit> sweepSphereMoving(
    glm::vec3 startA, glm::vec3 endA, float radiusA,
    glm::vec3 startB, glm::vec3 endB, float radiusB) {
    const glm::vec3 separation=startB-startA;
    const glm::vec3 relative=(endB-startB)-(endA-startA);
    const float lengthSquared=glm::dot(relative,relative);
    const float combined=radiusA+radiusB;
    const float c=glm::dot(separation,separation)-combined*combined;
    if (!(lengthSquared>1.0e-12f) || !std::isfinite(lengthSquared) ||
        !(c>0.0f) || !std::isfinite(c)) return std::nullopt;
    const float b=glm::dot(separation,relative);
    if (b>=0.0f) return std::nullopt; // separated or moving apart
    const float discriminant=b*b-lengthSquared*c;
    if (!(discriminant>=0.0f) || !std::isfinite(discriminant))
        return std::nullopt;
    const float time=(-b-std::sqrt(discriminant))/lengthSquared;
    // Quadratic roots can round a true end-of-step touch to 1+1ULP.
    // Accept only a tiny tolerance and clamp it back into the interval.
    constexpr float timeTolerance=1.0e-6f;
    if (time < -timeTolerance || time > 1.0f+timeTolerance)
        return std::nullopt;
    const float fraction=glm::clamp(time,0.0f,1.0f);
    const glm::vec3 difference=separation+relative*fraction;
    const float distance=glm::length(difference);
    if (!(distance>1.0e-7f) || !std::isfinite(distance))
        return std::nullopt;
    return SweptSphereHit{fraction,difference/distance};
}


// Exact relative-motion sweep for two translation-only axis-aligned boxes.
// The center of A is swept against B expanded by A's half extents. Normal
// points from A toward B at the first touching slab.
inline std::optional<SweptSphereHit> sweepBoxMoving(
    glm::vec3 startA, glm::vec3 endA, glm::vec3 halfA,
    glm::vec3 startB, glm::vec3 endB, glm::vec3 halfB) {
    if (boxBoxContact(startA,halfA,startB,halfB).colliding)
        return std::nullopt; // initial penetration belongs to discrete solver

    const glm::dvec3 offset=glm::dvec3(startA)-glm::dvec3(startB);
    const glm::dvec3 relative=
        (glm::dvec3(endA)-glm::dvec3(startA))-
        (glm::dvec3(endB)-glm::dvec3(startB));
    const glm::dvec3 expanded=glm::dvec3(halfA)+glm::dvec3(halfB);
    if (glm::dot(relative,relative)<1.0e-18)
        return std::nullopt;

    double enter=-std::numeric_limits<double>::infinity();
    double exit=std::numeric_limits<double>::infinity();
    int enterAxis=-1;
    double enterSign=0.0;
    constexpr double slabEpsilon=1.0e-12;

    for (int axis=0;axis<3;++axis) {
        if (std::abs(relative[axis])<1.0e-14) {
            if (offset[axis] < -expanded[axis]-slabEpsilon ||
                offset[axis] >  expanded[axis]+slabEpsilon)
                return std::nullopt;
            continue;
        }

        double nearTime;
        double farTime;
        double normalSign;
        if (relative[axis]>0.0) {
            nearTime=(-expanded[axis]-offset[axis])/relative[axis];
            farTime=( expanded[axis]-offset[axis])/relative[axis];
            normalSign=1.0;
        } else {
            nearTime=( expanded[axis]-offset[axis])/relative[axis];
            farTime=(-expanded[axis]-offset[axis])/relative[axis];
            normalSign=-1.0;
        }

        if (nearTime>enter) {
            enter=nearTime;
            enterAxis=axis;
            enterSign=normalSign;
        }
        exit=std::min(exit,farTime);
        if (enter>exit+slabEpsilon)
            return std::nullopt;
    }

    constexpr double timeTolerance=1.0e-8;
    if (enterAxis<0 || !std::isfinite(enter) ||
        enter < -timeTolerance || enter > 1.0+timeTolerance ||
        exit < -timeTolerance)
        return std::nullopt;

    glm::vec3 normal(0.0f);
    normal[enterAxis]=static_cast<float>(enterSign);
    if (glm::dot(relative,glm::dvec3(normal))<=1.0e-12)
        return std::nullopt;
    return SweptSphereHit{
        static_cast<float>(std::clamp(enter,0.0,1.0)),
        normal,SweptContactFeature::Face
    };
}

// Generic translation-only CCD for the current collider set. Relative motion
// makes moving sphere/box pairs identical to the exact rounded-box sweep from
// v2.4; AABB/AABB uses the exact Minkowski-expanded slab sweep above.
inline std::optional<SweptSphereHit> sweepColliderMoving(
    glm::vec3 startA, glm::vec3 endA, const ColliderGeometry& a,
    glm::vec3 startB, glm::vec3 endB, const ColliderGeometry& b) {
    if (colliderContact(a,startA,b,startB).colliding)
        return std::nullopt;

    if (a.shape==ColliderShape::Sphere && b.shape==ColliderShape::Sphere)
        return sweepSphereMoving(
            startA,endA,a.radius,startB,endB,b.radius);

    if (a.shape==ColliderShape::Sphere && b.shape==ColliderShape::Box) {
        const glm::vec3 relativeEnd=
            startA+(endA-startA)-(endB-startB);
        return sweepSphereRoundedBox(
            startA,relativeEnd,a.radius,startB,b.halfExtents);
    }

    if (a.shape==ColliderShape::Box && b.shape==ColliderShape::Sphere) {
        const glm::vec3 relativeEnd=
            startB+(endB-startB)-(endA-startA);
        auto hit=sweepSphereRoundedBox(
            startB,relativeEnd,b.radius,startA,a.halfExtents);
        if (hit) hit->normal=-hit->normal;
        return hit;
    }

    return sweepBoxMoving(
        startA,endA,a.halfExtents,startB,endB,b.halfExtents);
}

} // namespace engine2
