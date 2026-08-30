if(NOT DEFINED GAME_RUNTIME OR NOT DEFINED SOURCE_FIXTURE OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "GAME_RUNTIME, SOURCE_FIXTURE and TEST_ROOT are required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
file(COPY "${SOURCE_FIXTURE}/" DESTINATION "${TEST_ROOT}/project")

set(PROJECT_ROOT "${TEST_ROOT}/project")
set(SCENE_DIRECTORY "${PROJECT_ROOT}/scenes")
set(PROGRESS_PATH "${TEST_ROOT}/progress.json")
file(MAKE_DIRECTORY "${SCENE_DIRECTORY}")

file(WRITE "${SCENE_DIRECTORY}/saved-scene.scene.json" [=[
{
  "schemaVersion": 1,
  "type": "scene",
  "id": "saved-scene",
  "name": "Saved Scene",
  "maps": [
    {
      "map": {"id": "textile-head-preview", "expectedType": "map"},
      "layer": "instances"
    }
  ],
  "entryMap": {"id": "textile-head-preview", "expectedType": "map"},
  "transitions": []
}
]=])

file(WRITE "${SCENE_DIRECTORY}/bootstrap-scene.scene.json" [=[
{
  "schemaVersion": 1,
  "type": "scene",
  "id": "bootstrap-scene",
  "name": "Bootstrap Scene",
  "maps": [
    {
      "map": {"id": "textile-head-preview", "expectedType": "map"},
      "layer": "instances"
    }
  ],
  "entryMap": {"id": "textile-head-preview", "expectedType": "map"},
  "transitions": []
}
]=])

file(WRITE "${PROGRESS_PATH}" [=[
{
  "schemaVersion": 1,
  "build": "seed-build",
  "scene": {"id": "saved-scene", "expectedType": "scene"},
  "properties": {
    "has-key": {"type": "bool", "value": true},
    "coins": {"type": "int", "value": 12},
    "player-name": {"type": "text", "value": "Ada"}
  }
}
]=])

execute_process(
    COMMAND "${GAME_RUNTIME}"
        --project "${PROJECT_ROOT}"
        --scene bootstrap-scene
        --save-path "${PROGRESS_PATH}"
        --smoke-test 1
    RESULT_VARIABLE RUNTIME_RESULT
    OUTPUT_VARIABLE RUNTIME_OUTPUT
    ERROR_VARIABLE RUNTIME_ERROR
)
if(NOT RUNTIME_RESULT EQUAL 0)
    message(FATAL_ERROR
        "game_runtime failed (${RUNTIME_RESULT})\n${RUNTIME_OUTPUT}\n${RUNTIME_ERROR}")
endif()

file(READ "${PROGRESS_PATH}" SAVED_PROGRESS)
string(JSON SAVED_SCENE GET "${SAVED_PROGRESS}" scene id)
string(JSON SAVED_HAS_KEY GET "${SAVED_PROGRESS}" properties has-key value)
string(JSON SAVED_COINS GET "${SAVED_PROGRESS}" properties coins value)
string(JSON SAVED_PLAYER_NAME GET "${SAVED_PROGRESS}" properties player-name value)

if(NOT SAVED_SCENE STREQUAL "saved-scene")
    message(FATAL_ERROR "existing slot scene was not authoritative")
endif()
if(NOT SAVED_HAS_KEY OR NOT SAVED_COINS EQUAL 12 OR
   NOT SAVED_PLAYER_NAME STREQUAL "Ada")
    message(FATAL_ERROR "existing slot properties were not preserved")
endif()

set(INVALID_PROGRESS_PATH "${TEST_ROOT}/invalid-progress.json")
set(INVALID_PROGRESS "{invalid progress\n")
file(WRITE "${INVALID_PROGRESS_PATH}" "${INVALID_PROGRESS}")
execute_process(
    COMMAND "${GAME_RUNTIME}"
        --project "${PROJECT_ROOT}"
        --scene bootstrap-scene
        --save-path "${INVALID_PROGRESS_PATH}"
        --smoke-test 1
    RESULT_VARIABLE INVALID_RESULT
    OUTPUT_QUIET
    ERROR_QUIET
)
if(INVALID_RESULT EQUAL 0)
    message(FATAL_ERROR "invalid existing progress unexpectedly launched")
endif()
file(READ "${INVALID_PROGRESS_PATH}" INVALID_PROGRESS_AFTER)
if(NOT INVALID_PROGRESS_AFTER STREQUAL INVALID_PROGRESS)
    message(FATAL_ERROR "invalid existing progress was overwritten")
endif()

set(MISSING_PROGRESS_PATH "${TEST_ROOT}/missing-progress.json")
execute_process(
    COMMAND "${GAME_RUNTIME}"
        --project "${PROJECT_ROOT}"
        --save-path "${MISSING_PROGRESS_PATH}"
        --smoke-test 1
    RESULT_VARIABLE MISSING_RESULT
    OUTPUT_QUIET
    ERROR_QUIET
)
if(MISSING_RESULT EQUAL 0 OR EXISTS "${MISSING_PROGRESS_PATH}")
    message(FATAL_ERROR "an absent progress save launched without a bootstrap scene")
endif()

set(NEW_PROGRESS_PATH "${TEST_ROOT}/new-progress.json")
execute_process(
    COMMAND "${GAME_RUNTIME}"
        --project "${PROJECT_ROOT}"
        --scene bootstrap-scene
        --save-path "${NEW_PROGRESS_PATH}"
        --smoke-test 1
    RESULT_VARIABLE NEW_RESULT
    OUTPUT_VARIABLE NEW_OUTPUT
    ERROR_VARIABLE NEW_ERROR
)
if(NOT NEW_RESULT EQUAL 0)
    message(FATAL_ERROR
        "new progress bootstrap failed (${NEW_RESULT})\n${NEW_OUTPUT}\n${NEW_ERROR}")
endif()
file(READ "${NEW_PROGRESS_PATH}" NEW_PROGRESS)
string(JSON NEW_SCENE GET "${NEW_PROGRESS}" scene id)
string(JSON NEW_PROPERTY_COUNT LENGTH "${NEW_PROGRESS}" properties)
if(NOT NEW_SCENE STREQUAL "bootstrap-scene" OR NOT NEW_PROPERTY_COUNT EQUAL 0)
    message(FATAL_ERROR "new progress did not use the bootstrap scene")
endif()
