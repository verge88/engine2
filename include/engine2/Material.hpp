#pragma once

#include <glad/gl.h>
#include <glm/vec3.hpp>

namespace engine2 {

// Texture objects are owned by a LoadedScene; a RenderItem may reference them.
enum class AlphaMode : int {
    Opaque = 0,
    Mask = 1,
    Blend = 2
};

struct Material {
    glm::vec3 albedo{0.8f, 0.5f, 0.3f}; // linear color
    float metallic = 0.0f;
    float roughness = 0.5f;
    float alpha = 1.0f;
    float alphaCutoff = 0.5f;
    AlphaMode alphaMode = AlphaMode::Opaque;
    glm::vec3 emissive{0.0f};
    float occlusionStrength = 1.0f;

    GLuint baseColorTexture = 0;           // sRGB
    GLuint metallicRoughnessTexture = 0;   // G: roughness, B: metallic; linear
    GLuint normalTexture = 0;              // linear
    GLuint occlusionTexture = 0;           // linear
    GLuint emissiveTexture = 0;            // sRGB
    float normalScale = 1.0f;

    bool checker = false;
    bool doubleSided = false;
};

} // namespace engine2
