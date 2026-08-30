if(NOT DEFINED MAP_STUDIO OR NOT DEFINED SOURCE_FIXTURE OR
   NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "MAP_STUDIO, SOURCE_FIXTURE and TEST_ROOT are required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")

foreach(MODE IN ITEMS clean window shortcut save save-failure)
    set(PROJECT_ROOT "${TEST_ROOT}/${MODE}")
    file(COPY "${SOURCE_FIXTURE}/" DESTINATION "${PROJECT_ROOT}")
    execute_process(
        COMMAND "${MAP_STUDIO}" --e2e-close "${MODE}"
            "${PROJECT_ROOT}" textile-head-preview
        RESULT_VARIABLE STUDIO_RESULT
        OUTPUT_VARIABLE STUDIO_OUTPUT
        ERROR_VARIABLE STUDIO_ERROR
    )
    if(STUDIO_RESULT EQUAL 77)
        message("SKIP: Map Studio close E2E requires a display")
        return()
    elseif(NOT STUDIO_RESULT EQUAL 0)
        message(FATAL_ERROR
            "Map Studio ${MODE} close failed (${STUDIO_RESULT})\n"
            "${STUDIO_OUTPUT}\n${STUDIO_ERROR}")
    endif()
    set(PRIMARY
        "${PROJECT_ROOT}/maps/textile-head-preview.map.json")
    set(AUTOSAVE
        "${PROJECT_ROOT}/.vertex-loom/autosave/maps/textile-head-preview.map.json")
    if(NOT EXISTS "${PRIMARY}" OR IS_DIRECTORY "${PRIMARY}")
        message(FATAL_ERROR
            "Map Studio ${MODE} close did not preserve primary")
    endif()
    if(NOT MODE STREQUAL "clean" AND NOT EXISTS "${AUTOSAVE}")
        message(FATAL_ERROR
            "Map Studio ${MODE} close did not preserve autosave")
    endif()
    if(EXISTS "${PRIMARY}.e2e-backup")
        message(FATAL_ERROR "Map Studio ${MODE} left a backup behind")
    endif()
endforeach()
