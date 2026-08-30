if(NOT DEFINED ASSET_STUDIO OR NOT DEFINED PROJECT_VALIDATE OR
   NOT DEFINED SOURCE_FIXTURE OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "Asset Studio Transformation E2E arguments are required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
file(COPY "${SOURCE_FIXTURE}/" DESTINATION "${TEST_ROOT}/project")

execute_process(
    COMMAND "${ASSET_STUDIO}" --e2e-transformation "${TEST_ROOT}/project"
    RESULT_VARIABLE STUDIO_RESULT
    OUTPUT_VARIABLE STUDIO_OUTPUT
    ERROR_VARIABLE STUDIO_ERROR)
if(STUDIO_RESULT EQUAL 77)
    message("SKIP: Asset Studio Transformation E2E requires a display")
    return()
elseif(NOT STUDIO_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Asset Studio Transformation E2E failed (${STUDIO_RESULT})\n${STUDIO_OUTPUT}\n${STUDIO_ERROR}")
endif()

set(TRANSFORMATION
    "${TEST_ROOT}/project/assets/transformations/transformation-studio-e2e.transformation.json")
if(NOT EXISTS "${TRANSFORMATION}")
    message(FATAL_ERROR "Transformation document was not saved")
endif()
file(READ "${TRANSFORMATION}" TRANSFORMATION_JSON)
string(JSON SOURCE GET "${TRANSFORMATION_JSON}" sourceEntity id)
string(JSON DESTINATION GET "${TRANSFORMATION_JSON}" destinationEntity id)
string(JSON PROPERTY_MODE GET "${TRANSFORMATION_JSON}" policy properties)
string(JSON MAPPING_COUNT LENGTH "${TRANSFORMATION_JSON}" policy mappings)
if(NOT SOURCE STREQUAL "rotating-platform-entity" OR
   NOT DESTINATION STREQUAL "textile-head-entity" OR
   NOT PROPERTY_MODE STREQUAL "mapping" OR NOT MAPPING_COUNT EQUAL 1)
    message(FATAL_ERROR "Transformation did not reload with authored policy")
endif()

execute_process(
    COMMAND "${PROJECT_VALIDATE}" "${TEST_ROOT}/project"
    RESULT_VARIABLE VALIDATE_RESULT
    OUTPUT_VARIABLE VALIDATE_OUTPUT
    ERROR_VARIABLE VALIDATE_ERROR)
if(NOT VALIDATE_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Authored project is invalid\n${VALIDATE_OUTPUT}\n${VALIDATE_ERROR}")
endif()
