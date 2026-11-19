#pragma once

#include "engine2/Scene.hpp"

#include <glad/gl.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace engine2 {

// Stable keys decouple serialized scenes from ephemeral OpenGL handles and
// Mesh pointers. Assets must already be loaded and must outlive the Scene.
// E.g. "sphere"/"floor" or "gltf/mesh/0"/"gltf/texture/0".
class SceneAssetRegistry {
public:
    void registerMesh(std::string key, const Mesh* mesh);
    void registerTexture(std::string key, GLuint texture);

    [[nodiscard]] const Mesh* mesh(std::string_view key) const;
    [[nodiscard]] GLuint texture(std::string_view key) const;
    [[nodiscard]] const std::string& meshKey(const Mesh* mesh) const;
    [[nodiscard]] const std::string& textureKey(GLuint texture) const;

private:
    std::unordered_map<std::string, const Mesh*> meshes_;
    std::unordered_map<const Mesh*, std::string> meshKeys_;
    std::unordered_map<std::string, GLuint> textures_;
    std::unordered_map<GLuint, std::string> textureKeys_;
};

// Versioned JSON file (engine2.scene v1). All live entities, local transforms,
// hierarchy, enabled flags, light and physics components, complete core PBR
// material values and asset references survive a round trip. Runtime handles,
// physics accumulator, fixed-step counters and OpenGL asset data do not.
// Deserialization creates a separate Scene and throws on invalid/cyclic input.
[[nodiscard]] std::string serializeScene(const Scene& scene,
                                         const SceneAssetRegistry& assets);
[[nodiscard]] Scene deserializeScene(std::string_view jsonText,
                                     const SceneAssetRegistry& assets);
void saveScene(const Scene& scene, const SceneAssetRegistry& assets,
               const std::filesystem::path& path);
[[nodiscard]] Scene loadScene(const std::filesystem::path& path,
                              const SceneAssetRegistry& assets);

} // namespace engine2
