cmake_minimum_required(VERSION 3.24)
project(engine2 VERSION 0.2.0 LANGUAGES C CXX)

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
FetchContent_Declare(tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG v2.9.7
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw glm glad)
# tinygltf is header-only; don't build its examples/tests.
FetchContent_GetProperties(tinygltf)
if(NOT tinygltf_POPULATED)
    FetchContent_Populate(tinygltf)
endif()
glad_add_library(glad_gl_core_33 REPRODUCIBLE API gl:core=3.3)

add_library(engine2_core STATIC
    src/Camera.cpp
    src/Mesh.cpp
    src/Shader.cpp
    src/Renderer.cpp
    src/Texture.cpp
    src/GltfLoader.cpp
)
target_include_directories(engine2_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${tinygltf_SOURCE_DIR}
)
target_link_libraries(engine2_core PUBLIC glfw glad_gl_core_33 glm::glm)
target_compile_features(engine2_core PUBLIC cxx_std_20)

add_executable(engine2 src/main.cpp)
target_link_libraries(engine2 PRIVATE engine2_core)

if(MSVC)
    target_compile_options(engine2_core PRIVATE /W4 /permissive-)
    target_compile_options(engine2 PRIVATE /W4 /permissive-)
else()
    target_compile_options(engine2_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(engine2 PRIVATE -Wall -Wextra -Wpedantic)
    # tinygltf's vendored JSON and stb headers are third-party code.
    set_source_files_properties(src/GltfLoader.cpp PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-missing-field-initializers")
endif()

add_custom_command(TARGET engine2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:engine2>/shaders"
    COMMENT "Copying GLSL shaders next to the executable"
)

include(CTest)
if(BUILD_TESTING AND UNIX)
    find_program(XVFB_RUN xvfb-run)
    if(XVFB_RUN)
        add_test(NAME procedural-smoke
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:engine2> --frames 3)
        add_test(NAME gltf-smoke
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:engine2> --model
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/textured-triangle.gltf"
                    --frames 3)
        set_tests_properties(procedural-smoke gltf-smoke PROPERTIES
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
    endif()
endif()

install(TARGETS engine2 RUNTIME DESTINATION bin)
install(DIRECTORY shaders/ DESTINATION bin/shaders)
