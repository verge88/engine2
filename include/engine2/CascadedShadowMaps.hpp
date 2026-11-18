#pragma once

#include "engine2/CascadeMath.hpp"
#include "engine2/Shader.hpp"

#include <glad/gl.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <vector>

namespace engine2 {

struct RenderItem;

// Three independently rendered depth layers, addressed by sampler2DArray in
// the PBR shader. GL 3.3, no geometry shaders or vendor extensions required.
class CascadedShadowMaps {
public:
    static constexpr int resolution = 1536;

    explicit CascadedShadowMaps(const std::filesystem::path& shaderDirectory);
    ~CascadedShadowMaps();

    CascadedShadowMaps(const CascadedShadowMaps&) = delete;
    CascadedShadowMaps& operator=(const CascadedShadowMaps&) = delete;

    // count=0 disables directional shadowing; count=1 or 3 renders cascades.
    std::size_t render(const glm::mat4& view, const glm::mat4& projection,
                       const glm::vec3& lightDirection,
                       const std::vector<RenderItem>& items,
                       float nearPlane, float farPlane,
                       float shadowDistance, int count);

    [[nodiscard]] GLuint texture() const { return depthArray_; }
    [[nodiscard]] const std::array<CascadeTransform, maxSunCascades>&
        cascades() const { return cascades_; }

private:
    Shader shader_;
    GLuint framebuffer_ = 0;
    GLuint depthArray_ = 0;
    std::array<CascadeTransform, maxSunCascades> cascades_{};
};

} // namespace engine2
