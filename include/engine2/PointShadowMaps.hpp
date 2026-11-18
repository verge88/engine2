#pragma once

#include "engine2/Shader.hpp"

#include <glad/gl.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <vector>

namespace engine2 {

struct RenderItem;
struct PointLight;

// OpenGL 3.3 omnidirectional depth cubemaps. Six 90-degree renders per light;
// view-space depth is explicitly replaced by normalized radial distance.
// Only the first two enabled lights cast shadows; the other point lights still
// illuminate the scene without shadows.
class PointShadowMaps {
public:
    static constexpr std::size_t maxShadowLights = 2;
    static constexpr int resolution = 256;

    explicit PointShadowMaps(const std::filesystem::path& shaderDirectory);
    ~PointShadowMaps();

    PointShadowMaps(const PointShadowMaps&) = delete;
    PointShadowMaps& operator=(const PointShadowMaps&) = delete;

    // Returns number of actual mesh draws across all six cubemap faces.
    [[nodiscard]] std::size_t render(const std::vector<RenderItem>& items,
                                     const std::vector<PointLight>& lights,
                                     std::size_t shadowLightCount);

    [[nodiscard]] GLuint texture(std::size_t index) const {
        return index < maxShadowLights ? cubemaps_[index] : 0;
    }

private:
    Shader depthShader_;
    GLuint framebuffer_ = 0;
    std::array<GLuint, maxShadowLights> cubemaps_{};
};

} // namespace engine2
