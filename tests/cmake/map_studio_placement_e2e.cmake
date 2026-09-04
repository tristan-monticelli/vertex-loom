if(NOT DEFINED MAP_STUDIO OR NOT DEFINED PROJECT_VALIDATE OR
   NOT DEFINED SOURCE_FIXTURE OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR
        "MAP_STUDIO, PROJECT_VALIDATE, SOURCE_FIXTURE and TEST_ROOT are required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
set(PROJECT_ROOT "${TEST_ROOT}/project")
file(COPY "${SOURCE_FIXTURE}/" DESTINATION "${PROJECT_ROOT}")

execute_process(
    COMMAND "${MAP_STUDIO}" --e2e-placement
        "${PROJECT_ROOT}" textile-head-preview
    RESULT_VARIABLE STUDIO_RESULT
    OUTPUT_VARIABLE STUDIO_OUTPUT
    ERROR_VARIABLE STUDIO_ERROR
)
if(STUDIO_RESULT EQUAL 77)
    message("SKIP: Map Studio placement E2E requires a display")
    return()
elseif(NOT STUDIO_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Map Studio continuous placement failed (${STUDIO_RESULT})\n"
        "${STUDIO_OUTPUT}\n${STUDIO_ERROR}")
endif()

set(MAP_DOCUMENT
    "${PROJECT_ROOT}/maps/textile-head-preview.map.json")
set(CAPTURE
    "${PROJECT_ROOT}/map-studio-continuous-placement-e2e.ppm")
if(NOT EXISTS "${MAP_DOCUMENT}" OR IS_DIRECTORY "${MAP_DOCUMENT}" OR
   NOT EXISTS "${CAPTURE}" OR IS_DIRECTORY "${CAPTURE}")
    message(FATAL_ERROR
        "Map Studio did not persist and capture continuous placement")
endif()

file(READ "${MAP_DOCUMENT}" MAP_JSON)
string(JSON INSTANCE_COUNT LENGTH "${MAP_JSON}" instances)
if(NOT INSTANCE_COUNT EQUAL 4)
    message(FATAL_ERROR
        "Expected two original plus two UI-authored instances, got ${INSTANCE_COUNT}")
endif()

set(AUTHORED_COUNT 0)
foreach(INDEX RANGE 2 3)
    string(JSON INSTANCE_ID GET "${MAP_JSON}" instances ${INDEX} id)
    string(JSON ENTITY_ID GET "${MAP_JSON}" instances ${INDEX} entity id)
    if(INSTANCE_ID MATCHES "^textile-head-entity-instance" AND
       ENTITY_ID STREQUAL "textile-head-entity")
        math(EXPR AUTHORED_COUNT "${AUTHORED_COUNT} + 1")
    endif()
endforeach()
if(NOT AUTHORED_COUNT EQUAL 2)
    message(FATAL_ERROR
        "Continuous placement did not write two distinct selected resources")
endif()

execute_process(
    COMMAND "${PROJECT_VALIDATE}" "${PROJECT_ROOT}"
    RESULT_VARIABLE VALIDATE_RESULT
    OUTPUT_VARIABLE VALIDATE_OUTPUT
    ERROR_VARIABLE VALIDATE_ERROR
)
if(NOT VALIDATE_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Continuous placement produced an invalid project (${VALIDATE_RESULT})\n"
        "${VALIDATE_OUTPUT}\n${VALIDATE_ERROR}")
endif()
