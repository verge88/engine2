#include "engine2/Scene.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace {

void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

glm::mat4 translate(float x, float y = 0.0f, float z = 0.0f) {
    return glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z));
}

float x(const engine2::Scene& scene, engine2::Entity entity) {
    return scene.worldTransform(entity)[3][0];
}

engine2::RigidBodyComponent body(glm::vec3 velocity,
                                  float mass = 1.0f,
                                  float restitution = 1.0f) {
    engine2::RigidBodyComponent result;
    result.gravity = false;
    result.velocity = velocity;
    result.mass = mass;
    result.restitution = restitution;
    return result;
}

void advance(engine2::Scene& scene, int count) {
    for (int i = 0; i < count; ++i)
        (void)scene.advance(engine2::Scene::fixedDeltaSeconds);
}

void headOnEqualMass() {
    engine2::Scene scene;
    const auto a = scene.createEntity("left");
    const auto b = scene.createEntity("right");
    scene.setLocalTransform(a, translate(-0.8f));
    scene.setLocalTransform(b, translate(0.8f));
    scene.setRigidBody(a, body({2.0f, 0.0f, 0.0f}));
    scene.setRigidBody(b, body({-2.0f, 0.0f, 0.0f}));
    advance(scene, 15);
    check(scene.stats().sphereContacts > 0, "Equal-mass spheres never collided");
    check(scene.rigidBody(a).velocity.x < -1.9f &&
          scene.rigidBody(b).velocity.x > 1.9f,
          "Elastic head-on collision did not exchange velocities");
    check(x(scene, b) - x(scene, a) >= 0.9998f,
          "Spheres remain interpenetrating after correction");
}

void unequalMass() {
    engine2::Scene scene;
    const auto a = scene.createEntity("light");
    const auto b = scene.createEntity("heavy");
    scene.setLocalTransform(a, translate(-0.8f));
    scene.setLocalTransform(b, translate(0.4f));
    scene.setRigidBody(a, body({2.0f, 0.0f, 0.0f}, 1.0f));
    scene.setRigidBody(b, body({0.0f, 0.0f, 0.0f}, 3.0f));
    advance(scene, 9);
    check(scene.stats().sphereContacts > 0, "Unequal masses never collided");
    // 1D elastic impact: m1=1,m2=3,u1=2,u2=0 -> v1=-1, v2=1.
    check(std::abs(scene.rigidBody(a).velocity.x + 1.0f) < 0.001f &&
          std::abs(scene.rigidBody(b).velocity.x - 1.0f) < 0.001f,
          "Collision impulse did not respect inverse masses");
}

void againstStaticCollider() {
    engine2::Scene scene;
    const auto moving = scene.createEntity("moving");
    const auto obstacle = scene.createEntity("obstacle");
    scene.setLocalTransform(moving, translate(-0.9f));
    scene.setLocalTransform(obstacle, translate(0.5f));
    scene.setRigidBody(moving, body({3.0f, 0.0f, 0.0f}, 1.0f, 0.8f));
    auto fixed = body({0.0f, 0.0f, 0.0f}, 1.0f, 0.8f);
    fixed.isStatic = true;
    scene.setRigidBody(obstacle, fixed);
    advance(scene, 12);
    check(scene.stats().sphereContacts > 0, "Static collider was skipped");
    check(std::abs(x(scene, obstacle) - 0.5f) < 1.e-6f,
          "Static collider was moved by impact");
    check(scene.rigidBody(moving).velocity.x < -2.39f,
          "Static collider did not reflect moving body");
}

void coincidentAndSparse() {
    engine2::Scene coincident;
    const auto a = coincident.createEntity("same-a");
    const auto b = coincident.createEntity("same-b");
    coincident.setRigidBody(a, body({0.0f, 0.0f, 0.0f}));
    coincident.setRigidBody(b, body({0.0f, 0.0f, 0.0f}));
    advance(coincident, 1);
    check(coincident.stats().sphereContacts > 0,
          "Coincident centers never resolved");
    check(x(coincident, b) - x(coincident, a) >= 0.9998f,
          "Coincident centers produced NaN or interpenetration");

    engine2::Scene sparse;
    for (int i = 0; i < 50; ++i) {
        auto entity = sparse.createEntity();
        sparse.setLocalTransform(entity, translate(i * 5.0f));
        sparse.setRigidBody(entity, body({0.0f, 0.0f, 0.0f}));
    }
    advance(sparse, 1);
    check(sparse.stats().broadphasePairs == 0 &&
          sparse.stats().sphereContacts == 0,
          "Sweep-and-prune must reject disjoint x intervals");
}

void timestepAndValidation() {
    engine2::Scene a;
    engine2::Scene b;
    const auto e1 = a.createEntity();
    const auto e2 = b.createEntity();
    a.setLocalTransform(e1, translate(0, 3.0f));
    b.setLocalTransform(e2, translate(0, 3.0f));
    auto falling = body({0.0f, 0.0f, 0.0f});
    falling.gravity = true;
    a.setRigidBody(e1, falling);
    b.setRigidBody(e2, falling);
    for (int i = 0; i < 60; ++i)
        (void)a.advance(1.0f / 60.0f);
    for (int i = 0; i < 30; ++i)
        (void)b.advance(1.0f / 30.0f);
    check(std::abs(a.worldTransform(e1)[3][1] -
                   b.worldTransform(e2)[3][1]) < 0.0001f,
          "Fixed-step physics changed with rendering frame grouping");
    check(a.stats().fixedSteps == 60 && b.stats().fixedSteps == 60,
          "Fixed-step count incorrect");

    for (float invalid : {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()}) {
        auto invalidBody = body({0.0f, 0.0f, 0.0f});
        invalidBody.mass = invalid;
        bool rejected = false;
        try { a.setRigidBody(e1, invalidBody); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "Invalid mass was accepted");
    }
    bool rejected = false;
    try { (void)a.advance(-0.01f); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Negative delta was accepted");
}

} // namespace

int main() {
    headOnEqualMass();
    unequalMass();
    againstStaticCollider();
    coincidentAndSparse();
    timestepAndValidation();
}
