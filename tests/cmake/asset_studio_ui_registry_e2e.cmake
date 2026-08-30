if(NOT DEFINED ASSET_STUDIO OR NOT DEFINED SOURCE_FIXTURE OR
   NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "Asset Studio UI registry E2E arguments are required")
endif()

if(NOT DEFINED UI_ARGUMENT)
    set(UI_ARGUMENT "--ui-test")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
file(COPY "${SOURCE_FIXTURE}/" DESTINATION "${TEST_ROOT}/project")

execute_process(
    COMMAND "${ASSET_STUDIO}" "${UI_ARGUMENT}" "${TEST_ROOT}/project"
    RESULT_VARIABLE FIRST_RESULT
    OUTPUT_VARIABLE FIRST_OUTPUT
    ERROR_VARIABLE FIRST_ERROR)
if(FIRST_RESULT EQUAL 77)
    message("SKIP: Asset Studio UI registry E2E requires a display")
    return()
elseif(NOT FIRST_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Asset Studio UI registry E2E failed (${FIRST_RESULT})\n${FIRST_OUTPUT}\n${FIRST_ERROR}")
endif()

set(REGISTRY "${TEST_ROOT}/project/asset-studio-ui-widgets.json")
if(NOT EXISTS "${REGISTRY}")
    message(FATAL_ERROR "Asset Studio UI test did not produce its registry")
endif()
if(NOT EXISTS "${TEST_ROOT}/project/asset_studio-ui-test.ppm")
    message(FATAL_ERROR "Asset Studio UI test did not produce its screenshot")
endif()
if(DEFINED FOCUS_ARTIFACT)
    if(NOT EXISTS "${TEST_ROOT}/project/${FOCUS_ARTIFACT}")
        message(FATAL_ERROR "Asset Studio UI test did not produce ${FOCUS_ARTIFACT}")
    endif()
    file(READ "${TEST_ROOT}/project/${FOCUS_ARTIFACT}" FOCUS_RESULT)
    string(FIND "${FOCUS_RESULT}" "\"focused_first_invalid_field\": true" FOCUS_POSITION)
    if(FOCUS_POSITION LESS 0)
        message(FATAL_ERROR "Asset Studio UI focus test did not focus the first invalid field")
    endif()
endif()
if(DEFINED ACCESSIBILITY_ARTIFACT)
    if(NOT EXISTS "${TEST_ROOT}/project/${ACCESSIBILITY_ARTIFACT}")
        message(FATAL_ERROR "Asset Studio UI test did not produce ${ACCESSIBILITY_ARTIFACT}")
    endif()
    file(READ "${TEST_ROOT}/project/${ACCESSIBILITY_ARTIFACT}" ACCESSIBILITY_RESULT)
    foreach(REQUIRED_RESULT
            "\"keyboard_navigation_enabled\": true"
            "\"text_window_contrast_ok\": true")
        string(FIND "${ACCESSIBILITY_RESULT}" "${REQUIRED_RESULT}" RESULT_POSITION)
        if(RESULT_POSITION LESS 0)
            message(FATAL_ERROR "Asset Studio accessibility probe is missing ${REQUIRED_RESULT}")
        endif()
    endforeach()
endif()
if(DEFINED DRAG_ARTIFACT)
    if(NOT EXISTS "${TEST_ROOT}/project/${DRAG_ARTIFACT}")
        message(FATAL_ERROR "Asset Studio UI test did not produce ${DRAG_ARTIFACT}")
    endif()
    file(READ "${TEST_ROOT}/project/${DRAG_ARTIFACT}" DRAG_RESULT)
    foreach(REQUIRED_RESULT
            "\"source_widget_seen\": true"
            "\"target_widget_seen\": true"
            "\"drop_applied_to_existing_node\": true")
        string(FIND "${DRAG_RESULT}" "${REQUIRED_RESULT}" RESULT_POSITION)
        if(RESULT_POSITION LESS 0)
            message(FATAL_ERROR "Asset Studio drag probe is missing ${REQUIRED_RESULT}")
        endif()
    endforeach()
endif()
file(READ "${REGISTRY}" FIRST_REGISTRY)
foreach(REQUIRED_ID
        "resource-row-head-face"
        "resource-row-head-button-artwork"
        "resource-row-textile-head-entity"
        "entity-node-root")
    string(FIND "${FIRST_REGISTRY}" "${REQUIRED_ID}" ID_POSITION)
    if(ID_POSITION LESS 0)
        message(FATAL_ERROR "UI registry is missing stable ID ${REQUIRED_ID}")
    endif()
endforeach()

execute_process(
    COMMAND "${ASSET_STUDIO}" "${UI_ARGUMENT}" "${TEST_ROOT}/project"
    RESULT_VARIABLE SECOND_RESULT
    OUTPUT_VARIABLE SECOND_OUTPUT
    ERROR_VARIABLE SECOND_ERROR)
if(SECOND_RESULT EQUAL 77)
    message("SKIP: Asset Studio UI registry E2E requires a display")
    return()
elseif(NOT SECOND_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Asset Studio UI registry repeat failed (${SECOND_RESULT})\n${SECOND_OUTPUT}\n${SECOND_ERROR}")
endif()
file(READ "${REGISTRY}" SECOND_REGISTRY)
if(NOT FIRST_REGISTRY STREQUAL SECOND_REGISTRY)
    message(FATAL_ERROR "Asset Studio UI registry is not stable across repeated frames")
endif()
