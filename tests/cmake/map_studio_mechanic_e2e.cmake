if(NOT DEFINED MAP_STUDIO OR NOT DEFINED PROJECT_VALIDATE OR
   NOT DEFINED SOURCE_FIXTURE OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "Map Studio Mechanic E2E arguments are required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
file(COPY "${SOURCE_FIXTURE}/" DESTINATION "${TEST_ROOT}/project")

execute_process(
    COMMAND "${MAP_STUDIO}" --e2e-mechanic
            "${TEST_ROOT}/project" textile-head-preview
    RESULT_VARIABLE STUDIO_RESULT
    OUTPUT_VARIABLE STUDIO_OUTPUT
    ERROR_VARIABLE STUDIO_ERROR)
if(STUDIO_RESULT EQUAL 77)
    message("SKIP: Map Studio Mechanic E2E requires a display")
    return()
elseif(NOT STUDIO_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Map Studio Mechanic E2E failed (${STUDIO_RESULT})\n${STUDIO_OUTPUT}\n${STUDIO_ERROR}")
endif()

if(NOT EXISTS "${TEST_ROOT}/project/map-studio-mechanic-graph-e2e.ppm")
    message(FATAL_ERROR "Mechanic graph E2E did not produce a visual capture")
endif()
if(NOT EXISTS "${TEST_ROOT}/project/map-studio-mechanic-entry-e2e.ppm")
    message(FATAL_ERROR "Mechanic E2E did not capture the selected-instance action")
endif()
if(NOT EXISTS "${TEST_ROOT}/project/map-studio-mechanic-source-e2e.ppm")
    message(FATAL_ERROR "Mechanic E2E did not capture the contextual document")
endif()
if(NOT EXISTS "${TEST_ROOT}/project/map-studio-mechanic-map-overlay-e2e.ppm")
    message(FATAL_ERROR "Mechanic E2E did not capture the scoped Map overlay")
endif()
if(NOT EXISTS "${TEST_ROOT}/project/map-studio-publish-runtime-e2e.ppm")
    message(FATAL_ERROR "Publish workspace E2E did not produce a visual capture")
endif()
if(NOT EXISTS
   "${TEST_ROOT}/textile-head-preview.map-package/map-package.json")
    message(FATAL_ERROR "Publish workspace did not produce a map package")
endif()
execute_process(
    COMMAND "${PROJECT_VALIDATE}" "${TEST_ROOT}/project"
    RESULT_VARIABLE VALIDATE_RESULT
    OUTPUT_VARIABLE VALIDATE_OUTPUT
    ERROR_VARIABLE VALIDATE_ERROR)
if(NOT VALIDATE_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Mechanic graph project is invalid\n${VALIDATE_OUTPUT}\n${VALIDATE_ERROR}")
endif()
