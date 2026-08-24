execute_process(
    COMMAND "${VALIDATOR}" "${PROJECT}"
    RESULT_VARIABLE validator_result
    ERROR_VARIABLE validator_error
)

if(validator_result EQUAL 0)
    message(FATAL_ERROR "validator accepted an invalid project")
endif()

if(NOT validator_error MATCHES "invalid_path")
    message(FATAL_ERROR "validator did not report invalid_path: ${validator_error}")
endif()
