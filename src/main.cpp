#include "engine2/Camera.hpp"
#include "engine2/GltfLoader.hpp"
#include "engine2/Mesh.hpp"
#include "engine2/Renderer.hpp"

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
    engine2::RendererOptions renderer;
    int pointLights = 3;
    std::optional<float> fixedLightTime;
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
        } else if (arg == "--exposure" && i + 1 < argc) {
            args.renderer.exposure = parseSetting(argv[++i], "--exposure", 0.05f, 16.0f);
        } else if (arg == "--bloom" && i + 1 < argc) {
            args.renderer.bloomStrength = parseSetting(argv[++i], "--bloom", 0.0f, 2.0f);
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
                     "               [--ssao 0|1] [--culling 0|1] [--exposure X] [--bloom X]\n"
                     "               [--stats] [--screenshot frame.ppm]\n";
        std::cout << "Controls: WASD move, mouse look, Space/Ctrl up/down, "
                     "Shift sprint, wheel FOV, Tab cursor, Esc exit.\n";
        return 0;
    }

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
        static_cast<engine2::Camera*>(glfwGetWindowUserPointer(w))->onScroll(y);
    });

    // All GL resources must be destroyed before GlfwSession destroys the context.
    engine2::Renderer renderer(shaderDirectory(argv[0]), args.renderer);
    std::optional<engine2::LoadedScene> imported;
    std::unique_ptr<engine2::Mesh> sphere;
    std::unique_ptr<engine2::Mesh> floor;
    std::vector<engine2::RenderItem> scene;
    if (args.model) {
        imported.emplace(engine2::loadGltf(*args.model));
        scene = imported->items;
        if (scene.empty()) throw std::runtime_error("glTF scene has no renderable primitives");
    } else {
        sphere = std::make_unique<engine2::Mesh>(engine2::Mesh::sphere());
        floor = std::make_unique<engine2::Mesh>(engine2::Mesh::plane());
        scene = createScene(*sphere, *floor);
    }

    double lastTime = glfwGetTime();
    bool previousTab = false;
    bool previousLess = false;
    bool previousMore = false;
    bool previousBloom = false;
    bool previousOcclusion = false;
    bool previousCascadeDebug = false;
    bool previousTemporal = false;
    bool screenshotSaved = false;
    std::uint64_t rendered = 0;
    while (!glfwWindowShouldClose(window) && (!args.frames || rendered < *args.frames)) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        const bool tab = glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS;
        if (tab && !previousTab) {
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

        const double now = glfwGetTime();
        camera.update(window, static_cast<float>(now - lastTime));
        lastTime = now;

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width == 0 || height == 0) continue;

        renderer.setPointLights(animatedLights(
            args.pointLights, args.fixedLightTime.value_or(static_cast<float>(now))));
        renderer.render(camera, scene, width, height);
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
                  << " taaHistoryFrames=" << stats.taaHistoryFrames << '\n';
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
