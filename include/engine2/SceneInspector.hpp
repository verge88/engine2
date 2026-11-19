#pragma once

#include "engine2/SceneIO.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace engine2 {

// Export a human-readable, machine-readable snapshot of all scene entities,
// including disabled entities, hierarchy, world/local position, component
// presence and editable fields. The "index" is the ordinal in Scene::entities()
// at inspection time (the same indexing used by the scene-edit JSON format).
[[nodiscard]] std::string inspectScene(const Scene& scene,
                                       const SceneAssetRegistry& assets);
void saveSceneInspection(const Scene& scene,
                         const SceneAssetRegistry& assets,
                         const std::filesystem::path& path);

// Apply a transactional JSON change set to a copy of the scene; failures
// (invalid component, out-of-range index, cycle, singular reparent, etc.)
// never modify the caller's Scene. Entity indices are resolved from a single
// pre-edit snapshot, so delete operations do not renumber later references.
//
// engine2.scene-edits v1 supports: rename, set-enabled, set-position
// (local/world), reparent (keepWorld), set-albedo, set-light-intensity,
// set-body-velocity and delete.
[[nodiscard]] std::size_t applySceneEdits(Scene& scene,
                                          std::string_view editsJson);
[[nodiscard]] std::size_t applySceneEditsFile(
    Scene& scene, const std::filesystem::path& path);

} // namespace engine2
