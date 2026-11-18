#pragma once

#include <glad/gl.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <filesystem>
#include <string_view>

namespace engine2 {

class Shader {
public:
    Shader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath);
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    void use() const;
    void setMat4(std::string_view name, const glm::mat4& value) const;
    void setVec3(std::string_view name, const glm::vec3& value) const;
    void setFloat(std::string_view name, float value) const;
    void setInt(std::string_view name, int value) const;

private:
    GLuint program_ = 0;
};

} // namespace engine2
