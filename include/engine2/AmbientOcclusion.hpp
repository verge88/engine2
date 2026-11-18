#pragma once

#include "engine2/Shader.hpp"

#include <glad/gl.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <filesystem>
#include <vector>

namespace engine2 {

struct RenderItem;

// Half-resolution screen-space ambient occlusion. Stores linear view-space
// position and normal in a depth-tested geometry prepass, evaluates a seeded
// 16-sample hemisphere kernel, then edge-aware blurs the result.
// Samples ambient/IBL only, not direct illumination or sky.
class AmbientOcclusion {
public:
    explicit AmbientOcclusion(const std::filesystem::path& shaderDirectory);
    ~AmbientOcclusion();

    AmbientOcclusion(const AmbientOcclusion&) = delete;
    AmbientOcclusion& operator=(const AmbientOcclusion&) = delete;

    void render(const glm::mat4& view, const glm::mat4& projection,
                const std::vector<RenderItem>& items, int width, int height);

    [[nodiscard]] GLuint texture() const { return blurTexture_; }

private:
    void resize(int width, int height);
    void drawTriangle() const;

    Shader geometryShader_;
    Shader occlusionShader_;
    Shader blurShader_;

    GLuint geometryFbo_ = 0;
    GLuint positionsTexture_ = 0;
    GLuint normalsTexture_ = 0;
    GLuint depthBuffer_ = 0;
    GLuint occlusionFbo_ = 0;
    GLuint occlusionTexture_ = 0;
    GLuint blurFbo_ = 0;
    GLuint blurTexture_ = 0;
    GLuint fullscreenVao_ = 0;

    int width_ = 0;
    int height_ = 0;
    std::array<glm::vec3, 16> samples_{};
};

} // namespace engine2
