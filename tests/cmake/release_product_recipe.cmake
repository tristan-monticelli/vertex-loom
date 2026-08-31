foreach(REQUIRED IN ITEMS ASSET_STUDIO PROJECT_VALIDATOR GAME_RUNTIME SOURCE_FIXTURE TEST_ROOT)
    if(NOT DEFINED ${REQUIRED})
        message(FATAL_ERROR "${REQUIRED} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
file(COPY "${SOURCE_FIXTURE}/" DESTINATION "${TEST_ROOT}/project")
set(PROJECT "${TEST_ROOT}/project")

execute_process(
    COMMAND "${ASSET_STUDIO}" --e2e-vector-canvas "${PROJECT}"
    RESULT_VARIABLE STUDIO_RESULT
    OUTPUT_VARIABLE STUDIO_OUTPUT
    ERROR_VARIABLE STUDIO_ERROR
)
if(STUDIO_RESULT EQUAL 77)
    message("SKIP: release product recipe requires a display")
    return()
elseif(NOT STUDIO_RESULT EQUAL 0)
    message(FATAL_ERROR "Asset Studio recipe failed (${STUDIO_RESULT})\n${STUDIO_OUTPUT}\n${STUDIO_ERROR}")
endif()

execute_process(
    COMMAND "${PROJECT_VALIDATOR}" "${PROJECT}"
    RESULT_VARIABLE VALIDATOR_RESULT
    OUTPUT_VARIABLE VALIDATOR_OUTPUT
    ERROR_VARIABLE VALIDATOR_ERROR
)
if(NOT VALIDATOR_RESULT EQUAL 0)
    message(FATAL_ERROR "saved project validation failed: ${VALIDATOR_OUTPUT}${VALIDATOR_ERROR}")
endif()

execute_process(
    COMMAND "${GAME_RUNTIME}" --project "${PROJECT}" --map textile-head-preview --smoke-test 4
    RESULT_VARIABLE RUNTIME_RESULT
    OUTPUT_VARIABLE RUNTIME_OUTPUT
    ERROR_VARIABLE RUNTIME_ERROR
)
if(NOT RUNTIME_RESULT EQUAL 0)
    message(FATAL_ERROR "published runtime recipe failed: ${RUNTIME_OUTPUT}${RUNTIME_ERROR}")
endif()

if(NOT EXISTS "${PROJECT}/assets/vectors/head-button-artwork.vector.json")
    message(FATAL_ERROR "recipe did not preserve the edited vector resource")
endif()
message("Release product recipe passed: create/edit/save/reopen/validate/runtime")
