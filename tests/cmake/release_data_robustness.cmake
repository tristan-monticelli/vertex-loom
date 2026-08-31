foreach(REQUIRED IN ITEMS VALIDATOR SOURCE_FIXTURE INVALID_FIXTURE TEST_ROOT)
    if(NOT DEFINED ${REQUIRED})
        message(FATAL_ERROR "${REQUIRED} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
file(COPY "${SOURCE_FIXTURE}/" DESTINATION "${TEST_ROOT}/valid")
file(COPY "${INVALID_FIXTURE}/" DESTINATION "${TEST_ROOT}/invalid")

execute_process(
    COMMAND "${VALIDATOR}" "${TEST_ROOT}/valid"
    RESULT_VARIABLE VALID_RESULT
    OUTPUT_VARIABLE VALID_OUTPUT
    ERROR_VARIABLE VALID_ERROR
)
if(NOT VALID_RESULT EQUAL 0)
    message(FATAL_ERROR "valid project was rejected: ${VALID_OUTPUT}${VALID_ERROR}")
endif()

execute_process(
    COMMAND "${VALIDATOR}" "${TEST_ROOT}/invalid"
    RESULT_VARIABLE INVALID_RESULT
    OUTPUT_VARIABLE INVALID_OUTPUT
    ERROR_VARIABLE INVALID_ERROR
)
if(INVALID_RESULT EQUAL 0)
    message(FATAL_ERROR "invalid project was accepted")
endif()

file(REMOVE "${TEST_ROOT}/valid/assets/vectors/head-button-artwork.vector.json")
execute_process(
    COMMAND "${VALIDATOR}" --json "${TEST_ROOT}/valid"
    RESULT_VARIABLE MISSING_RESULT
    OUTPUT_VARIABLE MISSING_OUTPUT
    ERROR_VARIABLE MISSING_ERROR
)
if(MISSING_RESULT EQUAL 0)
    message(FATAL_ERROR "project with missing vector resource was accepted")
endif()
string(FIND "${MISSING_ERROR}" "missing" MISSING_ERROR_CODE)
if(MISSING_ERROR_CODE EQUAL -1)
    message(FATAL_ERROR "missing-resource validation did not report a useful error: ${MISSING_ERROR}")
endif()

message("Release data robustness passed: valid, invalid and missing-resource cases")
