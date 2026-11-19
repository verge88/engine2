#include "engine2/ContinuousCollision.hpp"

#include <cmath>
#include <stdexcept>

namespace {
void require(bool okay, const char* message) {
    if (!okay) throw std::runtime_error(message);
}
bool close(float a, float b, float epsilon = 0.0004f) {
    return std::abs(a - b) < epsilon;
}
}

int main() {
    using namespace engine2;

    const ColliderGeometry wideBox{
        ColliderShape::Box, 0.5f, glm::vec3(0.5f)
    };
    const ColliderGeometry narrowBox{
        ColliderShape::Box, 0.5f, glm::vec3(0.25f, 0.5f, 0.5f)
    };

    // Translation-only AABB CCD is exact: sweep A's center against B expanded
    // by A's half extents. The first contact is x=-0.75.
    const auto staticBox = sweepColliderMoving(
        {-3,0,0}, {3,0,0}, wideBox,
        {0,0,0}, {0,0,0}, narrowBox);
    require(staticBox && close(staticBox->fraction, 0.375f) &&
            staticBox->feature == SweptContactFeature::Face &&
            close(staticBox->normal.x, 1.0f),
            "Moving box must hit static AABB at exact first contact");

    const auto reversed = sweepColliderMoving(
        {0,0,0}, {0,0,0}, narrowBox,
        {-3,0,0}, {3,0,0}, wideBox);
    require(reversed && close(reversed->fraction, staticBox->fraction) &&
            close(reversed->normal.x, -1.0f),
            "Reversing box order must preserve TOI and reverse normal");

    // Two fast dynamic boxes cross completely in one fixed-step displacement.
    const ColliderGeometry smallBox{
        ColliderShape::Box, 0.5f, glm::vec3(0.2f)
    };
    const auto headOn = sweepColliderMoving(
        {-2,0,0}, {3,0,0}, smallBox,
        {2,0,0}, {-3,0,0}, smallBox);
    require(headOn && close(headOn->fraction, 0.36f) &&
            close(headOn->normal.x, 1.0f),
            "Relative-motion box CCD must find head-on impact");

    require(!sweepColliderMoving(
        {-2,0,0}, {2,0,0}, smallBox,
        {2,0,0}, {6,0,0}, smallBox),
        "Equal box velocities must not create CCD contact");
    require(!sweepColliderMoving(
        {-2,1.1f,0}, {2,1.1f,0}, smallBox,
        {2,0,0}, {-2,0,0}, smallBox),
        "Separated box tracks falsely collided");
    require(!sweepColliderMoving(
        {0,0,0}, {2,0,0}, smallBox,
        {0.1f,0,0}, {-2,0,0}, smallBox),
        "Initially overlapping boxes must remain a discrete contact");

    const auto touchingInward = sweepColliderMoving(
        {-0.4f,0,0}, {0,0,0}, smallBox,
        {0,0,0}, {0,0,0}, smallBox);
    require(touchingInward && close(touchingInward->fraction, 0.0f) &&
            close(touchingInward->normal.x, 1.0f),
            "Touching AABBs moving inward need immediate TOI");
    require(!sweepColliderMoving(
        {-0.4f,0,0}, {-1,0,0}, smallBox,
        {0,0,0}, {0,0,0}, smallBox),
        "Touching AABBs moving apart must not collide");

    // Sphere/box uses relative motion too, preserving the exact rounded-box
    // corner from v2.4 even when the box is the first collider in the pair.
    const ColliderGeometry sphere{
        ColliderShape::Sphere, 0.3f, glm::vec3(0.3f)
    };
    const auto sphereToBox = sweepColliderMoving(
        {-2,0.7f,0.7f}, {2,0.7f,0.7f}, sphere,
        {0,0,0}, {0,0,0}, wideBox);
    const auto boxToSphere = sweepColliderMoving(
        {0,0,0}, {0,0,0}, wideBox,
        {-2,0.7f,0.7f}, {2,0.7f,0.7f}, sphere);
    require(sphereToBox && boxToSphere &&
            close(sphereToBox->fraction, 0.35f) &&
            close(boxToSphere->fraction, sphereToBox->fraction) &&
            sphereToBox->feature == SweptContactFeature::Corner &&
            boxToSphere->feature == SweptContactFeature::Corner &&
            close(boxToSphere->normal.x, -sphereToBox->normal.x) &&
            close(boxToSphere->normal.y, -sphereToBox->normal.y) &&
            close(boxToSphere->normal.z, -sphereToBox->normal.z),
            "Box/sphere CCD must preserve rounded-corner TOI and normal symmetry");

    const ColliderGeometry targetBox{
        ColliderShape::Box, 0.5f, glm::vec3(0.3f)
    };
    const auto fast = sweepColliderMoving(
        {-500,0,0}, {500,0,0}, smallBox,
        {0,0,0}, {0,0,0}, targetBox);
    require(fast && close(fast->fraction, 499.5f / 1000.0f, 1.0e-6f),
            "High-speed AABB CCD tunnelling regression");

    return 0;
}
