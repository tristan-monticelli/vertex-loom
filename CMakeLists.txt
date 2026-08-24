cmake_minimum_required(VERSION 3.24)

project(VertexLoom VERSION 0.1.0 LANGUAGES CXX)

option(FABRIC_BUILD_EDITORS "Build Asset Studio and Map Studio" ON)
option(FABRIC_BUILD_RUNTIME "Build the preview game runtime" ON)
option(FABRIC_BUILD_TESTS "Build the C++ test suite" ON)

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

add_library(fabric_core STATIC
    engine/fabric_core/src/log.cpp
    engine/fabric_core/src/resource_id.cpp
    engine/fabric_core/src/version.cpp
)
target_include_directories(fabric_core PUBLIC engine/fabric_core/include)
target_compile_features(fabric_core PUBLIC cxx_std_20)

add_library(fabric_project STATIC
    engine/fabric_project/src/atomic_save.cpp
    engine/fabric_project/src/manifest.cpp
    engine/fabric_project/src/migrations.cpp
    engine/fabric_project/src/project_creator.cpp
    engine/fabric_project/src/project_validator.cpp
)
target_include_directories(fabric_project PUBLIC engine/fabric_project/include)
target_compile_features(fabric_project PUBLIC cxx_std_20)
target_link_libraries(fabric_project PUBLIC fabric_core PRIVATE nlohmann_json::nlohmann_json)

add_library(fabric_editor STATIC
    engine/fabric_editor/src/project_session.cpp
)
target_include_directories(fabric_editor PUBLIC engine/fabric_editor/include)
target_compile_features(fabric_editor PUBLIC cxx_std_20)
target_link_libraries(fabric_editor PUBLIC fabric_project)

add_executable(fabric_project_validate tools/project_validate/main.cpp)
target_link_libraries(fabric_project_validate PRIVATE fabric_project)

if(FABRIC_BUILD_EDITORS)
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
    FetchContent_Declare(
        imgui_source
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.9
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(SDL2 imgui_source)
    find_package(OpenGL REQUIRED)

    add_library(fabric_imgui STATIC
        ${imgui_source_SOURCE_DIR}/imgui.cpp
        ${imgui_source_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_source_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_source_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_source_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ${imgui_source_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    )
    target_include_directories(fabric_imgui PUBLIC
        ${imgui_source_SOURCE_DIR}
        ${imgui_source_SOURCE_DIR}/backends
    )
    target_compile_features(fabric_imgui PUBLIC cxx_std_20)
    target_link_libraries(fabric_imgui PUBLIC SDL2::SDL2-static OpenGL::GL)

    add_executable(asset_studio editors/asset_studio/main.cpp)
    target_compile_definitions(asset_studio PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(asset_studio PRIVATE fabric_editor fabric_imgui)
    add_executable(map_studio editors/map_studio/main.cpp)
    target_link_libraries(map_studio PRIVATE fabric_project)
endif()

if(FABRIC_BUILD_RUNTIME)
    add_executable(game_runtime game/runtime/main.cpp)
    target_link_libraries(game_runtime PRIVATE fabric_project)
endif()

include(CTest)
if(BUILD_TESTING AND FABRIC_BUILD_TESTS)
    add_executable(fabric_core_smoke tests/cpp/core_smoke.cpp)
    target_link_libraries(fabric_core_smoke PRIVATE fabric_core nlohmann_json::nlohmann_json)
    add_test(NAME fabric_core_smoke COMMAND fabric_core_smoke)

    add_executable(fabric_project_tests tests/cpp/project_manifest_tests.cpp)
    target_link_libraries(fabric_project_tests PRIVATE fabric_project)
    add_test(NAME fabric_project_tests COMMAND fabric_project_tests)

    add_executable(fabric_editor_tests tests/cpp/project_session_tests.cpp)
    target_link_libraries(fabric_editor_tests PRIVATE fabric_editor)
    add_test(NAME fabric_editor_tests COMMAND fabric_editor_tests)

    add_test(
        NAME project_validator_accepts_valid_project
        COMMAND fabric_project_validate ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/valid-project
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
