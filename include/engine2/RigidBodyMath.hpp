#pragma once

#include "engine2/CollisionMath.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec3.hpp>

#include <cmath>

namespace engine2 {

inline glm::mat3 rigidBodyRotation(const glm::mat4& transform) {
    glm::vec3 x(transform[0]);
    glm::vec3 y(transform[1]);
    glm::vec3 zSource(transform[2]);

    const float xLength=glm::length(x);
    if (!(xLength>1.0e-7f) || !std::isfinite(xLength))
        return glm::mat3(1.0f);
    x/=xLength;

    y-=x*glm::dot(y,x);
    float yLength=glm::length(y);
    if (!(yLength>1.0e-7f) || !std::isfinite(yLength)) {
        const glm::vec3 fallback=
            std::abs(x.y)<0.9f ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
        y=fallback-x*glm::dot(fallback,x);
        yLength=glm::length(y);
    }
    y/=yLength;

    glm::vec3 z=glm::normalize(glm::cross(x,y));
    if (glm::dot(z,zSource)<0.0f) z=-z;
    y=glm::normalize(glm::cross(z,x));
    return glm::mat3(x,y,z);
}

inline bool axisAlignedRotation(const glm::mat3& rotation,float epsilon=1.0e-4f) {
    bool used[3]{false,false,false};
    for (int column=0;column<3;++column) {
        int dominant=0;
        float magnitude=std::abs(rotation[column][0]);
        for (int row=1;row<3;++row) {
            const float candidate=std::abs(rotation[column][row]);
            if (candidate>magnitude) {
                magnitude=candidate;
                dominant=row;
            }
        }
        if (magnitude<1.0f-epsilon || used[dominant]) return false;
        for (int row=0;row<3;++row)
            if (row!=dominant && std::abs(rotation[column][row])>epsilon)
                return false;
        used[dominant]=true;
    }
    return true;
}

inline glm::vec3 orientedBoxAabbHalf(
    glm::vec3 halfExtents,const glm::mat3& rotation) {
    return glm::abs(rotation[0])*halfExtents.x +
           glm::abs(rotation[1])*halfExtents.y +
           glm::abs(rotation[2])*halfExtents.z;
}

inline glm::mat3 inverseInertiaBody(
    ColliderShape shape,float mass,float radius,glm::vec3 halfExtents) {
    if (!(mass>0.0f) || !std::isfinite(mass)) return glm::mat3(0.0f);

    glm::vec3 diagonal(0.0f);
    if (shape==ColliderShape::Sphere) {
        const float inertia=0.4f*mass*radius*radius;
        if (inertia>1.0e-12f && std::isfinite(inertia))
            diagonal=glm::vec3(1.0f/inertia);
    } else {
        const float x=(mass/3.0f)*
            (halfExtents.y*halfExtents.y+halfExtents.z*halfExtents.z);
        const float y=(mass/3.0f)*
            (halfExtents.x*halfExtents.x+halfExtents.z*halfExtents.z);
        const float z=(mass/3.0f)*
            (halfExtents.x*halfExtents.x+halfExtents.y*halfExtents.y);
        if (x>1.0e-12f && y>1.0e-12f && z>1.0e-12f &&
            std::isfinite(x) && std::isfinite(y) && std::isfinite(z))
            diagonal=glm::vec3(1.0f/x,1.0f/y,1.0f/z);
    }

    glm::mat3 result(0.0f);
    result[0][0]=diagonal.x;
    result[1][1]=diagonal.y;
    result[2][2]=diagonal.z;
    return result;
}

inline glm::mat3 worldInverseInertia(
    ColliderShape shape,float mass,float radius,glm::vec3 halfExtents,
    const glm::mat3& rotation) {
    if (shape==ColliderShape::Sphere)
        return inverseInertiaBody(shape,mass,radius,halfExtents);
    const glm::mat3 body=
        inverseInertiaBody(shape,mass,radius,halfExtents);
    return rotation*body*glm::transpose(rotation);
}

} // namespace engine2
