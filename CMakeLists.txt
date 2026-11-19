cmake_minimum_required(VERSION 3.24)
project(engine2 VERSION 2.4.0 LANGUAGES C CXX)

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
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw glm glad)
# tinygltf is header-only; don't build its examples/tests.
FetchContent_GetProperties(tinygltf)
if(NOT tinygltf_POPULATED)
    FetchContent_Populate(tinygltf)
endif()
# Dear ImGui is a source library with no upstream CMakeLists.txt.
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()
add_library(engine2_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(engine2_imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(engine2_imgui PUBLIC glfw)

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
    src/PointShadowMaps.cpp
    src/CascadedShadowMaps.cpp
    src/Scene.cpp
    src/SceneIO.cpp
    src/SceneInspector.cpp
)
target_include_directories(engine2_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${tinygltf_SOURCE_DIR}
)
target_link_libraries(engine2_core PUBLIC glfw glad_gl_core_33 glm::glm)
target_compile_features(engine2_core PUBLIC cxx_std_20)

add_executable(engine2 src/main.cpp src/SceneGui.cpp)
target_link_libraries(engine2 PRIVATE engine2_core engine2_imgui)

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
    add_executable(engine2_cascade_math_tests tests/cascade_math_test.cpp)
    target_include_directories(engine2_cascade_math_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(engine2_cascade_math_tests PRIVATE glm::glm)
    add_test(NAME cascade-math-unit COMMAND engine2_cascade_math_tests)
    add_executable(engine2_temporal_jitter_tests tests/temporal_jitter_test.cpp)
    target_include_directories(engine2_temporal_jitter_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(engine2_temporal_jitter_tests PRIVATE glm::glm)
    add_test(NAME temporal-jitter-unit COMMAND engine2_temporal_jitter_tests)
    add_executable(engine2_exposure_tests tests/exposure_test.cpp)
    target_include_directories(engine2_exposure_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME auto-exposure-unit COMMAND engine2_exposure_tests)
    add_executable(engine2_scene_tests tests/scene_test.cpp)
    target_link_libraries(engine2_scene_tests PRIVATE engine2_core)
    add_test(NAME scene-entities-physics-unit COMMAND engine2_scene_tests)
    add_executable(engine2_sphere_collision_tests tests/sphere_collision_test.cpp)
    target_link_libraries(engine2_sphere_collision_tests PRIVATE engine2_core)
    add_test(NAME sphere-collision-unit COMMAND engine2_sphere_collision_tests)
    add_executable(engine2_scene_io_tests tests/scene_io_test.cpp)
    target_link_libraries(engine2_scene_io_tests PRIVATE engine2_core)
    add_test(NAME scene-json-io-unit COMMAND engine2_scene_io_tests)
    add_executable(engine2_box_collision_tests tests/box_collision_test.cpp)
    target_link_libraries(engine2_box_collision_tests PRIVATE engine2_core)
    add_test(NAME box-collision-unit COMMAND engine2_box_collision_tests)
    # Sequential normal impulses, Coulomb static/dynamic friction and a
    # gravity-supported stack, independent of a graphics context.
    add_executable(engine2_contact_solver_tests tests/contact_solver_test.cpp)
    target_include_directories(engine2_contact_solver_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(engine2_contact_solver_tests PRIVATE glm::glm)
    add_test(NAME contact-solver-friction-math-unit COMMAND engine2_contact_solver_tests)
    add_executable(engine2_contact_physics_tests tests/contact_physics_test.cpp)
    target_link_libraries(engine2_contact_physics_tests PRIVATE engine2_core)
    add_test(NAME scene-friction-stacking-json-unit COMMAND engine2_contact_physics_tests)
    add_executable(engine2_physics_broadphase_ccd_math_tests
        tests/physics_broadphase_ccd_math_test.cpp)
    target_include_directories(engine2_physics_broadphase_ccd_math_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(engine2_physics_broadphase_ccd_math_tests PRIVATE glm::glm)
    add_test(NAME physics-3d-broadphase-ccd-math-unit
             COMMAND engine2_physics_broadphase_ccd_math_tests)

    add_executable(engine2_physics_broadphase_ccd_runtime_tests
        tests/physics_broadphase_ccd_runtime_test.cpp)
    target_link_libraries(engine2_physics_broadphase_ccd_runtime_tests PRIVATE engine2_core)
    add_test(NAME physics-3d-broadphase-ccd-scene-unit
             COMMAND engine2_physics_broadphase_ccd_runtime_tests)

    add_executable(engine2_ccd_multi_math_tests tests/ccd_multi_math_test.cpp)
    target_include_directories(engine2_ccd_multi_math_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(engine2_ccd_multi_math_tests PRIVATE glm::glm)
    add_test(NAME ccd-moving-spheres-math-unit
             COMMAND engine2_ccd_multi_math_tests)

    add_executable(engine2_ccd_multi_scene_tests tests/ccd_multi_scene_test.cpp)
    target_link_libraries(engine2_ccd_multi_scene_tests PRIVATE engine2_core)
    add_test(NAME ccd-multi-impact-scene-unit
             COMMAND engine2_ccd_multi_scene_tests)

    # Exact swept sphere vs rounded AABB (face/edge/corner) rather than
    # the previous expanded-box slab approximation.
    add_executable(engine2_ccd_rounded_box_math_tests
        tests/ccd_rounded_box_math_test.cpp)
    target_include_directories(engine2_ccd_rounded_box_math_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(engine2_ccd_rounded_box_math_tests PRIVATE glm::glm)
    add_test(NAME ccd-rounded-box-geometry-unit
             COMMAND engine2_ccd_rounded_box_math_tests)

    add_executable(engine2_ccd_rounded_box_scene_tests
        tests/ccd_rounded_box_scene_test.cpp)
    target_link_libraries(engine2_ccd_rounded_box_scene_tests PRIVATE engine2_core)
    add_test(NAME ccd-rounded-box-scene-unit
             COMMAND engine2_ccd_rounded_box_scene_tests)

    # v2.5 translation-only continuous collision detection for dynamic AABBs.
    add_executable(engine2_ccd_box_math_tests tests/ccd_box_math_test.cpp)
    target_include_directories(engine2_ccd_box_math_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(engine2_ccd_box_math_tests PRIVATE glm::glm)
    add_test(NAME ccd-moving-box-math-unit
             COMMAND engine2_ccd_box_math_tests)

    add_executable(engine2_ccd_box_scene_tests tests/ccd_box_scene_test.cpp)
    target_link_libraries(engine2_ccd_box_scene_tests PRIVATE engine2_core)
    add_test(NAME ccd-moving-box-scene-unit
             COMMAND engine2_ccd_box_scene_tests)
    add_executable(engine2_scene_inspector_tests tests/scene_inspector_test.cpp)
    target_link_libraries(engine2_scene_inspector_tests PRIVATE engine2_core)
    target_include_directories(engine2_scene_inspector_tests PRIVATE ${tinygltf_SOURCE_DIR})
    add_test(NAME scene-inspector-editor-unit COMMAND engine2_scene_inspector_tests)
    add_executable(engine2_scene_history_tests tests/scene_history_test.cpp)
    target_link_libraries(engine2_scene_history_tests PRIVATE engine2_core)
    add_test(NAME scene-editor-undo-redo-unit COMMAND engine2_scene_history_tests)
    add_executable(engine2_gizmo_math_tests tests/gizmo_math_test.cpp)
    target_include_directories(engine2_gizmo_math_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(engine2_gizmo_math_tests PRIVATE glm::glm)
    add_test(NAME viewport-gizmo-math-unit COMMAND engine2_gizmo_math_tests)
    add_executable(engine2_gizmo_transform_tests tests/gizmo_transform_test.cpp)
    target_link_libraries(engine2_gizmo_transform_tests PRIVATE glm::glm)
    target_include_directories(engine2_gizmo_transform_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME gizmo-transform-snap-unit COMMAND engine2_gizmo_transform_tests)
    add_executable(engine2_scene_picking_tests tests/scene_picking_test.cpp)
    target_link_libraries(engine2_scene_picking_tests PRIVATE engine2_core)
    add_test(NAME scene-ray-picking-unit COMMAND engine2_scene_picking_tests)
    add_executable(engine2_triangle_marquee_tests tests/triangle_marquee_test.cpp)
    target_link_libraries(engine2_triangle_marquee_tests PRIVATE engine2_core)
    add_test(NAME triangle-marquee-unit COMMAND engine2_triangle_marquee_tests)
    # Stable operation-count benchmark: BVH must match brute-force results
    # while visiting <1/50 of triangles on a dense 32k triangle grid.
    add_executable(engine2_triangle_bvh_tests tests/triangle_bvh_test.cpp)
    target_include_directories(engine2_triangle_bvh_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(engine2_triangle_bvh_tests PRIVATE glm::glm)
    add_test(NAME triangle-bvh-acceleration-unit COMMAND engine2_triangle_bvh_tests)

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
        # Deterministic demo lights: the only difference between images is
        # point-shadow visibility (not animation, SSAO or bloom).
        foreach(MODE on off)
            if(MODE STREQUAL "on")
                set(SHADOW_COUNT 1)
            else()
                set(SHADOW_COUNT 0)
            endif()
            add_test(NAME point-shadow-${MODE}-capture
                COMMAND ${XVFB_RUN} -a $<TARGET_FILE:engine2>
                        --lights 1 --point-shadows ${SHADOW_COUNT}
                        --light-time 0 --ssao 0 --bloom 0 --frames 2
                        --stats --screenshot
                        "${CMAKE_CURRENT_BINARY_DIR}/point-shadow-${MODE}.ppm")
            set_tests_properties(point-shadow-${MODE}-capture PROPERTIES
                ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        endforeach()
        add_test(NAME point-shadow-image-difference
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_point_shadows.py"
                    "${CMAKE_CURRENT_BINARY_DIR}/point-shadow-on.ppm"
                    "${CMAKE_CURRENT_BINARY_DIR}/point-shadow-off.ppm")
        set_tests_properties(point-shadow-image-difference PROPERTIES
            DEPENDS "point-shadow-on-capture;point-shadow-off-capture")
        # Compare directional shadows with an otherwise identical frame,
        # and smoke-test the one-cascade and debug-visualization paths.
        foreach(MODE three off)
            if(MODE STREQUAL "three")
                set(CASCADE_COUNT 3)
            else()
                set(CASCADE_COUNT 0)
            endif()
            add_test(NAME sun-shadow-${MODE}-capture
                COMMAND ${XVFB_RUN} -a $<TARGET_FILE:engine2>
                        --sun-cascades ${CASCADE_COUNT} --sun-distance 95
                        --lights 0 --ssao 0 --bloom 0 --frames 2
                        --screenshot "${CMAKE_CURRENT_BINARY_DIR}/sun-${MODE}.ppm")
            set_tests_properties(sun-shadow-${MODE}-capture PROPERTIES
                ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        endforeach()
        add_test(NAME sun-shadow-image-difference
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_sun_cascades.py"
                    "${CMAKE_CURRENT_BINARY_DIR}/sun-three.ppm"
                    "${CMAKE_CURRENT_BINARY_DIR}/sun-off.ppm")
        set_tests_properties(sun-shadow-image-difference PROPERTIES
            DEPENDS "sun-shadow-three-capture;sun-shadow-off-capture")
        add_test(NAME sun-single-cascade-smoke
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:engine2>
                    --sun-cascades 1 --sun-distance 60 --cascade-debug 1
                    --lights 0 --ssao 0 --frames 1)
        set_tests_properties(sun-single-cascade-smoke PROPERTIES
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        # A real headless OpenGL app is run with TAA on/off for eight frames.
        # Verify both temporal history progression and changed output pixels.
        add_test(NAME taa-temporal-regression
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_taa.py"
                    "${XVFB_RUN}" "$<TARGET_FILE:engine2>"
                    "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(taa-temporal-regression PROPERTIES
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        # Exercise true HDR luminance metering and multi-mip bloom with real
        # OpenGL screenshots. Validate numeric effective exposure and pixels.
        add_test(NAME exposure-bloom-image-regression
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_exposure_bloom.py"
                    "${XVFB_RUN}" "$<TARGET_FILE:engine2>"
                    "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(exposure-bloom-image-regression PROPERTIES
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        # Two 12-frame scenes with/without a falling rigid body must retain
        # correct entity counts and produce visibly different PPM screenshots.
        add_test(NAME scene-physics-image-regression
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_scene.py"
                    "${XVFB_RUN}" "$<TARGET_FILE:engine2>"
                    "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(scene-physics-image-regression PROPERTIES
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        # Collision broadphase + impulses must affect both numerical physics
        # telemetry and the pixels in a real headless OpenGL frame.
        add_test(NAME sphere-collision-image-regression
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_collisions.py"
                    "${XVFB_RUN}" "$<TARGET_FILE:engine2>"
                    "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(sphere-collision-image-regression PROPERTIES
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        # Actual headless OpenGL roundtrips for procedural physics and
        # glTF MASK materials with stable GPU texture references.
        add_test(NAME scene-json-roundtrip-image-regression
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_scene_io.py"
                    "${XVFB_RUN}" "$<TARGET_FILE:engine2>"
                    "${CMAKE_CURRENT_BINARY_DIR}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/alpha-mask.gltf")
        set_tests_properties(scene-json-roundtrip-image-regression PROPERTIES
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        add_test(NAME box-collision-image-regression
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_box_colliders.py"
                    "${XVFB_RUN}" "$<TARGET_FILE:engine2>"
                    "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(box-collision-image-regression PROPERTIES
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        # Export a real physics scene, edit contact-friction parameters,
        # compare glancing sphere-box collision pixels, then save/reload.
        add_test(NAME contact-friction-image-regression
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_contact_friction.py"
                    "${XVFB_RUN}" "$<TARGET_FILE:engine2>"
                    "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(contact-friction-image-regression PROPERTIES
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        add_test(NAME physics-3d-broadphase-ccd-image-regression
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_physics_ccd.py"
                    "${XVFB_RUN}" "$<TARGET_FILE:engine2>"
                    "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(physics-3d-broadphase-ccd-image-regression PROPERTIES
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        add_test(NAME ccd-multi-impact-image-regression
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_ccd_multi.py"
                    "${XVFB_RUN}" "$<TARGET_FILE:engine2>"
                    "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(ccd-multi-impact-image-regression PROPERTIES
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        add_test(NAME ccd-rounded-box-image-regression
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_rounded_box_ccd.py"
                    "${XVFB_RUN}" "$<TARGET_FILE:engine2>"
                    "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(ccd-rounded-box-image-regression PROPERTIES
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        # JSON inspector + transaction editor: check world-space positions,
        # hidden meshes, rendered pixel changes and save/load equivalence.
        add_test(NAME scene-inspector-editor-image-regression
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_scene_inspector.py"
                    "${XVFB_RUN}" "$<TARGET_FILE:engine2>"
                    "${CMAKE_CURRENT_BINARY_DIR}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/scene-edits-demo.json")
        set_tests_properties(scene-inspector-editor-image-regression PROPERTIES
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        # Actual Dear ImGui GPU overlay, selected entities, headless
        # compositing, edited materials, and invalid GUI selection.
        add_test(NAME scene-gui-overlay-image-regression
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_scene_gui.py"
                    "${XVFB_RUN}" "$<TARGET_FILE:engine2>"
                    "${CMAKE_CURRENT_BINARY_DIR}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/scene-edits-demo.json")
        set_tests_properties(scene-gui-overlay-image-regression PROPERTIES
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        add_test(NAME viewport-gizmo-overlay-image-regression
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_gizmo.py"
                    "${XVFB_RUN}" "$<TARGET_FILE:engine2>"
                    "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(viewport-gizmo-overlay-image-regression PROPERTIES
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        add_test(NAME gizmo-modes-image-regression
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_gizmo_modes.py"
                    "${XVFB_RUN}" "$<TARGET_FILE:engine2>"
                    "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(gizmo-modes-image-regression PROPERTIES
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        add_test(NAME scene-ray-picking-image-regression
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_scene_picking.py"
                    "${XVFB_RUN}" "$<TARGET_FILE:engine2>"
                    "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(scene-ray-picking-image-regression PROPERTIES
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        # Real glTF triangular silhouette (false AABB hit) + rectangle
        # group selection and additive union, compared on OpenGL PPM images.
        add_test(NAME precise-triangle-marquee-image-regression
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_triangle_marquee.py"
                    "${XVFB_RUN}" "$<TARGET_FILE:engine2>"
                    "${CMAKE_CURRENT_BINARY_DIR}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/triangle-picking-gap.gltf")
        set_tests_properties(precise-triangle-marquee-image-regression PROPERTIES
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        # A real GPU scene tests selection identity with BVH, compares the
        # editor viewport to bounds-mode and verifies per-ray traversal
        # counters are significantly below brute force.
        add_test(NAME triangle-bvh-runtime-image-regression
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_bvh_picking.py"
                    "${XVFB_RUN}" "$<TARGET_FILE:engine2>"
                    "${CMAKE_CURRENT_BINARY_DIR}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/triangle-picking-gap.gltf")
        set_tests_properties(triangle-bvh-runtime-image-regression PROPERTIES
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")





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
