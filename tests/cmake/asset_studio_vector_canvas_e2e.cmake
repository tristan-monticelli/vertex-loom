if(NOT DEFINED ASSET_STUDIO OR NOT DEFINED PROJECT_VALIDATE OR
   NOT DEFINED SOURCE_FIXTURE OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "Asset Studio Vector Canvas E2E arguments are required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
file(COPY "${SOURCE_FIXTURE}/" DESTINATION "${TEST_ROOT}/project")

execute_process(
    COMMAND "${ASSET_STUDIO}" --e2e-vector-canvas "${TEST_ROOT}/project"
    RESULT_VARIABLE STUDIO_RESULT
    OUTPUT_VARIABLE STUDIO_OUTPUT
    ERROR_VARIABLE STUDIO_ERROR)
if(STUDIO_RESULT EQUAL 77)
    message("SKIP: Asset Studio Vector Canvas E2E requires a display")
    return()
elseif(NOT STUDIO_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Asset Studio Vector Canvas E2E failed (${STUDIO_RESULT})\n${STUDIO_OUTPUT}\n${STUDIO_ERROR}")
endif()

set(VISUAL_CAPTURE "${TEST_ROOT}/project/asset-studio-vector-canvas-final.ppm")
set(VISUAL_PROBE "${TEST_ROOT}/project/asset-studio-vector-canvas-visual.json")
if(NOT EXISTS "${VISUAL_CAPTURE}" OR NOT EXISTS "${VISUAL_PROBE}")
    message(FATAL_ERROR "Vector Canvas E2E did not produce visual evidence")
endif()
file(SIZE "${VISUAL_CAPTURE}" VISUAL_CAPTURE_SIZE)
if(VISUAL_CAPTURE_SIZE LESS 1000)
    message(FATAL_ERROR "Vector Canvas E2E capture is unexpectedly small")
endif()
file(READ "${VISUAL_PROBE}" VISUAL_PROBE_CONTENT)
string(REGEX MATCH "\"non_background_pixels\"[ \\t\r\n]*:[ \\t\r\n]*([0-9]+)" _ "${VISUAL_PROBE_CONTENT}")
if(NOT CMAKE_MATCH_1 OR CMAKE_MATCH_1 LESS 50)
    message(FATAL_ERROR "Vector Canvas E2E canvas appears empty or flat: ${VISUAL_PROBE_CONTENT}")
endif()

execute_process(
    COMMAND "${PROJECT_VALIDATE}" "${TEST_ROOT}/project"
    RESULT_VARIABLE VALIDATE_RESULT
    OUTPUT_VARIABLE VALIDATE_OUTPUT
    ERROR_VARIABLE VALIDATE_ERROR)
if(NOT VALIDATE_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Vector Canvas E2E project is invalid\n${VALIDATE_OUTPUT}\n${VALIDATE_ERROR}")
endif()
