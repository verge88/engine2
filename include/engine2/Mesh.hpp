#pragma once

#include <glad/gl.h>
#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace engine2 {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
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

    [[nodiscard]] static Mesh sphere(unsigned int sectors = 48, unsigned int stacks = 32);
    [[nodiscard]] static Mesh plane();

private:
    void release() noexcept;

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLsizei indexCount_ = 0;
};

} // namespace engine2
