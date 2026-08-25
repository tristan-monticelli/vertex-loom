cmake_minimum_required(VERSION 3.24)

project(VertexLoom VERSION 0.1.0 LANGUAGES C CXX)

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
    engine/fabric_project/src/document_storage.cpp
    engine/fabric_project/src/entity.cpp
    engine/fabric_project/src/manifest.cpp
    engine/fabric_project/src/material.cpp
    engine/fabric_project/src/migrations.cpp
    engine/fabric_project/src/project_creator.cpp
    engine/fabric_project/src/project_validator.cpp
    engine/fabric_project/src/resource_registry.cpp
    engine/fabric_project/src/texture_asset.cpp
    engine/fabric_project/src/vector_asset.cpp
)
target_include_directories(fabric_project PUBLIC engine/fabric_project/include)
target_compile_features(fabric_project PUBLIC cxx_std_20)
target_link_libraries(fabric_project PUBLIC fabric_core PRIVATE
    nlohmann_json::nlohmann_json
)

add_executable(fabric_project_validate tools/project_validate/main.cpp)
target_link_libraries(fabric_project_validate PRIVATE fabric_project)


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
    engine/fabric_render/src/raster_image.cpp
    engine/fabric_render/src/opengl_vector_renderer.cpp
    engine/fabric_render/src/svg_vector.cpp
    engine/fabric_render/src/vector_geometry.cpp
)
target_include_directories(fabric_render PUBLIC engine/fabric_render/include
    PRIVATE ${SDL2_image_SOURCE_DIR}/src)
target_compile_features(fabric_render PUBLIC cxx_std_20)
target_link_libraries(fabric_render PUBLIC fabric_project OpenGL::GL PRIVATE
    SDL2::SDL2-static
    SDL2_image::SDL2_image-static
)

add_executable(fabric_asset_preview tools/asset_preview/main.cpp)
target_link_libraries(fabric_asset_preview PRIVATE
    fabric_render
    nlohmann_json::nlohmann_json
)

add_library(fabric_editor STATIC
    engine/fabric_editor/src/autosave_scheduler.cpp
    engine/fabric_editor/src/command_stack.cpp
    engine/fabric_editor/src/creation_prompts.cpp
    engine/fabric_editor/src/project_session.cpp
    engine/fabric_editor/src/session_transition.cpp
)
target_include_directories(fabric_editor PUBLIC engine/fabric_editor/include)
target_compile_features(fabric_editor PUBLIC cxx_std_20)
target_link_libraries(fabric_editor PUBLIC fabric_project fabric_render)

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
    target_link_libraries(map_studio PRIVATE fabric_project)
endif()

if(FABRIC_BUILD_RUNTIME)
    add_executable(game_runtime game/runtime/main.cpp)
    target_link_libraries(game_runtime PRIVATE fabric_project)
endif()

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

    add_executable(fabric_animation_tests
        tests/cpp/animation_tests.cpp
    )
    target_link_libraries(fabric_animation_tests PRIVATE
        fabric_project Catch2::Catch2WithMain)
    add_test(NAME fabric_animation_tests COMMAND fabric_animation_tests)

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

    add_executable(fabric_creation_prompts_tests
        tests/cpp/creation_prompts_tests.cpp
    )
    target_link_libraries(fabric_creation_prompts_tests PRIVATE
        fabric_editor
        Catch2::Catch2WithMain
    )
    add_test(NAME fabric_creation_prompts_tests
             COMMAND fabric_creation_prompts_tests)

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
endif()
