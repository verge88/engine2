#pragma once

#include "engine2/Mesh.hpp"
#include "engine2/Renderer.hpp"
#include "engine2/Texture.hpp"

#include <filesystem>
#include <memory>
#include <vector>

namespace engine2 {

// GPU resources outlive RenderItem's non-owning mesh and texture references.
// Keep LoadedScene alive until no Renderer calls use its items.
struct LoadedScene {
    std::vector<std::unique_ptr<Mesh>> meshes;
    std::vector<std::unique_ptr<Texture2D>> textures;
    std::vector<RenderItem> items;
};

// Opaque triangle primitives, node transforms, UV0, tangents and core PBR textures.
// No skinning, animations, morph targets, extensions, UV1 or alpha blending yet.
LoadedScene loadGltf(const std::filesystem::path& path);

} // namespace engine2
