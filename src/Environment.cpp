#include "engine2/Environment.hpp"

#include <stb_image.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace engine2 {
namespace {

constexpr int environmentResolution = 128;
constexpr int irradianceResolution = 16;
constexpr int specularResolution = 64;
constexpr int specularMipLevels = 5;
constexpr int brdfResolution = 128;

void requireComplete(const char* pass) {
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error(std::string("Incomplete framebuffer during ") + pass);
    }
}

GLuint makeCubemap(int resolution, int mipLevels) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
    for (int mip = 0; mip < mipLevels; ++mip) {
        const int edge = std::max(resolution >> mip, 1);
        for (int face = 0; face < 6; ++face) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, mip, GL_RGB16F,
                         edge, edge, 0, GL_RGB, GL_FLOAT, nullptr);
        }
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                    mipLevels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, mipLevels - 1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    return texture;
}

std::vector<float> proceduralSky(int width, int height) {
    std::vector<float> pixels(static_cast<std::size_t>(width) *
                              static_cast<std::size_t>(height) * 3);
    const glm::vec3 sun = glm::normalize(glm::vec3(0.55f, 0.8f, -0.25f));
    for (int y = 0; y < height; ++y) {
        const float latitude = ((static_cast<float>(y) + 0.5f) / height - 0.5f)
                               * glm::pi<float>();
        for (int x = 0; x < width; ++x) {
            const float longitude = ((static_cast<float>(x) + 0.5f) / width - 0.5f)
                                    * glm::two_pi<float>();
            const glm::vec3 dir(std::cos(latitude) * std::cos(longitude),
                                std::sin(latitude),
                                std::cos(latitude) * std::sin(longitude));
            const float horizon = glm::smoothstep(-0.1f, 0.65f, dir.y);
            glm::vec3 radiance = glm::mix(glm::vec3(0.16f, 0.14f, 0.12f),
                                           glm::vec3(0.32f, 0.50f, 0.90f), horizon);
            radiance += glm::vec3(1.0f, 0.55f, 0.18f) *
                        std::pow(glm::clamp(1.0f - std::abs(dir.y), 0.0f, 1.0f), 5.0f) *
                        0.18f;
            const float disk = std::pow(glm::max(glm::dot(dir, sun), 0.0f), 4096.0f);
            radiance += glm::vec3(45.0f, 36.0f, 25.0f) * disk;
            const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 3;
            pixels[i] = radiance.r;
            pixels[i + 1] = radiance.g;
            pixels[i + 2] = radiance.b;
        }
    }
    return pixels;
}

} // namespace

Environment::Environment(const std::filesystem::path& shaderDirectory,
                         const std::optional<std::filesystem::path>& hdrFile)
    : filterShader_(shaderDirectory / "fullscreen.vert",
                    shaderDirectory / "environment-filter.frag"),
      brdfShader_(shaderDirectory / "fullscreen.vert",
                  shaderDirectory / "brdf-lut.frag"),
      skyShader_(shaderDirectory / "fullscreen.vert",
                 shaderDirectory / "sky.frag") {
    glGenFramebuffers(1, &frameBuffer_);
    glGenVertexArrays(1, &fullScreenVao_);
    try {
        createPanorama(hdrFile);
        createMaps();
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        std::cout << "IBL: diffuse irradiance, 5-level GGX specular and BRDF LUT ready\n";
    } catch (...) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (panorama_) glDeleteTextures(1, &panorama_);
        if (environmentMap_) glDeleteTextures(1, &environmentMap_);
        if (irradianceMap_) glDeleteTextures(1, &irradianceMap_);
        if (prefilterMap_) glDeleteTextures(1, &prefilterMap_);
        if (brdfLut_) glDeleteTextures(1, &brdfLut_);
        if (frameBuffer_) glDeleteFramebuffers(1, &frameBuffer_);
        if (fullScreenVao_) glDeleteVertexArrays(1, &fullScreenVao_);
        throw;
    }
}

Environment::~Environment() {
    if (panorama_) glDeleteTextures(1, &panorama_);
    if (environmentMap_) glDeleteTextures(1, &environmentMap_);
    if (irradianceMap_) glDeleteTextures(1, &irradianceMap_);
    if (prefilterMap_) glDeleteTextures(1, &prefilterMap_);
    if (brdfLut_) glDeleteTextures(1, &brdfLut_);
    if (frameBuffer_) glDeleteFramebuffers(1, &frameBuffer_);
    if (fullScreenVao_) glDeleteVertexArrays(1, &fullScreenVao_);
}

void Environment::drawTriangle() const {
    glBindVertexArray(fullScreenVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void Environment::createPanorama(const std::optional<std::filesystem::path>& hdrFile) {
    int width = 256, height = 128;
    std::vector<float> generated;
    float* decoded = nullptr;
    const float* pixels = nullptr;

    if (hdrFile) {
        int channels = 0;
        decoded = stbi_loadf(hdrFile->string().c_str(), &width, &height, &channels, 3);
        if (!decoded) {
            throw std::runtime_error("Cannot load HDR panorama " + hdrFile->string() +
                                     ": " + stbi_failure_reason());
        }
        pixels = decoded;
        std::cout << "IBL: loading equirectangular panorama " << hdrFile->string() << '\n';
    } else {
        generated = proceduralSky(width, height);
        pixels = generated.data();
        std::cout << "IBL: generating procedural HDR sky\n";
    }

    glGenTextures(1, &panorama_);
    glBindTexture(GL_TEXTURE_2D, panorama_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0,
                 GL_RGB, GL_FLOAT, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (decoded) stbi_image_free(decoded);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Environment::renderCubeFaces(GLuint target, int resolution, int mipLevel,
                                  int mode, float roughness) {
    glViewport(0, 0, resolution, resolution);
    filterShader_.use();
    filterShader_.setInt("uMode", mode);
    filterShader_.setFloat("uRoughness", roughness);
    for (int face = 0; face < 6; ++face) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, target, mipLevel);
        requireComplete("environment cubemap");
        filterShader_.setInt("uFace", face);
        glClear(GL_COLOR_BUFFER_BIT);
        drawTriangle();
    }
}

void Environment::createMaps() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer_);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    environmentMap_ = makeCubemap(environmentResolution, 8);
    irradianceMap_ = makeCubemap(irradianceResolution, 1);
    prefilterMap_ = makeCubemap(specularResolution, specularMipLevels);

    filterShader_.use();
    filterShader_.setInt("uPanorama", 0);
    filterShader_.setInt("uEnvironment", 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, panorama_);
    renderCubeFaces(environmentMap_, environmentResolution, 0, 0, 0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, environmentMap_);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, environmentMap_);
    renderCubeFaces(irradianceMap_, irradianceResolution, 0, 1, 0.0f);
    for (int mip = 0; mip < specularMipLevels; ++mip) {
        const float roughness = static_cast<float>(mip) / (specularMipLevels - 1);
        renderCubeFaces(prefilterMap_, specularResolution >> mip, mip, 2, roughness);
    }

    glGenTextures(1, &brdfLut_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, brdfLut_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, brdfResolution, brdfResolution,
                 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, brdfLut_, 0);
    requireComplete("BRDF integration");
    glViewport(0, 0, brdfResolution, brdfResolution);
    brdfShader_.use();
    glClear(GL_COLOR_BUFFER_BIT);
    drawTriangle();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Environment::drawSky(const glm::mat4& view, const glm::mat4& projection,
                          const glm::vec3& cameraPosition) const {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    skyShader_.use();
    skyShader_.setMat4("uInverseViewProjection", glm::inverse(projection * view));
    skyShader_.setVec3("uCameraPosition", cameraPosition);
    skyShader_.setInt("uEnvironment", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, environmentMap_);
    drawTriangle();
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

} // namespace engine2
