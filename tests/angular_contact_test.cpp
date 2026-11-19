#include "engine2/Scene.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>

namespace {
void require(bool okay,const char* message) {
    if (!okay) throw std::runtime_error(message);
}
engine2::Entity makeSphere(
    engine2::Scene& scene,glm::vec3 position,glm::vec3 velocity,
    bool continuous=false) {
    using namespace engine2;
    const Entity entity=scene.createEntity("sphere");
    scene.setLocalTransform(entity,glm::translate(glm::mat4(1.0f),position));
    RigidBodyComponent body;
    body.shape=ColliderShape::Sphere;
    body.radius=0.5f;
    body.mass=1.0f;
    body.gravity=false;
    body.groundHeight=-100.0f;
    body.restitution=0.0f;
    body.staticFriction=1.0f;
    body.dynamicFriction=0.7f;
    body.velocity=velocity;
    body.continuous=continuous;
    scene.setRigidBody(entity,body);
    return entity;
}
engine2::Entity makeStaticBox(
    engine2::Scene& scene,glm::vec3 position,glm::vec3 half={0.5f,0.5f,0.5f}) {
    using namespace engine2;
    const Entity entity=scene.createEntity("box");
    scene.setLocalTransform(entity,glm::translate(glm::mat4(1.0f),position));
    RigidBodyComponent body;
    body.shape=ColliderShape::Box;
    body.halfExtents=half;
    body.gravity=false;
    body.isStatic=true;
    body.restitution=0.0f;
    body.staticFriction=1.0f;
    body.dynamicFriction=0.7f;
    scene.setRigidBody(entity,body);
    return entity;
}
}

int main() {
    using namespace engine2;

    // Discrete sphere/AABB: tangential slip at the face should create spin.
    {
        Scene scene;
        const Entity sphere=makeSphere(scene,{0,0,0},{2,1,0});
        makeStaticBox(scene,{1,0,0});
        (void)scene.advance(Scene::fixedDeltaSeconds);
        const RigidBodyComponent& result=scene.rigidBody(sphere);
        require(result.angularVelocity.z < -0.1f,
                "Sphere/AABB friction did not generate collision torque");
        require(result.velocity.y > 0.0f && result.velocity.y < 1.0f,
                "Sphere/AABB friction did not share slip with rotation");
    }

    // Two moving spheres: equal/opposite tangential slip produces the same
    // rolling-direction spin on both contact surfaces.
    {
        Scene scene;
        const float dt=Scene::fixedDeltaSeconds;
        const Entity a=makeSphere(scene,{-0.48f,-dt,0},{1,1,0});
        const Entity b=makeSphere(scene,{ 0.48f, dt,0},{-1,-1,0});
        (void)scene.advance(dt);
        const RigidBodyComponent& bodyA=scene.rigidBody(a);
        const RigidBodyComponent& bodyB=scene.rigidBody(b);
        require(bodyA.angularVelocity.z < -0.1f &&
                bodyB.angularVelocity.z < -0.1f,
                "Sphere/sphere friction torque has wrong or missing spin");
        require(std::abs(bodyA.angularVelocity.z-bodyB.angularVelocity.z)<0.02f,
                "Symmetric sphere collision produced asymmetric spin");
    }

    // Deep sphere/AABB containment uses a correction normal whose sign is
    // opposite the physical nearest-face point. Verify that geometry directly;
    // after positional correction the sphere exits the face and the lever arm
    // legitimately flips to the outside contact side on later solver passes.
    {
        const ColliderGeometry sphereGeometry{
            ColliderShape::Sphere,0.25f,{0.5f,0.5f,0.5f}};
        const ColliderGeometry boxGeometry{
            ColliderShape::Box,0.5f,{0.5f,0.5f,0.5f}};
        const glm::vec3 spherePosition(0.30f,0,0);
        const glm::vec3 boxPosition(0.0f);
        const Contact contact=colliderContact(
            sphereGeometry,spherePosition,boxGeometry,boxPosition);
        require(contact.colliding && contact.normal.x < -0.9f,
                "Containment fixture did not select the expected exit face");
        const glm::vec3 lever=colliderContactLeverArm(
            sphereGeometry,spherePosition,boxGeometry,boxPosition,
            contact.normal,true);
        require(lever.x > 0.24f,
                "Contained sphere/AABB lever arm is not on the nearest face");

        Scene scene;
        const Entity sphere=makeSphere(scene,{0.30f,0,0},{-1,1,0});
        RigidBodyComponent small=scene.rigidBody(sphere);
        small.radius=0.25f;
        scene.setRigidBody(sphere,small);
        makeStaticBox(scene,{0,0,0},{0.5f,0.5f,0.5f});
        (void)scene.advance(Scene::fixedDeltaSeconds);
        require(std::abs(scene.rigidBody(sphere).angularVelocity.z) > 0.1f,
                "Contained sphere/AABB contact generated no friction torque");
    }

    // CCD path must apply the same angular impulse at time-of-impact rather
    // than tunnelling with unchanged angular velocity.
    {
        Scene scene;
        const Entity sphere=makeSphere(scene,{-2,0,0},{180,3,0},true);
        RigidBodyComponent fast=scene.rigidBody(sphere);
        fast.radius=0.25f;
        scene.setRigidBody(sphere,fast);
        makeStaticBox(scene,{0,0,0},{0.25f,0.5f,0.5f});
        (void)scene.advance(Scene::fixedDeltaSeconds);
        require(scene.stats().ccdHits>0,
                "CCD torque regression did not reach a time-of-impact event");
        require(scene.rigidBody(sphere).angularVelocity.z < -1.0f,
                "CCD friction impulse did not generate sphere spin");
    }

    // Ground friction also uses rotational effective mass: a sliding sphere
    // settles toward rolling instead of losing all horizontal COM velocity.
    {
        Scene scene;
        const Entity sphere=makeSphere(scene,{0,0.49f,0},{1,-1,0});
        RigidBodyComponent body=scene.rigidBody(sphere);
        body.groundHeight=0.0f;
        scene.setRigidBody(sphere,body);
        (void)scene.advance(Scene::fixedDeltaSeconds);
        const RigidBodyComponent& result=scene.rigidBody(sphere);
        require(result.angularVelocity.z < -0.1f,
                "Ground friction did not generate rolling spin");
        require(result.velocity.x>0.1f && result.velocity.x<1.0f,
                "Ground angular coupling removed an invalid amount of COM speed");
    }

    return 0;
}
