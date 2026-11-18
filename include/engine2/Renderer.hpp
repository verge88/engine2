#pragma once

#include "engine2/Environment.hpp"
#include "engine2/Material.hpp"
#include "engine2/PostProcess.hpp"
#include "engine2/Shader.hpp"

#include <glad/gl.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <filesystem>
#include <optional>
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
    RendererOptions options_;

    GLuint shadowFbo_ = 0;
    GLuint shadowTexture_ = 0;

    glm::vec3 lightDirection_{-0.55f, -1.0f, -0.35f};
};

} // namespace engine2
