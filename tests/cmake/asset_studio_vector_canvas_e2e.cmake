if(NOT DEFINED ASSET_STUDIO OR NOT DEFINED PROJECT_VALIDATE OR
   NOT DEFINED SOURCE_FIXTURE OR NOT DEFINED TEST_ROOT OR
   NOT DEFINED VISUAL_BASELINE)
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
set(PEN_CAPTURE "${TEST_ROOT}/project/asset-studio-vector-canvas-pen.ppm")
set(HANDLES_CAPTURE "${TEST_ROOT}/project/asset-studio-vector-canvas-handles.ppm")
set(VISUAL_PROBE "${TEST_ROOT}/project/asset-studio-vector-canvas-visual.json")
set(MITER_BUTT_CAPTURE "${TEST_ROOT}/project/asset-studio-vector-canvas-miter-butt.ppm")
set(ROUND_ROUND_CAPTURE "${TEST_ROOT}/project/asset-studio-vector-canvas-round-round.ppm")
set(BEVEL_SQUARE_CAPTURE "${TEST_ROOT}/project/asset-studio-vector-canvas-bevel-square.ppm")
set(ADVANCED_CAPTURE "${TEST_ROOT}/project/asset-studio-vector-canvas-advanced.ppm")
if(NOT EXISTS "${VISUAL_BASELINE}")
    message(FATAL_ERROR "Vector Canvas visual baseline is missing: ${VISUAL_BASELINE}")
endif()
if(NOT EXISTS "${VISUAL_CAPTURE}" OR NOT EXISTS "${PEN_CAPTURE}" OR
   NOT EXISTS "${HANDLES_CAPTURE}" OR NOT EXISTS "${VISUAL_PROBE}" OR
   NOT EXISTS "${MITER_BUTT_CAPTURE}" OR NOT EXISTS "${ROUND_ROUND_CAPTURE}" OR
   NOT EXISTS "${BEVEL_SQUARE_CAPTURE}" OR NOT EXISTS "${ADVANCED_CAPTURE}")
    message(FATAL_ERROR "Vector Canvas E2E did not produce visual evidence")
endif()
foreach(CAPTURE IN ITEMS "${VISUAL_CAPTURE}" "${PEN_CAPTURE}" "${HANDLES_CAPTURE}"
        "${MITER_BUTT_CAPTURE}" "${ROUND_ROUND_CAPTURE}"
        "${BEVEL_SQUARE_CAPTURE}" "${ADVANCED_CAPTURE}")
    file(SIZE "${CAPTURE}" CAPTURE_SIZE)
    if(CAPTURE_SIZE LESS 1000)
        message(FATAL_ERROR "Vector Canvas E2E capture is unexpectedly small: ${CAPTURE}")
    endif()
endforeach()
file(READ "${VISUAL_PROBE}" VISUAL_PROBE_CONTENT)
string(REGEX MATCH "\"non_background_pixels\"[ \\t\r\n]*:[ \\t\r\n]*([0-9]+)" _ "${VISUAL_PROBE_CONTENT}")
if(NOT CMAKE_MATCH_1 OR CMAKE_MATCH_1 LESS 50)
    message(FATAL_ERROR "Vector Canvas E2E canvas appears empty or flat: ${VISUAL_PROBE_CONTENT}")
endif()
string(JSON MINIMUM_CHANNEL GET "${VISUAL_PROBE_CONTENT}" minimum_channel)
string(JSON MAXIMUM_CHANNEL GET "${VISUAL_PROBE_CONTENT}" maximum_channel)
if(MINIMUM_CHANNEL STREQUAL "" OR MAXIMUM_CHANNEL STREQUAL "")
    message(FATAL_ERROR "Vector Canvas E2E probe lacks channel range: ${VISUAL_PROBE_CONTENT}")
endif()
math(EXPR MIN_CHANNEL_LIMIT "${MINIMUM_CHANNEL} + 40")
if(MAXIMUM_CHANNEL LESS MIN_CHANNEL_LIMIT)
    message(FATAL_ERROR "Vector Canvas E2E lacks visible color variation: ${VISUAL_PROBE_CONTENT}")
endif()

file(READ "${VISUAL_BASELINE}" VISUAL_BASELINE_CONTENT)
string(JSON BASELINE_MIN_PIXELS GET "${VISUAL_BASELINE_CONTENT}"
       minimum_non_background_pixels)
string(JSON BASELINE_MAX_PIXELS GET "${VISUAL_BASELINE_CONTENT}"
       maximum_non_background_pixels)
string(JSON BASELINE_MIN_CHANNEL GET "${VISUAL_BASELINE_CONTENT}" minimum_channel)
string(JSON BASELINE_MAX_CHANNEL GET "${VISUAL_BASELINE_CONTENT}" maximum_channel)
if(BASELINE_MIN_PIXELS STREQUAL "" OR BASELINE_MAX_PIXELS STREQUAL "" OR
   BASELINE_MIN_CHANNEL STREQUAL "" OR BASELINE_MAX_CHANNEL STREQUAL "")
    message(FATAL_ERROR "Vector Canvas visual baseline is incomplete")
endif()
string(JSON CURRENT_PIXELS GET "${VISUAL_PROBE_CONTENT}" non_background_pixels)
if(CURRENT_PIXELS LESS BASELINE_MIN_PIXELS OR CURRENT_PIXELS GREATER BASELINE_MAX_PIXELS)
    message(FATAL_ERROR "Vector Canvas visual regression in pixel occupancy: ${VISUAL_PROBE_CONTENT}")
endif()
if(MINIMUM_CHANNEL LESS BASELINE_MIN_CHANNEL OR MAXIMUM_CHANNEL GREATER BASELINE_MAX_CHANNEL)
    message(FATAL_ERROR "Vector Canvas visual regression in channel range: ${VISUAL_PROBE_CONTENT}")
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
