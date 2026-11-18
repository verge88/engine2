#include "engine2/Renderer.hpp"

#include "engine2/Camera.hpp"
#include "engine2/Frustum.hpp"
#include "engine2/Mesh.hpp"
#include "engine2/TemporalAA.hpp"

#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
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
      sunShadows_(shaderDir),
      environment_(shaderDir, options.hdrEnvironment),
      postProcess_(shaderDir),
      ambientOcclusion_(shaderDir),
      pointShadowMaps_(shaderDir),
      options_(std::move(options)) {
    setExposure(options_.exposure);
    setBloomStrength(options_.bloomStrength);
    setPointShadowCount(options_.pointShadowCount);
    setSunCascades(options_.sunCascades);
    setSunShadowDistance(options_.sunShadowDistance);
    setTaaHistoryWeight(options_.taaHistoryWeight);
    lightDirection_ = glm::normalize(lightDirection_);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_MULTISAMPLE);

}

void Renderer::setTaa(bool enabled) {
    if (options_.taa == enabled) return;
    options_.taa = enabled;
    temporalFrame_ = 0;
    postProcess_.resetTemporalHistory();
}

void Renderer::setTaaHistoryWeight(float weight) {
    if (!std::isfinite(weight))
        throw std::invalid_argument("TAA history weight must be finite");
    options_.taaHistoryWeight = glm::clamp(weight, 0.0f, 0.98f);
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

void Renderer::setPointShadowCount(std::size_t count) {
    options_.pointShadowCount = std::min(count, PointShadowMaps::maxShadowLights);
}

void Renderer::setSunCascades(int count) {
    if (count != 0 && count != 1 && count != maxSunCascades)
        throw std::invalid_argument("sun cascades must be 0, 1 or 3");
    options_.sunCascades = count;
}

void Renderer::setSunShadowDistance(float distance) {
    if (!std::isfinite(distance) || distance < 5.0f || distance > Camera::farPlane)
        throw std::invalid_argument("sun shadow distance must be between 5 and 150");
    options_.sunShadowDistance = distance;
}

void Renderer::render(const Camera& camera, const std::vector<RenderItem>& items,
                      int framebufferWidth, int framebufferHeight) {
    if (framebufferWidth <= 0 || framebufferHeight <= 0) return;

    const glm::mat4 view = camera.viewMatrix();
    const glm::mat4 stableProjection = camera.projectionMatrix(
        static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight));
    // Apply jitter to the camera rendering only, *not* to cascade fitting or
    // visibility culling. This keeps sun shadow matrices stable across frames.
    glm::mat4 projection = stableProjection;
    if (options_.taa) {
        projection = jitterProjection(
            stableProjection, temporalJitter(temporalFrame_++),
            framebufferWidth, framebufferHeight);
    }

    stats_ = {};
    stats_.submittedItems = items.size();
    std::vector<RenderItem> visible;
    visible.reserve(items.size());
    const Frustum frustum(stableProjection * view);
    for (const auto& item : items) {
        if (!item.mesh) continue;
        if (options_.frustumCulling && !frustum.intersects(item.mesh->bounds(), item.model)) {
            ++stats_.culledItems;
            continue;
        }
        visible.push_back(item);
    }
    stats_.visibleItems = visible.size();

    // Shadow atlas: 0/1/3 fitted sun cascades, clipped to the configured
    // distance. Each layer is independently depth-rendered and culls against
    // its own light-space frustum (including off-camera shadow casters).
    stats_.shadowDrawCalls = sunShadows_.render(
        view, stableProjection, lightDirection_, items,
        Camera::nearPlane, Camera::farPlane, options_.sunShadowDistance,
        options_.sunCascades);

    // Pass 1b: six radial depth faces per shadow-enabled point light.
    // Off-camera casters remain: they can cast shadows into the visible frame.
    const std::size_t activePointShadows =
        std::min(options_.pointShadowCount, pointLights_.size());
    stats_.pointShadowDrawCalls = pointShadowMaps_.render(
        items, pointLights_, activePointShadows);

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
    pbrShader_.setInt("uCascadeCount", options_.sunCascades);
    pbrShader_.setInt("uDebugCascades", options_.cascadeDebug ? 1 : 0);
    const auto& cascadeData = sunShadows_.cascades();
    for (int i = 0; i < options_.sunCascades; ++i) {
        const std::string index = std::to_string(i);
        pbrShader_.setMat4("uCascadeLightSpace[" + index + "]",
                           cascadeData[static_cast<std::size_t>(i)].lightSpace);
        pbrShader_.setFloat("uCascadeFar[" + index + "]",
                            cascadeData[static_cast<std::size_t>(i)].farDistance);
    }
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
    pbrShader_.setInt("uPointShadow0", 10);
    pbrShader_.setInt("uPointShadow1", 11);
    pbrShader_.setInt("uPointShadowCount", static_cast<int>(activePointShadows));
    pbrShader_.setInt("uPointLightCount", static_cast<int>(pointLights_.size()));
    for (std::size_t i = 0; i < pointLights_.size(); ++i) {
        const std::string name = "uPointLights[" + std::to_string(i) + "].";
        pbrShader_.setVec3(name + "position", pointLights_[i].position);
        pbrShader_.setVec3(name + "color", pointLights_[i].color);
        pbrShader_.setFloat(name + "intensity", pointLights_[i].intensity);
        pbrShader_.setFloat(name + "range", pointLights_[i].range);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, sunShadows_.texture());
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_CUBE_MAP, environment_.irradianceMap());
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_CUBE_MAP, environment_.prefilterMap());
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, environment_.brdfLut());
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, options_.ssao ? ambientOcclusion_.texture() : 0);
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_CUBE_MAP,
                  activePointShadows >= 1 ? pointShadowMaps_.texture(0) : 0);
    glActiveTexture(GL_TEXTURE11);
    glBindTexture(GL_TEXTURE_CUBE_MAP,
                  activePointShadows >= 2 ? pointShadowMaps_.texture(1) : 0);

    auto drawPbrItem = [&](const RenderItem& item) {
        const Material& material = item.material;
        setCulling(material.doubleSided);
        pbrShader_.setMat4("uModel", item.model);
        pbrShader_.setVec3("uAlbedo", material.albedo);
        pbrShader_.setFloat("uMetallic", material.metallic);
        pbrShader_.setFloat("uRoughness", material.roughness);
        pbrShader_.setFloat("uAlphaFactor", material.alpha);
        pbrShader_.setFloat("uAlphaCutoff", material.alphaCutoff);
        pbrShader_.setInt("uAlphaMode", static_cast<int>(material.alphaMode));
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
        if (material.alphaMode == AlphaMode::Mask) ++stats_.alphaMaskedDrawCalls;
        if (material.alphaMode == AlphaMode::Blend) ++stats_.alphaBlendedDrawCalls;
    };

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    // Opaque and alpha-masked draws establish depth before translucent items.
    for (const RenderItem& item : visible) {
        if (item.material.alphaMode == AlphaMode::Blend) continue;
        drawPbrItem(item);
    }

    std::vector<const RenderItem*> transparent;
    transparent.reserve(visible.size());
    for (const RenderItem& item : visible) {
        if (item.material.alphaMode == AlphaMode::Blend)
            transparent.push_back(&item);
    }
    const glm::vec3 eye = camera.position();
    const auto distanceSquared = [&](const RenderItem* item) {
        const Bounds& b = item->mesh->bounds();
        const glm::vec3 center = glm::vec3(
            item->model * glm::vec4((b.min + b.max) * 0.5f, 1.0f));
        const glm::vec3 delta = center - eye;
        return glm::dot(delta, delta);
    };
    std::stable_sort(transparent.begin(), transparent.end(),
        [&](const RenderItem* a, const RenderItem* b) {
            return distanceSquared(a) > distanceSquared(b);
        });

    if (!transparent.empty()) {
        // Blend only after the opaque depth buffer is populated. Sorted
        // back-to-front; depth test stays on, depth *writes* stay off.
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        for (const RenderItem* item : transparent) drawPbrItem(*item);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
    glEnable(GL_CULL_FACE);
    for (int unit = 11; unit >= 0; --unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }

    // Pass 3: full-resolution HDR temporal resolve (reproject depth from the
    // current camera and clamp history to the current 3x3 color neighborhood).
    // All postprocessing reads the resolved HDR texture, not the raw scene.
    postProcess_.resolveTemporal(
        projection * view, camera.position(), Camera::nearPlane,
        Camera::farPlane, options_.taa, options_.taaHistoryWeight);
    stats_.taaHistoryFrames = postProcess_.temporalFrames();

    // Pass 4: half-resolution bloom and filmic tonemapping into the display.
    postProcess_.present(options_.exposure, options_.bloomStrength);
}

} // namespace engine2
