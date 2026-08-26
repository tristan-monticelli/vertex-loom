if(NOT DEFINED ASSET_STUDIO OR NOT DEFINED PROJECT_VALIDATE OR
   NOT DEFINED SOURCE_FIXTURE OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "Asset Studio Texture E2E arguments are required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
file(COPY "${SOURCE_FIXTURE}/" DESTINATION "${TEST_ROOT}/project")

execute_process(
    COMMAND "${ASSET_STUDIO}" --e2e-texture "${TEST_ROOT}/project"
    RESULT_VARIABLE STUDIO_RESULT
    OUTPUT_VARIABLE STUDIO_OUTPUT
    ERROR_VARIABLE STUDIO_ERROR)
if(NOT STUDIO_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Asset Studio Texture E2E failed (${STUDIO_RESULT})\n${STUDIO_OUTPUT}\n${STUDIO_ERROR}")
endif()

set(TEXTURE
    "${TEST_ROOT}/project/assets/textures/texture-studio-e2e.texture.json")
set(MATERIAL
    "${TEST_ROOT}/project/assets/materials/texture-studio-e2e-material.material.json")
if(NOT EXISTS "${TEXTURE}" OR NOT EXISTS "${MATERIAL}")
    message(FATAL_ERROR "Texture crop or follow-up material was not persisted")
endif()
file(READ "${TEXTURE}" TEXTURE_JSON)
string(FIND "${TEXTURE_JSON}" "\"view\"" VIEW_POSITION)
if(VIEW_POSITION LESS 0)
    message(FATAL_ERROR "Texture E2E did not persist the non-destructive view")
endif()

execute_process(
    COMMAND "${PROJECT_VALIDATE}" "${TEST_ROOT}/project"
    RESULT_VARIABLE VALIDATE_RESULT
    OUTPUT_VARIABLE VALIDATE_OUTPUT
    ERROR_VARIABLE VALIDATE_ERROR)
if(NOT VALIDATE_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Texture E2E project is invalid\n${VALIDATE_OUTPUT}\n${VALIDATE_ERROR}")
endif()
