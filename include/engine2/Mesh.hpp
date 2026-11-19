#pragma once

#include "engine2/Frustum.hpp"

#include <glad/gl.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace engine2 {

struct Vertex {
    glm::vec3 position{};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
};

class Mesh {
public:
    Mesh(std::vector<Vertex> vertices, std::vector<std::uint32_t> indices);
    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    void draw() const;
    [[nodiscard]] const Bounds& bounds() const { return bounds_; }
    // True triangle ray intersection in mesh-local coordinates, two sided.
    // t uses the (possibly unnormalized) input direction's parameterization.
    [[nodiscard]] std::optional<float> raycastLocal(
        const glm::vec3& origin, const glm::vec3& direction,
        float maximumDistance) const;

    [[nodiscard]] static Mesh sphere(unsigned int sectors = 48, unsigned int stacks = 32);
    [[nodiscard]] static Mesh plane();
    [[nodiscard]] static Mesh cube();

private:
    void release() noexcept;

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLsizei indexCount_ = 0;
    Bounds bounds_{};
    std::vector<glm::vec3> pickPositions_;
    std::vector<std::uint32_t> pickIndices_;
};

} // namespace engine2
