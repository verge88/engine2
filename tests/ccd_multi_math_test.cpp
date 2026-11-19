#include "engine2/ContinuousCollision.hpp"

#include <cmath>
#include <stdexcept>

namespace {
void check(bool valid,const char* message) {
    if (!valid) throw std::runtime_error(message);
}
bool close(float a,float b,float epsilon=0.0002f) {
    return std::abs(a-b)<epsilon;
}
}

int main() {
    using namespace engine2;

    // Both spheres cross their entire original separation in one 60 Hz step.
    const auto crossing=sweepSphereMoving(
        {-2,0,0},{3,0,0},0.2f, {2,0,0},{-3,0,0},0.2f);
    check(crossing && close(crossing->fraction,0.36f) &&
          close(crossing->normal.x,1.0f),
          "Relative-motion sphere CCD must select exact first impact");
    const auto swapped=sweepSphereMoving(
        {2,0,0},{-3,0,0},0.2f, {-2,0,0},{3,0,0},0.2f);
    check(swapped && close(swapped->fraction,crossing->fraction) &&
          close(swapped->normal.x,-1.0f),
          "Reversing body order must reverse CCD normal");

    // One moving actor against a stationary sphere is the static CCD case.
    const auto stationary=sweepSphereMoving(
        {-3,0,0},{3,0,0},0.2f,{0,0,0},{0,0,0},0.5f);
    const ColliderGeometry target{
        ColliderShape::Sphere,0.5f,glm::vec3(0.5f)
    };
    const auto reference=sweepSphereStatic(
        {-3,0,0},{3,0,0},0.2f,target,{0,0,0});
    check(stationary && reference &&
          close(stationary->fraction,reference->fraction) &&
          close(stationary->normal.x,reference->normal.x),
          "Moving/moving analytic sweep must match old static-sphere CCD");

    // No collision when both spheres move at the same velocity.
    check(!sweepSphereMoving(
        {-2,0,0},{2,0,0},0.2f,{2,0,0},{6,0,0},0.2f),
          "Equal velocities must not create relative-motion CCD");
    check(!sweepSphereMoving(
        {-2,0,0},{-3,0,0},0.2f,{2,0,0},{3,0,0},0.2f),
          "Moving-apart spheres falsely collided");
    check(!sweepSphereMoving(
        {-2,3,0},{2,3,0},0.2f,{2,0,0},{-2,0,0},0.2f),
          "Spatially separated tracks falsely intersected");
    check(!sweepSphereMoving(
        {0,0,0},{2,0,0},0.2f,{0.1f,0,0},{-2,0,0},0.2f),
          "Initially overlapping spheres must remain a discrete contact");

    // Grazing touch at the exact end of the interval.
    const auto atEnd=sweepSphereMoving(
        {-1,0,0},{-0.2f,0,0},0.1f,
        {1,0,0},{0.2f,0,0},0.1f);
    check(!atEnd, "Non-overlapping endpoint must not count as a contact");
    const auto touching=sweepSphereMoving(
        {-1,0,0},{-0.1f,0,0},0.1f,
        {1,0,0},{0.1f,0,0},0.1f);
    check(touching && close(touching->fraction,1.0f),
          "Touching sphere surfaces at end of sweep must count");
    return 0;
}
