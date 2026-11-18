#include "engine2/PostProcess.hpp"

#include <algorithm>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <cmath>
#include <stdexcept>
#include <string>

namespace engine2 {
namespace {

void requireComplete(const char* stage) {
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error(std::string("Incomplete HDR framebuffer: ") + stage);
    }
}

void allocateColor(GLuint texture, int width, int height) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F,
                 width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

} // namespace

PostProcess::PostProcess(const std::filesystem::path& shaderDirectory)
    : brightShader_(shaderDirectory / "fullscreen.vert",
                    shaderDirectory / "bloom-bright.frag"),
      blurShader_(shaderDirectory / "fullscreen.vert",
                  shaderDirectory / "bloom-blur.frag"),
      compositeShader_(shaderDirectory / "fullscreen.vert",
                       shaderDirectory / "composite.frag"),
      temporalShader_(shaderDirectory / "fullscreen.vert",
                      shaderDirectory / "temporal-resolve.frag") {
    glGenFramebuffers(1, &sceneFbo_);
    glGenTextures(1, &hdrTexture_);
    glGenTextures(1, &depthTexture_);
    glGenFramebuffers(2, temporalFbos_.data());
    glGenTextures(2, temporalTextures_.data());
    glGenFramebuffers(2, blurFbos_.data());
    glGenTextures(2, blurTextures_.data());
    glGenVertexArrays(1, &fullScreenVao_);
}

PostProcess::~PostProcess() {
    if (fullScreenVao_) glDeleteVertexArrays(1, &fullScreenVao_);
    glDeleteTextures(2, blurTextures_.data());
    glDeleteFramebuffers(2, blurFbos_.data());
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
    bloomWidth_ = std::max(1, width_ / 2);
    bloomHeight_ = std::max(1, height_ / 2);
    resetTemporalHistory();

    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    allocateColor(hdrTexture_, width_, height_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, hdrTexture_, 0);
    // Texture instead of a renderbuffer: reprojection must sample
    // the original rasterized depth at full output resolution.
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

    for (int i = 0; i < 2; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, blurFbos_[static_cast<std::size_t>(i)]);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        allocateColor(blurTextures_[static_cast<std::size_t>(i)],
                      bloomWidth_, bloomHeight_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               blurTextures_[static_cast<std::size_t>(i)], 0);
        requireComplete("bloom");
    }

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

void PostProcess::resolveTemporal(const glm::mat4& viewProjection,
                                  const glm::vec3& cameraPosition,
                                  float nearPlane, float farPlane,
                                  bool enabled, float historyWeight) {
    presentTexture_ = hdrTexture_;
    if (!enabled) {
        resetTemporalHistory();
        return;
    }

    // For large instantaneous camera teleports, reprojection history can
    // be invalid even if projected UV/depth happen to match.
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
    glBindTexture(GL_TEXTURE_2D, presentTexture_);
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

void PostProcess::present(float exposure, float bloomStrength) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glViewport(0, 0, bloomWidth_, bloomHeight_);

    glBindFramebuffer(GL_FRAMEBUFFER, blurFbos_[0]);
    brightShader_.use();
    brightShader_.setInt("uScene", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, presentTexture_);
    drawTriangle();

    blurShader_.use();
    blurShader_.setInt("uImage", 0);
    for (int i = 0; i < 6; ++i) {
        const bool horizontal = (i % 2 == 0);
        const std::size_t target = horizontal ? 1 : 0;
        const std::size_t source = horizontal ? 0 : 1;
        glBindFramebuffer(GL_FRAMEBUFFER, blurFbos_[target]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, blurTextures_[source]);
        blurShader_.setVec2("uDirection",
                            horizontal ? glm::vec2(1.0f, 0.0f)
                                       : glm::vec2(0.0f, 1.0f));
        drawTriangle();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    compositeShader_.use();
    compositeShader_.setInt("uScene", 0);
    compositeShader_.setInt("uBloom", 1);
    compositeShader_.setFloat("uExposure", exposure);
    compositeShader_.setFloat("uBloomStrength", bloomStrength);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrTexture_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, blurTextures_[0]);
    drawTriangle();

    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

} // namespace engine2
