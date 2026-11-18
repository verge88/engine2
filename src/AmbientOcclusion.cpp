#include "engine2/AmbientOcclusion.hpp"

#include "engine2/Mesh.hpp"
#include "engine2/Renderer.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>
#include <string>

namespace engine2 {
namespace {

void ensureComplete(const char* name) {
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error(std::string("Incomplete SSAO framebuffer: ") + name);
    }
}

void allocateTexture(GLuint texture, GLenum internalFormat, GLenum format,
                     int width, int height) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat),
                 width, height, 0, format, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void cull(bool doubleSided) {
    if (doubleSided) glDisable(GL_CULL_FACE);
    else glEnable(GL_CULL_FACE);
}

} // namespace

AmbientOcclusion::AmbientOcclusion(const std::filesystem::path& shaderDirectory)
    : geometryShader_(shaderDirectory / "ssao-geometry.vert",
                      shaderDirectory / "ssao-geometry.frag"),
      occlusionShader_(shaderDirectory / "fullscreen.vert",
                       shaderDirectory / "ssao.frag"),
      blurShader_(shaderDirectory / "fullscreen.vert",
                  shaderDirectory / "ssao-blur.frag") {
    std::mt19937 generator(1337);
    std::uniform_real_distribution<float> uniform(0.0f, 1.0f);
    for (std::size_t i = 0; i < samples_.size(); ++i) {
        const float phi = uniform(generator) * 6.2831853f;
        const float z = uniform(generator);
        const float r = std::sqrt(std::max(1.0f - z * z, 0.0f));
        const float fraction = static_cast<float>(i) / static_cast<float>(samples_.size());
        const float scale = 0.1f + 0.9f * fraction * fraction;
        const float distance = uniform(generator) * scale;
        samples_[i] = glm::vec3(std::cos(phi) * r, std::sin(phi) * r, z) * distance;
    }

    glGenFramebuffers(1, &geometryFbo_);
    glGenFramebuffers(1, &occlusionFbo_);
    glGenFramebuffers(1, &blurFbo_);
    glGenTextures(1, &positionsTexture_);
    glGenTextures(1, &normalsTexture_);
    glGenTextures(1, &occlusionTexture_);
    glGenTextures(1, &blurTexture_);
    glGenRenderbuffers(1, &depthBuffer_);
    glGenVertexArrays(1, &fullscreenVao_);
}

AmbientOcclusion::~AmbientOcclusion() {
    if (fullscreenVao_) glDeleteVertexArrays(1, &fullscreenVao_);
    if (depthBuffer_) glDeleteRenderbuffers(1, &depthBuffer_);
    if (positionsTexture_) glDeleteTextures(1, &positionsTexture_);
    if (normalsTexture_) glDeleteTextures(1, &normalsTexture_);
    if (occlusionTexture_) glDeleteTextures(1, &occlusionTexture_);
    if (blurTexture_) glDeleteTextures(1, &blurTexture_);
    if (geometryFbo_) glDeleteFramebuffers(1, &geometryFbo_);
    if (occlusionFbo_) glDeleteFramebuffers(1, &occlusionFbo_);
    if (blurFbo_) glDeleteFramebuffers(1, &blurFbo_);
}

void AmbientOcclusion::drawTriangle() const {
    glBindVertexArray(fullscreenVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void AmbientOcclusion::resize(int width, int height) {
    const int halfWidth = std::max(1, (width + 1) / 2);
    const int halfHeight = std::max(1, (height + 1) / 2);
    if (width_ == halfWidth && height_ == halfHeight) return;
    width_ = halfWidth;
    height_ = halfHeight;

    glBindFramebuffer(GL_FRAMEBUFFER, geometryFbo_);
    allocateTexture(positionsTexture_, GL_RGBA16F, GL_RGBA, width_, height_);
    allocateTexture(normalsTexture_, GL_RGBA16F, GL_RGBA, width_, height_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, positionsTexture_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
                           GL_TEXTURE_2D, normalsTexture_, 0);
    const GLenum attachments[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, attachments);
    glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width_, height_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                             GL_RENDERBUFFER, depthBuffer_);
    ensureComplete("geometry");

    glBindFramebuffer(GL_FRAMEBUFFER, occlusionFbo_);
    allocateTexture(occlusionTexture_, GL_R16F, GL_RED, width_, height_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, occlusionTexture_, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    ensureComplete("occlusion");

    glBindFramebuffer(GL_FRAMEBUFFER, blurFbo_);
    allocateTexture(blurTexture_, GL_R16F, GL_RED, width_, height_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, blurTexture_, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    ensureComplete("bilateral blur");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

void AmbientOcclusion::render(const glm::mat4& view, const glm::mat4& projection,
                              const std::vector<RenderItem>& items,
                              int width, int height) {
    if (width <= 0 || height <= 0) return;
    resize(width, height);
    glViewport(0, 0, width_, height_);

    // Geometry: camera-space position and normal, clipped by the closest surface.
    glBindFramebuffer(GL_FRAMEBUFFER, geometryFbo_);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    geometryShader_.use();
    geometryShader_.setMat4("uView", view);
    geometryShader_.setMat4("uProjection", projection);
    for (const auto& item : items) {
        if (!item.mesh) continue;
        cull(item.material.doubleSided);
        geometryShader_.setMat4("uModel", item.model);
        geometryShader_.setInt("uDoubleSided", item.material.doubleSided ? 1 : 0);
        item.mesh->draw();
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);

    // Evaluate ambient occlusion with a reproducible 16-sample hemisphere.
    glBindFramebuffer(GL_FRAMEBUFFER, occlusionFbo_);
    occlusionShader_.use();
    occlusionShader_.setMat4("uProjection", projection);
    occlusionShader_.setInt("uPositions", 0);
    occlusionShader_.setInt("uNormals", 1);
    for (std::size_t i = 0; i < samples_.size(); ++i) {
        occlusionShader_.setVec3("uSamples[" + std::to_string(i) + "]", samples_[i]);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, positionsTexture_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, normalsTexture_);
    drawTriangle();

    // Depth- and normal-aware blur to avoid bleeding across silhouettes.
    glBindFramebuffer(GL_FRAMEBUFFER, blurFbo_);
    blurShader_.use();
    blurShader_.setInt("uPositions", 0);
    blurShader_.setInt("uNormals", 1);
    blurShader_.setInt("uOcclusion", 2);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, occlusionTexture_);
    drawTriangle();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    for (int unit = 2; unit >= 0; --unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

} // namespace engine2
