if(NOT DEFINED VCPKG_SHARE_DIR OR VCPKG_SHARE_DIR STREQUAL "")
    message(FATAL_ERROR "VCPKG_SHARE_DIR is required.")
endif()

if(NOT DEFINED OUTPUT_FILE OR OUTPUT_FILE STREQUAL "")
    message(FATAL_ERROR "OUTPUT_FILE is required.")
endif()

if(NOT IS_DIRECTORY "${VCPKG_SHARE_DIR}")
    message(FATAL_ERROR
        "The vcpkg share directory does not exist: ${VCPKG_SHARE_DIR}"
    )
endif()

file(GLOB notice_files LIST_DIRECTORIES FALSE
    "${VCPKG_SHARE_DIR}/*/copyright"
)
list(SORT notice_files)

if(NOT notice_files)
    message(FATAL_ERROR
        "No vcpkg copyright files were found under ${VCPKG_SHARE_DIR}."
    )
endif()

file(WRITE "${OUTPUT_FILE}"
    "WebRTC AEC3 and transitive third-party notices\n"
    "==============================================\n\n"
    "This file is generated from the copyright files installed by the pinned "
    "vcpkg manifest used for the WebRTC AEC3 build.\n\n"
)

foreach(notice_file IN LISTS notice_files)
    get_filename_component(package_share_dir "${notice_file}" DIRECTORY)
    get_filename_component(package_name "${package_share_dir}" NAME)
    file(READ "${notice_file}" notice_text)

    file(APPEND "${OUTPUT_FILE}"
        "------------------------------------------------------------------------\n"
        "Package: ${package_name}\n"
        "Source notice: share/${package_name}/copyright\n"
        "------------------------------------------------------------------------\n\n"
        "${notice_text}\n\n"
    )
endforeach()
