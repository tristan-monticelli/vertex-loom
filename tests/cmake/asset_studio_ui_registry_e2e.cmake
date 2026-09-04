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

if(NOT DEFINED SKIP_REGISTRY_CHECK)
set(REGISTRY "${TEST_ROOT}/project/asset-studio-ui-widgets.json")
if(NOT EXISTS "${REGISTRY}")
    message(FATAL_ERROR "Asset Studio UI test did not produce its registry")
endif()
if(NOT EXISTS "${TEST_ROOT}/project/asset_studio-ui-test.ppm")
    message(FATAL_ERROR "Asset Studio UI test did not produce its screenshot")
endif()
file(READ "${REGISTRY}" REGISTRY_RESULT)
foreach(REQUIRED_WORKSPACE_RESULT
        "\"rendered\": true"
        "\"project_viewer_inspector_order\": true"
        "\"viewer_minimum_width_ok\": true"
        "\"fit_control\": true"
        "\"grid_control\": true"
        "\"background_control\": true")
    string(FIND "${REGISTRY_RESULT}" "${REQUIRED_WORKSPACE_RESULT}"
           WORKSPACE_RESULT_POSITION)
    if(WORKSPACE_RESULT_POSITION LESS 0)
        message(FATAL_ERROR
            "Asset Studio workspace probe is missing ${REQUIRED_WORKSPACE_RESULT}")
    endif()
endforeach()
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
            "\"drop_destination\": \"${DRAG_DESTINATION}\""
            "\"source_widget_seen\": true"
            "\"target_widget_seen\": true"
            "\"drop_applied_to_existing_node\": true"
            "\"drop_persisted_after_reload\": true")
        string(FIND "${DRAG_RESULT}" "${REQUIRED_RESULT}" RESULT_POSITION)
        if(RESULT_POSITION LESS 0)
            message(FATAL_ERROR "Asset Studio drag probe is missing ${REQUIRED_RESULT}")
        endif()
    endforeach()
endif()
if(DEFINED OVERRIDE_ARTIFACT)
    if(NOT EXISTS "${TEST_ROOT}/project/${OVERRIDE_ARTIFACT}")
        message(FATAL_ERROR "Asset Studio UI test did not produce ${OVERRIDE_ARTIFACT}")
    endif()
    file(READ "${TEST_ROOT}/project/${OVERRIDE_ARTIFACT}" OVERRIDE_RESULT)
    foreach(REQUIRED_RESULT
            "\"confirmation_modal_seen\": true"
            "\"cancel_preserved_override\": true"
            "\"confirm_applied\": true")
        string(FIND "${OVERRIDE_RESULT}" "${REQUIRED_RESULT}" RESULT_POSITION)
        if(RESULT_POSITION LESS 0)
            message(FATAL_ERROR "Asset Studio override probe is missing ${REQUIRED_RESULT}")
        endif()
    endforeach()
endif()
if(DEFINED TEXTURE_ARTIFACT)
    if(NOT EXISTS "${TEST_ROOT}/project/${TEXTURE_ARTIFACT}")
        message(FATAL_ERROR "Asset Studio UI test did not produce ${TEXTURE_ARTIFACT}")
    endif()
    file(READ "${TEST_ROOT}/project/${TEXTURE_ARTIFACT}" TEXTURE_RESULT)
    foreach(REQUIRED_RESULT
            "\"crop_canvas_seen\": true"
            "\"crop_applied\": true")
        string(FIND "${TEXTURE_RESULT}" "${REQUIRED_RESULT}" RESULT_POSITION)
        if(RESULT_POSITION LESS 0)
            message(FATAL_ERROR "Asset Studio texture probe is missing ${REQUIRED_RESULT}")
        endif()
    endforeach()
endif()
if(DEFINED INPUT_ARTIFACT)
    if(NOT EXISTS "${TEST_ROOT}/project/${INPUT_ARTIFACT}")
        message(FATAL_ERROR "Asset Studio UI test did not produce ${INPUT_ARTIFACT}")
    endif()
    file(READ "${TEST_ROOT}/project/${INPUT_ARTIFACT}" INPUT_RESULT)
    foreach(REQUIRED_RESULT
            "\"modal_seen\": true"
            "\"created\": true"
            "\"reloaded\": true")
        string(FIND "${INPUT_RESULT}" "${REQUIRED_RESULT}" RESULT_POSITION)
        if(RESULT_POSITION LESS 0)
            message(FATAL_ERROR "Asset Studio input probe is missing ${REQUIRED_RESULT}")
        endif()
    endforeach()
endif()
if(DEFINED BEAM_ARTIFACT)
    if(NOT EXISTS "${TEST_ROOT}/project/${BEAM_ARTIFACT}")
        message(FATAL_ERROR "Asset Studio UI test did not produce ${BEAM_ARTIFACT}")
    endif()
    if(NOT EXISTS "${TEST_ROOT}/project/asset-studio-beam-create.ppm")
        message(FATAL_ERROR "Asset Studio Beam test did not capture its creation form")
    endif()
    file(READ "${TEST_ROOT}/project/${BEAM_ARTIFACT}" BEAM_RESULT)
    foreach(REQUIRED_RESULT
            "\"create_button_seen\": true"
            "\"created_by_click\": true"
            "\"reloaded_with_default_texture\": true")
        string(FIND "${BEAM_RESULT}" "${REQUIRED_RESULT}" RESULT_POSITION)
        if(RESULT_POSITION LESS 0)
            message(FATAL_ERROR "Asset Studio Beam probe is missing ${REQUIRED_RESULT}")
        endif()
    endforeach()
endif()
if(DEFINED BUTTON_ARTIFACT)
    if(NOT EXISTS "${TEST_ROOT}/project/${BUTTON_ARTIFACT}")
        message(FATAL_ERROR "Asset Studio UI test did not produce ${BUTTON_ARTIFACT}")
    endif()
    if(NOT EXISTS "${TEST_ROOT}/project/asset-studio-button-create.ppm")
        message(FATAL_ERROR "Asset Studio Button test did not capture its creation form")
    endif()
    file(READ "${TEST_ROOT}/project/${BUTTON_ARTIFACT}" BUTTON_RESULT)
    foreach(REQUIRED_RESULT
            "\"create_button_seen\": true"
            "\"created_by_click\": true"
            "\"reloaded_with_original_texture_and_shader\": true")
        string(FIND "${BUTTON_RESULT}" "${REQUIRED_RESULT}" RESULT_POSITION)
        if(RESULT_POSITION LESS 0)
            message(FATAL_ERROR "Asset Studio Button probe is missing ${REQUIRED_RESULT}")
        endif()
    endforeach()
endif()
file(READ "${REGISTRY}" FIRST_REGISTRY)
if(NOT DEFINED REGISTRY_REQUIRED_IDS)
    set(REGISTRY_REQUIRED_IDS
        "resource-row-head-face"
        "resource-row-beam-border"
        "resource-row-textile-head-entity"
        "entity-node-root")
endif()
foreach(REQUIRED_ID IN LISTS REGISTRY_REQUIRED_IDS)
    string(FIND "${FIRST_REGISTRY}" "${REQUIRED_ID}" ID_POSITION)
    if(ID_POSITION LESS 0)
        message(FATAL_ERROR "UI registry is missing stable ID ${REQUIRED_ID}")
    endif()
endforeach()

if(NOT DEFINED DRAG_ARTIFACT AND NOT DEFINED OVERRIDE_ARTIFACT AND
   NOT DEFINED TEXTURE_ARTIFACT AND NOT DEFINED INPUT_ARTIFACT AND
   NOT DEFINED BEAM_ARTIFACT AND NOT DEFINED BUTTON_ARTIFACT)
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
endif()
endif()
