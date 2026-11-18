#pragma once

#include "engine2/Shader.hpp"

#include <glad/gl.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <filesystem>

namespace engine2 {

// OpenGL 3.3 HDR scene, temporal reprojection, geometric-mean auto exposure,
// five-level downsample/upsample bloom and ACES display tonemapping.
class PostProcess {
public:
    static constexpr std::size_t bloomLevels = 5;

    explicit PostProcess(const std::filesystem::path& shaderDirectory);
    ~PostProcess();

    PostProcess(const PostProcess&) = delete;
    PostProcess& operator=(const PostProcess&) = delete;

    void beginScene(int framebufferWidth, int framebufferHeight);
    // Reprojection runs in linear HDR before metering, bloom or tonemapping.
    void resolveTemporal(const glm::mat4& viewProjection,
                         const glm::vec3& cameraPosition,
                         float nearPlane, float farPlane,
                         bool enabled, float historyWeight);
    void resetTemporalHistory();
    [[nodiscard]] std::size_t temporalFrames() const { return temporalFrames_; }

    void resetExposure();
    void present(float manualExposure, float bloomStrength,
                 float bloomThreshold, bool autoExposure, float exposureKey,
                 float adaptationSpeed, float deltaSeconds);
    [[nodiscard]] float effectiveExposure() const { return effectiveExposure_; }
    [[nodiscard]] float adaptedExposure() const { return adaptedExposure_; }
    [[nodiscard]] std::size_t bloomMipCount() const { return bloomLevels; }

private:
    void resize(int framebufferWidth, int framebufferHeight);
    void drawTriangle() const;
    void meterExposure(float key, float speed, float deltaSeconds);
    void buildBloom(float threshold);

    Shader brightShader_;
    Shader downsampleShader_;
    Shader upsampleShader_;
    Shader compositeShader_;
    Shader temporalShader_;
    Shader logLuminanceShader_;

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

    std::array<GLuint, bloomLevels> bloomFbos_{};
    std::array<GLuint, bloomLevels> bloomTextures_{};
    std::array<int, bloomLevels> bloomWidths_{};
    std::array<int, bloomLevels> bloomHeights_{};

    GLuint luminanceFbo_ = 0;
    GLuint luminanceTexture_ = 0;
    bool exposureReady_ = false;
    float adaptedExposure_ = 1.0f;
    float effectiveExposure_ = 1.0f;

    GLuint fullScreenVao_ = 0;
    int width_ = 0;
    int height_ = 0;
};

} // namespace engine2
