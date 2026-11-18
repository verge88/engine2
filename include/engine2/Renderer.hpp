#pragma once

#include "engine2/Shader.hpp"

#include <glad/gl.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <filesystem>
#include <vector>

namespace engine2 {

class Camera;
class Mesh;

struct RenderItem {
    const Mesh* mesh = nullptr;
    glm::mat4 model{1.0f};
    glm::vec3 albedo{0.8f, 0.5f, 0.3f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    bool checker = false;
};

class Renderer {
public:
    explicit Renderer(const std::filesystem::path& shaderDir);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void render(const Camera& camera, const std::vector<RenderItem>& items,
                int framebufferWidth, int framebufferHeight);

private:
    static constexpr int shadowResolution_ = 2048;

    Shader pbrShader_;
    Shader shadowShader_;
    GLuint shadowFbo_ = 0;
    GLuint shadowTexture_ = 0;

    glm::vec3 lightDirection_{-0.55f, -1.0f, -0.35f};
};

} // namespace engine2
