cmake_minimum_required(VERSION 3.24)

project(FabricEngine VERSION 0.1.0 LANGUAGES CXX)

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
    engine/fabric_project/src/project_validator.cpp
)
target_include_directories(fabric_project PUBLIC engine/fabric_project/include)
target_compile_features(fabric_project PUBLIC cxx_std_20)
target_link_libraries(fabric_project PUBLIC fabric_core PRIVATE nlohmann_json::nlohmann_json)

add_executable(fabric_project_validate tools/project_validate/main.cpp)
target_link_libraries(fabric_project_validate PRIVATE fabric_project)

if(FABRIC_BUILD_EDITORS)
    add_executable(asset_studio editors/asset_studio/main.cpp)
    target_link_libraries(asset_studio PRIVATE fabric_project)
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
