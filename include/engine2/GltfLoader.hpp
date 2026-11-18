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

// glTF triangle primitives, flattened node transforms, UV0, tangents, core
// PBR textures and alpha OPAQUE/MASK/BLEND. Runtime Scene re-imports each
// primitive's already-computed world transform as a separate entity.
// No skinning, animations, morph targets, extensions or UV1 yet.
LoadedScene loadGltf(const std::filesystem::path& path);

} // namespace engine2
