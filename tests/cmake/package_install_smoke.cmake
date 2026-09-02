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

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${STAGING}"
    RESULT_VARIABLE UPDATE_RESULT
    OUTPUT_VARIABLE UPDATE_OUTPUT
    ERROR_VARIABLE UPDATE_ERROR
)
if(NOT UPDATE_RESULT EQUAL 0)
    message(FATAL_ERROR "update over an existing installation failed: ${UPDATE_OUTPUT}${UPDATE_ERROR}")
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

set(DEFAULT_ASSET_ROOT "${STAGING}/share/vertex-loom/asset-studio-defaults")
if(NOT EXISTS "${DEFAULT_ASSET_ROOT}/manifest.json" OR
   NOT EXISTS "${DEFAULT_ASSET_ROOT}/README.md")
    message(FATAL_ERROR "installed default asset manifest or notice is missing")
endif()
function(verify_default_asset DEFAULT_ASSET_FILE DEFAULT_ASSET_SHA256)
    set(DEFAULT_ASSET_PATH "${DEFAULT_ASSET_ROOT}/${DEFAULT_ASSET_FILE}")
    if(NOT EXISTS "${DEFAULT_ASSET_PATH}")
        message(FATAL_ERROR "installed default asset is missing: ${DEFAULT_ASSET_FILE}")
    endif()
    file(SHA256 "${DEFAULT_ASSET_PATH}" INSTALLED_SHA256)
    if(NOT INSTALLED_SHA256 STREQUAL DEFAULT_ASSET_SHA256)
        message(FATAL_ERROR "installed default asset checksum mismatch: ${DEFAULT_ASSET_FILE}")
    endif()
endfunction()
verify_default_asset("beam-thread.png"
    "32133353656077f486cf62c4bcf6f5cf3dd51210b07023f2c4bf94b11f6bda01")
verify_default_asset("button-primary.png"
    "fbcc1a24f9c9d2b33fe8fe6c16903c1787adcdf9bedfbaa6594e0042350a1253")
verify_default_asset("button-secondary.png"
    "c266d783d715950da86818c5241e5b4ea9fb017937679a8f356aa7379dea683f")
foreach(LICENSE_FILE IN ITEMS LICENSE NOTICE THIRD_PARTY_NOTICES.md)
    if(NOT EXISTS "${STAGING}/share/vertex-loom/licenses/${LICENSE_FILE}")
        message(FATAL_ERROR "installed license file is missing: ${LICENSE_FILE}")
    endif()
endforeach()

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

file(GLOB_RECURSE INSTALLED_FILES LIST_DIRECTORIES false "${STAGING}/*")
foreach(INSTALLED_FILE IN LISTS INSTALLED_FILES)
    file(REMOVE "${INSTALLED_FILE}")
endforeach()
file(GLOB_RECURSE INSTALLED_AFTER_UNINSTALL LIST_DIRECTORIES false "${STAGING}/*")
if(INSTALLED_AFTER_UNINSTALL)
    message(FATAL_ERROR "uninstall left installed files: ${INSTALLED_AFTER_UNINSTALL}")
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
