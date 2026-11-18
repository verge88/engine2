#include "engine2/PointShadowMaps.hpp"

#include "engine2/Mesh.hpp"
#include "engine2/Renderer.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

namespace engine2 {
namespace {

struct FaceOrientation {
    glm::vec3 direction;
    glm::vec3 up;
};

const std::array<FaceOrientation, 6> cubeFaces{{
    {{ 1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
    {{-1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
    {{0.0f,  1.0f, 0.0f}, {0.0f, 0.0f,  1.0f}},
    {{0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
    {{0.0f, 0.0f,  1.0f}, {0.0f, -1.0f, 0.0f}},
    {{0.0f, 0.0f, -1.0f}, {0.0f, -1.0f, 0.0f}}
}};

bool intersectsLightSphere(const RenderItem& item, const PointLight& light) {
    const Bounds& bounds = item.mesh->bounds();
    const glm::vec3 center = glm::vec3(
        item.model * glm::vec4((bounds.min + bounds.max) * 0.5f, 1.0f));
    const glm::vec3 extents = (bounds.max - bounds.min) * 0.5f;
    // The radius of the world OBB's enclosing sphere is conservative
    // for rotations, nonuniform scales and shears.
    const float radius = glm::length(glm::vec3(item.model[0])) * extents.x +
                         glm::length(glm::vec3(item.model[1])) * extents.y +
                         glm::length(glm::vec3(item.model[2])) * extents.z;
    return glm::length(center - light.position) <= light.range + radius;
}

void checkComplete() {
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Incomplete point-light shadow cubemap framebuffer");
}

} // namespace

PointShadowMaps::PointShadowMaps(const std::filesystem::path& shaderDirectory)
    : depthShader_(shaderDirectory / "point-shadow.vert",
                   shaderDirectory / "point-shadow.frag") {
    glGenFramebuffers(1, &framebuffer_);
    glGenTextures(static_cast<GLsizei>(cubemaps_.size()), cubemaps_.data());

    try {
        for (GLuint texture : cubemaps_) {
            glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
            for (int face = 0; face < 6; ++face) {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0,
                             GL_DEPTH_COMPONENT24, resolution, resolution,
                             0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
            }
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X, cubemaps_[0], 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        checkComplete();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    } catch (...) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        glDeleteTextures(static_cast<GLsizei>(cubemaps_.size()), cubemaps_.data());
        glDeleteFramebuffers(1, &framebuffer_);
        throw;
    }
}

PointShadowMaps::~PointShadowMaps() {
    glDeleteTextures(static_cast<GLsizei>(cubemaps_.size()), cubemaps_.data());
    if (framebuffer_) glDeleteFramebuffers(1, &framebuffer_);
}

std::size_t PointShadowMaps::render(const std::vector<RenderItem>& items,
                                    const std::vector<PointLight>& lights,
                                    std::size_t shadowLightCount) {
    const auto count = std::min({shadowLightCount, lights.size(), maxShadowLights});
    if (count == 0) return 0;

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, resolution, resolution);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);
    depthShader_.use();
    depthShader_.setInt("uBaseColorTexture", 0);

    std::size_t draws = 0;
    for (std::size_t lightIndex = 0; lightIndex < count; ++lightIndex) {
        const PointLight& light = lights[lightIndex];
        const float farPlane = std::max(light.range, 0.5f);
        const glm::mat4 projection =
            glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, farPlane);
        depthShader_.setVec3("uLightPosition", light.position);
        depthShader_.setFloat("uFarPlane", farPlane);

        for (std::size_t face = 0; face < cubeFaces.size(); ++face) {
            const auto& side = cubeFaces[face];
            const glm::mat4 view = glm::lookAt(
                light.position, light.position + side.direction, side.up);
            glFramebufferTexture2D(
                GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(face),
                cubemaps_[lightIndex], 0);
            checkComplete();
            glClear(GL_DEPTH_BUFFER_BIT);
            depthShader_.setMat4("uLightSpace", projection * view);

            for (const RenderItem& item : items) {
                if (!item.mesh || item.material.alphaMode == AlphaMode::Blend)
                    continue;
                if (!intersectsLightSphere(item, light)) continue;

                if (item.material.doubleSided) glDisable(GL_CULL_FACE);
                else glEnable(GL_CULL_FACE);
                depthShader_.setMat4("uModel", item.model);
                depthShader_.setInt("uAlphaMode",
                                   static_cast<int>(item.material.alphaMode));
                depthShader_.setFloat("uAlphaFactor", item.material.alpha);
                depthShader_.setFloat("uAlphaCutoff", item.material.alphaCutoff);
                depthShader_.setInt("uHasBaseColorTexture",
                                   item.material.baseColorTexture != 0 ? 1 : 0);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, item.material.baseColorTexture);
                item.mesh->draw();
                ++draws;
            }
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    return draws;
}

} // namespace engine2
