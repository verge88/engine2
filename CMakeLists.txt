cmake_minimum_required(VERSION 3.24)
project(engine2 VERSION 0.5.0 LANGUAGES C CXX)

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
    src/Environment.cpp
    src/PostProcess.cpp
    src/AmbientOcclusion.cpp
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
    # Third-party JSON and stb headers are compiled in GltfLoader.cpp.
    set_source_files_properties(src/GltfLoader.cpp PROPERTIES
        COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-missing-field-initializers")
endif()

add_custom_command(TARGET engine2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:engine2>/shaders"
    COMMENT "Copying GLSL shaders next to the executable"
)

include(CTest)
if(BUILD_TESTING)
    add_executable(engine2_frustum_tests tests/frustum_test.cpp)
    target_include_directories(engine2_frustum_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(engine2_frustum_tests PRIVATE glm::glm)
    add_test(NAME frustum-unit COMMAND engine2_frustum_tests)
endif()
if(BUILD_TESTING AND UNIX)
    find_program(XVFB_RUN xvfb-run)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    if(XVFB_RUN)
        add_test(NAME procedural-smoke
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:engine2> --frames 2)
        add_test(NAME gltf-hdr-smoke
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:engine2>
                    --model "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/textured-triangle.gltf"
                    --environment "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/ambient.hdr"
                    --frames 2)
        add_test(NAME image-capture
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:engine2> --frames 2
                    --screenshot "${CMAKE_CURRENT_BINARY_DIR}/engine2-smoke.ppm")
        add_test(NAME validate-image
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_ppm.py"
                    "${CMAKE_CURRENT_BINARY_DIR}/engine2-smoke.ppm")
        add_test(NAME no-ssao-no-lights-smoke
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:engine2> --frames 2
                    --ssao 0 --lights 0 --stats
                    --screenshot "${CMAKE_CURRENT_BINARY_DIR}/no-lights.ppm")
        add_test(NAME lighting-image-difference
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_ppm.py"
                    "${CMAKE_CURRENT_BINARY_DIR}/engine2-smoke.ppm"
                    "${CMAKE_CURRENT_BINARY_DIR}/no-lights.ppm")
        # Separate alpha-mode captures exercise the full glTF loader,
        # PBR fragment discard, HDR blend ordering and cutout shadow path.
        foreach(MODE opaque mask blend)
            add_test(NAME alpha-${MODE}-capture
                COMMAND ${XVFB_RUN} -a $<TARGET_FILE:engine2>
                        --model "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/alpha-${MODE}.gltf"
                        --environment "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/ambient.hdr"
                        --ssao 0 --lights 0 --bloom 0 --frames 2 --stats
                        --screenshot "${CMAKE_CURRENT_BINARY_DIR}/alpha-${MODE}.ppm")
            set_tests_properties(alpha-${MODE}-capture PROPERTIES
                ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        endforeach()
        add_test(NAME alpha-modes-image-regression
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_alpha.py"
                    "${CMAKE_CURRENT_BINARY_DIR}/alpha-opaque.ppm"
                    "${CMAKE_CURRENT_BINARY_DIR}/alpha-mask.ppm"
                    "${CMAKE_CURRENT_BINARY_DIR}/alpha-blend.ppm")
        set_tests_properties(alpha-modes-image-regression PROPERTIES
            DEPENDS "alpha-opaque-capture;alpha-mask-capture;alpha-blend-capture")
        set_tests_properties(lighting-image-difference PROPERTIES
            DEPENDS "image-capture;no-ssao-no-lights-smoke")
        set_tests_properties(validate-image PROPERTIES DEPENDS image-capture)
        set_tests_properties(procedural-smoke gltf-hdr-smoke image-capture
                             no-ssao-no-lights-smoke PROPERTIES
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
    endif()
endif()

install(TARGETS engine2 RUNTIME DESTINATION bin)
install(DIRECTORY shaders/ DESTINATION bin/shaders)
