cmake_minimum_required(VERSION 3.25)

set(SOUNDBOARD_VERSION "2.0.0-rc.2")

project(SoundBoardFasaFiso VERSION 2.0.0 LANGUAGES CXX)

if(SOUNDBOARD_VERSION MATCHES "-")
    set(SOUNDBOARD_VERSION_FILE_FLAGS "VS_FF_PRERELEASE")
else()
    set(SOUNDBOARD_VERSION_FILE_FLAGS "0x0L")
endif()

if(NOT WIN32)
    message(FATAL_ERROR "SoundBoardFasaFiso currently supports Windows only.")
endif()

enable_language(RC)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(
    SOUNDBOARD_WARNINGS_AS_ERRORS
    "Treat project warnings as errors"
    OFF
)

set(SOUNDBOARD_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

file(MAKE_DIRECTORY "${SOUNDBOARD_GENERATED_DIR}/app")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/app/Version.hpp.in"
    "${SOUNDBOARD_GENERATED_DIR}/app/Version.hpp"
    @ONLY
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/SoundBoardFasaFiso.rc.in"
    "${SOUNDBOARD_GENERATED_DIR}/SoundBoardFasaFiso.rc"
    @ONLY
)

add_library(
    SoundBoardFasaFisoMiniaudio
    OBJECT
    src/audio/miniaudio_impl.cpp
)

target_include_directories(
    SoundBoardFasaFisoMiniaudio
    SYSTEM PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)

add_executable(
    SoundBoardFasaFiso
    WIN32
    src/main.cpp
    src/audio/Audio.cpp
    $<TARGET_OBJECTS:SoundBoardFasaFisoMiniaudio>
    src/config/Config.cpp
    src/diagnostics/Logger.cpp
    src/gui/ControlWindow.cpp
    src/hotkeys/HotkeyManager.cpp
    src/localization/Localization.cpp
    src/platform/DebugConsole.cpp
    src/platform/SingleInstance.cpp
    src/platform/StartupManager.cpp
    src/tray/TrayIcon.cpp
    src/update/UpdateChecker.cpp
    "${SOUNDBOARD_GENERATED_DIR}/SoundBoardFasaFiso.rc"
)

target_include_directories(
    SoundBoardFasaFiso
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        "${SOUNDBOARD_GENERATED_DIR}"
)

target_include_directories(
    SoundBoardFasaFiso
    SYSTEM PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)

target_compile_definitions(
    SoundBoardFasaFiso
    PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
)

if(MSVC)
    target_compile_options(
        SoundBoardFasaFiso
        PRIVATE
            /W4
            /permissive-
            /utf-8
            /sdl
            /guard:cf
    )

    if(SOUNDBOARD_WARNINGS_AS_ERRORS)
        target_compile_options(SoundBoardFasaFiso PRIVATE /WX)
    endif()

    target_compile_options(
        SoundBoardFasaFiso
        PRIVATE
            $<$<CONFIG:Release>:/Gy>
            $<$<CONFIG:Release>:/Gw>
    )

    target_link_options(
        SoundBoardFasaFiso
        PRIVATE
            /guard:cf
            /DYNAMICBASE
            /NXCOMPAT
            $<$<CONFIG:Release>:/OPT:REF>
            $<$<CONFIG:Release>:/OPT:ICF>
            $<$<CONFIG:Release>:/INCREMENTAL:NO>
    )

    target_compile_options(
        SoundBoardFasaFisoMiniaudio
        PRIVATE
            /W0
            /utf-8
    )

    set_property(
        TARGET SoundBoardFasaFisoMiniaudio
        PROPERTY MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )

    set_property(
        TARGET SoundBoardFasaFiso
        PROPERTY MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT SOUNDBOARD_IPO_SUPPORTED OUTPUT SOUNDBOARD_IPO_ERROR)

if(SOUNDBOARD_IPO_SUPPORTED)
    set_property(
        TARGET SoundBoardFasaFiso
        PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    )
endif()

target_link_libraries(
    SoundBoardFasaFiso
    PRIVATE
        user32
        shell32
        comctl32
        comdlg32
        advapi32
        dwmapi
        uxtheme
        winhttp
)

add_custom_target(
    SyncRuntimeFiles ALL

    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_CURRENT_BINARY_DIR}/sounds"

    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/sounds"
        "${CMAKE_CURRENT_BINARY_DIR}/sounds"

    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/config.txt"
        "-DDEST_CONFIG=${CMAKE_CURRENT_BINARY_DIR}/config.txt"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SyncConfigIfMissing.cmake"

    COMMENT "Syncing example sounds and creating config.txt when missing..."
    VERBATIM
)

add_dependencies(SoundBoardFasaFiso SyncRuntimeFiles)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(PORTABLE_RELEASE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/portable")
    set(PORTABLE_RELEASE_DIR "${PORTABLE_RELEASE_ROOT}/SoundBoardFasaFiso")
    set(
        PORTABLE_RELEASE_BASENAME
        "SoundBoardFasaFiso-v${SOUNDBOARD_VERSION}-windows-x64-portable"
    )
    set(
        PORTABLE_RELEASE_ZIP
        "${CMAKE_CURRENT_BINARY_DIR}/${PORTABLE_RELEASE_BASENAME}.zip"
    )

    add_custom_target(
        PortableRelease ALL

        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${PORTABLE_RELEASE_ROOT}"

        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "${PORTABLE_RELEASE_DIR}"

        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${PORTABLE_RELEASE_DIR}/sounds"

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SoundBoardFasaFiso>"
            "${PORTABLE_RELEASE_DIR}/SoundBoardFasaFiso.exe"

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/config.txt"
            "${PORTABLE_RELEASE_DIR}/config.txt"

        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/sounds"
            "${PORTABLE_RELEASE_DIR}/sounds"

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/README.txt"
            "${PORTABLE_RELEASE_DIR}/README.txt"

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
            "${PORTABLE_RELEASE_DIR}/LICENSE"

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt"
            "${PORTABLE_RELEASE_DIR}/THIRD_PARTY_NOTICES.txt"

        COMMAND ${CMAKE_COMMAND} -E rm -f
            "${PORTABLE_RELEASE_ZIP}"

        COMMAND ${CMAKE_COMMAND} -E chdir
            "${PORTABLE_RELEASE_ROOT}"
            ${CMAKE_COMMAND} -E tar cf
            "${PORTABLE_RELEASE_ZIP}"
            --format=zip
            --
            "SoundBoardFasaFiso"

        DEPENDS SoundBoardFasaFiso
        COMMENT "Creating portable release folder and ZIP archive..."
        VERBATIM
    )
endif()
