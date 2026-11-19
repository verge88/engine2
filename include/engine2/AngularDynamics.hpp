#pragma once

#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace engine2 {

// Integrate a root rigid body's WORLD-axis angular velocity in radians/second.
// Translation is preserved exactly and the existing 3x3 basis (including
// authored visual scale) is rotated as a whole. Spheres and discrete OBBs use
// this path; continuous boxes remain translation-only until rotating OBB CCD.
inline glm::mat4 integrateAngularVelocity(
    glm::mat4 transform, glm::vec3 angularVelocity, float deltaSeconds) {
    const float speed=glm::length(angularVelocity);
    if (!(speed>1.0e-7f) || !(deltaSeconds>0.0f) ||
        !std::isfinite(speed) || !std::isfinite(deltaSeconds))
        return transform;

    const glm::vec4 translation=transform[3];
    transform[3]=glm::vec4(0,0,0,1);
    const glm::mat4 rotation=glm::rotate(
        glm::mat4(1.0f),speed*deltaSeconds,angularVelocity/speed);
    glm::mat4 result=rotation*transform;
    result[3]=translation;
    return result;
}

} // namespace engine2
