#pragma once

#include <glad/gl.h>

#include <cstdint>

namespace engine2 {

class Texture2D {
public:
    Texture2D(int width, int height, const std::uint8_t* rgba, bool srgb,
              GLint wrapS, GLint wrapT, GLint minFilter, GLint magFilter);
    ~Texture2D();

    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;

    [[nodiscard]] GLuint id() const { return id_; }

private:
    GLuint id_ = 0;
};

} // namespace engine2
