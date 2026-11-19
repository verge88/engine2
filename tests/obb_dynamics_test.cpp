#include "engine2/CollisionMath.hpp"
#include "engine2/ContactSolver.hpp"
#include "engine2/RigidBodyMath.hpp"
#include "engine2/Scene.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>

namespace {
void require(bool okay,const char* message) {
    if (!okay) throw std::runtime_error(message);
}
bool near(float a,float b,float epsilon=0.004f) {
    return std::abs(a-b)<=epsilon;
}
void expectFailure(auto&& operation,const char* message) {
    bool failed=false;
    try { operation(); } catch (const std::exception&) { failed=true; }
    require(failed,message);
}
}

int main() {
    using namespace engine2;

    // Solid box inertia for full dimensions 2*half:
    // Ixx = 1/3 m (hy^2 + hz^2), and cyclic permutations.
    const glm::vec3 half(1.0f,2.0f,3.0f);
    const glm::mat3 bodyInv=inverseInertiaBody(
        ColliderShape::Box,3.0f,0.5f,half);
    require(near(bodyInv[0][0],1.0f/13.0f) &&
            near(bodyInv[1][1],1.0f/10.0f) &&
            near(bodyInv[2][2],1.0f/5.0f),
            "Box body-space inverse inertia tensor is incorrect");

    glm::mat4 quarterTurn=glm::rotate(
        glm::mat4(1.0f),glm::half_pi<float>(),glm::vec3(0,0,1));
    const glm::mat3 rotation=rigidBodyRotation(quarterTurn);
    require(axisAlignedRotation(rotation),
            "90-degree signed-permutation box rotation should remain CCD-axis-aligned");
    const glm::vec3 worldHalf=orientedBoxAabbHalf(half,rotation);
    require(near(worldHalf.x,2.0f) && near(worldHalf.y,1.0f) &&
            near(worldHalf.z,3.0f),
            "Rotated OBB world AABB did not permute half extents");

    const glm::mat3 worldInv=worldInverseInertia(
        ColliderShape::Box,3.0f,0.5f,half,rotation);
    require(near(worldInv[0][0],1.0f/10.0f) &&
            near(worldInv[1][1],1.0f/13.0f) &&
            near(worldInv[2][2],1.0f/5.0f),
            "World inverse inertia did not rotate with the OBB");

    const glm::mat3 diagonalRotation=rigidBodyRotation(glm::rotate(
        glm::mat4(1.0f),glm::quarter_pi<float>(),glm::vec3(0,1,0)));
    require(!axisAlignedRotation(diagonalRotation),
            "45-degree OBB must not be treated as translation-only AABB CCD");

    // Sphere against a rotated OBB: place the center 0.3 beyond the +X face
    // in box-local space with radius 0.5 -> penetration is exactly 0.2.
    const OrientedBox obb{
        glm::vec3(0.0f),diagonalRotation,glm::vec3(1.0f,0.5f,0.5f)
    };
    const glm::vec3 sphereCenter=obb.center+obb.rotation[0]*1.3f;
    const Contact sphereBox=sphereOrientedBoxContact(
        sphereCenter,0.5f,obb);
    require(sphereBox.colliding && near(sphereBox.penetration,0.2f),
            "Sphere/OBB closest-point contact penetration is incorrect");
    require(glm::dot(sphereBox.normal,-obb.rotation[0])>0.999f,
            "Sphere/OBB normal does not follow the rotated face");

    const OrientedBox a{
        glm::vec3(0.0f),glm::mat3(1.0f),glm::vec3(1.0f,0.7f,0.5f)
    };
    const OrientedBox b{
        glm::vec3(1.15f,0.15f,0.0f),
        rigidBodyRotation(glm::rotate(
            glm::mat4(1.0f),glm::quarter_pi<float>(),glm::vec3(0,0,1))),
        glm::vec3(0.75f,0.45f,0.5f)
    };
    require(orientedBoxContact(a,b).colliding,
            "15-axis OBB SAT missed a rotated overlap");
    OrientedBox separated=b;
    separated.center=glm::vec3(4.0f,0.0f,0.0f);
    require(!orientedBoxContact(a,separated).colliding,
            "15-axis OBB SAT accepted separated boxes");

    // A normal impulse applied above a box center must generate angular speed.
    glm::vec3 velocityA(2.0f,0.0f,0.0f);
    glm::vec3 angularA(0.0f);
    glm::vec3 velocityB(0.0f);
    glm::vec3 angularB(0.0f);
    const glm::mat3 invBox=worldInverseInertia(
        ColliderShape::Box,2.0f,0.5f,glm::vec3(0.5f),glm::mat3(1.0f));
    ContactImpulseState impulse;
    solveContactImpulseTensor(
        velocityA,angularA,velocityB,angularB,
        0.5f,0.0f,invBox,glm::mat3(0.0f),
        glm::vec3(0.5f,0.4f,0.0f),glm::vec3(0.0f),
        glm::vec3(1.0f,0.0f,0.0f),
        {0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f},impulse);
    require(angularA.z>0.1f,
            "Off-center tensor contact impulse did not spin the box");

    // Scene integration accepts free-spinning discrete boxes and rotates their
    // actual collider transform, not just a visual-only mesh.
    {
        Scene scene;
        const Entity box=scene.createEntity("spinner box");
        RigidBodyComponent body;
        body.shape=ColliderShape::Box;
        body.halfExtents=glm::vec3(0.8f,0.4f,0.25f);
        body.gravity=false;
        body.groundHeight=-100.0f;
        body.angularVelocity=glm::vec3(0,glm::pi<float>(),0);
        scene.setRigidBody(box,body);
        for (int i=0;i<30;++i)
            (void)scene.advance(Scene::fixedDeltaSeconds);
        const glm::mat3 after=rigidBodyRotation(scene.worldTransform(box));
        require(std::abs(after[0].x)<0.02f &&
                std::abs(after[0].z)>0.98f,
                "Discrete box free-spin did not integrate 90 degrees");
    }

    // A glancing sphere hit transfers normal impulse at an off-center contact
    // and therefore gives the dynamic OBB angular velocity.
    {
        Scene scene;
        const Entity box=scene.createEntity("dynamic box");
        RigidBodyComponent boxBody;
        boxBody.shape=ColliderShape::Box;
        boxBody.halfExtents=glm::vec3(0.5f);
        boxBody.mass=2.0f;
        boxBody.gravity=false;
        boxBody.groundHeight=-100.0f;
        boxBody.restitution=0.0f;
        scene.setRigidBody(box,boxBody);

        const Entity sphere=scene.createEntity("glancing sphere");
        glm::mat4 sphereTransform=glm::translate(
            glm::mat4(1.0f),glm::vec3(-0.9f,0.35f,0.0f));
        scene.setLocalTransform(sphere,sphereTransform);
        RigidBodyComponent sphereBody;
        sphereBody.shape=ColliderShape::Sphere;
        sphereBody.radius=0.5f;
        sphereBody.gravity=false;
        sphereBody.groundHeight=-100.0f;
        sphereBody.restitution=0.0f;
        sphereBody.velocity=glm::vec3(3.0f,0.0f,0.0f);
        scene.setRigidBody(sphere,sphereBody);

        (void)scene.advance(Scene::fixedDeltaSeconds);
        require(std::abs(scene.rigidBody(box).angularVelocity.z)>0.05f,
                "Glancing Sphere/OBB scene contact did not spin the box");
    }

    // Rotating-box CCD is intentionally not claimed yet. Existing continuous
    // AABB support remains valid for axis-aligned, non-spinning boxes.
    {
        Scene scene;
        const Entity box=scene.createEntity("continuous rotated box");
        scene.setLocalTransform(box,glm::rotate(
            glm::mat4(1.0f),glm::quarter_pi<float>(),glm::vec3(0,1,0)));
        RigidBodyComponent body;
        body.shape=ColliderShape::Box;
        body.continuous=true;
        expectFailure([&] { scene.setRigidBody(box,body); },
                      "Rotated continuous Box should be rejected");
    }
    {
        Scene scene;
        const Entity box=scene.createEntity("continuous box");
        RigidBodyComponent body;
        body.shape=ColliderShape::Box;
        body.continuous=true;
        scene.setRigidBody(box,body);
        expectFailure([&] {
            scene.setLocalTransform(box,glm::rotate(
                glm::mat4(1.0f),glm::quarter_pi<float>(),glm::vec3(0,1,0)));
        },"Rotating a continuous Box transform should be rejected");
    }

    return 0;
}
