#include "engine2/PostProcess.hpp"

#include "engine2/Exposure.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace engine2 {
namespace {

constexpr int luminanceResolution = 128;
constexpr int luminanceMipLevels = 8; // 128 -> 64 -> ... -> 1

void requireComplete(const char* stage) {
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error(std::string("Incomplete HDR framebuffer: ") + stage);
    }
}

void allocateColor(GLuint texture, int width, int height) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0,
                 GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

} // namespace

PostProcess::PostProcess(const std::filesystem::path& shaderDirectory)
    : brightShader_(shaderDirectory / "fullscreen.vert",
                    shaderDirectory / "bloom-bright.frag"),
      downsampleShader_(shaderDirectory / "fullscreen.vert",
                        shaderDirectory / "bloom-downsample.frag"),
      upsampleShader_(shaderDirectory / "fullscreen.vert",
                      shaderDirectory / "bloom-upsample.frag"),
      compositeShader_(shaderDirectory / "fullscreen.vert",
                       shaderDirectory / "composite.frag"),
      temporalShader_(shaderDirectory / "fullscreen.vert",
                      shaderDirectory / "temporal-resolve.frag"),
      logLuminanceShader_(shaderDirectory / "fullscreen.vert",
                          shaderDirectory / "log-luminance.frag") {
    glGenFramebuffers(1, &sceneFbo_);
    glGenTextures(1, &hdrTexture_);
    glGenTextures(1, &depthTexture_);
    glGenFramebuffers(2, temporalFbos_.data());
    glGenTextures(2, temporalTextures_.data());
    glGenFramebuffers(static_cast<GLsizei>(bloomLevels), bloomFbos_.data());
    glGenTextures(static_cast<GLsizei>(bloomLevels), bloomTextures_.data());
    glGenFramebuffers(1, &luminanceFbo_);
    glGenTextures(1, &luminanceTexture_);
    glGenVertexArrays(1, &fullScreenVao_);
}

PostProcess::~PostProcess() {
    if (fullScreenVao_) glDeleteVertexArrays(1, &fullScreenVao_);
    if (luminanceTexture_) glDeleteTextures(1, &luminanceTexture_);
    if (luminanceFbo_) glDeleteFramebuffers(1, &luminanceFbo_);
    glDeleteTextures(static_cast<GLsizei>(bloomLevels), bloomTextures_.data());
    glDeleteFramebuffers(static_cast<GLsizei>(bloomLevels), bloomFbos_.data());
    glDeleteTextures(2, temporalTextures_.data());
    glDeleteFramebuffers(2, temporalFbos_.data());
    if (depthTexture_) glDeleteTextures(1, &depthTexture_);
    if (hdrTexture_) glDeleteTextures(1, &hdrTexture_);
    if (sceneFbo_) glDeleteFramebuffers(1, &sceneFbo_);
}

void PostProcess::drawTriangle() const {
    glBindVertexArray(fullScreenVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void PostProcess::resize(int framebufferWidth, int framebufferHeight) {
    if (width_ == framebufferWidth && height_ == framebufferHeight) return;
    width_ = framebufferWidth;
    height_ = framebufferHeight;
    resetTemporalHistory();
    resetExposure();

    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    allocateColor(hdrTexture_, width_, height_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, hdrTexture_, 0);

    glBindTexture(GL_TEXTURE_2D, depthTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width_, height_,
                 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, depthTexture_, 0);
    requireComplete("scene");

    for (std::size_t i = 0; i < temporalFbos_.size(); ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, temporalFbos_[i]);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        allocateColor(temporalTextures_[i], width_, height_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, temporalTextures_[i], 0);
        requireComplete("temporal history");
    }

    int w = std::max(1, width_ / 2);
    int h = std::max(1, height_ / 2);
    for (std::size_t i = 0; i < bloomLevels; ++i) {
        bloomWidths_[i] = w;
        bloomHeights_[i] = h;
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbos_[i]);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        allocateColor(bloomTextures_[i], w, h);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, bloomTextures_[i], 0);
        requireComplete("bloom pyramid");
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
    }

    // A single 128x128 log-luminance image with a full mip chain. Mipmap
    // averaging of log(Y) computes an approximation of the geometric mean.
    glBindTexture(GL_TEXTURE_2D, luminanceTexture_);
    for (int mip = 0; mip < luminanceMipLevels; ++mip) {
        const int side = luminanceResolution >> mip;
        glTexImage2D(GL_TEXTURE_2D, mip, GL_R16F, side, side,
                     0, GL_RED, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, luminanceMipLevels - 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, luminanceFbo_);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, luminanceTexture_, 0);
    requireComplete("log luminance");

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void PostProcess::beginScene(int framebufferWidth, int framebufferHeight) {
    resize(framebufferWidth, framebufferHeight);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    glViewport(0, 0, width_, height_);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    presentTexture_ = hdrTexture_;
}

void PostProcess::resetTemporalHistory() {
    hasTemporalHistory_ = false;
    temporalFrames_ = 0;
    temporalReadIndex_ = 0;
}

void PostProcess::resetExposure() {
    exposureReady_ = false;
    adaptedExposure_ = 1.0f;
    effectiveExposure_ = 1.0f;
}

void PostProcess::resolveTemporal(const glm::mat4& viewProjection,
                                  const glm::vec3& cameraPosition,
                                  float nearPlane, float farPlane,
                                  bool enabled, float historyWeight) {
    presentTexture_ = hdrTexture_;
    if (!enabled) {
        resetTemporalHistory();
        return;
    }

    if (hasTemporalHistory_ &&
        glm::distance(cameraPosition, previousCameraPosition_) > 3.0f) {
        resetTemporalHistory();
    }

    const int writeIndex = 1 - temporalReadIndex_;
    glBindFramebuffer(GL_FRAMEBUFFER, temporalFbos_[writeIndex]);
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);

    temporalShader_.use();
    temporalShader_.setInt("uCurrent", 0);
    temporalShader_.setInt("uDepth", 1);
    temporalShader_.setInt("uHistory", 2);
    temporalShader_.setMat4("uInverseViewProjection",
                            glm::inverse(viewProjection));
    temporalShader_.setMat4("uPreviousViewProjection",
                            previousViewProjection_);
    temporalShader_.setInt("uHistoryValid", hasTemporalHistory_ ? 1 : 0);
    temporalShader_.setFloat("uHistoryWeight",
                             std::clamp(historyWeight, 0.0f, 0.98f));
    temporalShader_.setFloat("uNearPlane", nearPlane);
    temporalShader_.setFloat("uFarPlane", farPlane);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrTexture_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, depthTexture_);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, temporalTextures_[temporalReadIndex_]);
    drawTriangle();

    for (int unit = 2; unit >= 0; --unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    temporalReadIndex_ = writeIndex;
    presentTexture_ = temporalTextures_[writeIndex];
    hasTemporalHistory_ = true;
    ++temporalFrames_;
    previousCameraPosition_ = cameraPosition;
    previousViewProjection_ = viewProjection;
}

void PostProcess::meterExposure(float key, float speed, float deltaSeconds) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);

    glBindFramebuffer(GL_FRAMEBUFFER, luminanceFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, luminanceTexture_, 0);
    glViewport(0, 0, luminanceResolution, luminanceResolution);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    logLuminanceShader_.use();
    logLuminanceShader_.setInt("uScene", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, presentTexture_);
    drawTriangle();
    glBindTexture(GL_TEXTURE_2D, 0);

    // Finish the luminance FBO render pass before generating the texture's
    // mip chain, then rebind the FBO for reading the 1x1 reduction result.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, luminanceTexture_);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, luminanceFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, luminanceTexture_,
                           luminanceMipLevels - 1);
    requireComplete("metered luminance mip");
    float averageLogLuminance = 0.0f;
    // A single 1x1 GPU->CPU readback per frame. This is easy to profile and
    // replace with a deferred pixel transfer if it becomes a bottleneck.
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, 1, 1, GL_RED, GL_FLOAT, &averageLogLuminance);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (!std::isfinite(averageLogLuminance)) return;
    const float target = targetAutoExposure(averageLogLuminance, key);
    if (!exposureReady_) {
        adaptedExposure_ = target;
        exposureReady_ = true;
    } else {
        adaptedExposure_ = adaptAutoExposure(
            adaptedExposure_, target, speed, std::max(deltaSeconds, 0.0f));
    }
}

void PostProcess::buildBloom(float threshold) {
    glDisable(GL_BLEND);
    glViewport(0, 0, bloomWidths_[0], bloomHeights_[0]);
    glBindFramebuffer(GL_FRAMEBUFFER, bloomFbos_[0]);
    brightShader_.use();
    brightShader_.setInt("uScene", 0);
    brightShader_.setFloat("uThreshold", threshold);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, presentTexture_);
    drawTriangle();

    downsampleShader_.use();
    downsampleShader_.setInt("uSource", 0);
    for (std::size_t i = 1; i < bloomLevels; ++i) {
        glViewport(0, 0, bloomWidths_[i], bloomHeights_[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbos_[i]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, bloomTextures_[i - 1]);
        drawTriangle();
    }

    // Add tent-filtered small mips into progressively larger mips. Each
    // pass reads a different texture than its framebuffer attachment.
    upsampleShader_.use();
    upsampleShader_.setInt("uSource", 0);
    upsampleShader_.setFloat("uScatter", 0.72f);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE);
    for (std::size_t i = bloomLevels - 1; i > 0; --i) {
        glViewport(0, 0, bloomWidths_[i - 1], bloomHeights_[i - 1]);
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbos_[i - 1]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, bloomTextures_[i]);
        drawTriangle();
    }
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void PostProcess::present(float manualExposure, float bloomStrength,
                          float bloomThreshold, bool autoExposure, float exposureKey,
                          float adaptationSpeed, float deltaSeconds) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);

    if (autoExposure) {
        meterExposure(exposureKey, adaptationSpeed, deltaSeconds);
    } else {
        resetExposure();
    }
    // Manual exposure remains an exposure-compensation multiplier in auto mode.
    effectiveExposure_ = std::clamp(
        manualExposure * adaptedExposure_, 0.01f, 64.0f);

    if (bloomStrength > 0.0f) buildBloom(bloomThreshold);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    compositeShader_.use();
    compositeShader_.setInt("uScene", 0);
    compositeShader_.setInt("uBloom", 1);
    compositeShader_.setFloat("uExposure", effectiveExposure_);
    compositeShader_.setFloat("uBloomStrength", bloomStrength);
    glActiveTexture(GL_TEXTURE0);
    // In v0.8 the composite accidentally sampled raw HDR: use resolved TAA!
    glBindTexture(GL_TEXTURE_2D, presentTexture_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomTextures_[0]);
    drawTriangle();

    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

} // namespace engine2
