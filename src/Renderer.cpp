#include "engine2/Renderer.hpp"

#include "engine2/Camera.hpp"
#include "engine2/Frustum.hpp"
#include "engine2/Mesh.hpp"

#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace engine2 {
namespace {

void bindMaterialTexture(const Shader& shader, const char* activeName,
                         GLuint texture, GLuint slot) {
    shader.setInt(activeName, texture != 0 ? 1 : 0);
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, texture);
}

void setCulling(bool doubleSided) {
    if (doubleSided) glDisable(GL_CULL_FACE);
    else glEnable(GL_CULL_FACE);
}

} // namespace

Renderer::Renderer(const std::filesystem::path& shaderDir, RendererOptions options)
    : pbrShader_(shaderDir / "pbr.vert", shaderDir / "pbr.frag"),
      shadowShader_(shaderDir / "shadow.vert", shaderDir / "shadow.frag"),
      environment_(shaderDir, options.hdrEnvironment),
      postProcess_(shaderDir),
      ambientOcclusion_(shaderDir),
      options_(std::move(options)) {
    setExposure(options_.exposure);
    setBloomStrength(options_.bloomStrength);
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

void Renderer::setExposure(float exposure) {
    options_.exposure = glm::clamp(exposure, 0.05f, 16.0f);
}

void Renderer::setBloomStrength(float strength) {
    options_.bloomStrength = glm::clamp(strength, 0.0f, 2.0f);
}

void Renderer::setPointLights(const std::vector<PointLight>& lights) {
    constexpr std::size_t maxPointLights = 8;
    pointLights_.assign(lights.begin(),
                         lights.begin() + static_cast<std::ptrdiff_t>(
                             std::min(lights.size(), maxPointLights)));
}

void Renderer::render(const Camera& camera, const std::vector<RenderItem>& items,
                      int framebufferWidth, int framebufferHeight) {
    if (framebufferWidth <= 0 || framebufferHeight <= 0) return;

    const glm::mat4 view = camera.viewMatrix();
    const glm::mat4 projection = camera.projectionMatrix(
        static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight));

    stats_ = {};
    stats_.submittedItems = items.size();
    std::vector<RenderItem> visible;
    visible.reserve(items.size());
    const Frustum frustum(projection * view);
    for (const auto& item : items) {
        if (!item.mesh) continue;
        if (options_.frustumCulling && !frustum.intersects(item.mesh->bounds(), item.model)) {
            ++stats_.culledItems;
            continue;
        }
        visible.push_back(item);
    }
    stats_.visibleItems = visible.size();

    const glm::mat4 lightView = glm::lookAt(-lightDirection_ * 22.0f,
                                            glm::vec3(0.0f),
                                            glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 lightProjection = glm::ortho(-16.0f, 16.0f, -16.0f, 16.0f,
                                                  0.1f, 60.0f);
    const glm::mat4 lightSpace = lightProjection * lightView;

    // Pass 1: 2048x2048 directional depth map.
    glViewport(0, 0, shadowResolution_, shadowResolution_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glClear(GL_DEPTH_BUFFER_BIT);
    glCullFace(GL_FRONT);
    shadowShader_.use();
    shadowShader_.setMat4("uLightSpace", lightSpace);
    // Shadow casters must include all objects inside the light-space bounds,
    // even if outside the camera frustum: offscreen casters may shadow onscreen.
    for (const RenderItem& item : items) {
        if (!item.mesh) continue;
        if (!Frustum(lightSpace).intersects(item.mesh->bounds(), item.model)) continue;
        setCulling(item.material.doubleSided);
        shadowShader_.setMat4("uModel", item.model);
        item.mesh->draw();
        ++stats_.shadowDrawCalls;
    }
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // Pass 2: linear HDR color, cubemap sky, textured GGX + diffuse/specular IBL.
    if (options_.ssao) {
        ambientOcclusion_.render(view, projection, visible,
                                 framebufferWidth, framebufferHeight);
    }
    postProcess_.beginScene(framebufferWidth, framebufferHeight);
    environment_.drawSky(view, projection, camera.position());

    pbrShader_.use();
    pbrShader_.setMat4("uView", view);
    pbrShader_.setMat4("uProjection", projection);
    pbrShader_.setMat4("uLightSpace", lightSpace);
    pbrShader_.setVec3("uViewPos", camera.position());
    pbrShader_.setVec3("uLightDir", -lightDirection_);
    pbrShader_.setVec3("uLightColor", glm::vec3(5.6f, 5.1f, 4.6f));
    pbrShader_.setInt("uShadowMap", 0);
    pbrShader_.setInt("uBaseColorTexture", 1);
    pbrShader_.setInt("uMetallicRoughnessTexture", 2);
    pbrShader_.setInt("uNormalTexture", 3);
    pbrShader_.setInt("uOcclusionTexture", 4);
    pbrShader_.setInt("uEmissiveTexture", 5);
    pbrShader_.setInt("uIrradianceMap", 6);
    pbrShader_.setInt("uPrefilterMap", 7);
    pbrShader_.setInt("uBrdfLut", 8);
    pbrShader_.setInt("uSSAOTex", 9);
    pbrShader_.setInt("uUseSSAO", options_.ssao ? 1 : 0);
    pbrShader_.setVec2("uViewport", glm::vec2(
        static_cast<float>(framebufferWidth), static_cast<float>(framebufferHeight)));
    pbrShader_.setInt("uPointLightCount", static_cast<int>(pointLights_.size()));
    for (std::size_t i = 0; i < pointLights_.size(); ++i) {
        const std::string name = "uPointLights[" + std::to_string(i) + "].";
        pbrShader_.setVec3(name + "position", pointLights_[i].position);
        pbrShader_.setVec3(name + "color", pointLights_[i].color);
        pbrShader_.setFloat(name + "intensity", pointLights_[i].intensity);
        pbrShader_.setFloat(name + "range", pointLights_[i].range);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shadowTexture_);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_CUBE_MAP, environment_.irradianceMap());
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_CUBE_MAP, environment_.prefilterMap());
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, environment_.brdfLut());
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, options_.ssao ? ambientOcclusion_.texture() : 0);

    for (const RenderItem& item : visible) {
        const Material& material = item.material;
        setCulling(material.doubleSided);
        pbrShader_.setMat4("uModel", item.model);
        pbrShader_.setVec3("uAlbedo", material.albedo);
        pbrShader_.setFloat("uMetallic", material.metallic);
        pbrShader_.setFloat("uRoughness", material.roughness);
        pbrShader_.setVec3("uEmissive", material.emissive);
        pbrShader_.setFloat("uOcclusionStrength", material.occlusionStrength);
        pbrShader_.setFloat("uNormalScale", material.normalScale);
        pbrShader_.setInt("uChecker", material.checker ? 1 : 0);
        pbrShader_.setInt("uDoubleSided", material.doubleSided ? 1 : 0);
        bindMaterialTexture(pbrShader_, "uHasBaseColorTexture",
                            material.baseColorTexture, 1);
        bindMaterialTexture(pbrShader_, "uHasMetallicRoughnessTexture",
                            material.metallicRoughnessTexture, 2);
        bindMaterialTexture(pbrShader_, "uHasNormalTexture", material.normalTexture, 3);
        bindMaterialTexture(pbrShader_, "uHasOcclusionTexture",
                            material.occlusionTexture, 4);
        bindMaterialTexture(pbrShader_, "uHasEmissiveTexture",
                            material.emissiveTexture, 5);
        item.mesh->draw();
        ++stats_.geometryDrawCalls;
    }
    glEnable(GL_CULL_FACE);
    for (int unit = 9; unit >= 0; --unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

    // Pass 3: half-resolution bloom and filmic tonemapping into the display.
    postProcess_.present(options_.exposure, options_.bloomStrength);
}

} // namespace engine2
