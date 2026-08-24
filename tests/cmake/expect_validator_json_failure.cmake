execute_process(
    COMMAND "${VALIDATOR}" --json "${PROJECT}"
    RESULT_VARIABLE validator_result
    ERROR_VARIABLE validator_error
)

if(validator_result EQUAL 0)
    message(FATAL_ERROR "validator accepted an invalid project")
endif()

if(NOT validator_error MATCHES "\"level\":\"error\"")
    message(FATAL_ERROR "validator did not emit a structured level: ${validator_error}")
endif()

if(NOT validator_error MATCHES "\"code\":\"invalid_path\"")
    message(FATAL_ERROR "validator did not emit a structured code: ${validator_error}")
endif()

if(NOT validator_error MATCHES "\"field\":\"directories.assets\"")
    message(FATAL_ERROR "validator did not emit a structured field: ${validator_error}")
endif()
