#include "engine2/Scene.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

void check(bool good, const char* message) {
    if (!good) throw std::runtime_error(message);
}

glm::mat4 at(glm::vec3 position) {
    return glm::translate(glm::mat4(1.0f), position);
}

glm::vec3 position(const glm::mat4& matrix) {
    return glm::vec3(matrix[3]);
}

bool near(glm::vec3 a, glm::vec3 b, float tolerance = 0.0002f) {
    return glm::length(a - b) < tolerance;
}

} // namespace

int main() {
    using namespace engine2;

    Scene scene;
    const Entity root = scene.createEntity("root");
    const Entity child = scene.createEntity("child");
    const Entity other = scene.createEntity("other");
    check(scene.contains(root) && scene.name(child) == "child",
          "Entity handles or names broken");

    scene.setLocalTransform(root, at({3.0f, 1.0f, 0.0f}));
    scene.setLocalTransform(child, at({0.0f, 2.0f, 0.0f}));
    scene.setParent(child, root);
    check(near(position(scene.worldTransform(child)), {3.0f, 3.0f, 0.0f}),
          "Parent transform was not applied");

    bool rejected = false;
    try { scene.setParent(root, child); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected && !scene.parent(root),
          "Cyclic reparenting must be rejected without mutation");

    scene.setLocalTransform(other, at({12.0f, 0.0f, 0.0f}));
    scene.setParent(child, other, true);
    check(near(position(scene.worldTransform(child)), {3.0f, 3.0f, 0.0f}),
          "keepWorld reparenting must preserve original world transform");
    check(near(position(scene.localTransform(child)), {-9.0f, 3.0f, 0.0f}),
          "keepWorld local matrix incorrect");

    PointLight light;
    light.position = {1.0f, 0.5f, 0.0f};
    light.intensity = 20.0f;
    scene.setPointLight(child, light);
    const auto lights = scene.pointLights();
    check(lights.size() == 1 &&
          near(lights[0].position, {4.0f, 3.5f, 0.0f}),
          "Light component local position was not transformed");

    scene.setEnabled(other, false);
    check(scene.pointLights().empty(), "Disabled parent must hide lights");
    scene.setEnabled(other, true);
    check(scene.pointLights().size() == 1, "Re-enabling parent must reveal lights");

    rejected = false;
    try { scene.setRigidBody(child, RigidBodyComponent{}); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Parented physics objects must be explicitly forbidden");

    rejected = false;
    glm::mat4 singular(1.0f);
    singular[0][0] = 0.0f;
    scene.setLocalTransform(root, singular);
    try { scene.setParent(child, root, true); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected && scene.parent(child) == other,
          "Singular keepWorld parent must not mutate hierarchy");

    scene.destroyEntity(other);
    check(!scene.contains(other) && !scene.contains(child),
          "Destroying a parent must recursively destroy its descendants");
    rejected = false;
    try { (void)scene.worldTransform(child); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Destroyed handles must be invalid");
    const Entity reused = scene.createEntity("reused");
    check(scene.contains(reused) && !scene.contains(child),
          "Generation handles must reject reused-slot stale references");

    Scene framesA;
    Scene framesB;
    const Entity ballA = framesA.createEntity("fallingA");
    const Entity ballB = framesB.createEntity("fallingB");
    framesA.setLocalTransform(ballA, at({0.0f, 3.5f, 0.0f}));
    framesB.setLocalTransform(ballB, at({0.0f, 3.5f, 0.0f}));

    RigidBodyComponent physics;
    physics.radius = 0.5f;
    physics.groundHeight = -1.0f;
    physics.restitution = 0.4f;
    framesA.setRigidBody(ballA, physics);
    framesB.setRigidBody(ballB, physics);
    for (int i = 0; i < 60; ++i) (void)framesA.advance(1.0f / 60.0f);
    for (int i = 0; i < 30; ++i) (void)framesB.advance(1.0f / 30.0f);
    const float heightA = position(framesA.worldTransform(ballA)).y;
    const float heightB = position(framesB.worldTransform(ballB)).y;
    check(std::abs(heightA - heightB) < 0.0003f,
          "Fixed timestep must be independent of frame grouping");
    check(heightA >= -0.50001f && heightA < 3.5f,
          "Ground collision must keep sphere above the floor");
    check(framesA.stats().fixedSteps == 60 && framesB.stats().fixedSteps == 60,
          "Fixed-step simulation count must match simulated time");
    check(framesA.stats().rigidBodies == 1,
          "Rigid-body component counter incorrect");

    const std::uint32_t capped = framesA.advance(0.5f);
    check(capped == 8, "Long frame must cap physics work to eight steps");
    rejected = false;
    try { (void)framesA.advance(-1.0f); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Negative delta time must be rejected");

    return 0;
}
