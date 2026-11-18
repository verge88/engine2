#include "engine2/Mesh.hpp"

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace engine2 {

Mesh::Mesh(std::vector<Vertex> vertices, std::vector<std::uint32_t> indices) {
    static_assert(std::is_standard_layout_v<Vertex>);
    if (vertices.empty() || indices.empty()) {
        throw std::invalid_argument("Mesh needs vertices and indices");
    }
    if (indices.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) {
        throw std::length_error("Mesh has too many indices");
    }

    bounds_.min = vertices.front().position;
    bounds_.max = vertices.front().position;
    for (const Vertex& vertex : vertices) {
        bounds_.min = glm::min(bounds_.min, vertex.position);
        bounds_.max = glm::max(bounds_.max, vertex.position);
    }

    indexCount_ = static_cast<GLsizei>(indices.size());
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                 vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
                 indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, uv)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, tangent)));
    glBindVertexArray(0);
}

Mesh::~Mesh() {
    release();
}

Mesh::Mesh(Mesh&& other) noexcept
    : vao_(std::exchange(other.vao_, 0)),
      vbo_(std::exchange(other.vbo_, 0)),
      ebo_(std::exchange(other.ebo_, 0)),
      indexCount_(std::exchange(other.indexCount_, 0)),
      bounds_(other.bounds_) {}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        release();
        vao_ = std::exchange(other.vao_, 0);
        vbo_ = std::exchange(other.vbo_, 0);
        ebo_ = std::exchange(other.ebo_, 0);
        indexCount_ = std::exchange(other.indexCount_, 0);
        bounds_ = other.bounds_;
    }
    return *this;
}

void Mesh::release() noexcept {
    if (ebo_ != 0) glDeleteBuffers(1, &ebo_);
    if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    vao_ = vbo_ = ebo_ = 0;
    indexCount_ = 0;
}

void Mesh::draw() const {
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

Mesh Mesh::sphere(unsigned int sectors, unsigned int stacks) {
    if (sectors < 3 || stacks < 2) {
        throw std::invalid_argument("Sphere requires >=3 sectors and >=2 stacks");
    }

    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(static_cast<std::size_t>(sectors + 1) * (stacks + 1));
    indices.reserve(static_cast<std::size_t>(sectors) * stacks * 6);

    for (unsigned int y = 0; y <= stacks; ++y) {
        const float theta = glm::pi<float>() * static_cast<float>(y) / static_cast<float>(stacks);
        for (unsigned int x = 0; x <= sectors; ++x) {
            const float phi = glm::two_pi<float>() * static_cast<float>(x)
                            / static_cast<float>(sectors);
            const glm::vec3 normal(
                std::sin(theta) * std::cos(phi),
                std::cos(theta),
                std::sin(theta) * std::sin(phi)
            );
            vertices.push_back(Vertex{
                normal, normal,
                glm::vec2(static_cast<float>(x) / static_cast<float>(sectors),
                          static_cast<float>(y) / static_cast<float>(stacks)),
                glm::vec4(-std::sin(phi), 0.0f, std::cos(phi), -1.0f)
            });
        }
    }

    for (unsigned int y = 0; y < stacks; ++y) {
        for (unsigned int x = 0; x < sectors; ++x) {
            const std::uint32_t a = y * (sectors + 1) + x;
            const std::uint32_t b = a + sectors + 1;
            indices.insert(indices.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
    }
    return Mesh(std::move(vertices), std::move(indices));
}

Mesh Mesh::plane() {
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec4 tangent(1.0f, 0.0f, 0.0f, -1.0f);
    return Mesh(
        {
            {{-0.5f, 0.0f, -0.5f}, up, {0.0f, 0.0f}, tangent},
            {{ 0.5f, 0.0f, -0.5f}, up, {1.0f, 0.0f}, tangent},
            {{ 0.5f, 0.0f,  0.5f}, up, {1.0f, 1.0f}, tangent},
            {{-0.5f, 0.0f,  0.5f}, up, {0.0f, 1.0f}, tangent}
        },
        {0, 2, 1, 0, 3, 2}
    );
}

} // namespace engine2
