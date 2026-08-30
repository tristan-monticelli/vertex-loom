if(NOT DEFINED MAP_STUDIO OR NOT DEFINED SOURCE_FIXTURE OR
   NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "MAP_STUDIO, SOURCE_FIXTURE and TEST_ROOT are required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
set(PROJECT_ROOT "${TEST_ROOT}/project")
file(COPY "${SOURCE_FIXTURE}/" DESTINATION "${PROJECT_ROOT}")

execute_process(
    COMMAND "${MAP_STUDIO}" --e2e-scene
        "${PROJECT_ROOT}" textile-head-preview
    RESULT_VARIABLE STUDIO_RESULT
    OUTPUT_VARIABLE STUDIO_OUTPUT
    ERROR_VARIABLE STUDIO_ERROR
)
if(STUDIO_RESULT EQUAL 77)
    message("SKIP: Map Studio scene E2E requires a display")
    return()
elseif(NOT STUDIO_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Map Studio scene authoring failed (${STUDIO_RESULT})\n"
        "${STUDIO_OUTPUT}\n${STUDIO_ERROR}")
endif()

set(SCENE_DOCUMENT
    "${PROJECT_ROOT}/scenes/scene-studio-e2e.scene.json")
set(PACKAGE_MANIFEST
    "${TEST_ROOT}/scene-studio-e2e.scene-package/scene-package.json")
set(MAP_DOCUMENT
    "${PROJECT_ROOT}/maps/textile-head-preview.map.json")
if(NOT EXISTS "${SCENE_DOCUMENT}" OR IS_DIRECTORY "${SCENE_DOCUMENT}" OR
   NOT EXISTS "${PACKAGE_MANIFEST}" OR IS_DIRECTORY "${PACKAGE_MANIFEST}")
    message(FATAL_ERROR
        "Scene Studio did not persist and publish the authored campaign")
endif()

file(READ "${MAP_DOCUMENT}" MAP_JSON)
string(JSON TRIGGER_COUNT LENGTH "${MAP_JSON}" triggers)
string(JSON TRIGGER_SOURCE GET "${MAP_JSON}" triggers 0 properties 0 value value)
if(NOT TRIGGER_COUNT EQUAL 1 OR
   NOT TRIGGER_SOURCE STREQUAL "scene-studio")
    message(FATAL_ERROR "Map Studio did not persist the trigger override")
endif()

file(READ "${SCENE_DOCUMENT}" SCENE_JSON)
string(JSON SCENE_ID GET "${SCENE_JSON}" id)
string(JSON MAP_COUNT LENGTH "${SCENE_JSON}" maps)
string(JSON TRANSITION_COUNT LENGTH "${SCENE_JSON}" transitions)
if(NOT SCENE_ID STREQUAL "scene-studio-e2e" OR
   NOT MAP_COUNT EQUAL 1 OR NOT TRANSITION_COUNT EQUAL 1)
    message(FATAL_ERROR "Scene Studio persisted an incomplete scene")
endif()
