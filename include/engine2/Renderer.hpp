#pragma once

#include "engine2/AmbientOcclusion.hpp"
#include "engine2/Environment.hpp"
#include "engine2/Material.hpp"
#include "engine2/PostProcess.hpp"
#include "engine2/Shader.hpp"

#include <glad/gl.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <filesystem>
#include <optional>
#include <cstddef>
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
    std::size_t geometryDrawCalls = 0;
    std::size_t alphaMaskedDrawCalls = 0;
    std::size_t alphaBlendedDrawCalls = 0;
};

class Renderer {
public:
    explicit Renderer(const std::filesystem::path& shaderDir,
                      RendererOptions options = {});
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void render(const Camera& camera, const std::vector<RenderItem>& items,
                int framebufferWidth, int framebufferHeight);

    void setPointLights(const std::vector<PointLight>& lights);
    [[nodiscard]] const FrameStats& stats() const { return stats_; }
    void setSsao(bool enabled) { options_.ssao = enabled; }
    [[nodiscard]] bool ssao() const { return options_.ssao; }
    void setFrustumCulling(bool enabled) { options_.frustumCulling = enabled; }

    void setExposure(float exposure);
    [[nodiscard]] float exposure() const { return options_.exposure; }
    void setBloomStrength(float strength);
    [[nodiscard]] float bloomStrength() const { return options_.bloomStrength; }

private:
    static constexpr int shadowResolution_ = 2048;

    Shader pbrShader_;
    Shader shadowShader_;
    Environment environment_;
    PostProcess postProcess_;
    AmbientOcclusion ambientOcclusion_;
    RendererOptions options_;
    std::vector<PointLight> pointLights_;
    FrameStats stats_{};

    GLuint shadowFbo_ = 0;
    GLuint shadowTexture_ = 0;

    glm::vec3 lightDirection_{-0.55f, -1.0f, -0.35f};
};

} // namespace engine2
