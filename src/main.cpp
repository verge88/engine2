#include "engine2/Camera.hpp"
#include "engine2/GltfLoader.hpp"
#include "engine2/Mesh.hpp"
#include "engine2/Renderer.hpp"
#include "engine2/Scene.hpp"
#include "engine2/SceneIO.hpp"
#include "engine2/SceneInspector.hpp"
#include "engine2/SceneGui.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class GlfwSession {
public:
    GlfwSession() {
        glfwSetErrorCallback([](int code, const char* message) {
            std::cerr << "GLFW [" << code << "]: " << (message ? message : "unknown") << '\n';
        });
        if (glfwInit() != GLFW_TRUE) {
            throw std::runtime_error("GLFW initialization failed");
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
        glfwWindowHint(GLFW_SAMPLES, 4);
        window_ = glfwCreateWindow(1280, 720, "engine2 | glTF PBR renderer", nullptr, nullptr);
        if (!window_) {
            glfwTerminate();
            throw std::runtime_error("Failed to create an OpenGL 3.3 window");
        }
        glfwMakeContextCurrent(window_);
    }

    ~GlfwSession() {
        if (window_) glfwDestroyWindow(window_);
        glfwTerminate();
    }

    GlfwSession(const GlfwSession&) = delete;
    GlfwSession& operator=(const GlfwSession&) = delete;
    [[nodiscard]] GLFWwindow* window() const { return window_; }

private:
    GLFWwindow* window_ = nullptr;
};

struct Arguments {
    std::optional<std::uint64_t> frames;
    std::optional<std::filesystem::path> model;
    std::optional<std::filesystem::path> screenshot;
    std::optional<std::filesystem::path> saveScene;
    std::optional<std::filesystem::path> loadScene;
    std::optional<std::filesystem::path> editScene;
    std::optional<std::filesystem::path> inspectScene;
    std::optional<bool> guiVisible;
    std::optional<std::size_t> guiSelection;
    engine2::RendererOptions renderer;
    int pointLights = 3;
    std::optional<float> fixedLightTime;
    bool physicsDemo = false;
    bool collisionDemo = false;
    bool boxDemo = false;
    std::optional<float> simulationDelta;
    bool stats = false;
    bool help = false;
};

float parseSetting(const std::string& text, const char* setting, float minimum, float maximum) {
    std::size_t parsed = 0;
    const float value = std::stof(text, &parsed);
    if (parsed != text.size() || !std::isfinite(value) ||
        value < minimum || value > maximum) {
        throw std::invalid_argument(std::string(setting) + " is out of range");
    }
    return value;
}

Arguments parseArgs(int argc, char** argv) {
    Arguments args;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            args.help = true;
        } else if (arg == "--frames" && i + 1 < argc) {
            const std::string count(argv[++i]);
            std::size_t parsed = 0;
            const std::uint64_t value = std::stoull(count, &parsed);
            if (parsed != count.size() || value == 0)
                throw std::invalid_argument("--frames expects a positive integer");
            args.frames = value;
        } else if (arg == "--model" && i + 1 < argc) {
            args.model = std::filesystem::path(argv[++i]);
        } else if (arg == "--environment" && i + 1 < argc) {
            args.renderer.hdrEnvironment = std::filesystem::path(argv[++i]);
        } else if (arg == "--auto-exposure" && i + 1 < argc) {
            const float setting = parseSetting(argv[++i], "--auto-exposure", 0.0f, 1.0f);
            if (setting != std::floor(setting))
                throw std::invalid_argument("--auto-exposure requires 0 or 1");
            args.renderer.autoExposure = setting != 0.0f;
        } else if (arg == "--exposure-key" && i + 1 < argc) {
            args.renderer.exposureKey =
                parseSetting(argv[++i], "--exposure-key", 0.02f, 1.0f);
        } else if (arg == "--exposure-speed" && i + 1 < argc) {
            args.renderer.exposureSpeed =
                parseSetting(argv[++i], "--exposure-speed", 0.0f, 12.0f);
        } else if (arg == "--bloom-threshold" && i + 1 < argc) {
            args.renderer.bloomThreshold =
                parseSetting(argv[++i], "--bloom-threshold", 0.05f, 10.0f);
        } else if (arg == "--exposure" && i + 1 < argc) {
            args.renderer.exposure = parseSetting(argv[++i], "--exposure", 0.05f, 16.0f);
        } else if (arg == "--bloom" && i + 1 < argc) {
            args.renderer.bloomStrength = parseSetting(argv[++i], "--bloom", 0.0f, 2.0f);
        } else if (arg == "--physics-demo" && i + 1 < argc) {
            const float value = parseSetting(argv[++i], "--physics-demo", 0.0f, 1.0f);
            if (value != std::floor(value))
                throw std::invalid_argument("--physics-demo requires 0 or 1");
            args.physicsDemo = value != 0.0f;
        } else if (arg == "--collision-demo" && i + 1 < argc) {
            const float value = parseSetting(argv[++i], "--collision-demo", 0.0f, 1.0f);
            if (value != std::floor(value))
                throw std::invalid_argument("--collision-demo requires 0 or 1");
            args.collisionDemo = value != 0.0f;
        } else if (arg == "--box-demo" && i + 1 < argc) {
            const float value = parseSetting(argv[++i], "--box-demo", 0.0f, 1.0f);
            if (value != std::floor(value))
                throw std::invalid_argument("--box-demo requires 0 or 1");
            args.boxDemo = value != 0.0f;
        } else if (arg == "--sim-dt" && i + 1 < argc) {
            args.simulationDelta = parseSetting(argv[++i], "--sim-dt", 0.0f, 0.25f);
        } else if (arg == "--lights" && i + 1 < argc) {
            const float value = parseSetting(argv[++i], "--lights", 0.0f, 8.0f);
            if (value != std::floor(value))
                throw std::invalid_argument("--lights requires an integer 0..8");
            args.pointLights = static_cast<int>(value);
        } else if (arg == "--sun-cascades" && i + 1 < argc) {
            const float value = parseSetting(argv[++i], "--sun-cascades", 0.0f, 3.0f);
            if (value != 0.0f && value != 1.0f && value != 3.0f)
                throw std::invalid_argument("--sun-cascades requires 0, 1 or 3");
            args.renderer.sunCascades = static_cast<int>(value);
        } else if (arg == "--sun-distance" && i + 1 < argc) {
            args.renderer.sunShadowDistance =
                parseSetting(argv[++i], "--sun-distance", 5.0f, 150.0f);
        } else if (arg == "--cascade-debug" && i + 1 < argc) {
            const float value = parseSetting(argv[++i], "--cascade-debug", 0.0f, 1.0f);
            if (value != std::floor(value))
                throw std::invalid_argument("--cascade-debug requires 0 or 1");
            args.renderer.cascadeDebug = value != 0.0f;
        } else if (arg == "--point-shadows" && i + 1 < argc) {
            const float value = parseSetting(argv[++i], "--point-shadows", 0.0f, 2.0f);
            if (value != std::floor(value))
                throw std::invalid_argument("--point-shadows requires an integer 0..2");
            args.renderer.pointShadowCount = static_cast<std::size_t>(value);
        } else if (arg == "--light-time" && i + 1 < argc) {
            args.fixedLightTime = parseSetting(argv[++i], "--light-time", 0.0f, 1000000.0f);
        } else if (arg == "--taa" && i + 1 < argc) {
            const float value = parseSetting(argv[++i], "--taa", 0.0f, 1.0f);
            if (value != std::floor(value))
                throw std::invalid_argument("--taa requires 0 or 1");
            args.renderer.taa = value != 0.0f;
        } else if (arg == "--taa-weight" && i + 1 < argc) {
            args.renderer.taaHistoryWeight =
                parseSetting(argv[++i], "--taa-weight", 0.0f, 0.98f);
        } else if (arg == "--ssao" && i + 1 < argc) {
            const float value = parseSetting(argv[++i], "--ssao", 0.0f, 1.0f);
            if (value != std::floor(value))
                throw std::invalid_argument("--ssao requires 0 or 1");
            args.renderer.ssao = value != 0.0f;
        } else if (arg == "--culling" && i + 1 < argc) {
            const float value = parseSetting(argv[++i], "--culling", 0.0f, 1.0f);
            if (value != std::floor(value))
                throw std::invalid_argument("--culling requires 0 or 1");
            args.renderer.frustumCulling = value != 0.0f;
        } else if (arg == "--stats") {
            args.stats = true;
        } else if (arg == "--gui" && i + 1 < argc) {
            const float value = parseSetting(argv[++i], "--gui", 0.0f, 1.0f);
            if (value != std::floor(value))
                throw std::invalid_argument("--gui requires 0 or 1");
            args.guiVisible = value != 0.0f;
        } else if (arg == "--gui-select" && i + 1 < argc) {
            const std::string value(argv[++i]);
            std::size_t parsed = 0;
            const unsigned long long index = std::stoull(value, &parsed);
            if (parsed != value.size() || value[0] == '-' ||
                index > 1000000ULL)
                throw std::invalid_argument("--gui-select requires an entity ordinal 0..1000000");
            args.guiSelection = static_cast<std::size_t>(index);
        } else if (arg == "--edit-scene" && i + 1 < argc) {
            args.editScene = std::filesystem::path(argv[++i]);
        } else if (arg == "--inspect-scene" && i + 1 < argc) {
            args.inspectScene = std::filesystem::path(argv[++i]);
        } else if (arg == "--save-scene" && i + 1 < argc) {
            args.saveScene = std::filesystem::path(argv[++i]);
        } else if (arg == "--load-scene" && i + 1 < argc) {
            args.loadScene = std::filesystem::path(argv[++i]);
        } else if (arg == "--screenshot" && i + 1 < argc) {
            args.screenshot = std::filesystem::path(argv[++i]);
        } else {
            throw std::invalid_argument("Unknown or incomplete argument: " + arg);
        }
    }
    return args;
}

void saveScreenshot(const std::filesystem::path& path, int width, int height) {
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glPixelStorei(GL_PACK_ALIGNMENT, 4);

    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("Cannot write screenshot: " + path.string());
    output << "P6\n" << width << ' ' << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y) {
        const auto* line = pixels.data() + static_cast<std::size_t>(y) * width * 3;
        output.write(reinterpret_cast<const char*>(line),
                     static_cast<std::streamsize>(width) * 3);
    }
    if (!output) throw std::runtime_error("Screenshot write failed: " + path.string());
    std::cout << "Screenshot saved: " << path << '\n';
}

std::filesystem::path shaderDirectory(const char* executable) {
    const auto executableDir = std::filesystem::absolute(executable).parent_path();
    return executableDir / "shaders";
}

std::vector<engine2::PointLight> animatedLights(int count, double seconds) {
    constexpr std::array<glm::vec3, 8> palette{{
        {1.00f, 0.22f, 0.14f}, {0.10f, 0.42f, 1.00f},
        {1.00f, 0.80f, 0.32f}, {0.30f, 1.00f, 0.52f},
        {0.92f, 0.27f, 0.90f}, {0.40f, 0.86f, 1.00f},
        {1.00f, 0.58f, 0.28f}, {0.76f, 0.67f, 1.00f}
    }};
    std::vector<engine2::PointLight> lights;
    lights.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const float phase = static_cast<float>(i) * 6.2831853f /
                            static_cast<float>(std::max(count, 1));
        const float angle = phase + static_cast<float>(seconds) * 0.18f;
        engine2::PointLight light;
        light.position = glm::vec3(std::cos(angle) * 4.2f,
                                    2.4f + std::sin(angle * 1.7f) * 0.4f,
                                    std::sin(angle) * 4.2f);
        light.color = palette[static_cast<std::size_t>(i)];
        light.intensity = 52.0f;
        light.range = 9.0f;
        lights.push_back(light);
    }
    return lights;
}

std::vector<engine2::RenderItem> createScene(const engine2::Mesh& sphere,
                                              const engine2::Mesh& floor) {
    std::vector<engine2::RenderItem> items;
    items.reserve(26);
    engine2::RenderItem ground;
    ground.mesh = &floor;
    ground.model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    ground.model = glm::scale(ground.model, glm::vec3(32.0f, 1.0f, 32.0f));
    ground.material.albedo = glm::vec3(0.3f);
    ground.material.roughness = 0.88f;
    ground.material.checker = true;
    items.push_back(ground);

    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 5; ++column) {
            engine2::RenderItem item;
            item.mesh = &sphere;
            item.model = glm::translate(glm::mat4(1.0f),
                glm::vec3((column - 2) * 2.1f, -0.52f, (row - 2) * 2.1f));
            item.model = glm::scale(item.model, glm::vec3(0.48f));
            item.material.albedo = glm::vec3(0.9f, 0.52f, 0.26f);
            item.material.metallic = static_cast<float>(row) / 4.0f;
            item.material.roughness = 0.08f + static_cast<float>(column) * 0.21f;
            items.push_back(item);
        }
    }
    return items;
}

int run(int argc, char** argv) {
    const Arguments args = parseArgs(argc, argv);
    if (args.help) {
        std::cout << "Usage: engine2 [--model scene.glb] [--environment sky.hdr] [--frames N]\n"
                     "               [--lights 0..8] [--point-shadows 0..2] [--light-time seconds]\n"
                     "               [--ssao 0|1] [--culling 0|1] [--auto-exposure 0|1]\n"
                     "               [--exposure X] [--exposure-key 0.02..1] [--exposure-speed 0..12]\n"
                     "               [--bloom 0..2] [--bloom-threshold 0.05..10]\n"
                     "               [--physics-demo 0|1] [--collision-demo 0|1] [--box-demo 0|1]\n"
                     "               [--sim-dt 0..0.25]\n"
                     "               [--save-scene world.json] [--load-scene world.json]\n"
                     "               [--edit-scene edits.json] [--inspect-scene report.json]\n"
                     "               [--gui 0|1] [--gui-select entity-index]\n"
                     "               [--stats] [--screenshot frame.ppm]\n";
        std::cout << "Controls: WASD move, mouse look, Space/Ctrl up/down, "
                     "Shift sprint, wheel FOV, T TAA, E auto-exposure, F1 GUI, Tab cursor, Esc exit.\n";
        return 0;
    }

    if ((args.physicsDemo || args.collisionDemo || args.boxDemo) && args.model)
        throw std::invalid_argument("Physics/collision demos require procedural scene (without --model)");
    if (args.loadScene && (args.physicsDemo || args.collisionDemo || args.boxDemo))
        throw std::invalid_argument("--load-scene cannot be combined with physics/collision demos");

    GlfwSession session;
    GLFWwindow* window = session.window();
    if (gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress)) == 0) {
        throw std::runtime_error("Failed to load OpenGL 3.3 functions");
    }

    std::cout << "OpenGL: " << reinterpret_cast<const char*>(glGetString(GL_VERSION))
              << "\nGLSL: " << reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION))
              << "\n";

    glfwSwapInterval(1);
    engine2::Camera camera;
    glfwSetWindowUserPointer(window, &camera);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) {
        auto* instance = static_cast<engine2::Camera*>(glfwGetWindowUserPointer(w));
        if (glfwGetInputMode(w, GLFW_CURSOR) == GLFW_CURSOR_DISABLED) {
            instance->onMouseMove(x, y);
        }
    });
    glfwSetScrollCallback(window, [](GLFWwindow* w, double, double y) {
        if (glfwGetInputMode(w, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
            static_cast<engine2::Camera*>(glfwGetWindowUserPointer(w))->onScroll(y);
    });

    // All GL resources must be destroyed before GlfwSession destroys the context.
    engine2::Renderer renderer(shaderDirectory(argv[0]), args.renderer);
    std::optional<engine2::LoadedScene> imported;
    std::unique_ptr<engine2::Mesh> sphere;
    std::unique_ptr<engine2::Mesh> floor;
    std::unique_ptr<engine2::Mesh> cube;
    std::vector<engine2::RenderItem> initialScene;
    engine2::SceneAssetRegistry assetRegistry;
    if (args.model) {
        imported.emplace(engine2::loadGltf(*args.model));
        initialScene = imported->items;
        if (initialScene.empty())
            throw std::runtime_error("glTF scene has no renderable primitives");
        // Stable references refer to resource *indices*, not GPU texture IDs
        // or the current process's Mesh memory addresses.
        for (std::size_t i = 0; i < imported->meshes.size(); ++i) {
            assetRegistry.registerMesh("gltf/mesh/" + std::to_string(i),
                                       imported->meshes[i].get());
        }
        for (std::size_t i = 0; i < imported->textures.size(); ++i) {
            assetRegistry.registerTexture("gltf/texture/" + std::to_string(i),
                                          imported->textures[i]->id());
        }
    } else {
        sphere = std::make_unique<engine2::Mesh>(engine2::Mesh::sphere());
        floor = std::make_unique<engine2::Mesh>(engine2::Mesh::plane());
        cube = std::make_unique<engine2::Mesh>(engine2::Mesh::cube());
        assetRegistry.registerMesh("procedural/cube", cube.get());
        assetRegistry.registerMesh("procedural/sphere", sphere.get());
        assetRegistry.registerMesh("procedural/floor", floor.get());
        initialScene = createScene(*sphere, *floor);
    }

    // A load replaces the default runtime Scene. The caller must preload the
    // same procedural asset set, or --model with the same original glTF.
    // The model/texture GPU owners remain alive while Scene uses their refs.
    engine2::Scene world;
    std::vector<engine2::Entity> lightEntities;
    if (args.loadScene) {
        world = engine2::loadScene(*args.loadScene, assetRegistry);
        std::cout << "Scene loaded: " << *args.loadScene
                  << " entities=" << world.stats().entities << '\n';
    } else {
        for (std::size_t i = 0; i < initialScene.size(); ++i) {
            const engine2::RenderItem& item = initialScene[i];
            const engine2::Entity entity =
                world.createEntity("Mesh " + std::to_string(i));
            world.setLocalTransform(entity, item.model);
            world.setRenderer(entity, {item.mesh, item.material});
        }

        // Lights are ordinary Scene entities, and are animated only for
        // newly generated demo scenes. A loaded scene retains saved lights.
        lightEntities.reserve(static_cast<std::size_t>(args.pointLights));
        for (int i = 0; i < args.pointLights; ++i)
            lightEntities.push_back(
                world.createEntity("Point Light " + std::to_string(i)));
    }

    // Optional playground for the fixed-step gravity + ground collision.
    // Disabled by default to keep all previous glTF/screenshots comparable.
    std::optional<engine2::Entity> fallingSphere;
    if (args.physicsDemo) {
        fallingSphere = world.createEntity("Physics Ball");
        glm::mat4 transform = glm::translate(
            glm::mat4(1.0f), glm::vec3(-1.05f, 3.4f, -1.05f));
        transform = glm::scale(transform, glm::vec3(0.5f));
        world.setLocalTransform(*fallingSphere, transform);

        engine2::Material gold;
        gold.albedo = glm::vec3(0.95f, 0.64f, 0.16f);
        gold.metallic = 0.8f;
        gold.roughness = 0.2f;
        world.setRenderer(*fallingSphere, {sphere.get(), gold});

        engine2::RigidBodyComponent body;
        body.radius = 0.5f;
        body.groundHeight = -1.0f;
        body.restitution = 0.45f;
        world.setRigidBody(*fallingSphere, body);
    }

    std::optional<std::array<engine2::Entity, 2>> collisionBalls;
    if (args.collisionDemo) {
        // Two isolated dynamic sphere colliders: same mesh resource, different
        // materials, masses and components. They head toward one another and
        // rebound at the same fixed rate regardless of rendering FPS.
        std::array<engine2::Entity, 2> handles{};
        for (int i = 0; i < 2; ++i) {
            const bool left = i == 0;
            handles[static_cast<std::size_t>(i)] =
                world.createEntity(left ? "Collision Red" : "Collision Blue");
            const glm::vec3 center(left ? -1.35f : 1.35f,
                                   1.15f, 1.5f);
            glm::mat4 transform =
                glm::translate(glm::mat4(1.0f), center);
            transform = glm::scale(transform, glm::vec3(0.55f));
            world.setLocalTransform(handles[static_cast<std::size_t>(i)],
                                    transform);

            engine2::Material material;
            material.albedo = left ? glm::vec3(0.94f, 0.16f, 0.13f)
                                   : glm::vec3(0.09f, 0.43f, 0.98f);
            material.metallic = 0.15f;
            material.roughness = 0.29f;
            world.setRenderer(handles[static_cast<std::size_t>(i)],
                              {sphere.get(), material});
            engine2::RigidBodyComponent body;
            body.gravity = false;
            body.radius = 0.55f;
            body.mass = 1.0f;
            body.restitution = 0.92f;
            body.velocity = glm::vec3(left ? 2.5f : -2.5f, 0.0f, 0.0f);
            world.setRigidBody(handles[static_cast<std::size_t>(i)], body);
        }
        collisionBalls = handles;
    }

    std::optional<std::array<engine2::Entity, 2>> boxDemoEntities;
    if (args.boxDemo) {
        // One static AABB and a dynamic sphere with a world-space radius.
        // Demonstrates contact generation and impulse response without a
        // dependency on the mesh scale or the procedural floor mesh.
        std::array<engine2::Entity, 2> handles{
            world.createEntity("Box obstacle"),
            world.createEntity("Sphere projectile")
        };
        glm::mat4 obstacleModel =
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.15f, 1.5f));
        obstacleModel = glm::scale(obstacleModel, glm::vec3(1.0f));
        world.setLocalTransform(handles[0], obstacleModel);
        engine2::Material boxMaterial;
        boxMaterial.albedo = glm::vec3(0.08f, 0.65f, 0.75f);
        boxMaterial.metallic = 0.30f;
        boxMaterial.roughness = 0.36f;
        world.setRenderer(handles[0], {cube.get(), boxMaterial});
        engine2::RigidBodyComponent boxBody;
        boxBody.shape = engine2::ColliderShape::Box;
        boxBody.halfExtents = glm::vec3(0.5f);
        boxBody.isStatic = true;
        boxBody.gravity = false;
        boxBody.restitution = 0.85f;
        world.setRigidBody(handles[0], boxBody);

        glm::mat4 sphereModel =
            glm::translate(glm::mat4(1.0f), glm::vec3(-1.65f, 1.15f, 1.5f));
        sphereModel = glm::scale(sphereModel, glm::vec3(0.45f));
        world.setLocalTransform(handles[1], sphereModel);
        engine2::Material ballMaterial;
        ballMaterial.albedo = glm::vec3(0.92f, 0.18f, 0.13f);
        ballMaterial.metallic = 0.05f;
        ballMaterial.roughness = 0.25f;
        world.setRenderer(handles[1], {sphere.get(), ballMaterial});
        engine2::RigidBodyComponent sphereBody;
        sphereBody.radius = 0.45f;
        sphereBody.gravity = false;
        sphereBody.restitution = 0.85f;
        sphereBody.velocity = glm::vec3(3.2f, 0.0f, 0.0f);
        world.setRigidBody(handles[1], sphereBody);
        boxDemoEntities = handles;
    }

    if (args.editScene) {
        // Generated demo lights do not yet have PointLight components until
        // the first frame. Initialize them before applying editor changes.
        // Editing freezes demo light animation so that it cannot overwrite
        // the user's explicit changes to position and intensity.
        if (!args.loadScene) {
            const auto lights = animatedLights(
                args.pointLights, args.fixedLightTime.value_or(0.0f));
            for (std::size_t i = 0; i < lightEntities.size(); ++i) {
                world.setLocalTransform(lightEntities[i],
                    glm::translate(glm::mat4(1.0f), lights[i].position));
                engine2::PointLight light = lights[i];
                light.position = glm::vec3(0.0f);
                world.setPointLight(lightEntities[i], light);
            }
        }
        const std::size_t count = engine2::applySceneEditsFile(world, *args.editScene);
        std::cout << "Scene edits applied: " << count << '\n';
    }

    // The overlay is shown by default in interactive sessions, but disabled
    // for --frames/headless regression captures unless explicitly requested.
    // Initialize after the camera GLFW callbacks so ImGui can chain them.
    engine2::SceneGui gui(window);
    if (args.guiSelection) gui.selectIndex(world, *args.guiSelection);
    gui.setVisible(args.guiVisible.value_or(!args.frames.has_value()));
    if (gui.visible() && !args.loadScene && !args.editScene) {
        // Initialize demo light entities now; they remain paused in edit mode.
        const auto lights = animatedLights(
            args.pointLights, args.fixedLightTime.value_or(0.0f));
        for (std::size_t i = 0; i < lightEntities.size(); ++i) {
            world.setLocalTransform(lightEntities[i],
                glm::translate(glm::mat4(1.0f), lights[i].position));
            engine2::PointLight light = lights[i];
            light.position = glm::vec3(0.0f);
            world.setPointLight(lightEntities[i], light);
        }
    }

    double lastTime = glfwGetTime();
    bool previousTab = false;
    bool previousLess = false;
    bool previousMore = false;
    bool previousBloom = false;
    bool previousOcclusion = false;
    bool previousCascadeDebug = false;
    bool previousTemporal = false;
    bool previousAutoExposure = false;
    bool previousGui = false;
    bool screenshotSaved = false;
    std::uint64_t rendered = 0;
    while (!glfwWindowShouldClose(window) && (!args.frames || rendered < *args.frames)) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        const bool toggleGui = glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS;
        if (toggleGui && !previousGui) {
            gui.setVisible(!gui.visible());
            camera.resetMouseTracking();
        }
        previousGui = toggleGui;

        const bool tab = glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS;
        if (tab && !previousTab && !gui.visible()) {
            const bool captured = glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
            glfwSetInputMode(window, GLFW_CURSOR, captured ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
            camera.resetMouseTracking();
        }
        previousTab = tab;

        const bool less = glfwGetKey(window, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS;
        const bool more = glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS;
        const bool toggleBloom = glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS;
        if (less && !previousLess) renderer.setExposure(renderer.exposure() / 1.2f);
        if (more && !previousMore) renderer.setExposure(renderer.exposure() * 1.2f);
        if (toggleBloom && !previousBloom) {
            renderer.setBloomStrength(renderer.bloomStrength() > 0.0f
                                         ? 0.0f : std::max(0.15f, args.renderer.bloomStrength));
        }
        previousLess = less;
        previousMore = more;
        previousBloom = toggleBloom;
        const bool toggleOcclusion = glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS;
        if (toggleOcclusion && !previousOcclusion) renderer.setSsao(!renderer.ssao());
        previousOcclusion = toggleOcclusion;
        const bool toggleCascade = glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS;
        if (toggleCascade && !previousCascadeDebug)
            renderer.setCascadeDebug(!renderer.cascadeDebug());
        previousCascadeDebug = toggleCascade;
        const bool toggleTaa = glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS;
        if (toggleTaa && !previousTemporal) renderer.setTaa(!renderer.taa());
        previousTemporal = toggleTaa;
        const bool toggleAutoExposure = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
        if (toggleAutoExposure && !previousAutoExposure)
            renderer.setAutoExposure(!renderer.autoExposure());
        previousAutoExposure = toggleAutoExposure;

        const double now = glfwGetTime();
        const float frameDelta = static_cast<float>(now - lastTime);
        if (!gui.visible()) camera.update(window, frameDelta);
        lastTime = now;

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width == 0 || height == 0) continue;

        if (!args.editScene && !gui.visible()) {
            const auto lights = animatedLights(
                args.pointLights, args.fixedLightTime.value_or(static_cast<float>(now)));
            for (std::size_t i = 0; i < lightEntities.size(); ++i) {
                world.setLocalTransform(lightEntities[i],
                    glm::translate(glm::mat4(1.0f), lights[i].position));
                engine2::PointLight localLight = lights[i];
                localLight.position = glm::vec3(0.0f);
                world.setPointLight(lightEntities[i], localLight);
            }
        }

        // The render backend receives only extracted scene components.
        (void)world.advance(gui.visible() ? 0.0f :
                            args.simulationDelta.value_or(frameDelta));
        renderer.setPointLights(world.pointLights());
        renderer.render(camera, world.renderItems(), width, height, frameDelta);
        // Composite the GUI on the default back buffer BEFORE PPM capture.
        gui.draw(world, assetRegistry);
        if (args.screenshot && !screenshotSaved &&
            (!args.frames || rendered + 1 == *args.frames)) {
            saveScreenshot(*args.screenshot, width, height);
            screenshotSaved = true;
        }
        glfwSwapBuffers(window);
        ++rendered;
    }
    if (args.stats) {
        const auto& stats = renderer.stats();
        std::cout << "FrameStats submitted=" << stats.submittedItems
                  << " visible=" << stats.visibleItems
                  << " culled=" << stats.culledItems
                  << " sunCascades=" << renderer.sunCascades()
                  << " sunDistance=" << renderer.sunShadowDistance()
                  << " shadowDraws=" << stats.shadowDrawCalls
                  << " pointShadowDraws=" << stats.pointShadowDrawCalls
                  << " geometryDraws=" << stats.geometryDrawCalls
                  << " maskedDraws=" << stats.alphaMaskedDrawCalls
                  << " blendedDraws=" << stats.alphaBlendedDrawCalls
                  << " taa=" << (renderer.taa() ? 1 : 0)
                  << " taaHistoryFrames=" << stats.taaHistoryFrames
                  << " autoExposure=" << (renderer.autoExposure() ? 1 : 0)
                  << " meteredExposure=" << stats.meteredExposure
                  << " effectiveExposure=" << stats.effectiveExposure
                  << " bloomLevels=" << stats.bloomMipLevels
                  << " guiVisible=" << (gui.visible() ? 1 : 0);
        const auto sceneStats = world.stats();
        std::cout << " sceneEntities=" << sceneStats.entities
                  << " sceneRenderables=" << sceneStats.renderables
                  << " sceneLights=" << sceneStats.pointLights
                  << " sceneBodies=" << sceneStats.rigidBodies
                  << " sceneFixedSteps=" << sceneStats.fixedSteps
                  << " sceneBroadphasePairs=" << sceneStats.broadphasePairs
                  << " sceneSphereContacts=" << sceneStats.sphereContacts
                  << " sceneBoxContacts=" << sceneStats.boxContacts;
        if (fallingSphere && world.contains(*fallingSphere)) {
            std::cout << " physicsY=" << world.worldTransform(*fallingSphere)[3][1];
        }
        if (collisionBalls && world.contains((*collisionBalls)[0]) &&
            world.contains((*collisionBalls)[1])) {
            const auto& a = (*collisionBalls)[0];
            const auto& b = (*collisionBalls)[1];
            std::cout << " collisionSeparation="
                      << world.worldTransform(b)[3][0] -
                         world.worldTransform(a)[3][0]
                      << " collisionVelocityA=" << world.rigidBody(a).velocity.x
                      << " collisionVelocityB=" << world.rigidBody(b).velocity.x;
        }
        if (boxDemoEntities && world.contains((*boxDemoEntities)[0]) &&
            world.contains((*boxDemoEntities)[1])) {
            const auto& obstacle = (*boxDemoEntities)[0];
            const auto& projectile = (*boxDemoEntities)[1];
            std::cout << " boxObstacleX=" << world.worldTransform(obstacle)[3][0]
                      << " boxSphereX=" << world.worldTransform(projectile)[3][0]
                      << " boxSphereVelocityX="
                      << world.rigidBody(projectile).velocity.x;
        }
        std::cout << '\n';
    }
    if (args.saveScene) {
        engine2::saveScene(world, assetRegistry, *args.saveScene);
        std::cout << "Scene saved: " << *args.saveScene
                  << " entities=" << world.stats().entities << '\n';
    }
    if (args.inspectScene) {
        engine2::saveSceneInspection(world, assetRegistry, *args.inspectScene);
        std::cout << "Scene inspection saved: " << *args.inspectScene
                  << " entities=" << world.stats().entities << '\n';
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "engine2: " << error.what() << '\n';
        return 1;
    }
}
