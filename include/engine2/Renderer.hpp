#pragma once

#include "engine2/AmbientOcclusion.hpp"
#include "engine2/Environment.hpp"
#include "engine2/Material.hpp"
#include "engine2/PostProcess.hpp"
#include "engine2/PointShadowMaps.hpp"
#include "engine2/CascadedShadowMaps.hpp"
#include "engine2/Shader.hpp"

#include <glad/gl.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <filesystem>
#include <optional>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine2 {

class Camera;
class Mesh;

struct RenderItem {
    const Mesh* mesh = nullptr;
    glm::mat4 model{1.0f};
    Material material{};
};

struct RendererOptions {
    std::optional<std::filesystem::path> hdrEnvironment;
    float exposure = 1.0f;
    float bloomStrength = 0.15f;
    bool ssao = true;
    bool frustumCulling = true;
    std::size_t pointShadowCount = 1; // first 0..2 point lights cast shadows
    int sunCascades = 3;              // 0, 1 or 3
    float sunShadowDistance = 95.0f; // world units, at most the camera far plane
    bool cascadeDebug = false;
    bool taa = true;
    float taaHistoryWeight = 0.88f;
};

struct PointLight {
    glm::vec3 position{0.0f};
    glm::vec3 color{1.0f};
    float intensity = 40.0f;
    float range = 9.0f;
};

struct FrameStats {
    std::size_t submittedItems = 0;
    std::size_t visibleItems = 0;
    std::size_t culledItems = 0;
    std::size_t shadowDrawCalls = 0;
    std::size_t pointShadowDrawCalls = 0;
    std::size_t geometryDrawCalls = 0;
    std::size_t alphaMaskedDrawCalls = 0;
    std::size_t alphaBlendedDrawCalls = 0;
    std::size_t taaHistoryFrames = 0;
};

class Renderer {
public:
    explicit Renderer(const std::filesystem::path& shaderDir,
                      RendererOptions options = {});

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void render(const Camera& camera, const std::vector<RenderItem>& items,
                int framebufferWidth, int framebufferHeight);

    void setPointLights(const std::vector<PointLight>& lights);
    void setPointShadowCount(std::size_t count);
    void setSunCascades(int count);
    void setSunShadowDistance(float distance);
    void setCascadeDebug(bool enabled) { options_.cascadeDebug = enabled; }
    [[nodiscard]] bool cascadeDebug() const { return options_.cascadeDebug; }

    [[nodiscard]] std::size_t pointShadowCount() const { return options_.pointShadowCount; }
    [[nodiscard]] int sunCascades() const { return options_.sunCascades; }
    [[nodiscard]] float sunShadowDistance() const { return options_.sunShadowDistance; }
    [[nodiscard]] const FrameStats& stats() const { return stats_; }
    void setSsao(bool enabled) { options_.ssao = enabled; }
    [[nodiscard]] bool ssao() const { return options_.ssao; }
    void setFrustumCulling(bool enabled) { options_.frustumCulling = enabled; }

    void setTaa(bool enabled);
    [[nodiscard]] bool taa() const { return options_.taa; }
    void setTaaHistoryWeight(float weight);
    [[nodiscard]] float taaHistoryWeight() const { return options_.taaHistoryWeight; }

    void setExposure(float exposure);
    [[nodiscard]] float exposure() const { return options_.exposure; }
    void setBloomStrength(float strength);
    [[nodiscard]] float bloomStrength() const { return options_.bloomStrength; }

private:
    Shader pbrShader_;
    CascadedShadowMaps sunShadows_;
    Environment environment_;
    PostProcess postProcess_;
    AmbientOcclusion ambientOcclusion_;
    PointShadowMaps pointShadowMaps_;
    RendererOptions options_;
    std::vector<PointLight> pointLights_;
    FrameStats stats_{};
    std::uint64_t temporalFrame_ = 0;

    glm::vec3 lightDirection_{-0.55f, -1.0f, -0.35f};
};

} // namespace engine2
