if(NOT DEFINED ENABLED)
    message(FATAL_ERROR "ENABLED was not provided.")
endif()

if(NOT ENABLED)
    return()
endif()

if(NOT DEFINED SOURCE_DIR OR NOT IS_DIRECTORY "${SOURCE_DIR}")
    message(FATAL_ERROR "Required directory is missing: ${SOURCE_DIR}")
endif()

if(NOT DEFINED DEST_DIR)
    message(FATAL_ERROR "DEST_DIR was not provided.")
endif()

file(COPY "${SOURCE_DIR}/" DESTINATION "${DEST_DIR}")
