cmake_minimum_required(VERSION 3.24)
project(engine2 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
if(UNIX AND NOT APPLE)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
endif()

set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)
FetchContent_Declare(glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v2.0.8
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR cmake
)

FetchContent_MakeAvailable(glfw glm glad)
glad_add_library(glad_gl_core_33 REPRODUCIBLE API gl:core=3.3)

add_library(engine2_core STATIC
    src/Camera.cpp
    src/Mesh.cpp
    src/Shader.cpp
    src/Renderer.cpp
)

target_include_directories(engine2_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(engine2_core PUBLIC
    glfw
    glad_gl_core_33
    glm::glm
)
target_compile_features(engine2_core PUBLIC cxx_std_20)

add_executable(engine2 src/main.cpp)
target_link_libraries(engine2 PRIVATE engine2_core)

if(MSVC)
    target_compile_options(engine2_core PRIVATE /W4 /permissive-)
    target_compile_options(engine2 PRIVATE /W4 /permissive-)
else()
    target_compile_options(engine2_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(engine2 PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(TARGET engine2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:engine2>/shaders"
    COMMENT "Copying GLSL shaders next to the executable"
)

install(TARGETS engine2 RUNTIME DESTINATION bin)
install(DIRECTORY shaders/ DESTINATION bin/shaders)
