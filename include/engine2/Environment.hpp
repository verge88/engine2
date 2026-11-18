#pragma once

#include "engine2/Shader.hpp"

#include <glad/gl.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <filesystem>
#include <optional>

namespace engine2 {

// Precomputes a diffuse irradiance cubemap, GGX specular radiance mips and
// split-sum BRDF LUT once on startup. Supports Radiance .hdr panoramas.
// Without an image, a procedural HDR sky is generated in linear radiance.
class Environment {
public:
    Environment(const std::filesystem::path& shaderDirectory,
                const std::optional<std::filesystem::path>& hdrFile = std::nullopt);
    ~Environment();

    Environment(const Environment&) = delete;
    Environment& operator=(const Environment&) = delete;

    [[nodiscard]] GLuint irradianceMap() const { return irradianceMap_; }
    [[nodiscard]] GLuint prefilterMap() const { return prefilterMap_; }
    [[nodiscard]] GLuint brdfLut() const { return brdfLut_; }

    void drawSky(const glm::mat4& view, const glm::mat4& projection,
                 const glm::vec3& cameraPosition) const;

private:
    void createPanorama(const std::optional<std::filesystem::path>& hdrFile);
    void createMaps();
    void renderCubeFaces(GLuint target, int resolution, int mipLevel,
                         int mode, float roughness);
    void drawTriangle() const;

    Shader filterShader_;
    Shader brdfShader_;
    Shader skyShader_;

    GLuint panorama_ = 0;
    GLuint environmentMap_ = 0;
    GLuint irradianceMap_ = 0;
    GLuint prefilterMap_ = 0;
    GLuint brdfLut_ = 0;
    GLuint frameBuffer_ = 0;
    GLuint fullScreenVao_ = 0;
};

} // namespace engine2
