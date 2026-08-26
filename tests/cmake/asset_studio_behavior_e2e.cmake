if(NOT DEFINED ASSET_STUDIO OR NOT DEFINED PROJECT_VALIDATE OR
   NOT DEFINED SOURCE_FIXTURE OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "Asset Studio Behavior E2E arguments are required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
file(COPY "${SOURCE_FIXTURE}/" DESTINATION "${TEST_ROOT}/project")

execute_process(
    COMMAND "${ASSET_STUDIO}" --e2e-behavior "${TEST_ROOT}/project"
    RESULT_VARIABLE STUDIO_RESULT
    OUTPUT_VARIABLE STUDIO_OUTPUT
    ERROR_VARIABLE STUDIO_ERROR)
if(NOT STUDIO_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Asset Studio Behavior E2E failed (${STUDIO_RESULT})\n${STUDIO_OUTPUT}\n${STUDIO_ERROR}")
endif()

set(BEHAVIOR
    "${TEST_ROOT}/project/assets/behaviors/behavior-studio-e2e.behavior.json")
if(NOT EXISTS "${BEHAVIOR}")
    message(FATAL_ERROR "Behavior document was not saved")
endif()
file(READ "${BEHAVIOR}" BEHAVIOR_JSON)
string(JSON NODE_COUNT LENGTH "${BEHAVIOR_JSON}" nodes)
string(JSON CONNECTION_COUNT LENGTH "${BEHAVIOR_JSON}" connections)
if(NOT NODE_COUNT EQUAL 2 OR NOT CONNECTION_COUNT EQUAL 1)
    message(FATAL_ERROR "Behavior graph did not reload with authored content")
endif()

set(INPUT
    "${TEST_ROOT}/project/assets/input/player-and-monster-controls.input.json")
if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "Input document was not saved")
endif()
file(READ "${INPUT}" INPUT_JSON)
string(JSON ACTION_COUNT LENGTH "${INPUT_JSON}" actions)
string(JSON MOVE_BINDING_COUNT LENGTH "${INPUT_JSON}" actions 0 bindings)
if(NOT ACTION_COUNT EQUAL 3 OR NOT MOVE_BINDING_COUNT EQUAL 2)
    message(FATAL_ERROR "Input document did not reload with multiple actions and bindings")
endif()

file(READ
    "${TEST_ROOT}/project/entities/rotating-platform-entity.entity.json"
    ENTITY_JSON)
string(JSON BEHAVIOR_ID GET "${ENTITY_JSON}" behavior id)
if(NOT BEHAVIOR_ID STREQUAL "behavior-studio-e2e")
    message(FATAL_ERROR "Entity did not persist the explicit BehaviorGraph")
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
