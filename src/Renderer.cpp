#include "engine2/Renderer.hpp"

#include "engine2/Camera.hpp"
#include "engine2/Mesh.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <stdexcept>

namespace engine2 {

Renderer::Renderer(const std::filesystem::path& shaderDir)
    : pbrShader_(shaderDir / "pbr.vert", shaderDir / "pbr.frag"),
      shadowShader_(shaderDir / "shadow.vert", shaderDir / "shadow.frag") {
    lightDirection_ = glm::normalize(lightDirection_);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_MULTISAMPLE);

    glGenFramebuffers(1, &shadowFbo_);
    glGenTextures(1, &shadowTexture_);
    glBindTexture(GL_TEXTURE_2D, shadowTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadowResolution_, shadowResolution_,
                 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           shadowTexture_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteTextures(1, &shadowTexture_);
        glDeleteFramebuffers(1, &shadowFbo_);
        throw std::runtime_error("Failed to create directional shadow framebuffer");
    }
}

Renderer::~Renderer() {
    if (shadowTexture_ != 0) glDeleteTextures(1, &shadowTexture_);
    if (shadowFbo_ != 0) glDeleteFramebuffers(1, &shadowFbo_);
}

void Renderer::render(const Camera& camera, const std::vector<RenderItem>& items,
                      int framebufferWidth, int framebufferHeight) {
    if (framebufferWidth <= 0 || framebufferHeight <= 0) return;

    const glm::mat4 lightView = glm::lookAt(-lightDirection_ * 22.0f,
                                            glm::vec3(0.0f),
                                            glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 lightProjection = glm::ortho(-16.0f, 16.0f, -16.0f, 16.0f,
                                                  0.1f, 60.0f);
    const glm::mat4 lightSpace = lightProjection * lightView;

    // Pass 1: render depth from the sun's point of view.
    glViewport(0, 0, shadowResolution_, shadowResolution_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glClear(GL_DEPTH_BUFFER_BIT);
    glCullFace(GL_FRONT);
    shadowShader_.use();
    shadowShader_.setMat4("uLightSpace", lightSpace);
    for (const RenderItem& item : items) {
        if (!item.mesh) continue;
        shadowShader_.setMat4("uModel", item.model);
        item.mesh->draw();
    }
    glCullFace(GL_BACK);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Pass 2: GGX PBR shading and filmic tonemapping.
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glClearColor(0.025f, 0.035f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    pbrShader_.use();
    pbrShader_.setMat4("uView", camera.viewMatrix());
    pbrShader_.setMat4("uProjection", camera.projectionMatrix(
        static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight)));
    pbrShader_.setMat4("uLightSpace", lightSpace);
    pbrShader_.setVec3("uViewPos", camera.position());
    pbrShader_.setVec3("uLightDir", -lightDirection_);
    pbrShader_.setVec3("uLightColor", glm::vec3(5.6f, 5.1f, 4.6f));
    pbrShader_.setInt("uShadowMap", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shadowTexture_);

    for (const RenderItem& item : items) {
        if (!item.mesh) continue;
        pbrShader_.setMat4("uModel", item.model);
        pbrShader_.setVec3("uAlbedo", item.albedo);
        pbrShader_.setFloat("uMetallic", item.metallic);
        pbrShader_.setFloat("uRoughness", item.roughness);
        pbrShader_.setInt("uChecker", item.checker ? 1 : 0);
        item.mesh->draw();
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace engine2
