#include "engine2/CascadedShadowMaps.hpp"

#include "engine2/Mesh.hpp"
#include "engine2/Renderer.hpp"
#include "engine2/Frustum.hpp"

#include <algorithm>
#include <stdexcept>

namespace engine2 {

CascadedShadowMaps::CascadedShadowMaps(
    const std::filesystem::path& shaderDirectory)
    : shader_(shaderDirectory / "shadow.vert",
              shaderDirectory / "shadow.frag") {
    glGenFramebuffers(1, &framebuffer_);
    glGenTextures(1, &depthArray_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, depthArray_);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24,
                 resolution, resolution, maxSunCascades,
                 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, border);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              depthArray_, 0, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const bool complete =
        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    if (!complete) {
        glDeleteTextures(1, &depthArray_);
        glDeleteFramebuffers(1, &framebuffer_);
        depthArray_ = 0;
        framebuffer_ = 0;
        throw std::runtime_error("Failed to allocate directional shadow array");
    }
}

CascadedShadowMaps::~CascadedShadowMaps() {
    if (depthArray_) glDeleteTextures(1, &depthArray_);
    if (framebuffer_) glDeleteFramebuffers(1, &framebuffer_);
}

std::size_t CascadedShadowMaps::render(
    const glm::mat4& view, const glm::mat4& projection,
    const glm::vec3& lightDirection, const std::vector<RenderItem>& items,
    float nearPlane, float farPlane, float shadowDistance, int count) {
    if (count == 0) return 0;
    cascades_ = computeSunCascades(view, projection, lightDirection,
                                   nearPlane, farPlane, shadowDistance,
                                   count, resolution);

    glViewport(0, 0, resolution, resolution);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    shader_.use();
    shader_.setInt("uBaseColorTexture", 0);

    std::size_t drawCalls = 0;
    for (int layer = 0; layer < count; ++layer) {
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  depthArray_, 0, layer);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            throw std::runtime_error("Incomplete directional shadow cascade");
        }
        glClear(GL_DEPTH_BUFFER_BIT);
        const glm::mat4& lightSpace =
            cascades_[static_cast<std::size_t>(layer)].lightSpace;
        shader_.setMat4("uLightSpace", lightSpace);
        const Frustum sunFrustum(lightSpace);
        // Off-screen occluders are tested against each cascade's light
        // frustum, not the visible camera frustum.
        for (const RenderItem& item : items) {
            if (!item.mesh || item.material.alphaMode == AlphaMode::Blend) continue;
            if (!sunFrustum.intersects(item.mesh->bounds(), item.model)) continue;
            if (item.material.doubleSided) glDisable(GL_CULL_FACE);
            else glEnable(GL_CULL_FACE);
            shader_.setMat4("uModel", item.model);
            shader_.setInt("uAlphaMode",
                           static_cast<int>(item.material.alphaMode));
            shader_.setFloat("uAlphaFactor", item.material.alpha);
            shader_.setFloat("uAlphaCutoff", item.material.alphaCutoff);
            shader_.setInt("uHasBaseColorTexture",
                item.material.baseColorTexture != 0 ? 1 : 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, item.material.baseColorTexture);
            item.mesh->draw();
            ++drawCalls;
        }
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    return drawCalls;
}

} // namespace engine2
