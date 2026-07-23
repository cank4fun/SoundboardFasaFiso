if(NOT DEFINED ENABLED)
    set(ENABLED OFF)
endif()

if(ENABLED)
    if(NOT DEFINED SOURCE_NOTICE OR NOT EXISTS "${SOURCE_NOTICE}")
        message(FATAL_ERROR
            "The optional notice file is enabled but missing: ${SOURCE_NOTICE}"
        )
    endif()

    if(NOT DEFINED DEST_NOTICE OR DEST_NOTICE STREQUAL "")
        message(FATAL_ERROR "DEST_NOTICE is required when ENABLED is true.")
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SOURCE_NOTICE}"
            "${DEST_NOTICE}"
        COMMAND_ERROR_IS_FATAL ANY
    )
elseif(DEFINED DEST_NOTICE AND EXISTS "${DEST_NOTICE}")
    file(REMOVE "${DEST_NOTICE}")
endif()
