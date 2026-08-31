cmake_minimum_required(VERSION 3.24)

project(VertexLoom VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)

option(FABRIC_BUILD_EDITORS "Build Asset Studio and Map Studio" ON)
option(FABRIC_BUILD_RUNTIME "Build the preview game runtime" ON)
option(FABRIC_BUILD_TESTS "Build the C++ test suite" ON)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
FetchContent_MakeAvailable(nlohmann_json)

set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    box2d
    GIT_REPOSITORY https://github.com/erincatto/box2d.git
    GIT_TAG v3.1.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(box2d)

if(BUILD_TESTING AND FABRIC_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.15.3
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)
endif()

add_library(fabric_core STATIC
    engine/fabric_core/src/log.cpp
    engine/fabric_core/src/resource_id.cpp
    engine/fabric_core/src/version.cpp
)
target_include_directories(fabric_core PUBLIC engine/fabric_core/include)
target_compile_features(fabric_core PUBLIC cxx_std_20)

add_library(fabric_project STATIC
    engine/fabric_project/src/asset_storage.cpp
    engine/fabric_project/src/atomic_save.cpp
    engine/fabric_project/src/animation.cpp
    engine/fabric_project/src/audio.cpp
    engine/fabric_project/src/animation_constraints.cpp
    engine/fabric_project/src/animation_ik.cpp
    engine/fabric_project/src/animation_state_machine.cpp
    engine/fabric_project/src/behavior_graph.cpp
    engine/fabric_project/src/document_storage.cpp
    engine/fabric_project/src/entity.cpp
    engine/fabric_project/src/entity_transformation.cpp
    engine/fabric_project/src/input.cpp
    engine/fabric_project/src/manifest.cpp
    engine/fabric_project/src/map.cpp
    engine/fabric_project/src/map_package.cpp
    engine/fabric_project/src/map_chunk_index.cpp
    engine/fabric_project/src/mechanic_graph.cpp
    engine/fabric_project/src/mesh_deformation.cpp
    engine/fabric_project/src/material.cpp
    engine/fabric_project/src/migrations.cpp
    engine/fabric_project/src/project_creator.cpp
    engine/fabric_project/src/project_validator.cpp
    engine/fabric_project/src/progress_save.cpp
    engine/fabric_project/src/resource_registry.cpp
    engine/fabric_project/src/shader_profile.cpp
    engine/fabric_project/src/replay.cpp
    engine/fabric_project/src/scene.cpp
    engine/fabric_project/src/texture_asset.cpp
    engine/fabric_project/src/textured_path.cpp
    engine/fabric_project/src/vector_asset.cpp
    engine/fabric_project/src/visual_component.cpp
    engine/fabric_project/src/visual_composition.cpp
    engine/fabric_project/src/xpbd.cpp
)
target_include_directories(fabric_project PUBLIC engine/fabric_project/include)
target_compile_features(fabric_project PUBLIC cxx_std_20)
target_link_libraries(fabric_project PUBLIC fabric_core PRIVATE
    nlohmann_json::nlohmann_json
)

add_executable(fabric_project_validate tools/project_validate/main.cpp)
target_link_libraries(fabric_project_validate PRIVATE fabric_project)

add_executable(fabric_map_package_export tools/map_package_export/main.cpp)
target_link_libraries(fabric_map_package_export PRIVATE fabric_project)

add_library(fabric_physics STATIC
    engine/fabric_physics/src/mechanic_plan.cpp
    engine/fabric_physics/src/mechanic_simulation.cpp
    engine/fabric_physics/src/physics_world.cpp
)
target_include_directories(fabric_physics PUBLIC engine/fabric_physics/include)
target_compile_features(fabric_physics PUBLIC cxx_std_20)
target_link_libraries(fabric_physics PUBLIC box2d fabric_project)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.32.10
    GIT_SHALLOW TRUE
)
set(SDL2IMAGE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_DEPS_SHARED OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_TESTS OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_BACKEND_IMAGEIO OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_BACKEND_STB ON CACHE BOOL "" FORCE)
set(SDL2IMAGE_AVIF OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_BMP OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_GIF OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_JPG OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_JXL OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_LBM OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_PCX OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_PNG ON CACHE BOOL "" FORCE)
set(SDL2IMAGE_PNG_SAVE OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_PNM OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_QOI OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_SVG ON CACHE BOOL "" FORCE)
set(SDL2IMAGE_TGA OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_TIF OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_WEBP OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_XCF OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_XPM OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_XV OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL2_image
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
    GIT_TAG release-2.8.12
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL2 SDL2_image)

find_package(OpenGL REQUIRED)

add_library(fabric_render STATIC
    engine/fabric_render/src/map_preview.cpp
    engine/fabric_render/src/raster_image.cpp
    engine/fabric_render/src/opengl_vector_renderer.cpp
    engine/fabric_render/src/svg_vector.cpp
    engine/fabric_render/src/textured_path_geometry.cpp
    engine/fabric_render/src/vector_geometry.cpp
    engine/fabric_render/src/visual_composition_renderer.cpp
)
target_include_directories(fabric_render PUBLIC engine/fabric_render/include
    PRIVATE ${SDL2_image_SOURCE_DIR}/src)
target_compile_features(fabric_render PUBLIC cxx_std_20)
target_link_libraries(fabric_render PUBLIC fabric_project OpenGL::GL PRIVATE
    SDL2::SDL2-static
    SDL2_image::SDL2_image-static
)

add_library(fabric_runtime STATIC
    engine/fabric_runtime/src/behavior_evaluator.cpp
    engine/fabric_runtime/src/character_controller.cpp
    engine/fabric_runtime/src/audio_mixer.cpp
    engine/fabric_runtime/src/camera2d.cpp
    engine/fabric_runtime/src/input.cpp
    engine/fabric_runtime/src/preview_runtime.cpp
    engine/fabric_runtime/src/progress_store.cpp
    engine/fabric_runtime/src/replay_player.cpp
    engine/fabric_runtime/src/scene_session.cpp
    engine/fabric_runtime/src/trigger_runtime.cpp
)
target_include_directories(fabric_runtime PUBLIC engine/fabric_runtime/include
    PRIVATE ${SDL2_image_SOURCE_DIR}/include)
target_compile_features(fabric_runtime PUBLIC cxx_std_20)
target_link_libraries(fabric_runtime PUBLIC fabric_project fabric_physics fabric_render
    PRIVATE SDL2::SDL2-static SDL2_image::SDL2_image-static)

add_executable(fabric_asset_preview tools/asset_preview/main.cpp)
target_link_libraries(fabric_asset_preview PRIVATE
    fabric_render
    nlohmann_json::nlohmann_json
)

add_executable(fabric_render_benchmark tools/render_benchmark/main.cpp)
target_link_libraries(fabric_render_benchmark PRIVATE
    fabric_render SDL2::SDL2-static)
if(WIN32)
    target_link_libraries(fabric_render_benchmark PRIVATE psapi)
endif()

if(FABRIC_BUILD_RUNTIME)
    add_executable(fabric_runtime_benchmark tools/runtime_benchmark/main.cpp)
    target_link_libraries(fabric_runtime_benchmark PRIVATE fabric_runtime)
    if(WIN32)
        target_link_libraries(fabric_runtime_benchmark PRIVATE psapi)
    endif()
endif()

add_library(fabric_editor STATIC
    engine/fabric_editor/src/animation_timeline.cpp
    engine/fabric_editor/src/autosave_scheduler.cpp
    engine/fabric_editor/src/behavior_session.cpp
    engine/fabric_editor/src/canvas_interaction.cpp
    engine/fabric_editor/src/command_stack.cpp
    engine/fabric_editor/src/creation_prompts.cpp
    engine/fabric_editor/src/map_session.cpp
    engine/fabric_editor/src/mechanic_presets.cpp
    engine/fabric_editor/src/mechanic_session.cpp
    engine/fabric_editor/src/project_session.cpp
    engine/fabric_editor/src/scene_session.cpp
    engine/fabric_editor/src/session_transition.cpp
    engine/fabric_editor/src/transformation_session.cpp
    engine/fabric_editor/src/visual_presets.cpp
)
target_include_directories(fabric_editor PUBLIC engine/fabric_editor/include)
target_compile_features(fabric_editor PUBLIC cxx_std_20)
target_link_libraries(fabric_editor PUBLIC fabric_project fabric_render fabric_physics fabric_runtime
    nlohmann_json::nlohmann_json)

if(FABRIC_BUILD_EDITORS)
    set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(NFD_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        native_file_dialog
        GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
        GIT_TAG v1.3.0
        GIT_SHALLOW TRUE
    )
    FetchContent_Declare(
        imgui_source
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.9
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(native_file_dialog imgui_source)
    add_library(fabric_imgui STATIC
        ${imgui_source_SOURCE_DIR}/imgui.cpp
        ${imgui_source_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_source_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_source_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_source_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
        ${imgui_source_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ${imgui_source_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    )
    target_include_directories(fabric_imgui PUBLIC
        ${imgui_source_SOURCE_DIR}
        ${imgui_source_SOURCE_DIR}/backends
        ${imgui_source_SOURCE_DIR}/misc/cpp
    )
    target_compile_features(fabric_imgui PUBLIC cxx_std_20)
    target_link_libraries(fabric_imgui PUBLIC SDL2::SDL2-static OpenGL::GL)

    add_executable(asset_studio
        editors/asset_studio/main.cpp
        editors/asset_studio/vector_canvas.cpp
        editors/asset_studio/preview_canvas.cpp
        editors/asset_studio/import_workflow.cpp
    )
    target_compile_definitions(asset_studio PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(asset_studio PRIVATE
        fabric_editor
        fabric_imgui
        fabric_render
        nfd
    )
    add_executable(map_studio editors/map_studio/main.cpp)
    target_compile_definitions(map_studio PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(map_studio PRIVATE fabric_editor fabric_imgui nfd)
endif()

if(FABRIC_BUILD_RUNTIME)
    add_executable(game_runtime game/runtime/main.cpp)
    target_compile_definitions(game_runtime PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(game_runtime PRIVATE fabric_runtime)
endif()

install(TARGETS fabric_project_validate fabric_map_package_export fabric_asset_preview
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(FABRIC_BUILD_EDITORS)
    install(TARGETS asset_studio map_studio
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(FABRIC_BUILD_RUNTIME)
    install(TARGETS game_runtime
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY tests/fixtures/studio-textile-head/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/vertex-loom/examples/studio-textile-head)

set(CPACK_PACKAGE_NAME "vertex-loom")
set(CPACK_PACKAGE_VENDOR "Vertex Loom")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Cross-platform 2D engine and authoring tools for textile and vector worlds")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "vertex-loom@example.invalid")
set(CPACK_GENERATOR "TGZ;ZIP")
include(CPack)

if(BUILD_TESTING AND FABRIC_BUILD_TESTS)
    add_executable(fabric_core_smoke tests/cpp/core_smoke.cpp)
    target_link_libraries(fabric_core_smoke PRIVATE fabric_core nlohmann_json::nlohmann_json)
    add_test(NAME fabric_core_smoke COMMAND fabric_core_smoke)

    add_executable(fabric_project_tests tests/cpp/project_manifest_tests.cpp)
    target_link_libraries(fabric_project_tests PRIVATE fabric_project)
    add_test(NAME fabric_project_tests COMMAND fabric_project_tests)

    add_executable(fabric_project_foundations_tests
        tests/cpp/project_foundations_tests.cpp
    )
    target_link_libraries(fabric_project_foundations_tests PRIVATE
        fabric_project
        Catch2::Catch2WithMain
    )
    add_test(NAME fabric_project_foundations_tests
             COMMAND fabric_project_foundations_tests)

    add_executable(fabric_material_entity_tests
        tests/cpp/material_entity_tests.cpp
    )
    target_link_libraries(fabric_material_entity_tests PRIVATE
        fabric_project
        Catch2::Catch2WithMain
    )
    add_test(NAME fabric_material_entity_tests
             COMMAND fabric_material_entity_tests)

    add_executable(fabric_visual_composition_tests
        tests/cpp/visual_composition_tests.cpp
    )
    target_link_libraries(fabric_visual_composition_tests PRIVATE
        fabric_project
        Catch2::Catch2WithMain
    )
    add_test(NAME fabric_visual_composition_tests
             COMMAND fabric_visual_composition_tests)

    add_executable(fabric_visual_component_tests
        tests/cpp/visual_component_tests.cpp
    )
    target_link_libraries(fabric_visual_component_tests PRIVATE
        fabric_project
        Catch2::Catch2WithMain
    )
    add_test(NAME fabric_visual_component_tests
             COMMAND fabric_visual_component_tests)

    add_executable(fabric_textured_path_tests
        tests/cpp/textured_path_tests.cpp
    )
    target_link_libraries(fabric_textured_path_tests PRIVATE
        fabric_project
        Catch2::Catch2WithMain
    )
    add_test(NAME fabric_textured_path_tests
             COMMAND fabric_textured_path_tests)

    add_executable(fabric_mechanic_graph_tests
        tests/cpp/mechanic_graph_tests.cpp
    )
    target_link_libraries(fabric_mechanic_graph_tests PRIVATE
        fabric_project
        Catch2::Catch2WithMain
    )
    add_test(NAME fabric_mechanic_graph_tests
             COMMAND fabric_mechanic_graph_tests)

    add_executable(fabric_behavior_graph_tests
        tests/cpp/behavior_graph_tests.cpp
    )
    target_link_libraries(fabric_behavior_graph_tests PRIVATE
        fabric_project
        Catch2::Catch2WithMain
    )
    add_test(NAME fabric_behavior_graph_tests
             COMMAND fabric_behavior_graph_tests)

    add_executable(fabric_entity_transformation_tests
        tests/cpp/entity_transformation_tests.cpp
    )
    target_link_libraries(fabric_entity_transformation_tests PRIVATE
        fabric_project Catch2::Catch2WithMain)
    add_test(NAME fabric_entity_transformation_tests
             COMMAND fabric_entity_transformation_tests)

    add_executable(fabric_behavior_evaluator_tests
        tests/cpp/behavior_evaluator_tests.cpp
    )
    target_link_libraries(fabric_behavior_evaluator_tests PRIVATE
        fabric_runtime
        Catch2::Catch2WithMain
    )
    add_test(NAME fabric_behavior_evaluator_tests
             COMMAND fabric_behavior_evaluator_tests)

    add_executable(fabric_textured_path_geometry_tests
        tests/cpp/textured_path_geometry_tests.cpp
    )
    target_link_libraries(fabric_textured_path_geometry_tests PRIVATE
        fabric_render
        Catch2::Catch2WithMain
    )
    add_test(NAME fabric_textured_path_geometry_tests
             COMMAND fabric_textured_path_geometry_tests)

    add_executable(fabric_animation_tests
        tests/cpp/animation_tests.cpp
    )
    target_link_libraries(fabric_animation_tests PRIVATE
        fabric_project Catch2::Catch2WithMain)
    add_test(NAME fabric_animation_tests COMMAND fabric_animation_tests)

    add_executable(fabric_property_descriptor_tests
        tests/cpp/property_descriptor_tests.cpp
    )
    target_link_libraries(fabric_property_descriptor_tests PRIVATE
        fabric_project Catch2::Catch2WithMain)
    add_test(NAME fabric_property_descriptor_tests
             COMMAND fabric_property_descriptor_tests)

    add_executable(fabric_animation_timeline_tests
        tests/cpp/animation_timeline_tests.cpp
    )
    target_link_libraries(fabric_animation_timeline_tests PRIVATE
        fabric_editor Catch2::Catch2WithMain)
    add_test(NAME fabric_animation_timeline_tests
             COMMAND fabric_animation_timeline_tests)

    add_executable(fabric_animation_state_machine_tests
        tests/cpp/animation_state_machine_tests.cpp
    )
    target_link_libraries(fabric_animation_state_machine_tests PRIVATE
        fabric_project Catch2::Catch2WithMain)
    add_test(NAME fabric_animation_state_machine_tests
             COMMAND fabric_animation_state_machine_tests)

    add_executable(fabric_animation_constraints_tests
        tests/cpp/animation_constraints_tests.cpp
    )
    target_link_libraries(fabric_animation_constraints_tests PRIVATE
        fabric_project Catch2::Catch2WithMain)
    add_test(NAME fabric_animation_constraints_tests
             COMMAND fabric_animation_constraints_tests)

    add_executable(fabric_animation_ik_tests
        tests/cpp/animation_ik_tests.cpp
    )
    target_link_libraries(fabric_animation_ik_tests PRIVATE
        fabric_project Catch2::Catch2WithMain)
    add_test(NAME fabric_animation_ik_tests COMMAND fabric_animation_ik_tests)

    add_executable(fabric_xpbd_tests tests/cpp/xpbd_tests.cpp)
    target_link_libraries(fabric_xpbd_tests PRIVATE
        fabric_project Catch2::Catch2WithMain)
    add_test(NAME fabric_xpbd_tests COMMAND fabric_xpbd_tests)

    add_executable(fabric_mesh_deformation_tests
        tests/cpp/mesh_deformation_tests.cpp
    )
    target_link_libraries(fabric_mesh_deformation_tests PRIVATE
        fabric_project Catch2::Catch2WithMain)
    add_test(NAME fabric_mesh_deformation_tests
             COMMAND fabric_mesh_deformation_tests)

    add_executable(fabric_map_tests tests/cpp/map_tests.cpp)
    target_link_libraries(fabric_map_tests PRIVATE
        fabric_project Catch2::Catch2WithMain)
    add_test(NAME fabric_map_tests COMMAND fabric_map_tests)

    add_executable(fabric_map_package_tests tests/cpp/map_package_tests.cpp)
    target_link_libraries(fabric_map_package_tests PRIVATE
        fabric_project Catch2::Catch2WithMain)
    add_test(NAME fabric_map_package_tests COMMAND fabric_map_package_tests)

    add_executable(fabric_scene_tests tests/cpp/scene_tests.cpp)
    target_link_libraries(fabric_scene_tests PRIVATE
        fabric_project Catch2::Catch2WithMain)
    add_test(NAME fabric_scene_tests COMMAND fabric_scene_tests)

    add_executable(fabric_replay_tests tests/cpp/replay_tests.cpp)
    target_link_libraries(fabric_replay_tests PRIVATE
        fabric_project Catch2::Catch2WithMain)
    add_test(NAME fabric_replay_tests COMMAND fabric_replay_tests)

    add_executable(fabric_input_tests tests/cpp/input_tests.cpp)
    target_link_libraries(fabric_input_tests PRIVATE
        fabric_runtime Catch2::Catch2WithMain)
    add_test(NAME fabric_input_tests COMMAND fabric_input_tests)

    add_executable(fabric_input_document_tests
        tests/cpp/input_document_tests.cpp)
    target_link_libraries(fabric_input_document_tests PRIVATE
        fabric_project Catch2::Catch2WithMain)
    add_test(NAME fabric_input_document_tests COMMAND fabric_input_document_tests)

    add_executable(fabric_replay_player_tests tests/cpp/replay_player_tests.cpp)
    target_link_libraries(fabric_replay_player_tests PRIVATE
        fabric_runtime Catch2::Catch2WithMain)
    add_test(NAME fabric_replay_player_tests COMMAND fabric_replay_player_tests)

    add_executable(fabric_scene_session_tests tests/cpp/scene_session_tests.cpp)
    target_link_libraries(fabric_scene_session_tests PRIVATE
        fabric_runtime Catch2::Catch2WithMain)
    add_test(NAME fabric_scene_session_tests COMMAND fabric_scene_session_tests)

    add_executable(fabric_scene_editor_session_tests
        tests/cpp/scene_editor_session_tests.cpp)
    target_link_libraries(fabric_scene_editor_session_tests PRIVATE
        fabric_editor Catch2::Catch2WithMain)
    add_test(NAME fabric_scene_editor_session_tests
             COMMAND fabric_scene_editor_session_tests)

    add_executable(fabric_progress_save_tests tests/cpp/progress_save_tests.cpp)
    target_link_libraries(fabric_progress_save_tests PRIVATE
        fabric_project Catch2::Catch2WithMain)
    add_test(NAME fabric_progress_save_tests COMMAND fabric_progress_save_tests)

    add_executable(fabric_progress_store_tests tests/cpp/progress_store_tests.cpp)
    target_link_libraries(fabric_progress_store_tests PRIVATE
        fabric_runtime Catch2::Catch2WithMain)
    add_test(NAME fabric_progress_store_tests COMMAND fabric_progress_store_tests)

    if(FABRIC_BUILD_RUNTIME)
        add_test(
            NAME game_runtime_progress_resume
            COMMAND ${CMAKE_COMMAND}
                -DGAME_RUNTIME=$<TARGET_FILE:game_runtime>
                -DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head
                -DTEST_ROOT=${CMAKE_BINARY_DIR}/game-runtime-progress-resume
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/game_runtime_progress_resume.cmake
        )
    endif()

    add_test(
        NAME package_install_smoke
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DTEST_ROOT=${CMAKE_BINARY_DIR}/package-install-smoke
            -P ${CMAKE_SOURCE_DIR}/tests/cmake/package_install_smoke.cmake
    )

    if(FABRIC_BUILD_RUNTIME)
        add_test(
            NAME release_product_recipe
            COMMAND ${CMAKE_COMMAND}
                -DASSET_STUDIO=$<TARGET_FILE:asset_studio>
                -DPROJECT_VALIDATOR=$<TARGET_FILE:fabric_project_validate>
                "-DGAME_RUNTIME=$<TARGET_FILE:game_runtime>"
                "-DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head"
                "-DTEST_ROOT=${CMAKE_BINARY_DIR}/release-product-recipe"
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/release_product_recipe.cmake
        )
        set_tests_properties(release_product_recipe PROPERTIES TIMEOUT 180)
    endif()

    add_test(
        NAME release_data_robustness
        COMMAND ${CMAKE_COMMAND}
            "-DVALIDATOR=$<TARGET_FILE:fabric_project_validate>"
            "-DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head"
            "-DINVALID_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/invalid-project"
            "-DTEST_ROOT=${CMAKE_BINARY_DIR}/release-data-robustness"
            -P ${CMAKE_SOURCE_DIR}/tests/cmake/release_data_robustness.cmake
    )

    if(FABRIC_BUILD_EDITORS)
        add_executable(fabric_release_recovery_smoke
            tests/cpp/release_recovery_smoke.cpp)
        target_link_libraries(fabric_release_recovery_smoke PRIVATE fabric_editor)
        add_test(NAME release_recovery_smoke
            COMMAND fabric_release_recovery_smoke
                ${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head
                ${CMAKE_BINARY_DIR}/release-recovery-smoke)
    endif()

    if(FABRIC_BUILD_RUNTIME)
        add_test(
            NAME release_performance_smoke
            COMMAND ${CMAKE_COMMAND}
                "-DRENDER_BENCH=$<TARGET_FILE:fabric_render_benchmark>"
                "-DRUNTIME_BENCH=$<TARGET_FILE:fabric_runtime_benchmark>"
                "-DTEST_ROOT=${CMAKE_BINARY_DIR}/release-performance-smoke"
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/release_performance_smoke.cmake
        )
        set_tests_properties(release_performance_smoke PROPERTIES TIMEOUT 180)
    endif()

    if(FABRIC_BUILD_EDITORS)
        add_test(
            NAME asset_studio_ui_registry_e2e
            COMMAND ${CMAKE_COMMAND}
                -DASSET_STUDIO=$<TARGET_FILE:asset_studio>
                -DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head
                -DTEST_ROOT=${CMAKE_BINARY_DIR}/asset-studio-ui-registry-e2e
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/asset_studio_ui_registry_e2e.cmake
        )
        add_test(
            NAME asset_studio_ui_min_window_e2e
            COMMAND ${CMAKE_COMMAND}
                -DASSET_STUDIO=$<TARGET_FILE:asset_studio>
                -DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head
                -DTEST_ROOT=${CMAKE_BINARY_DIR}/asset-studio-ui-min-window-e2e
                -DUI_ARGUMENT=--ui-test-min-window
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/asset_studio_ui_registry_e2e.cmake
        )
        add_test(
            NAME asset_studio_ui_focus_e2e
            COMMAND ${CMAKE_COMMAND}
                -DASSET_STUDIO=$<TARGET_FILE:asset_studio>
                -DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head
                -DTEST_ROOT=${CMAKE_BINARY_DIR}/asset-studio-ui-focus-e2e
                -DUI_ARGUMENT=--ui-focus-test
                -DFOCUS_ARTIFACT=asset-studio-ui-focus.json
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/asset_studio_ui_registry_e2e.cmake
        )
        add_test(
            NAME asset_studio_ui_accessibility_e2e
            COMMAND ${CMAKE_COMMAND}
                -DASSET_STUDIO=$<TARGET_FILE:asset_studio>
                -DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head
                -DTEST_ROOT=${CMAKE_BINARY_DIR}/asset-studio-ui-accessibility-e2e
                -DUI_ARGUMENT=--ui-accessibility-test
                -DACCESSIBILITY_ARTIFACT=asset-studio-ui-accessibility.json
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/asset_studio_ui_registry_e2e.cmake
        )
        add_test(
            NAME map_studio_ui_accessibility_e2e
            COMMAND ${CMAKE_COMMAND}
                -DMAP_STUDIO=$<TARGET_FILE:map_studio>
                -DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head
                -DTEST_ROOT=${CMAKE_BINARY_DIR}/map-studio-ui-accessibility-e2e
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/map_studio_ui_accessibility_e2e.cmake
        )
        add_test(
            NAME asset_studio_ui_drag_e2e
            COMMAND ${CMAKE_COMMAND}
                -DASSET_STUDIO=$<TARGET_FILE:asset_studio>
                -DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head
                -DTEST_ROOT=${CMAKE_BINARY_DIR}/asset-studio-ui-drag-e2e
                -DUI_ARGUMENT=--ui-drag-test
                -DDRAG_ARTIFACT=asset-studio-ui-drag.json
                -DDRAG_DESTINATION=existing
                -DSKIP_REGISTRY_CHECK=1
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/asset_studio_ui_registry_e2e.cmake
        )
        add_test(
            NAME asset_studio_ui_drag_root_e2e
            COMMAND ${CMAKE_COMMAND}
                -DASSET_STUDIO=$<TARGET_FILE:asset_studio>
                -DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head
                -DTEST_ROOT=${CMAKE_BINARY_DIR}/asset-studio-ui-drag-root-e2e
                -DUI_ARGUMENT=--ui-drag-root-test
                -DDRAG_ARTIFACT=asset-studio-ui-drag.json
                -DDRAG_DESTINATION=root
                -DSKIP_REGISTRY_CHECK=1
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/asset_studio_ui_registry_e2e.cmake
        )
        add_test(
            NAME asset_studio_ui_drag_child_e2e
            COMMAND ${CMAKE_COMMAND}
                -DASSET_STUDIO=$<TARGET_FILE:asset_studio>
                -DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head
                -DTEST_ROOT=${CMAKE_BINARY_DIR}/asset-studio-ui-drag-child-e2e
                -DUI_ARGUMENT=--ui-drag-child-test
                -DDRAG_ARTIFACT=asset-studio-ui-drag.json
                -DDRAG_DESTINATION=child
                -DSKIP_REGISTRY_CHECK=1
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/asset_studio_ui_registry_e2e.cmake
        )
        add_test(
            NAME asset_studio_ui_overrides_e2e
            COMMAND ${CMAKE_COMMAND}
                -DASSET_STUDIO=$<TARGET_FILE:asset_studio>
                -DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head
                -DTEST_ROOT=${CMAKE_BINARY_DIR}/asset-studio-ui-overrides-e2e
                -DUI_ARGUMENT=--ui-overrides-test
                -DOVERRIDE_ARTIFACT=asset-studio-ui-overrides.json
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/asset_studio_ui_registry_e2e.cmake
        )
        add_test(
            NAME asset_studio_ui_texture_e2e
            COMMAND ${CMAKE_COMMAND}
                -DASSET_STUDIO=$<TARGET_FILE:asset_studio>
                -DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head
                -DTEST_ROOT=${CMAKE_BINARY_DIR}/asset-studio-ui-texture-e2e
                -DUI_ARGUMENT=--ui-texture-test
                -DTEXTURE_ARTIFACT=asset-studio-ui-texture.json
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/asset_studio_ui_registry_e2e.cmake
        )
        add_test(
            NAME asset_studio_ui_input_e2e
            COMMAND ${CMAKE_COMMAND}
                -DASSET_STUDIO=$<TARGET_FILE:asset_studio>
                -DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head
                -DTEST_ROOT=${CMAKE_BINARY_DIR}/asset-studio-ui-input-e2e
                -DUI_ARGUMENT=--ui-input-test
                -DINPUT_ARTIFACT=asset-studio-ui-input.json
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/asset_studio_ui_registry_e2e.cmake
        )
        add_test(
            NAME asset_studio_behavior_e2e
            COMMAND ${CMAKE_COMMAND}
                -DASSET_STUDIO=$<TARGET_FILE:asset_studio>
                -DPROJECT_VALIDATE=$<TARGET_FILE:fabric_project_validate>
                -DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head
                -DTEST_ROOT=${CMAKE_BINARY_DIR}/asset-studio-behavior-e2e
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/asset_studio_behavior_e2e.cmake
        )
        add_test(
            NAME asset_studio_transformation_e2e
            COMMAND ${CMAKE_COMMAND}
                -DASSET_STUDIO=$<TARGET_FILE:asset_studio>
                -DPROJECT_VALIDATE=$<TARGET_FILE:fabric_project_validate>
                -DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head
                -DTEST_ROOT=${CMAKE_BINARY_DIR}/asset-studio-transformation-e2e
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/asset_studio_transformation_e2e.cmake
        )
        add_test(
            NAME asset_studio_entity_e2e
            COMMAND ${CMAKE_COMMAND}
                -DASSET_STUDIO=$<TARGET_FILE:asset_studio>
                -DPROJECT_VALIDATE=$<TARGET_FILE:fabric_project_validate>
                -DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head
                -DTEST_ROOT=${CMAKE_BINARY_DIR}/asset-studio-entity-e2e
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/asset_studio_entity_e2e.cmake
        )
        add_test(
            NAME asset_studio_animation_e2e
            COMMAND ${CMAKE_COMMAND}
                -DASSET_STUDIO=$<TARGET_FILE:asset_studio>
                -DPROJECT_VALIDATE=$<TARGET_FILE:fabric_project_validate>
                -DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head
                -DTEST_ROOT=${CMAKE_BINARY_DIR}/asset-studio-animation-e2e
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/asset_studio_animation_e2e.cmake
        )
        add_test(
            NAME asset_studio_texture_e2e
            COMMAND ${CMAKE_COMMAND}
                -DASSET_STUDIO=$<TARGET_FILE:asset_studio>
                -DPROJECT_VALIDATE=$<TARGET_FILE:fabric_project_validate>
                -DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head
                -DTEST_ROOT=${CMAKE_BINARY_DIR}/asset-studio-texture-e2e
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/asset_studio_texture_e2e.cmake
        )
        add_test(
            NAME asset_studio_vector_e2e
            COMMAND ${CMAKE_COMMAND}
                -DASSET_STUDIO=$<TARGET_FILE:asset_studio>
                -DPROJECT_VALIDATE=$<TARGET_FILE:fabric_project_validate>
                -DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head
                -DTEST_ROOT=${CMAKE_BINARY_DIR}/asset-studio-vector-e2e
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/asset_studio_vector_e2e.cmake
        )
        add_test(
            NAME asset_studio_vector_canvas_e2e
            COMMAND ${CMAKE_COMMAND}
                -DASSET_STUDIO=$<TARGET_FILE:asset_studio>
                -DPROJECT_VALIDATE=$<TARGET_FILE:fabric_project_validate>
                -DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head
                -DTEST_ROOT=${CMAKE_BINARY_DIR}/asset-studio-vector-canvas-e2e
                -DVISUAL_BASELINE=${CMAKE_SOURCE_DIR}/tests/fixtures/visual-baselines/asset-studio-vector-canvas-visual.json
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/asset_studio_vector_canvas_e2e.cmake
        )
        add_test(
            NAME asset_studio_beam_vector_canvas_e2e
            COMMAND ${CMAKE_COMMAND}
                -DASSET_STUDIO=$<TARGET_FILE:asset_studio>
                -DPROJECT_VALIDATE=$<TARGET_FILE:fabric_project_validate>
                -DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-beam
                -DTEST_ROOT=${CMAKE_BINARY_DIR}/asset-studio-beam-vector-canvas-e2e
                -DVISUAL_BASELINE=${CMAKE_SOURCE_DIR}/tests/fixtures/visual-baselines/asset-studio-beam-vector-canvas-visual.json
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/asset_studio_vector_canvas_e2e.cmake
        )
        add_test(
            NAME map_studio_close_e2e
            COMMAND ${CMAKE_COMMAND}
                -DMAP_STUDIO=$<TARGET_FILE:map_studio>
                -DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head
                -DTEST_ROOT=${CMAKE_BINARY_DIR}/map-studio-close-e2e
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/map_studio_close_e2e.cmake
        )
        add_test(
            NAME map_studio_scene_e2e
            COMMAND ${CMAKE_COMMAND}
                -DMAP_STUDIO=$<TARGET_FILE:map_studio>
                -DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head
                -DTEST_ROOT=${CMAKE_BINARY_DIR}/map-studio-scene-e2e
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/map_studio_scene_e2e.cmake
        )
        add_test(
            NAME map_studio_transformation_e2e
            COMMAND ${CMAKE_COMMAND}
                -DMAP_STUDIO=$<TARGET_FILE:map_studio>
                -DPROJECT_VALIDATE=$<TARGET_FILE:fabric_project_validate>
                -DSOURCE_FIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/studio-textile-head
                -DTEST_ROOT=${CMAKE_BINARY_DIR}/map-studio-transformation-e2e
                -P ${CMAKE_SOURCE_DIR}/tests/cmake/map_studio_transformation_e2e.cmake
        )
        set_tests_properties(
            asset_studio_ui_registry_e2e
            asset_studio_ui_min_window_e2e
            asset_studio_ui_focus_e2e
            asset_studio_ui_accessibility_e2e
            map_studio_ui_accessibility_e2e
            asset_studio_ui_drag_e2e
            asset_studio_ui_drag_root_e2e
            asset_studio_ui_drag_child_e2e
            asset_studio_ui_overrides_e2e
            asset_studio_ui_texture_e2e
            asset_studio_ui_input_e2e
            asset_studio_behavior_e2e
            asset_studio_transformation_e2e
            asset_studio_entity_e2e
            asset_studio_animation_e2e
            asset_studio_texture_e2e
            asset_studio_vector_e2e
            asset_studio_vector_canvas_e2e
            asset_studio_beam_vector_canvas_e2e
            map_studio_close_e2e
            map_studio_scene_e2e
            map_studio_transformation_e2e
            PROPERTIES
                SKIP_REGULAR_EXPRESSION "SKIP:"
                TIMEOUT 180
        )
    endif()

    add_executable(fabric_audio_mixer_tests tests/cpp/audio_mixer_tests.cpp)
    target_link_libraries(fabric_audio_mixer_tests PRIVATE
        fabric_runtime Catch2::Catch2WithMain)
    add_test(NAME fabric_audio_mixer_tests COMMAND fabric_audio_mixer_tests)

    add_executable(fabric_camera_tests tests/cpp/camera_tests.cpp)
    target_link_libraries(fabric_camera_tests PRIVATE
        fabric_runtime Catch2::Catch2WithMain)
    add_test(NAME fabric_camera_tests COMMAND fabric_camera_tests)

    add_executable(fabric_trigger_runtime_tests tests/cpp/trigger_runtime_tests.cpp)
    target_link_libraries(fabric_trigger_runtime_tests PRIVATE
        fabric_runtime Catch2::Catch2WithMain)
    add_test(NAME fabric_trigger_runtime_tests COMMAND fabric_trigger_runtime_tests)

    add_executable(fabric_character_controller_tests
        tests/cpp/character_controller_tests.cpp)
    target_link_libraries(fabric_character_controller_tests PRIVATE
        fabric_runtime Catch2::Catch2WithMain)
    add_test(NAME fabric_character_controller_tests
             COMMAND fabric_character_controller_tests)

    add_executable(fabric_map_chunk_index_tests
        tests/cpp/map_chunk_index_tests.cpp
    )
    target_link_libraries(fabric_map_chunk_index_tests PRIVATE
        fabric_project Catch2::Catch2WithMain)
    add_test(NAME fabric_map_chunk_index_tests
             COMMAND fabric_map_chunk_index_tests)

    add_executable(fabric_physics_tests tests/cpp/physics_tests.cpp)
    target_link_libraries(fabric_physics_tests PRIVATE
        fabric_physics fabric_project Catch2::Catch2WithMain)
    add_test(NAME fabric_physics_tests COMMAND fabric_physics_tests)

    add_executable(fabric_map_session_tests tests/cpp/map_session_tests.cpp)
    target_link_libraries(fabric_map_session_tests PRIVATE
        fabric_editor Catch2::Catch2WithMain)
    add_test(NAME fabric_map_session_tests COMMAND fabric_map_session_tests)

    add_executable(fabric_mechanic_session_tests
        tests/cpp/mechanic_session_tests.cpp)
    target_link_libraries(fabric_mechanic_session_tests PRIVATE
        fabric_editor Catch2::Catch2WithMain)
    add_test(NAME fabric_mechanic_session_tests
             COMMAND fabric_mechanic_session_tests)

    add_executable(fabric_behavior_session_tests
        tests/cpp/behavior_session_tests.cpp)
    target_link_libraries(fabric_behavior_session_tests PRIVATE
        fabric_editor Catch2::Catch2WithMain)
    add_test(NAME fabric_behavior_session_tests
             COMMAND fabric_behavior_session_tests)

    add_executable(fabric_transformation_session_tests
        tests/cpp/transformation_session_tests.cpp)
    target_link_libraries(fabric_transformation_session_tests PRIVATE
        fabric_editor Catch2::Catch2WithMain)
    add_test(NAME fabric_transformation_session_tests
             COMMAND fabric_transformation_session_tests)

    add_executable(fabric_mechanic_presets_tests
        tests/cpp/mechanic_presets_tests.cpp)
    target_compile_definitions(fabric_mechanic_presets_tests PRIVATE
        FABRIC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(fabric_mechanic_presets_tests PRIVATE
        fabric_editor Catch2::Catch2WithMain)
    add_test(NAME fabric_mechanic_presets_tests
             COMMAND fabric_mechanic_presets_tests)

    add_executable(fabric_preview_runtime_tests tests/cpp/preview_runtime_tests.cpp)
    target_link_libraries(fabric_preview_runtime_tests PRIVATE
        fabric_runtime Catch2::Catch2WithMain)
    add_test(NAME fabric_preview_runtime_tests COMMAND fabric_preview_runtime_tests)

    add_executable(fabric_document_storage_tests
        tests/cpp/document_storage_tests.cpp
    )
    target_link_libraries(fabric_document_storage_tests PRIVATE
        fabric_project
        Catch2::Catch2WithMain
    )
    add_test(NAME fabric_document_storage_tests
             COMMAND fabric_document_storage_tests)

    add_executable(fabric_editor_tests tests/cpp/project_session_tests.cpp)
    target_link_libraries(fabric_editor_tests PRIVATE fabric_editor)
    add_test(NAME fabric_editor_tests COMMAND fabric_editor_tests)
    set_tests_properties(fabric_editor_tests PROPERTIES TIMEOUT 120)

    add_executable(fabric_editor_foundations_tests
        tests/cpp/editor_foundations_tests.cpp
    )
    target_link_libraries(fabric_editor_foundations_tests PRIVATE
        fabric_editor
        Catch2::Catch2WithMain
    )
    add_test(NAME fabric_editor_foundations_tests
             COMMAND fabric_editor_foundations_tests)

    add_executable(fabric_canvas_interaction_tests
        tests/cpp/canvas_interaction_tests.cpp
    )
    target_link_libraries(fabric_canvas_interaction_tests PRIVATE
        fabric_editor
        Catch2::Catch2WithMain
    )
    add_test(NAME fabric_canvas_interaction_tests
             COMMAND fabric_canvas_interaction_tests)

    add_executable(fabric_creation_prompts_tests
        tests/cpp/creation_prompts_tests.cpp
    )
    target_link_libraries(fabric_creation_prompts_tests PRIVATE
        fabric_editor
        Catch2::Catch2WithMain
    )
    add_test(NAME fabric_creation_prompts_tests
             COMMAND fabric_creation_prompts_tests)

    add_executable(fabric_visual_presets_tests
        tests/cpp/visual_presets_tests.cpp
    )
    target_compile_definitions(fabric_visual_presets_tests PRIVATE
        FABRIC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(fabric_visual_presets_tests PRIVATE
        fabric_editor fabric_runtime Catch2::Catch2WithMain)
    add_test(NAME fabric_visual_presets_tests
             COMMAND fabric_visual_presets_tests)

    add_executable(fabric_visual_composition_renderer_tests
        tests/cpp/visual_composition_renderer_tests.cpp
    )
    target_link_libraries(fabric_visual_composition_renderer_tests PRIVATE
        fabric_editor Catch2::Catch2WithMain)
    add_test(NAME fabric_visual_composition_renderer_tests
             COMMAND fabric_visual_composition_renderer_tests)

    add_executable(fabric_project_session_editing_tests
        tests/cpp/project_session_editing_tests.cpp
    )
    target_link_libraries(fabric_project_session_editing_tests PRIVATE
        fabric_editor
        Catch2::Catch2WithMain
    )
    add_test(NAME fabric_project_session_editing_tests
             COMMAND fabric_project_session_editing_tests)

    add_executable(fabric_render_tests tests/cpp/raster_image_tests.cpp)
    target_link_libraries(fabric_render_tests PRIVATE fabric_render)
    add_test(NAME fabric_render_tests COMMAND fabric_render_tests)

    add_executable(fabric_render_gl_smoke tests/cpp/opengl_vector_smoke.cpp)
    target_compile_definitions(fabric_render_gl_smoke PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(fabric_render_gl_smoke PRIVATE
        fabric_render
        SDL2::SDL2-static
    )
    add_test(NAME fabric_render_gl_smoke COMMAND fabric_render_gl_smoke)
    set_tests_properties(fabric_render_gl_smoke PROPERTIES SKIP_RETURN_CODE 77)

    add_test(
        NAME project_validator_accepts_valid_project
        COMMAND fabric_project_validate ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/valid-project
    )
    add_test(
        NAME asset_preview_emits_native_packets
        COMMAND fabric_asset_preview
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-preview-project
            triangle
    )
    add_test(
        NAME project_validator_rejects_invalid_project
        COMMAND ${CMAKE_COMMAND}
            -DVALIDATOR=$<TARGET_FILE:fabric_project_validate>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/invalid-project
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/expect_validator_failure.cmake
    )
    add_test(
        NAME project_validator_emits_structured_errors
        COMMAND ${CMAKE_COMMAND}
            -DVALIDATOR=$<TARGET_FILE:fabric_project_validate>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/invalid-project
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/expect_validator_json_failure.cmake
    )
    add_test(
        NAME no_legacy_sprite_contracts
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/no_legacy_sprite_contracts.cmake
    )
endif()
