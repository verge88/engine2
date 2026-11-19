#include "engine2/SceneIO.hpp"

#include <json.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine2 {
namespace {

using Json = nlohmann::json;
constexpr std::size_t maxSceneEntities = 100000;

void requireKey(const std::string& key) {
    if (key.empty() || key.size() > 256)
        throw std::invalid_argument("Scene asset key must contain 1..256 characters");
}

float finiteFloat(const Json& data) {
    if (!data.is_number())
        throw std::invalid_argument("Scene JSON expected a numeric value");
    const double value = data.get<double>();
    if (!std::isfinite(value) ||
        std::abs(value) > static_cast<double>(std::numeric_limits<float>::max()))
        throw std::invalid_argument("Scene JSON contains a non-finite number");
    return static_cast<float>(value);
}

float finiteValue(float value) {
    if (!std::isfinite(value))
        throw std::invalid_argument("Cannot serialize non-finite scene values");
    return value;
}

Json writeVec3(const glm::vec3& v) {
    return Json::array({finiteValue(v.x), finiteValue(v.y), finiteValue(v.z)});
}

glm::vec3 readVec3(const Json& data) {
    if (!data.is_array() || data.size() != 3)
        throw std::invalid_argument("Scene JSON expected a vec3 array");
    return {finiteFloat(data.at(0)), finiteFloat(data.at(1)), finiteFloat(data.at(2))};
}

Json writeMat4(const glm::mat4& matrix) {
    Json data = Json::array();
    // Explicit column-major order matches GLM matrix memory layout.
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row)
            data.push_back(finiteValue(matrix[column][row]));
    }
    return data;
}

glm::mat4 readMat4(const Json& data) {
    if (!data.is_array() || data.size() != 16)
        throw std::invalid_argument("Scene JSON expected 16 matrix elements");
    glm::mat4 result(1.0f);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row)
            result[column][row] = finiteFloat(data.at(column * 4 + row));
    }
    return result;
}

Json textureReference(GLuint texture, const SceneAssetRegistry& assets) {
    if (texture == 0) return nullptr;
    return assets.textureKey(texture);
}

GLuint loadTextureReference(const Json& value, const SceneAssetRegistry& assets) {
    if (value.is_null()) return 0;
    if (!value.is_string())
        throw std::invalid_argument("Texture reference must be null or a key");
    return assets.texture(value.get<std::string>());
}

Json writeMaterial(const Material& material, const SceneAssetRegistry& assets) {
    const int alphaMode = static_cast<int>(material.alphaMode);
    if (alphaMode < 0 || alphaMode > 2)
        throw std::invalid_argument("Invalid material alpha mode");
    return Json{
        {"albedo", writeVec3(material.albedo)},
        {"metallic", finiteValue(material.metallic)},
        {"roughness", finiteValue(material.roughness)},
        {"alpha", finiteValue(material.alpha)},
        {"alphaCutoff", finiteValue(material.alphaCutoff)},
        {"alphaMode", alphaMode == 0 ? "OPAQUE" : alphaMode == 1 ? "MASK" : "BLEND"},
        {"emissive", writeVec3(material.emissive)},
        {"occlusionStrength", finiteValue(material.occlusionStrength)},
        {"normalScale", finiteValue(material.normalScale)},
        {"checker", material.checker},
        {"doubleSided", material.doubleSided},
        {"textures", {
            {"baseColor", textureReference(material.baseColorTexture, assets)},
            {"metallicRoughness", textureReference(material.metallicRoughnessTexture, assets)},
            {"normal", textureReference(material.normalTexture, assets)},
            {"occlusion", textureReference(material.occlusionTexture, assets)},
            {"emissive", textureReference(material.emissiveTexture, assets)}
        }}
    };
}

Material readMaterial(const Json& data, const SceneAssetRegistry& assets) {
    if (!data.is_object())
        throw std::invalid_argument("Expected material object");
    Material result;
    result.albedo = readVec3(data.at("albedo"));
    result.metallic = finiteFloat(data.at("metallic"));
    result.roughness = finiteFloat(data.at("roughness"));
    result.alpha = finiteFloat(data.at("alpha"));
    result.alphaCutoff = finiteFloat(data.at("alphaCutoff"));
    const std::string mode = data.at("alphaMode").get<std::string>();
    if (mode == "OPAQUE") result.alphaMode = AlphaMode::Opaque;
    else if (mode == "MASK") result.alphaMode = AlphaMode::Mask;
    else if (mode == "BLEND") result.alphaMode = AlphaMode::Blend;
    else throw std::invalid_argument("Unsupported alpha mode in Scene JSON");
    result.emissive = readVec3(data.at("emissive"));
    result.occlusionStrength = finiteFloat(data.at("occlusionStrength"));
    result.normalScale = finiteFloat(data.at("normalScale"));
    result.checker = data.at("checker").get<bool>();
    result.doubleSided = data.at("doubleSided").get<bool>();
    const Json& textures = data.at("textures");
    result.baseColorTexture = loadTextureReference(textures.at("baseColor"), assets);
    result.metallicRoughnessTexture =
        loadTextureReference(textures.at("metallicRoughness"), assets);
    result.normalTexture = loadTextureReference(textures.at("normal"), assets);
    result.occlusionTexture = loadTextureReference(textures.at("occlusion"), assets);
    result.emissiveTexture = loadTextureReference(textures.at("emissive"), assets);
    return result;
}

Json writeBody(const RigidBodyComponent& body) {
    return Json{
        {"velocity", writeVec3(body.velocity)},
        {"gravity", body.gravity},
        {"radius", finiteValue(body.radius)},
        {"groundHeight", finiteValue(body.groundHeight)},
        {"restitution", finiteValue(body.restitution)},
        {"staticFriction", finiteValue(body.staticFriction)},
        {"dynamicFriction", finiteValue(body.dynamicFriction)},
        {"mass", finiteValue(body.mass)},
        {"isStatic", body.isStatic},
        {"continuous", body.continuous},
        {"shape", body.shape == ColliderShape::Box ? "BOX" : "SPHERE"},
        {"halfExtents", writeVec3(body.halfExtents)}
    };
}

RigidBodyComponent readBody(const Json& data) {
    RigidBodyComponent result;
    result.velocity = readVec3(data.at("velocity"));
    result.gravity = data.at("gravity").get<bool>();
    result.radius = finiteFloat(data.at("radius"));
    result.groundHeight = finiteFloat(data.at("groundHeight"));
    result.restitution = finiteFloat(data.at("restitution"));
    // v1 schema remains compatible: older scene JSON lacks friction fields.
    if (data.contains("staticFriction"))
        result.staticFriction = finiteFloat(data.at("staticFriction"));
    if (data.contains("dynamicFriction"))
        result.dynamicFriction = finiteFloat(data.at("dynamicFriction"));
    result.mass = finiteFloat(data.at("mass"));
    result.isStatic = data.at("isStatic").get<bool>();
    // Older v1 scene JSON has no CCD flag: preserve discrete response.
    if (data.contains("continuous"))
        result.continuous = data.at("continuous").get<bool>();
    // Backward compatibility: v1 scenes saved by engine2 v1.2 do not have
    // shape/halfExtents. A missing shape is a sphere, as before.
    if (data.contains("shape")) {
        const std::string shape = data.at("shape").get<std::string>();
        if (shape == "BOX") result.shape = ColliderShape::Box;
        else if (shape == "SPHERE") result.shape = ColliderShape::Sphere;
        else throw std::invalid_argument("Unsupported physics collider shape");
    }
    if (data.contains("halfExtents"))
        result.halfExtents = readVec3(data.at("halfExtents"));
    return result;
}

Json writeLight(const PointLight& light) {
    return Json{
        {"position", writeVec3(light.position)},
        {"color", writeVec3(light.color)},
        {"intensity", finiteValue(light.intensity)},
        {"range", finiteValue(light.range)}
    };
}

PointLight readLight(const Json& data) {
    PointLight result;
    result.position = readVec3(data.at("position"));
    result.color = readVec3(data.at("color"));
    result.intensity = finiteFloat(data.at("intensity"));
    result.range = finiteFloat(data.at("range"));
    return result;
}

std::size_t readIndex(const Json& value, std::size_t count) {
    if (!value.is_number_integer())
        throw std::invalid_argument("Entity parent index must be an integer");
    const std::int64_t index = value.get<std::int64_t>();
    if (index < 0 || static_cast<std::uint64_t>(index) >= count)
        throw std::invalid_argument("Entity parent index is out of range");
    return static_cast<std::size_t>(index);
}

} // namespace

void SceneAssetRegistry::registerMesh(std::string key, const Mesh* mesh) {
    requireKey(key);
    if (!mesh || meshes_.contains(key) || meshKeys_.contains(mesh))
        throw std::invalid_argument("Duplicate or null Scene mesh asset");
    meshKeys_.emplace(mesh, key);
    meshes_.emplace(std::move(key), mesh);
}

void SceneAssetRegistry::registerTexture(std::string key, GLuint texture) {
    requireKey(key);
    if (texture == 0 || textures_.contains(key) || textureKeys_.contains(texture))
        throw std::invalid_argument("Duplicate or null Scene texture asset");
    textureKeys_.emplace(texture, key);
    textures_.emplace(std::move(key), texture);
}

const Mesh* SceneAssetRegistry::mesh(std::string_view key) const {
    const auto it = meshes_.find(std::string(key));
    if (it == meshes_.end())
        throw std::invalid_argument("Scene mesh asset not found: " + std::string(key));
    return it->second;
}

GLuint SceneAssetRegistry::texture(std::string_view key) const {
    const auto it = textures_.find(std::string(key));
    if (it == textures_.end())
        throw std::invalid_argument("Scene texture asset not found: " + std::string(key));
    return it->second;
}

const std::string& SceneAssetRegistry::meshKey(const Mesh* mesh) const {
    const auto it = meshKeys_.find(mesh);
    if (it == meshKeys_.end())
        throw std::invalid_argument("Scene mesh pointer is not registered");
    return it->second;
}

const std::string& SceneAssetRegistry::textureKey(GLuint texture) const {
    const auto it = textureKeys_.find(texture);
    if (it == textureKeys_.end())
        throw std::invalid_argument("Scene GPU texture is not registered");
    return it->second;
}

std::string serializeScene(const Scene& scene, const SceneAssetRegistry& assets) {
    const auto handles = scene.entities();
    if (handles.size() > maxSceneEntities)
        throw std::invalid_argument("Scene entity count exceeds JSON limit");

    std::unordered_map<std::uint32_t, std::size_t> indices;
    indices.reserve(handles.size());
    for (std::size_t i = 0; i < handles.size(); ++i)
        indices.emplace(handles[i].index, i);

    Json root = {
        {"format", "engine2.scene"},
        {"version", 1},
        {"entities", Json::array()}
    };

    for (Entity entity : handles) {
        Json node = {
            {"name", scene.name(entity)},
            {"enabled", scene.enabled(entity)},
            {"local", writeMat4(scene.localTransform(entity))},
            {"parent", nullptr}
        };
        if (Entity parent = scene.parent(entity)) {
            const auto it = indices.find(parent.index);
            if (it == indices.end())
                throw std::runtime_error("Broken entity parent during serialization");
            node["parent"] = it->second;
        }
        if (const auto* component = scene.rendererComponent(entity)) {
            node["renderer"] = {
                {"mesh", component->mesh
                    ? Json(assets.meshKey(component->mesh)) : Json(nullptr)},
                {"material", writeMaterial(component->material, assets)}
            };
        }
        if (const auto* light = scene.pointLightComponent(entity))
            node["pointLight"] = writeLight(*light);
        if (const auto* body = scene.rigidBodyComponent(entity))
            node["rigidBody"] = writeBody(*body);
        root["entities"].push_back(std::move(node));
    }
    return root.dump(2) + "\n";
}

Scene deserializeScene(std::string_view jsonText, const SceneAssetRegistry& assets) {
    const Json root = Json::parse(jsonText.begin(), jsonText.end());
    if (!root.is_object() ||
        root.at("format") != "engine2.scene" ||
        root.at("version") != 1)
        throw std::invalid_argument("Unsupported engine2 scene JSON format/version");
    const Json& nodes = root.at("entities");
    if (!nodes.is_array() || nodes.size() > maxSceneEntities)
        throw std::invalid_argument("Invalid or oversized scene entity list");

    // Two stages create handles before linking parent indices. Any parsing,
    // missing asset or hierarchy error leaves the caller's Scene unchanged.
    Scene loaded;
    std::vector<Entity> handles;
    handles.reserve(nodes.size());
    for (const Json& node : nodes) {
        if (!node.is_object())
            throw std::invalid_argument("Scene entity must be an object");
        const std::string label = node.at("name").get<std::string>();
        if (label.size() > 1024)
            throw std::invalid_argument("Scene entity name exceeds 1024 bytes");
        const Entity handle = loaded.createEntity(label);
        handles.push_back(handle);
        loaded.setLocalTransform(handle, readMat4(node.at("local")));
        loaded.setEnabled(handle, node.at("enabled").get<bool>());
        if (node.contains("renderer")) {
            const auto& renderer = node.at("renderer");
            const Json& mesh = renderer.at("mesh");
            const Mesh* resolvedMesh = nullptr;
            if (!mesh.is_null()) {
                if (!mesh.is_string())
                    throw std::invalid_argument("Scene renderer mesh reference is invalid");
                resolvedMesh = assets.mesh(mesh.get<std::string>());
            }
            loaded.setRenderer(handle, {
                resolvedMesh, readMaterial(renderer.at("material"), assets)
            });
        }
        if (node.contains("pointLight"))
            loaded.setPointLight(handle, readLight(node.at("pointLight")));
        if (node.contains("rigidBody"))
            loaded.setRigidBody(handle, readBody(node.at("rigidBody")));
    }
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const Json& parent = nodes.at(i).at("parent");
        if (parent.is_null()) continue;
        const std::size_t index = readIndex(parent, handles.size());
        loaded.setParent(handles[i], handles[index]);
    }
    return loaded;
}

void saveScene(const Scene& scene, const SceneAssetRegistry& assets,
               const std::filesystem::path& path) {
    // Serialize all data before opening the output; malformed/unregistered
    // assets never truncate a valid existing scene file.
    const std::string data = serializeScene(scene, assets);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("Cannot write scene file: " + path.string());
    output << data;
    if (!output)
        throw std::runtime_error("Writing scene file failed: " + path.string());
}

Scene loadScene(const std::filesystem::path& path,
                const SceneAssetRegistry& assets) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot open scene file: " + path.string());
    // Bound file size before parsing untrusted files.
    constexpr std::streamoff maxSize = 64 * 1024 * 1024;
    input.seekg(0, std::ios::end);
    const std::streamoff bytes = input.tellg();
    if (bytes < 0 || bytes > maxSize)
        throw std::invalid_argument("Scene JSON file exceeds 64 MiB limit");
    input.seekg(0, std::ios::beg);
    std::string data(static_cast<std::size_t>(bytes), '\0');
    input.read(data.data(), static_cast<std::streamsize>(data.size()));
    if (!input)
        throw std::runtime_error("Could not read scene file: " + path.string());
    return deserializeScene(data, assets);
}

} // namespace engine2
