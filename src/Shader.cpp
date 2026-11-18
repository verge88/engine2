#include "engine2/Shader.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace engine2 {
namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Unable to open shader: " + path.string());
    }
    std::ostringstream output;
    output << stream.rdbuf();
    return output.str();
}

GLuint compileStage(GLenum stage, const std::string& source) {
    const GLuint shader = glCreateShader(stage);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(length > 0 ? length : 1), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error("GLSL compile error: " + log);
    }
    return shader;
}

} // namespace

Shader::Shader(const std::filesystem::path& vertexPath,
               const std::filesystem::path& fragmentPath) {
    const GLuint vertex = compileStage(GL_VERTEX_SHADER, readFile(vertexPath));
    GLuint fragment = 0;
    try {
        fragment = compileStage(GL_FRAGMENT_SHADER, readFile(fragmentPath));
        program_ = glCreateProgram();
        glAttachShader(program_, vertex);
        glAttachShader(program_, fragment);
        glLinkProgram(program_);

        GLint ok = GL_FALSE;
        glGetProgramiv(program_, GL_LINK_STATUS, &ok);
        if (ok != GL_TRUE) {
            GLint length = 0;
            glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<std::size_t>(length > 0 ? length : 1), '\0');
            glGetProgramInfoLog(program_, length, nullptr, log.data());
            throw std::runtime_error("GLSL link error: " + log);
        }
    } catch (...) {
        if (program_ != 0) glDeleteProgram(program_);
        if (fragment != 0) glDeleteShader(fragment);
        glDeleteShader(vertex);
        program_ = 0;
        throw;
    }

    glDeleteShader(fragment);
    glDeleteShader(vertex);
}

Shader::~Shader() {
    if (program_ != 0) glDeleteProgram(program_);
}

Shader::Shader(Shader&& other) noexcept
    : program_(std::exchange(other.program_, 0)) {}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        if (program_ != 0) glDeleteProgram(program_);
        program_ = std::exchange(other.program_, 0);
    }
    return *this;
}

void Shader::use() const {
    glUseProgram(program_);
}

void Shader::setMat4(std::string_view name, const glm::mat4& value) const {
    const std::string uniform(name);
    glUniformMatrix4fv(glGetUniformLocation(program_, uniform.c_str()), 1, GL_FALSE,
                       glm::value_ptr(value));
}

void Shader::setVec2(std::string_view name, const glm::vec2& value) const {
    const std::string uniform(name);
    glUniform2fv(glGetUniformLocation(program_, uniform.c_str()), 1, glm::value_ptr(value));
}

void Shader::setVec3(std::string_view name, const glm::vec3& value) const {
    const std::string uniform(name);
    glUniform3fv(glGetUniformLocation(program_, uniform.c_str()), 1, glm::value_ptr(value));
}

void Shader::setFloat(std::string_view name, float value) const {
    const std::string uniform(name);
    glUniform1f(glGetUniformLocation(program_, uniform.c_str()), value);
}

void Shader::setInt(std::string_view name, int value) const {
    const std::string uniform(name);
    glUniform1i(glGetUniformLocation(program_, uniform.c_str()), value);
}

} // namespace engine2
