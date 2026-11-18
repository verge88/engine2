#pragma once

#include "engine2/Shader.hpp"

#include <glad/gl.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <filesystem>
#include <cstddef>

namespace engine2 {

// HDR scene with sampleable depth, optional reprojection + temporal history,
// then half-resolution bloom and ACES tone mapping.
class PostProcess {
public:
    explicit PostProcess(const std::filesystem::path& shaderDirectory);
    ~PostProcess();

    PostProcess(const PostProcess&) = delete;
    PostProcess& operator=(const PostProcess&) = delete;

    void beginScene(int framebufferWidth, int framebufferHeight);
    // Resolve before bloom and tonemapping so history is accumulated in HDR.
    void resolveTemporal(const glm::mat4& viewProjection,
                         const glm::vec3& cameraPosition,
                         float nearPlane, float farPlane,
                         bool enabled, float historyWeight);
    void resetTemporalHistory();
    [[nodiscard]] std::size_t temporalFrames() const { return temporalFrames_; }
    void present(float exposure, float bloomStrength);

private:
    void resize(int framebufferWidth, int framebufferHeight);
    void drawTriangle() const;

    Shader brightShader_;
    Shader blurShader_;
    Shader compositeShader_;
    Shader temporalShader_;

    GLuint sceneFbo_ = 0;
    GLuint hdrTexture_ = 0;
    GLuint depthTexture_ = 0;
    std::array<GLuint, 2> temporalFbos_{};
    std::array<GLuint, 2> temporalTextures_{};
    GLuint presentTexture_ = 0;
    int temporalReadIndex_ = 0;
    std::size_t temporalFrames_ = 0;
    glm::mat4 previousViewProjection_{1.0f};
    glm::vec3 previousCameraPosition_{0.0f};
    bool hasTemporalHistory_ = false;
    std::array<GLuint, 2> blurFbos_{};
    std::array<GLuint, 2> blurTextures_{};
    GLuint fullScreenVao_ = 0;
    int width_ = 0;
    int height_ = 0;
    int bloomWidth_ = 0;
    int bloomHeight_ = 0;
};

} // namespace engine2
