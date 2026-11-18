#pragma once

#include "engine2/Shader.hpp"

#include <glad/gl.h>

#include <array>
#include <filesystem>

namespace engine2 {

// Renders the scene in linear floating-point HDR, extracts bright regions at
// half-resolution, applies a separable Gaussian blur and tone-maps to sRGB.
class PostProcess {
public:
    explicit PostProcess(const std::filesystem::path& shaderDirectory);
    ~PostProcess();

    PostProcess(const PostProcess&) = delete;
    PostProcess& operator=(const PostProcess&) = delete;

    void beginScene(int framebufferWidth, int framebufferHeight);
    void present(float exposure, float bloomStrength);

private:
    void resize(int framebufferWidth, int framebufferHeight);
    void drawTriangle() const;

    Shader brightShader_;
    Shader blurShader_;
    Shader compositeShader_;

    GLuint sceneFbo_ = 0;
    GLuint hdrTexture_ = 0;
    GLuint depthBuffer_ = 0;
    std::array<GLuint, 2> blurFbos_{};
    std::array<GLuint, 2> blurTextures_{};
    GLuint fullScreenVao_ = 0;
    int width_ = 0;
    int height_ = 0;
    int bloomWidth_ = 0;
    int bloomHeight_ = 0;
};

} // namespace engine2
