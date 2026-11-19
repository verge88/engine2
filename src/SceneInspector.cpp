#include "engine2/SceneInspector.hpp"

#include <json.hpp>

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine2 {
namespace {
using Json = nlohmann::json;

Json vectorJson(const glm::vec3& value) {
    return Json::array({value.x, value.y, value.z});
}

float numeric(const Json& data, float min, float max) {
    if (!data.is_number())
        throw std::invalid_argument("Scene edit expected a finite number");
    const double d = data.get<double>();
    if (!std::isfinite(d) || d < min || d > max)
        throw std::invalid_argument("Scene edit number out of range");
    return static_cast<float>(d);
}

glm::vec3 vec3(const Json& value, float minimum, float maximum) {
    if (!value.is_array() || value.size() != 3)
        throw std::invalid_argument("Scene edit expected a three-element array");
    return {numeric(value.at(0), minimum, maximum),
            numeric(value.at(1), minimum, maximum),
            numeric(value.at(2), minimum, maximum)};
}

std::size_t indexOf(const Json& value, std::size_t count) {
    if (!value.is_number_integer())
        throw std::invalid_argument("Scene edit entity index must be an integer");
    const std::int64_t index = value.get<std::int64_t>();
    if (index < 0 || static_cast<std::uint64_t>(index) >= count)
        throw std::invalid_argument("Scene edit entity index is out of range");
    return static_cast<std::size_t>(index);
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Cannot read scene edits: " + path.string());
    const auto count = input.tellg();
    if (count < 0 || count > 4 * 1024 * 1024)
        throw std::invalid_argument("Scene edit file exceeds 4 MiB limit");
    input.seekg(0);
    std::string data(static_cast<std::size_t>(count), '\0');
    input.read(data.data(), static_cast<std::streamsize>(data.size()));
    if (!input) throw std::runtime_error("Could not load scene edits: " + path.string());
    return data;
}

void writeText(const std::filesystem::path& path, std::string_view content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot write scene inspection: " + path.string());
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output) throw std::runtime_error("Writing scene inspection failed: " + path.string());
}

} // namespace

std::string inspectScene(const Scene& scene, const SceneAssetRegistry& assets) {
    const auto handles = scene.entities();
    std::unordered_map<std::uint32_t, std::size_t> ordinals;
    ordinals.reserve(handles.size());
    for (std::size_t i = 0; i < handles.size(); ++i)
        ordinals.emplace(handles[i].index, i);

    const SceneStats stats = scene.stats();
    Json document = {
        {"format", "engine2.scene-inspection"},
        {"version", 1},
        {"summary", {
            {"entities", stats.entities},
            {"renderables", stats.renderables},
            {"lights", stats.pointLights},
            {"rigidBodies", stats.rigidBodies},
            {"fixedSteps", stats.fixedSteps}
        }},
        {"entities", Json::array()}
    };
    for (std::size_t i = 0; i < handles.size(); ++i) {
        const Entity handle = handles[i];
        std::size_t depth = 0;
        bool effectiveEnabled = true;
        Json parent = nullptr;
        for (Entity current = handle; current; current = scene.parent(current)) {
            effectiveEnabled = effectiveEnabled && scene.enabled(current);
            if (current != handle) ++depth;
        }
        if (Entity p = scene.parent(handle)) {
            const auto it = ordinals.find(p.index);
            if (it == ordinals.end()) throw std::runtime_error("Invalid scene hierarchy");
            parent = it->second;
        }
        Json record = {
            {"index", i},
            {"name", scene.name(handle)},
            {"parent", parent},
            {"depth", depth},
            {"enabled", scene.enabled(handle)},
            {"effectiveEnabled", effectiveEnabled},
            {"localPosition", vectorJson(glm::vec3(scene.localTransform(handle)[3]))},
            {"worldPosition", vectorJson(glm::vec3(scene.worldTransform(handle)[3]))}
        };
        if (const auto* renderer = scene.rendererComponent(handle)) {
            record["renderer"] = {
                {"mesh", renderer->mesh ? Json(assets.meshKey(renderer->mesh)) : Json(nullptr)},
                {"albedo", vectorJson(renderer->material.albedo)},
                {"metallic", renderer->material.metallic},
                {"roughness", renderer->material.roughness}
            };
        }
        if (const auto* light = scene.pointLightComponent(handle)) {
            record["pointLight"] = {
                {"intensity", light->intensity},
                {"range", light->range},
                {"color", vectorJson(light->color)}
            };
        }
        if (const auto* body = scene.rigidBodyComponent(handle)) {
            record["rigidBody"] = {
                {"shape", body->shape == ColliderShape::Box ? "BOX" : "SPHERE"},
                {"mass", body->mass},
                {"restitution", body->restitution},
                {"staticFriction", body->staticFriction},
                {"dynamicFriction", body->dynamicFriction},
                {"isStatic", body->isStatic},
                {"velocity", vectorJson(body->velocity)}
            };
        }
        document["entities"].push_back(std::move(record));
    }
    return document.dump(2) + "\n";
}

void saveSceneInspection(const Scene& scene, const SceneAssetRegistry& assets,
                         const std::filesystem::path& path) {
    const std::string text = inspectScene(scene, assets);
    writeText(path, text);
}

std::size_t applySceneEdits(Scene& scene, std::string_view editsJson) {
    const Json root = Json::parse(editsJson.begin(), editsJson.end());
    if (!root.is_object() || root.at("format") != "engine2.scene-edits" ||
        root.at("version") != 1)
        throw std::invalid_argument("Unsupported engine2 scene-edit format/version");
    const Json& changes = root.at("operations");
    if (!changes.is_array() || changes.size() > 10000)
        throw std::invalid_argument("Invalid or oversized scene edit operations");

    Scene working = scene; // Atomicity: commit only after every operation succeeds.
    const auto handles = working.entities();

    for (const Json& change : changes) {
        if (!change.is_object())
            throw std::invalid_argument("Scene edit operation must be an object");
        const std::string op = change.at("op").get<std::string>();
        const Entity entity = handles[indexOf(change.at("entity"), handles.size())];
        if (!working.contains(entity))
            throw std::invalid_argument("Scene edit references an already deleted entity");

        if (op == "rename") {
            const std::string name = change.at("name").get<std::string>();
            working.setName(entity, name);
        } else if (op == "set-enabled") {
            working.setEnabled(entity, change.at("enabled").get<bool>());
        } else if (op == "set-position") {
            const glm::vec3 position = vec3(change.at("position"), -1.0e6f, 1.0e6f);
            const std::string space = change.value("space", std::string("local"));
            glm::mat4 local = working.localTransform(entity);
            if (space == "local") {
                local[3] = glm::vec4(position, 1.0f);
            } else if (space == "world") {
                glm::mat4 parentWorld(1.0f);
                if (Entity p = working.parent(entity))
                    parentWorld = working.worldTransform(p);
                if (std::abs(glm::determinant(parentWorld)) < 1.e-8f)
                    throw std::invalid_argument("World position edit requires invertible parent");
                const glm::vec4 translated = glm::inverse(parentWorld) *
                    glm::vec4(position, 1.0f);
                local[3] = glm::vec4(glm::vec3(translated), 1.0f);
            } else {
                throw std::invalid_argument("Unknown scene position space");
            }
            working.setLocalTransform(entity, local);
        } else if (op == "reparent") {
            Entity target{};
            if (!change.at("parent").is_null())
                target = handles[indexOf(change.at("parent"), handles.size())];
            working.setParent(entity, target, change.value("keepWorld", true));
        } else if (op == "set-albedo") {
            const MeshRendererComponent* current = working.rendererComponent(entity);
            if (!current)
                throw std::invalid_argument("Scene edit target has no MeshRenderer");
            MeshRendererComponent modified = *current;
            modified.material.albedo = vec3(change.at("color"), 0.0f, 1.0f);
            working.setRenderer(entity, modified);
        } else if (op == "set-light-intensity") {
            const PointLight* current = working.pointLightComponent(entity);
            if (!current)
                throw std::invalid_argument("Scene edit target has no PointLight");
            PointLight modified = *current;
            modified.intensity = numeric(change.at("intensity"), 0.0f, 1.0e6f);
            working.setPointLight(entity, modified);
        } else if (op == "set-body-friction") {
            const RigidBodyComponent* current = working.rigidBodyComponent(entity);
            if (!current)
                throw std::invalid_argument("Scene edit target has no RigidBody");
            RigidBodyComponent modified = *current;
            modified.staticFriction = numeric(change.at("staticFriction"),0.0f,4.0f);
            modified.dynamicFriction = numeric(change.at("dynamicFriction"),0.0f,4.0f);
            working.setRigidBody(entity,modified);
        } else if (op == "set-body-velocity") {
            const RigidBodyComponent* current = working.rigidBodyComponent(entity);
            if (!current)
                throw std::invalid_argument("Scene edit target has no RigidBody");
            RigidBodyComponent modified = *current;
            modified.velocity = vec3(change.at("velocity"), -1.0e5f, 1.0e5f);
            working.setRigidBody(entity, modified);
        } else if (op == "delete") {
            working.destroyEntity(entity);
        } else {
            throw std::invalid_argument("Unknown scene edit operation: " + op);
        }
    }
    scene = std::move(working);
    return changes.size();
}

std::size_t applySceneEditsFile(Scene& scene, const std::filesystem::path& path) {
    const std::string input = readText(path);
    return applySceneEdits(scene, input);
}

} // namespace engine2
