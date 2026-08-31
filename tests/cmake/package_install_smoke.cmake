if(NOT DEFINED BUILD_DIR OR NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "BUILD_DIR, SOURCE_DIR and TEST_ROOT are required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
set(STAGING "${TEST_ROOT}/staging")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${STAGING}"
    RESULT_VARIABLE INSTALL_RESULT
    OUTPUT_VARIABLE INSTALL_OUTPUT
    ERROR_VARIABLE INSTALL_ERROR
)
if(NOT INSTALL_RESULT EQUAL 0)
    message(FATAL_ERROR "install failed: ${INSTALL_OUTPUT}${INSTALL_ERROR}")
endif()

if(WIN32)
    set(EXE_SUFFIX ".exe")
else()
    set(EXE_SUFFIX "")
endif()
foreach(EXECUTABLE IN ITEMS fabric_project_validate fabric_map_package_export
                            fabric_asset_preview asset_studio map_studio game_runtime)
    if((EXECUTABLE STREQUAL "asset_studio" OR EXECUTABLE STREQUAL "map_studio") AND
       NOT EXISTS "${BUILD_DIR}/${EXECUTABLE}${EXE_SUFFIX}")
        continue()
    endif()
    if((EXECUTABLE STREQUAL "game_runtime") AND
       NOT EXISTS "${BUILD_DIR}/${EXECUTABLE}${EXE_SUFFIX}")
        continue()
    endif()
    if(NOT EXISTS "${STAGING}/bin/${EXECUTABLE}${EXE_SUFFIX}")
        message(FATAL_ERROR "installed executable is missing: ${EXECUTABLE}")
    endif()
endforeach()

set(EXAMPLE_PROJECT "${STAGING}/share/vertex-loom/examples/studio-textile-head/project.json")
if(NOT EXISTS "${EXAMPLE_PROJECT}")
    message(FATAL_ERROR "installed example project is missing: ${EXAMPLE_PROJECT}")
endif()

execute_process(
    COMMAND "${STAGING}/bin/fabric_project_validate${EXE_SUFFIX}" "${STAGING}/share/vertex-loom/examples/studio-textile-head"
    RESULT_VARIABLE VALIDATE_RESULT
    OUTPUT_VARIABLE VALIDATE_OUTPUT
    ERROR_VARIABLE VALIDATE_ERROR
)
if(NOT VALIDATE_RESULT EQUAL 0)
    message(FATAL_ERROR "installed example validation failed: ${VALIDATE_OUTPUT}${VALIDATE_ERROR}")
endif()

execute_process(
    COMMAND "${STAGING}/bin/game_runtime${EXE_SUFFIX}" --help
    RESULT_VARIABLE RUNTIME_RESULT
    OUTPUT_VARIABLE RUNTIME_OUTPUT
    ERROR_VARIABLE RUNTIME_ERROR
)
if(NOT RUNTIME_RESULT EQUAL 0)
    message(FATAL_ERROR "installed runtime help failed: ${RUNTIME_OUTPUT}${RUNTIME_ERROR}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIR}" --target package
    RESULT_VARIABLE PACKAGE_RESULT
    OUTPUT_VARIABLE PACKAGE_OUTPUT
    ERROR_VARIABLE PACKAGE_ERROR
)
if(NOT PACKAGE_RESULT EQUAL 0)
    message(FATAL_ERROR "CPack package failed: ${PACKAGE_OUTPUT}${PACKAGE_ERROR}")
endif()
file(GLOB PACKAGES "${BUILD_DIR}/vertex-loom-*.tar.gz"
                   "${BUILD_DIR}/vertex-loom-*.zip")
if(NOT PACKAGES)
    message(FATAL_ERROR "CPack did not produce an archive")
endif()
