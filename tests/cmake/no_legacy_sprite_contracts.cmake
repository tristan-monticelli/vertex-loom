cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(PRODUCT_ROOTS
    "${SOURCE_DIR}/engine"
    "${SOURCE_DIR}/editors"
    "${SOURCE_DIR}/game"
    "${SOURCE_DIR}/tools"
)

set(FORBIDDEN_IDENTIFIERS
    "SpriteSheetDefinition"
    "sprite_sheet"
    "spritesheet"
)

foreach(ROOT IN LISTS PRODUCT_ROOTS)
    if(NOT IS_DIRECTORY "${ROOT}")
        message(FATAL_ERROR "Missing product root: ${ROOT}")
    endif()
    file(GLOB_RECURSE SOURCES
        "${ROOT}/*.c"
        "${ROOT}/*.cc"
        "${ROOT}/*.cpp"
        "${ROOT}/*.h"
        "${ROOT}/*.hh"
        "${ROOT}/*.hpp"
    )
    foreach(FILE_PATH IN LISTS SOURCES)
        file(READ "${FILE_PATH}" CONTENT)
        foreach(IDENTIFIER IN LISTS FORBIDDEN_IDENTIFIERS)
            string(FIND "${CONTENT}" "${IDENTIFIER}" OFFSET)
            if(NOT OFFSET EQUAL -1)
                message(FATAL_ERROR
                    "Legacy sprite identifier '${IDENTIFIER}' found in ${FILE_PATH}")
            endif()
        endforeach()
    endforeach()
endforeach()

message(STATUS "No legacy sprite contract identifiers found in product sources")
