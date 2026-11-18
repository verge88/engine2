#include "engine2/GltfLoader.hpp"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine2 {
namespace {

struct AccessorView {
    const unsigned char* bytes = nullptr;
    std::size_t stride = 0;
    std::size_t count = 0;
    int type = 0;
    int componentType = 0;
    bool normalized = false;
};

std::size_t componentSize(int type) {
    switch (type) {
    case TINYGLTF_COMPONENT_TYPE_BYTE:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: return 1;
    case TINYGLTF_COMPONENT_TYPE_SHORT:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return 2;
    case TINYGLTF_COMPONENT_TYPE_INT:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
    case TINYGLTF_COMPONENT_TYPE_FLOAT: return 4;
    case TINYGLTF_COMPONENT_TYPE_DOUBLE: return 8;
    default: throw std::runtime_error("Unsupported glTF accessor component type");
    }
}

std::size_t componentCount(int type) {
    switch (type) {
    case TINYGLTF_TYPE_SCALAR: return 1;
    case TINYGLTF_TYPE_VEC2: return 2;
    case TINYGLTF_TYPE_VEC3: return 3;
    case TINYGLTF_TYPE_VEC4: return 4;
    case TINYGLTF_TYPE_MAT2: return 4;
    case TINYGLTF_TYPE_MAT3: return 9;
    case TINYGLTF_TYPE_MAT4: return 16;
    default: throw std::runtime_error("Unsupported glTF accessor type");
    }
}

template <typename T>
T readUnaligned(const unsigned char* ptr) {
    T value{};
    std::memcpy(&value, ptr, sizeof(T));
    return value;
}

AccessorView accessorView(const tinygltf::Model& model, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= model.accessors.size()) {
        throw std::runtime_error("Invalid glTF accessor index");
    }
    const auto& accessor = model.accessors[static_cast<std::size_t>(index)];
    if (accessor.sparse.isSparse) {
        throw std::runtime_error("Sparse glTF accessors are not supported yet");
    }
    if (accessor.bufferView < 0 ||
        static_cast<std::size_t>(accessor.bufferView) >= model.bufferViews.size()) {
        throw std::runtime_error("glTF accessor is missing a buffer view");
    }
    const auto& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    if (view.buffer < 0 || static_cast<std::size_t>(view.buffer) >= model.buffers.size()) {
        throw std::runtime_error("Invalid glTF buffer index");
    }

    const std::size_t packed = componentCount(accessor.type) *
                                componentSize(accessor.componentType);
    const int byteStride = accessor.ByteStride(view);
    if (byteStride < 0) throw std::runtime_error("Invalid glTF buffer stride");
    const std::size_t stride = static_cast<std::size_t>(byteStride);
    if (stride < packed) throw std::runtime_error("glTF accessor stride too small");

    const auto& data = model.buffers[static_cast<std::size_t>(view.buffer)].data;
    if (view.byteOffset > data.size() || view.byteLength > data.size() - view.byteOffset ||
        accessor.byteOffset > view.byteLength) {
        throw std::runtime_error("glTF accessor buffer range is out of bounds");
    }
    const std::size_t available = view.byteLength - accessor.byteOffset;
    if (accessor.count > 0 &&
        (packed > available ||
         (accessor.count - 1) > (available - packed) / stride)) {
        throw std::runtime_error("glTF accessor data is truncated");
    }
    const std::size_t offset = view.byteOffset + accessor.byteOffset;
    return {data.data() + offset, stride, accessor.count,
            accessor.type, accessor.componentType, accessor.normalized};
}

float readFloatComponent(const unsigned char* pointer, int type, bool normalized) {
    switch (type) {
    case TINYGLTF_COMPONENT_TYPE_FLOAT: return readUnaligned<float>(pointer);
    case TINYGLTF_COMPONENT_TYPE_BYTE: {
        const float v = static_cast<float>(readUnaligned<std::int8_t>(pointer));
        return normalized ? std::max(-1.0f, v / 127.0f) : v;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
        const float v = static_cast<float>(readUnaligned<std::uint8_t>(pointer));
        return normalized ? v / 255.0f : v;
    }
    case TINYGLTF_COMPONENT_TYPE_SHORT: {
        const float v = static_cast<float>(readUnaligned<std::int16_t>(pointer));
        return normalized ? std::max(-1.0f, v / 32767.0f) : v;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
        const float v = static_cast<float>(readUnaligned<std::uint16_t>(pointer));
        return normalized ? v / 65535.0f : v;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        return static_cast<float>(readUnaligned<std::uint32_t>(pointer));
    default: throw std::runtime_error("Unsupported glTF vertex component type");
    }
}

glm::vec4 readVector(const AccessorView& accessor, std::size_t index, std::size_t count) {
    if (index >= accessor.count || componentCount(accessor.type) != count) {
        throw std::runtime_error("Invalid glTF vertex attribute dimensions");
    }
    glm::vec4 value(0.0f);
    const unsigned char* bytes = accessor.bytes + index * accessor.stride;
    for (std::size_t i = 0; i < count; ++i) {
        value[static_cast<int>(i)] = readFloatComponent(
            bytes + i * componentSize(accessor.componentType),
            accessor.componentType, accessor.normalized);
    }
    return value;
}

std::uint32_t readIndex(const AccessorView& accessor, std::size_t index) {
    if (index >= accessor.count || accessor.type != TINYGLTF_TYPE_SCALAR) {
        throw std::runtime_error("Invalid glTF index accessor");
    }
    const auto* bytes = accessor.bytes + index * accessor.stride;
    switch (accessor.componentType) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: return readUnaligned<std::uint8_t>(bytes);
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return readUnaligned<std::uint16_t>(bytes);
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: return readUnaligned<std::uint32_t>(bytes);
    default: throw std::runtime_error("Invalid glTF index component type");
    }
}

void generateNormals(std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices) {
    for (auto& v : vertices) v.normal = glm::vec3(0.0f);
    for (std::size_t i = 0; i < indices.size(); i += 3) {
        auto& a = vertices[indices[i]];
        auto& b = vertices[indices[i + 1]];
        auto& c = vertices[indices[i + 2]];
        const glm::vec3 face = glm::cross(b.position - a.position, c.position - a.position);
        a.normal += face;
        b.normal += face;
        c.normal += face;
    }
    for (auto& v : vertices) {
        v.normal = glm::length(v.normal) > 1.0e-7f
                       ? glm::normalize(v.normal) : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

void generateTangents(std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices) {
    std::vector<glm::vec3> tangents(vertices.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> bitangents(vertices.size(), glm::vec3(0.0f));

    for (std::size_t i = 0; i < indices.size(); i += 3) {
        const auto i0 = indices[i];
        const auto i1 = indices[i + 1];
        const auto i2 = indices[i + 2];
        const glm::vec3 e1 = vertices[i1].position - vertices[i0].position;
        const glm::vec3 e2 = vertices[i2].position - vertices[i0].position;
        const glm::vec2 d1 = vertices[i1].uv - vertices[i0].uv;
        const glm::vec2 d2 = vertices[i2].uv - vertices[i0].uv;
        const float denom = d1.x * d2.y - d1.y * d2.x;
        if (std::abs(denom) < 1.0e-8f) continue;

        const glm::vec3 T = (e1 * d2.y - e2 * d1.y) / denom;
        const glm::vec3 B = (e2 * d1.x - e1 * d2.x) / denom;
        for (auto index : {i0, i1, i2}) {
            tangents[index] += T;
            bitangents[index] += B;
        }
    }

    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const glm::vec3 N = vertices[i].normal;
        glm::vec3 T = tangents[i] - N * glm::dot(N, tangents[i]);
        if (glm::length(T) < 1.0e-7f) {
            const glm::vec3 axis = std::abs(N.y) < 0.95f
                                       ? glm::vec3(0.0f, 1.0f, 0.0f)
                                       : glm::vec3(1.0f, 0.0f, 0.0f);
            T = glm::cross(axis, N);
        }
        T = glm::normalize(T);
        const float sign = glm::dot(glm::cross(N, T), bitangents[i]) < 0.0f ? -1.0f : 1.0f;
        vertices[i].tangent = glm::vec4(T, sign);
    }
}

struct PrimitiveRef {
    const Mesh* mesh;
    int materialIndex;
};

struct Context {
    const tinygltf::Model& source;
    LoadedScene& destination;
    std::unordered_map<std::uint64_t, GLuint> textureCache;

    GLuint uploadTexture(int textureIndex, bool srgb) {
        if (textureIndex < 0 || static_cast<std::size_t>(textureIndex) >= source.textures.size()) {
            return 0;
        }
        const auto key = (static_cast<std::uint64_t>(textureIndex) << 1) | (srgb ? 1u : 0u);
        if (const auto cached = textureCache.find(key); cached != textureCache.end()) {
            return cached->second;
        }
        const auto& texture = source.textures[static_cast<std::size_t>(textureIndex)];
        if (texture.source < 0 || static_cast<std::size_t>(texture.source) >= source.images.size()) {
            return 0;
        }
        const auto& image = source.images[static_cast<std::size_t>(texture.source)];
        if (image.width <= 0 || image.height <= 0 || image.bits != 8 ||
            image.component < 1 || image.component > 4) {
            std::cerr << "glTF: unsupported image format (expected 8-bit L/LA/RGB/RGBA)\n";
            return 0;
        }
        const std::size_t pixels = static_cast<std::size_t>(image.width) *
                                   static_cast<std::size_t>(image.height);
        if (pixels > std::numeric_limits<std::size_t>::max() / 4 ||
            pixels > image.image.size() / static_cast<std::size_t>(image.component)) {
            throw std::runtime_error("Truncated glTF image data");
        }
        std::vector<std::uint8_t> rgba(pixels * 4);
        for (std::size_t i = 0; i < pixels; ++i) {
            const auto* src = image.image.data() + i * static_cast<std::size_t>(image.component);
            auto* dst = rgba.data() + i * 4;
            if (image.component == 1) {
                dst[0] = dst[1] = dst[2] = src[0];
                dst[3] = 255;
            } else if (image.component == 2) {
                dst[0] = dst[1] = dst[2] = src[0];
                dst[3] = src[1];
            } else {
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = image.component == 4 ? src[3] : 255;
            }
        }

        GLint wrapS = GL_REPEAT, wrapT = GL_REPEAT;
        GLint minFilter = GL_LINEAR_MIPMAP_LINEAR, magFilter = GL_LINEAR;
        if (texture.sampler >= 0 &&
            static_cast<std::size_t>(texture.sampler) < source.samplers.size()) {
            const auto& sampler = source.samplers[static_cast<std::size_t>(texture.sampler)];
            const auto validWrap = [](int mode) {
                return mode == GL_CLAMP_TO_EDGE || mode == GL_MIRRORED_REPEAT || mode == GL_REPEAT;
            };
            if (validWrap(sampler.wrapS)) wrapS = sampler.wrapS;
            if (validWrap(sampler.wrapT)) wrapT = sampler.wrapT;
            if (sampler.magFilter == GL_NEAREST || sampler.magFilter == GL_LINEAR)
                magFilter = sampler.magFilter;
            switch (sampler.minFilter) {
            case GL_NEAREST: case GL_LINEAR: case GL_NEAREST_MIPMAP_NEAREST:
            case GL_LINEAR_MIPMAP_NEAREST: case GL_NEAREST_MIPMAP_LINEAR:
            case GL_LINEAR_MIPMAP_LINEAR: minFilter = sampler.minFilter; break;
            default: break;
            }
        }

        auto uploaded = std::make_unique<Texture2D>(
            image.width, image.height, rgba.data(), srgb, wrapS, wrapT, minFilter, magFilter);
        const GLuint handle = uploaded->id();
        destination.textures.push_back(std::move(uploaded));
        textureCache.emplace(key, handle);
        return handle;
    }

    template <typename TextureInfo>
    GLuint loadTexture(const TextureInfo& info, bool srgb) {
        if (info.index < 0) return 0;
        if (info.texCoord != 0) {
            std::cerr << "glTF: only TEXCOORD_0 supported; ignoring texture using UV set "
                      << info.texCoord << '\n';
            return 0;
        }
        return uploadTexture(info.index, srgb);
    }

    Material loadMaterial(int index) {
        Material material;
        material.albedo = glm::vec3(1.0f);
        material.metallic = 1.0f;
        material.roughness = 1.0f;
        if (index < 0 || static_cast<std::size_t>(index) >= source.materials.size()) {
            return material;
        }
        const auto& input = source.materials[static_cast<std::size_t>(index)];
        const auto& pbr = input.pbrMetallicRoughness;

        if (pbr.baseColorFactor.size() >= 3) {
            material.albedo = glm::vec3(
                static_cast<float>(pbr.baseColorFactor[0]),
                static_cast<float>(pbr.baseColorFactor[1]),
                static_cast<float>(pbr.baseColorFactor[2]));
        }

        if (pbr.baseColorFactor.size() >= 4) {
            material.alpha = static_cast<float>(pbr.baseColorFactor[3]);
        }
        if (input.alphaMode == "MASK") {
            material.alphaMode = AlphaMode::Mask;
            material.alphaCutoff = static_cast<float>(input.alphaCutoff);
        } else if (input.alphaMode == "BLEND") {
            material.alphaMode = AlphaMode::Blend;
        } else if (input.alphaMode != "OPAQUE") {
            throw std::runtime_error("Unsupported glTF alphaMode: " + input.alphaMode);
        }
        material.metallic = static_cast<float>(pbr.metallicFactor);
        material.roughness = static_cast<float>(pbr.roughnessFactor);
        if (input.emissiveFactor.size() >= 3) {
            material.emissive = glm::vec3(
                static_cast<float>(input.emissiveFactor[0]),
                static_cast<float>(input.emissiveFactor[1]),
                static_cast<float>(input.emissiveFactor[2]));
        }

        material.baseColorTexture = loadTexture(pbr.baseColorTexture, true);
        material.metallicRoughnessTexture = loadTexture(pbr.metallicRoughnessTexture, false);
        material.normalTexture = loadTexture(input.normalTexture, false);
        material.occlusionTexture = loadTexture(input.occlusionTexture, false);
        material.emissiveTexture = loadTexture(input.emissiveTexture, true);
        material.normalScale = static_cast<float>(input.normalTexture.scale);
        material.occlusionStrength = static_cast<float>(input.occlusionTexture.strength);
        material.doubleSided = input.doubleSided;
        return material;
    }
};

glm::mat4 nodeTransform(const tinygltf::Node& node) {
    if (node.matrix.size() == 16) {
        glm::mat4 transform(1.0f);
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                transform[col][row] =
                    static_cast<float>(node.matrix[static_cast<std::size_t>(col * 4 + row)]);
            }
        }
        return transform;
    }

    glm::mat4 transform(1.0f);
    if (node.translation.size() == 3) {
        transform = glm::translate(transform, glm::vec3(
            static_cast<float>(node.translation[0]),
            static_cast<float>(node.translation[1]),
            static_cast<float>(node.translation[2])));
    }
    if (node.rotation.size() == 4) {
        const glm::quat rotation(
            static_cast<float>(node.rotation[3]),
            static_cast<float>(node.rotation[0]),
            static_cast<float>(node.rotation[1]),
            static_cast<float>(node.rotation[2]));
        transform *= glm::mat4_cast(glm::normalize(rotation));
    }
    if (node.scale.size() == 3) {
        transform = glm::scale(transform, glm::vec3(
            static_cast<float>(node.scale[0]),
            static_cast<float>(node.scale[1]),
            static_cast<float>(node.scale[2])));
    }
    return transform;
}

std::vector<std::uint32_t> readIndices(const tinygltf::Model& source,
                                        const tinygltf::Primitive& primitive,
                                        std::size_t vertexCount) {
    std::vector<std::uint32_t> indices;
    if (primitive.indices >= 0) {
        const AccessorView data = accessorView(source, primitive.indices);
        indices.reserve(data.count);
        for (std::size_t i = 0; i < data.count; ++i) {
            indices.push_back(readIndex(data, i));
        }
    } else {
        if (vertexCount > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("glTF primitive has too many vertices");
        }
        indices.reserve(vertexCount);
        for (std::size_t i = 0; i < vertexCount; ++i)
            indices.push_back(static_cast<std::uint32_t>(i));
    }
    if (indices.empty() || indices.size() % 3 != 0) {
        throw std::runtime_error("Expected glTF triangle indices");
    }
    for (const auto index : indices) {
        if (index >= vertexCount) throw std::runtime_error("glTF vertex index out of bounds");
    }
    return indices;
}

std::unique_ptr<Mesh> loadPrimitive(const tinygltf::Model& source,
                                     const tinygltf::Primitive& primitive) {
    const auto positionIt = primitive.attributes.find("POSITION");
    if (positionIt == primitive.attributes.end())
        throw std::runtime_error("glTF mesh is missing POSITION");

    const AccessorView positions = accessorView(source, positionIt->second);
    if (positions.type != TINYGLTF_TYPE_VEC3 || positions.count == 0) {
        throw std::runtime_error("Invalid glTF POSITION attribute");
    }
    if (positions.count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("Too many vertices");
    }
    std::vector<Vertex> vertices(positions.count);
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        vertices[i].position = glm::vec3(readVector(positions, i, 3));
    }

    auto readAttribute = [&](const char* name, int type, auto writer) {
        const auto attr = primitive.attributes.find(name);
        if (attr == primitive.attributes.end()) return false;
        const AccessorView data = accessorView(source, attr->second);
        if (data.count != vertices.size() || data.type != type)
            throw std::runtime_error(std::string("Invalid glTF attribute: ") + name);
        for (std::size_t i = 0; i < vertices.size(); ++i) writer(vertices[i], data, i);
        return true;
    };

    const bool normals = readAttribute("NORMAL", TINYGLTF_TYPE_VEC3,
        [](Vertex& vertex, const AccessorView& data, std::size_t i) {
            vertex.normal = glm::vec3(readVector(data, i, 3));
        });
    readAttribute("TEXCOORD_0", TINYGLTF_TYPE_VEC2,
        [](Vertex& vertex, const AccessorView& data, std::size_t i) {
            vertex.uv = glm::vec2(readVector(data, i, 2));
        });
    const bool tangents = readAttribute("TANGENT", TINYGLTF_TYPE_VEC4,
        [](Vertex& vertex, const AccessorView& data, std::size_t i) {
            vertex.tangent = readVector(data, i, 4);
        });

    const auto indices = readIndices(source, primitive, vertices.size());
    if (!normals) generateNormals(vertices, indices);
    if (!tangents) generateTangents(vertices, indices);
    return std::make_unique<Mesh>(std::move(vertices), indices);
}

} // namespace

LoadedScene loadGltf(const std::filesystem::path& path) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string error;
    std::string warning;
    const auto file = path.string();
    const bool binary = path.extension() == ".glb" || path.extension() == ".GLB";
    const bool success = binary
        ? loader.LoadBinaryFromFile(&model, &error, &warning, file)
        : loader.LoadASCIIFromFile(&model, &error, &warning, file);
    if (!warning.empty()) std::cerr << "glTF warning: " << warning << '\n';
    if (!success) throw std::runtime_error("Failed to load " + file + ": " + error);

    LoadedScene output;
    Context context{model, output, {}};
    std::vector<std::vector<PrimitiveRef>> meshPrimitives(model.meshes.size());
    std::unordered_map<int, Material> materials;

    for (std::size_t i = 0; i < model.meshes.size(); ++i) {
        for (const auto& primitive : model.meshes[i].primitives) {
            if (primitive.mode != -1 && primitive.mode != TINYGLTF_MODE_TRIANGLES) {
                std::cerr << "glTF: skipping non-triangle primitive\n";
                continue;
            }
            if (!materials.contains(primitive.material)) {
                materials.emplace(primitive.material, context.loadMaterial(primitive.material));
            }
            auto mesh = loadPrimitive(model, primitive);
            const Mesh* ptr = mesh.get();
            output.meshes.push_back(std::move(mesh));
            meshPrimitives[i].push_back({ptr, primitive.material});
        }
    }

    std::vector<bool> active(model.nodes.size(), false);
    std::function<void(int, const glm::mat4&)> visit =
        [&](int index, const glm::mat4& parent) {
            if (index < 0 || static_cast<std::size_t>(index) >= model.nodes.size())
                throw std::runtime_error("Invalid glTF node index");
            const std::size_t id = static_cast<std::size_t>(index);
            if (active[id]) throw std::runtime_error("Cycle in glTF node hierarchy");
            active[id] = true;
            const auto& node = model.nodes[id];
            const glm::mat4 transform = parent * nodeTransform(node);

            if (node.mesh >= 0) {
                if (static_cast<std::size_t>(node.mesh) >= meshPrimitives.size())
                    throw std::runtime_error("Invalid glTF node mesh index");
                for (const auto& primitive : meshPrimitives[static_cast<std::size_t>(node.mesh)]) {
                    RenderItem item;
                    item.mesh = primitive.mesh;
                    item.model = transform;
                    item.material = materials.at(primitive.materialIndex);
                    output.items.push_back(item);
                }
            }
            for (const int child : node.children) visit(child, transform);
            active[id] = false;
        };

    if (!model.scenes.empty()) {
        const int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
        if (static_cast<std::size_t>(sceneIndex) >= model.scenes.size())
            throw std::runtime_error("Invalid glTF default scene");
        for (const int node : model.scenes[static_cast<std::size_t>(sceneIndex)].nodes)
            visit(node, glm::mat4(1.0f));
    } else {
        for (std::size_t i = 0; i < model.nodes.size(); ++i)
            visit(static_cast<int>(i), glm::mat4(1.0f));
    }
    std::cout << "glTF: loaded " << output.meshes.size() << " primitives, "
              << output.items.size() << " scene instances, "
              << output.textures.size() << " GPU textures from " << file << '\n';
    return output;
}

} // namespace engine2
