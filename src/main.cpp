#include "engine2/Camera.hpp"
#include "engine2/Mesh.hpp"
#include "engine2/Renderer.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdint>
#include <filesystem>
#include <iostream>
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
        window_ = glfwCreateWindow(1280, 720, "engine2 | PBR renderer", nullptr, nullptr);
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

std::optional<std::uint64_t> parseArgs(int argc, char** argv) {
    std::optional<std::uint64_t> frameLimit;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: engine2 [--frames N]\n";
            std::cout << "Controls: WASD move, mouse look, Space/Ctrl up/down, "
                         "Shift sprint, wheel FOV, Tab cursor, Esc exit.\n";
            return std::uint64_t{0};
        }
        if (arg == "--frames" && i + 1 < argc) {
            const std::string count(argv[++i]);
            std::size_t parsed = 0;
            const std::uint64_t value = std::stoull(count, &parsed);
            if (parsed != count.size() || value == 0) {
                throw std::invalid_argument("--frames expects a positive integer");
            }
            frameLimit = value;
        } else {
            throw std::invalid_argument("Unknown or incomplete argument: " + arg);
        }
    }
    return frameLimit;
}

std::filesystem::path shaderDirectory(const char* executable) {
    const auto executableDir = std::filesystem::absolute(executable).parent_path();
    return executableDir / "shaders";
}

std::vector<engine2::RenderItem> createScene(const engine2::Mesh& sphere,
                                              const engine2::Mesh& floor) {
    std::vector<engine2::RenderItem> items;
    items.reserve(26);
    engine2::RenderItem ground;
    ground.mesh = &floor;
    ground.model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    ground.model = glm::scale(ground.model, glm::vec3(32.0f, 1.0f, 32.0f));
    ground.albedo = glm::vec3(0.3f);
    ground.roughness = 0.88f;
    ground.checker = true;
    items.push_back(ground);

    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 5; ++column) {
            engine2::RenderItem item;
            item.mesh = &sphere;
            item.model = glm::translate(glm::mat4(1.0f),
                glm::vec3((column - 2) * 2.1f, -0.52f, (row - 2) * 2.1f));
            item.model = glm::scale(item.model, glm::vec3(0.48f));
            item.albedo = glm::vec3(0.9f, 0.52f, 0.26f);
            item.metallic = static_cast<float>(row) / 4.0f;
            item.roughness = 0.08f + static_cast<float>(column) * 0.21f;
            items.push_back(item);
        }
    }
    return items;
}

int run(int argc, char** argv) {
    const auto frames = parseArgs(argc, argv);
    if (frames && *frames == 0) return 0;

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

    // The GPU resources must be destroyed before GlfwSession destroys the context.
    engine2::Renderer renderer(shaderDirectory(argv[0]));
    engine2::Mesh sphere = engine2::Mesh::sphere();
    engine2::Mesh floor = engine2::Mesh::plane();
    const auto scene = createScene(sphere, floor);

    double lastTime = glfwGetTime();
    bool previousTab = false;
    std::uint64_t rendered = 0;
    while (!glfwWindowShouldClose(window) && (!frames || rendered < *frames)) {
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

        const double now = glfwGetTime();
        camera.update(window, static_cast<float>(now - lastTime));
        lastTime = now;

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width == 0 || height == 0) continue;

        renderer.render(camera, scene, width, height);
        glfwSwapBuffers(window);
        ++rendered;
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
