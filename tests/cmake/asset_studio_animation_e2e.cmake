if(NOT DEFINED ASSET_STUDIO OR NOT DEFINED PROJECT_VALIDATE OR
   NOT DEFINED SOURCE_FIXTURE OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "Asset Studio Animation E2E arguments are required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
file(COPY "${SOURCE_FIXTURE}/" DESTINATION "${TEST_ROOT}/project")

execute_process(
    COMMAND "${ASSET_STUDIO}" --e2e-animation "${TEST_ROOT}/project"
    RESULT_VARIABLE STUDIO_RESULT
    OUTPUT_VARIABLE STUDIO_OUTPUT
    ERROR_VARIABLE STUDIO_ERROR)
if(STUDIO_RESULT EQUAL 77)
    message("SKIP: Asset Studio Animation E2E requires a display")
    return()
elseif(NOT STUDIO_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Asset Studio Animation E2E failed (${STUDIO_RESULT})\n${STUDIO_OUTPUT}\n${STUDIO_ERROR}")
endif()
if(NOT EXISTS "${TEST_ROOT}/project/asset-studio-animation-e2e.ppm")
    message(FATAL_ERROR "Animation E2E did not produce a visual capture")
endif()

execute_process(
    COMMAND "${PROJECT_VALIDATE}" "${TEST_ROOT}/project"
    RESULT_VARIABLE VALIDATE_RESULT
    OUTPUT_VARIABLE VALIDATE_OUTPUT
    ERROR_VARIABLE VALIDATE_ERROR)
if(NOT VALIDATE_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Targeted animation project is invalid\n${VALIDATE_OUTPUT}\n${VALIDATE_ERROR}")
endif()
