#include "engine2/SceneIO.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

void check(bool good, const char* message) {
    if (!good) throw std::runtime_error(message);
}

glm::vec3 worldPosition(const engine2::Scene& scene, engine2::Entity e) {
    return glm::vec3(scene.worldTransform(e)[3]);
}

void replaceFirst(std::string& source, const std::string& old,
                  const std::string& replacement) {
    const auto position = source.find(old);
    if (position == std::string::npos)
        throw std::runtime_error("Unit fixture did not contain expected JSON fragment");
    source.replace(position, old.size(), replacement);
}

template <typename Callable>
void expectFailure(Callable&& operation, const char* reason) {
    bool rejected = false;
    try { operation(); } catch (const std::exception&) { rejected = true; }
    check(rejected, reason);
}

} // namespace

int main() {
    using namespace engine2;

    SceneAssetRegistry assets;
    expectFailure([&] { (void)assets.mesh("missing"); },
                  "Missing mesh key was not rejected");
    expectFailure([&] { (void)assets.texture("missing"); },
                  "Missing texture key was not rejected");
    expectFailure([&] { assets.registerTexture("null", 0); },
                  "Null texture asset was accepted");
    assets.registerTexture("test/light-cookie", 381);
    check(assets.texture("test/light-cookie") == 381 &&
          assets.textureKey(381) == "test/light-cookie",
          "Stable texture key cannot be resolved");
    expectFailure([&] { assets.registerTexture("duplicate", 381); },
                  "Duplicate texture ID was accepted");

    Scene original;
    // Create child before parent: forward parent references need two passes.
    const Entity child = original.createEntity("Child before parent");
    const Entity parent = original.createEntity("Parent root");
    const Entity ball = original.createEntity("Physics sphere");

    const glm::mat4 parentMatrix = glm::translate(
        glm::mat4(1.0f), glm::vec3(4.0f, 2.0f, 1.0f));
    const glm::mat4 childMatrix = glm::translate(
        glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, -2.0f));
    original.setLocalTransform(parent, parentMatrix);
    original.setLocalTransform(child, childMatrix);
    original.setParent(child, parent);
    original.setEnabled(child, false);

    PointLight light;
    light.position = {0.0f, 0.4f, 0.0f};
    light.color = {0.1f, 0.2f, 0.9f};
    light.intensity = 27.0f;
    light.range = 13.0f;
    original.setPointLight(child, light);

    original.setLocalTransform(ball, glm::translate(
        glm::mat4(1.0f), glm::vec3(-1.2f, 5.0f, 2.0f)));
    RigidBodyComponent body;
    body.velocity = {0.5f, -1.0f, 0.0f};
    body.radius = 0.7f;
    body.mass = 3.5f;
    body.restitution = 0.85f;
    body.groundHeight = -0.8f;
    body.gravity = false;
    body.isStatic = true;
    original.setRigidBody(ball, body);

    // Deleted slots must not survive the serialized scene. Their stale
    // generation handles are specific to the original runtime only.
    const Entity removed = original.createEntity("Removed");
    original.destroyEntity(removed);
    check(!original.contains(removed), "Deleted scene entity stayed alive");

    const std::string serialized = serializeScene(original, assets);
    check(serialized.find("\"format\": \"engine2.scene\"") != std::string::npos,
          "Missing versioned scene schema marker");
    Scene restored = deserializeScene(serialized, assets);
    const auto entities = restored.entities();
    check(entities.size() == 3 && restored.stats().entities == 3,
          "Live entity count changed after scene JSON roundtrip");
    check(restored.name(entities[0]) == "Child before parent" &&
          restored.name(entities[1]) == "Parent root" &&
          restored.name(entities[2]) == "Physics sphere",
          "Entity names or ordering changed");
    check(restored.parent(entities[0]) == entities[1] && !restored.parent(entities[1]),
          "Forward parent-child relation was not reconstructed");
    check(!restored.enabled(entities[0]),
          "Disabled component visibility was not restored");
    check(glm::length(worldPosition(restored, entities[0]) -
                      glm::vec3(5.0f, 2.0f, -1.0f)) < 0.00001f,
          "World transform changed during JSON roundtrip");

    const PointLight* roundtripLight = restored.pointLightComponent(entities[0]);
    check(roundtripLight &&
          std::abs(roundtripLight->intensity - 27.0f) < 0.0001f &&
          glm::length(roundtripLight->color - light.color) < 0.0001f,
          "Point light color/intensity was not restored");
    check(restored.pointLights().empty(),
          "An inherited-disabled light became active after load");

    const auto* restoredBody = restored.rigidBodyComponent(entities[2]);
    check(restoredBody && restoredBody->isStatic &&
          std::abs(restoredBody->mass - 3.5f) < 0.0001f &&
          std::abs(restoredBody->radius - 0.7f) < 0.0001f &&
          std::abs(restoredBody->velocity.y + 1.0f) < 0.0001f,
          "Rigid-body physical state was not restored");
    check(restored.stats().fixedSteps == 0 &&
          original.stats().fixedSteps == 0,
          "JSON should only store authoring and live physics component state");
    check(serializeScene(restored, assets) == serialized,
          "Second JSON serialization does not match the original");

    // Validate filesystem roundtrip too, not only JSON-in-memory calls.
    const auto path = std::filesystem::current_path() / "engine2-scene-io-unit.json";
    saveScene(original, assets, path);
    Scene fromFile = loadScene(path, assets);
    check(serializeScene(fromFile, assets) == serialized,
          "Filesystem JSON load differs from in-memory deserialization");
    std::filesystem::remove(path);

    std::string unsupported = serialized;
    replaceFirst(unsupported, "\"version\": 1", "\"version\": 999");
    expectFailure([&] { (void)deserializeScene(unsupported, assets); },
                  "Unknown scene JSON version was accepted");

    std::string cycle = serialized;
    replaceFirst(cycle, "\"parent\": null", "\"parent\": 0");
    expectFailure([&] { (void)deserializeScene(cycle, assets); },
                  "Cyclic entity hierarchy was accepted");

    std::string missingParent = serialized;
    replaceFirst(missingParent, "\"parent\": 1", "\"parent\": 123456");
    expectFailure([&] { (void)deserializeScene(missingParent, assets); },
                  "Out-of-range parent index was accepted");

    std::string invalidMatrix = serialized;
    // Replace entire 16-element matrix with a malformed length-3 array.
    const std::size_t local = invalidMatrix.find("\"local\": [");
    check(local != std::string::npos, "Local matrix absent from JSON");
    const std::size_t finish = invalidMatrix.find("]", local);
    invalidMatrix.replace(local, finish - local + 1, "\"local\": [1,2,3]");
    expectFailure([&] { (void)deserializeScene(invalidMatrix, assets); },
                  "Malformed matrix was accepted");

    return 0;
}
