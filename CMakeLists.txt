cmake_minimum_required(VERSION 3.25)

set(SOUNDBOARD_VERSION "2.1.0-rc.1")

project(SoundBoardFasaFiso VERSION 2.1.0 LANGUAGES C CXX)

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

option(
    SOUNDBOARD_ENABLE_WEBRTC_AEC3
    "Build acoustic echo cancellation with the WebRTC Audio Processing Module"
    OFF
)

option(
    SOUNDBOARD_BUILD_WEBRTC_AEC3_TESTS
    "Build WebRTC AEC3 smoke, processor, render-reference and runtime tests"
    ON
)

option(
    SOUNDBOARD_BUILD_WEBRTC_AEC3_SPIKE
    "Deprecated compatibility alias for SOUNDBOARD_ENABLE_WEBRTC_AEC3"
    OFF
)

if(SOUNDBOARD_BUILD_WEBRTC_AEC3_SPIKE)
    message(DEPRECATION
        "SOUNDBOARD_BUILD_WEBRTC_AEC3_SPIKE is deprecated. Use "
        "SOUNDBOARD_ENABLE_WEBRTC_AEC3=ON instead."
    )
    set(SOUNDBOARD_ENABLE_WEBRTC_AEC3 ON CACHE BOOL "" FORCE)
    set(SOUNDBOARD_BUILD_WEBRTC_AEC3_TESTS ON CACHE BOOL "" FORCE)
endif()

set(
    SOUNDBOARD_WEBRTC_TARGET
    ""
    CACHE STRING
    "Optional imported CMake target provided by unofficial-webrtc"
)

include(CTest)

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

set(SOUNDBOARD_RNNOISE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rnnoise")
set(
    SOUNDBOARD_RNNOISE_SOURCES
    "${SOUNDBOARD_RNNOISE_ROOT}/src/denoise.c"
    "${SOUNDBOARD_RNNOISE_ROOT}/src/rnn.c"
    "${SOUNDBOARD_RNNOISE_ROOT}/src/pitch.c"
    "${SOUNDBOARD_RNNOISE_ROOT}/src/kiss_fft.c"
    "${SOUNDBOARD_RNNOISE_ROOT}/src/celt_lpc.c"
    "${SOUNDBOARD_RNNOISE_ROOT}/src/nnet.c"
    "${SOUNDBOARD_RNNOISE_ROOT}/src/nnet_default.c"
    "${SOUNDBOARD_RNNOISE_ROOT}/src/parse_lpcnet_weights.c"
    "${SOUNDBOARD_RNNOISE_ROOT}/src/rnnoise_data_little.c"
    "${SOUNDBOARD_RNNOISE_ROOT}/src/rnnoise_tables.c"
)

set(
    SOUNDBOARD_RNNOISE_REQUIRED_FILES
    ${SOUNDBOARD_RNNOISE_SOURCES}
    "${SOUNDBOARD_RNNOISE_ROOT}/src/cpu_support.h"
    "${SOUNDBOARD_RNNOISE_ROOT}/src/os_support.h"
    "${SOUNDBOARD_RNNOISE_ROOT}/src/vec.h"
    "${SOUNDBOARD_RNNOISE_ROOT}/src/x86/x86_arch_macros.h"
)

foreach(rnnoise_file IN LISTS SOUNDBOARD_RNNOISE_REQUIRED_FILES)
    if(NOT EXISTS "${rnnoise_file}")
        message(FATAL_ERROR
            "RNNoise source is missing: ${rnnoise_file}. "
            "Run tools/Import-RnNoiseV02.ps1 before configuring."
        )
    endif()
endforeach()

add_library(
    SoundBoardFasaFisoRnNoise
    STATIC
    ${SOUNDBOARD_RNNOISE_SOURCES}
)

target_include_directories(
    SoundBoardFasaFisoRnNoise
    PUBLIC
        "${SOUNDBOARD_RNNOISE_ROOT}/include"
    PRIVATE
        "${SOUNDBOARD_RNNOISE_ROOT}/src"
)

target_compile_definitions(
    SoundBoardFasaFisoRnNoise
    PRIVATE
        RNNOISE_BUILD
        _USE_MATH_DEFINES
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
    src/audio/MicrophoneProcessingSettings.cpp
    src/audio/MicrophoneProcessor.cpp
    src/audio/MicrophoneProcessingRuntime.cpp
    src/audio/RnNoiseSuppressor.cpp
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

    target_compile_options(
        SoundBoardFasaFisoRnNoise
        PRIVATE
            /W0
            /bigobj
    )

    set_property(
        TARGET SoundBoardFasaFisoMiniaudio
        PROPERTY MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )

    set_property(
        TARGET SoundBoardFasaFisoRnNoise
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
        SoundBoardFasaFisoRnNoise
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

if(SOUNDBOARD_ENABLE_WEBRTC_AEC3)
    find_package(unofficial-webrtc CONFIG REQUIRED)

    if(SOUNDBOARD_WEBRTC_TARGET STREQUAL "")
        foreach(
            webrtc_candidate
            IN ITEMS
                unofficial::webrtc::webrtc
                unofficial-webrtc::webrtc
                webrtc::webrtc
        )
            if(TARGET "${webrtc_candidate}")
                set(SOUNDBOARD_WEBRTC_TARGET "${webrtc_candidate}")
                break()
            endif()
        endforeach()
    endif()

    if(SOUNDBOARD_WEBRTC_TARGET STREQUAL "")
        get_property(
            SOUNDBOARD_IMPORTED_TARGETS
            DIRECTORY
            PROPERTY IMPORTED_TARGETS
        )

        foreach(webrtc_candidate IN LISTS SOUNDBOARD_IMPORTED_TARGETS)
            if(webrtc_candidate MATCHES "(^|::)webrtc$")
                set(SOUNDBOARD_WEBRTC_TARGET "${webrtc_candidate}")
                break()
            endif()
        endforeach()
    endif()

    if(
        SOUNDBOARD_WEBRTC_TARGET STREQUAL ""
        OR NOT TARGET "${SOUNDBOARD_WEBRTC_TARGET}"
    )
        message(FATAL_ERROR
            "The unofficial-webrtc package was found, but its imported target "
            "could not be selected. Configure again with "
            "-DSOUNDBOARD_WEBRTC_TARGET=<target-name>."
        )
    endif()

    message(STATUS
        "WebRTC AEC3 target: ${SOUNDBOARD_WEBRTC_TARGET}"
    )

    target_sources(
        SoundBoardFasaFiso
        PRIVATE
            src/audio/AecRenderReferenceMixer.cpp
            src/audio/WebRtcAec3Processor.cpp
    )

    target_link_libraries(
        SoundBoardFasaFiso
        PRIVATE
            "${SOUNDBOARD_WEBRTC_TARGET}"
    )

    target_compile_definitions(
        SoundBoardFasaFiso
        PRIVATE
            SOUNDBOARD_ENABLE_WEBRTC_AEC3=1
    )


    if(NOT DEFINED VCPKG_INSTALLED_DIR OR VCPKG_INSTALLED_DIR STREQUAL "")
        message(FATAL_ERROR
            "WebRTC AEC3 builds require the vcpkg toolchain. Official release "
            "builds must use the pinned manifest; see docs/BUILDING_WITH_WEBRTC_AEC3.md."
        )
    endif()

    set(
        SOUNDBOARD_WEBRTC_NOTICE_FILE
        "${CMAKE_CURRENT_BINARY_DIR}/WEBRTC_THIRD_PARTY_NOTICES.txt"
    )

    add_custom_command(
        OUTPUT "${SOUNDBOARD_WEBRTC_NOTICE_FILE}"
        COMMAND ${CMAKE_COMMAND}
            "-DVCPKG_SHARE_DIR=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share"
            "-DOUTPUT_FILE=${SOUNDBOARD_WEBRTC_NOTICE_FILE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateVcpkgThirdPartyNotices.cmake"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateVcpkgThirdPartyNotices.cmake"
            "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg.json"
        COMMENT "Generating WebRTC and transitive dependency notices..."
        VERBATIM
    )

    add_custom_target(
        GenerateWebRtcThirdPartyNotices
        DEPENDS "${SOUNDBOARD_WEBRTC_NOTICE_FILE}"
    )

    add_dependencies(SoundBoardFasaFiso GenerateWebRtcThirdPartyNotices)

    if(BUILD_TESTING AND SOUNDBOARD_BUILD_WEBRTC_AEC3_TESTS)
        add_executable(
            SoundBoardFasaFisoWebRtcAec3Smoke
            spikes/WebRtcAec3BuildSmoke.cpp
        )

        target_link_libraries(
            SoundBoardFasaFisoWebRtcAec3Smoke
            PRIVATE
                "${SOUNDBOARD_WEBRTC_TARGET}"
        )

        target_compile_definitions(
            SoundBoardFasaFisoWebRtcAec3Smoke
            PRIVATE
                WIN32_LEAN_AND_MEAN
                NOMINMAX
        )

        if(MSVC)
            target_compile_options(
                SoundBoardFasaFisoWebRtcAec3Smoke
                PRIVATE
                    /W4
                    /permissive-
                    /utf-8
                    /sdl
            )

            set_property(
                TARGET SoundBoardFasaFisoWebRtcAec3Smoke
                PROPERTY MSVC_RUNTIME_LIBRARY
                    "MultiThreaded$<$<CONFIG:Debug>:Debug>"
            )
        endif()

        add_executable(
            SoundBoardFasaFisoWebRtcAec3ProcessorTests
            tests/WebRtcAec3ProcessorTests.cpp
            src/audio/WebRtcAec3Processor.cpp
        )

        target_include_directories(
            SoundBoardFasaFisoWebRtcAec3ProcessorTests
            PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/include"
        )

        target_link_libraries(
            SoundBoardFasaFisoWebRtcAec3ProcessorTests
            PRIVATE
                "${SOUNDBOARD_WEBRTC_TARGET}"
        )

        target_compile_definitions(
            SoundBoardFasaFisoWebRtcAec3ProcessorTests
            PRIVATE
                WIN32_LEAN_AND_MEAN
                NOMINMAX
        )

        if(MSVC)
            target_compile_options(
                SoundBoardFasaFisoWebRtcAec3ProcessorTests
                PRIVATE
                    /W4
                    /permissive-
                    /utf-8
                    /sdl
            )

            if(SOUNDBOARD_WARNINGS_AS_ERRORS)
                target_compile_options(
                    SoundBoardFasaFisoWebRtcAec3ProcessorTests
                    PRIVATE
                        /WX
                )
            endif()

            set_property(
                TARGET SoundBoardFasaFisoWebRtcAec3ProcessorTests
                PROPERTY MSVC_RUNTIME_LIBRARY
                    "MultiThreaded$<$<CONFIG:Debug>:Debug>"
            )
        endif()

        add_executable(
            SoundBoardFasaFisoAecRenderReferenceMixerTests
            tests/AecRenderReferenceMixerTests.cpp
            src/audio/AecRenderReferenceMixer.cpp
            $<TARGET_OBJECTS:SoundBoardFasaFisoMiniaudio>
        )

        target_include_directories(
            SoundBoardFasaFisoAecRenderReferenceMixerTests
            PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/include"
        )

        target_include_directories(
            SoundBoardFasaFisoAecRenderReferenceMixerTests
            SYSTEM PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
        )

        target_compile_definitions(
            SoundBoardFasaFisoAecRenderReferenceMixerTests
            PRIVATE
                WIN32_LEAN_AND_MEAN
                NOMINMAX
        )

        if(MSVC)
            target_compile_options(
                SoundBoardFasaFisoAecRenderReferenceMixerTests
                PRIVATE
                    /W4
                    /permissive-
                    /utf-8
                    /sdl
            )

            if(SOUNDBOARD_WARNINGS_AS_ERRORS)
                target_compile_options(
                    SoundBoardFasaFisoAecRenderReferenceMixerTests
                    PRIVATE
                        /WX
                )
            endif()

            set_property(
                TARGET SoundBoardFasaFisoAecRenderReferenceMixerTests
                PROPERTY MSVC_RUNTIME_LIBRARY
                    "MultiThreaded$<$<CONFIG:Debug>:Debug>"
            )
        endif()

        add_executable(
            SoundBoardFasaFisoMicrophoneProcessingAecRuntimeTests
            tests/MicrophoneProcessingAecRuntimeTests.cpp
            src/audio/MicrophoneProcessingRuntime.cpp
            src/audio/MicrophoneProcessor.cpp
            src/audio/MicrophoneProcessingSettings.cpp
            src/audio/RnNoiseSuppressor.cpp
            src/audio/WebRtcAec3Processor.cpp
            $<TARGET_OBJECTS:SoundBoardFasaFisoMiniaudio>
        )

        target_include_directories(
            SoundBoardFasaFisoMicrophoneProcessingAecRuntimeTests
            PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/include"
        )

        target_include_directories(
            SoundBoardFasaFisoMicrophoneProcessingAecRuntimeTests
            SYSTEM PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
                "${SOUNDBOARD_RNNOISE_ROOT}/include"
        )

        target_link_libraries(
            SoundBoardFasaFisoMicrophoneProcessingAecRuntimeTests
            PRIVATE
                SoundBoardFasaFisoRnNoise
                "${SOUNDBOARD_WEBRTC_TARGET}"
        )

        target_compile_definitions(
            SoundBoardFasaFisoMicrophoneProcessingAecRuntimeTests
            PRIVATE
                SOUNDBOARD_ENABLE_WEBRTC_AEC3=1
                WIN32_LEAN_AND_MEAN
                NOMINMAX
        )

        if(MSVC)
            target_compile_options(
                SoundBoardFasaFisoMicrophoneProcessingAecRuntimeTests
                PRIVATE
                    /W4
                    /permissive-
                    /utf-8
                    /sdl
            )

            if(SOUNDBOARD_WARNINGS_AS_ERRORS)
                target_compile_options(
                    SoundBoardFasaFisoMicrophoneProcessingAecRuntimeTests
                    PRIVATE
                        /WX
                )
            endif()

            set_property(
                TARGET SoundBoardFasaFisoMicrophoneProcessingAecRuntimeTests
                PROPERTY MSVC_RUNTIME_LIBRARY
                    "MultiThreaded$<$<CONFIG:Debug>:Debug>"
            )
        endif()

        add_test(
            NAME WebRtcAec3BuildSmoke
            COMMAND SoundBoardFasaFisoWebRtcAec3Smoke
        )

        add_test(
            NAME WebRtcAec3Processor
            COMMAND SoundBoardFasaFisoWebRtcAec3ProcessorTests
        )

        add_test(
            NAME AecRenderReferenceMixer
            COMMAND SoundBoardFasaFisoAecRenderReferenceMixerTests
        )

        add_test(
            NAME MicrophoneProcessingAecRuntime
            COMMAND SoundBoardFasaFisoMicrophoneProcessingAecRuntimeTests
        )
    endif()
endif()

if(BUILD_TESTING)
    add_executable(
        SoundBoardFasaFisoMicrophoneProcessingSettingsTests
        EXCLUDE_FROM_ALL
        tests/MicrophoneProcessingSettingsTests.cpp
        src/audio/MicrophoneProcessingSettings.cpp
    )

    target_include_directories(
        SoundBoardFasaFisoMicrophoneProcessingSettingsTests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )

    if(MSVC)
        target_compile_options(
            SoundBoardFasaFisoMicrophoneProcessingSettingsTests
            PRIVATE
                /W4
                /permissive-
                /utf-8
                /sdl
        )

        if(SOUNDBOARD_WARNINGS_AS_ERRORS)
            target_compile_options(
                SoundBoardFasaFisoMicrophoneProcessingSettingsTests
                PRIVATE
                    /WX
            )
        endif()

        set_property(
            TARGET SoundBoardFasaFisoMicrophoneProcessingSettingsTests
            PROPERTY MSVC_RUNTIME_LIBRARY
                "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    endif()

    add_test(
        NAME MicrophoneProcessingSettings
        COMMAND SoundBoardFasaFisoMicrophoneProcessingSettingsTests
    )

    add_executable(
        SoundBoardFasaFisoConfigMicrophoneProcessingTests
        EXCLUDE_FROM_ALL
        tests/ConfigMicrophoneProcessingTests.cpp
        src/config/Config.cpp
        src/audio/MicrophoneProcessingSettings.cpp
        src/localization/Localization.cpp
    )

    target_include_directories(
        SoundBoardFasaFisoConfigMicrophoneProcessingTests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )

    target_compile_definitions(
        SoundBoardFasaFisoConfigMicrophoneProcessingTests
        PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
    )

    if(MSVC)
        target_compile_options(
            SoundBoardFasaFisoConfigMicrophoneProcessingTests
            PRIVATE
                /W4
                /permissive-
                /utf-8
                /sdl
        )

        if(SOUNDBOARD_WARNINGS_AS_ERRORS)
            target_compile_options(
                SoundBoardFasaFisoConfigMicrophoneProcessingTests
                PRIVATE
                    /WX
            )
        endif()

        set_property(
            TARGET SoundBoardFasaFisoConfigMicrophoneProcessingTests
            PROPERTY MSVC_RUNTIME_LIBRARY
                "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    endif()

    add_test(
        NAME ConfigMicrophoneProcessing
        COMMAND SoundBoardFasaFisoConfigMicrophoneProcessingTests
    )

    add_executable(
        SoundBoardFasaFisoMicrophoneProcessorTests
        EXCLUDE_FROM_ALL
        tests/MicrophoneProcessorTests.cpp
        src/audio/MicrophoneProcessor.cpp
        src/audio/MicrophoneProcessingSettings.cpp
        src/audio/RnNoiseSuppressor.cpp
    )

    target_include_directories(
        SoundBoardFasaFisoMicrophoneProcessorTests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )

    target_link_libraries(
        SoundBoardFasaFisoMicrophoneProcessorTests
        PRIVATE
            SoundBoardFasaFisoRnNoise
    )

    if(MSVC)
        target_compile_options(
            SoundBoardFasaFisoMicrophoneProcessorTests
            PRIVATE
                /W4
                /permissive-
                /utf-8
                /sdl
        )

        if(SOUNDBOARD_WARNINGS_AS_ERRORS)
            target_compile_options(
                SoundBoardFasaFisoMicrophoneProcessorTests
                PRIVATE
                    /WX
            )
        endif()

        set_property(
            TARGET SoundBoardFasaFisoMicrophoneProcessorTests
            PROPERTY MSVC_RUNTIME_LIBRARY
                "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    endif()

    add_test(
        NAME MicrophoneProcessor
        COMMAND SoundBoardFasaFisoMicrophoneProcessorTests
    )

    add_executable(
        SoundBoardFasaFisoMicrophoneProcessingRuntimeTests
        EXCLUDE_FROM_ALL
        tests/MicrophoneProcessingRuntimeTests.cpp
        src/audio/MicrophoneProcessingRuntime.cpp
        src/audio/MicrophoneProcessor.cpp
        src/audio/MicrophoneProcessingSettings.cpp
        src/audio/RnNoiseSuppressor.cpp
        $<TARGET_OBJECTS:SoundBoardFasaFisoMiniaudio>
    )

    target_include_directories(
        SoundBoardFasaFisoMicrophoneProcessingRuntimeTests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )

    target_include_directories(
        SoundBoardFasaFisoMicrophoneProcessingRuntimeTests
        SYSTEM PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
    )

    target_link_libraries(
        SoundBoardFasaFisoMicrophoneProcessingRuntimeTests
        PRIVATE
            SoundBoardFasaFisoRnNoise
    )

    if(MSVC)
        target_compile_options(
            SoundBoardFasaFisoMicrophoneProcessingRuntimeTests
            PRIVATE
                /W4
                /permissive-
                /utf-8
                /sdl
        )

        if(SOUNDBOARD_WARNINGS_AS_ERRORS)
            target_compile_options(
                SoundBoardFasaFisoMicrophoneProcessingRuntimeTests
                PRIVATE
                    /WX
            )
        endif()

        set_property(
            TARGET SoundBoardFasaFisoMicrophoneProcessingRuntimeTests
            PROPERTY MSVC_RUNTIME_LIBRARY
                "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    endif()

    add_test(
        NAME MicrophoneProcessingRuntime
        COMMAND SoundBoardFasaFisoMicrophoneProcessingRuntimeTests
    )

    add_executable(
        SoundBoardFasaFisoRnNoiseSuppressorTests
        EXCLUDE_FROM_ALL
        tests/RnNoiseSuppressorTests.cpp
        src/audio/RnNoiseSuppressor.cpp
    )

    target_include_directories(
        SoundBoardFasaFisoRnNoiseSuppressorTests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )

    target_link_libraries(
        SoundBoardFasaFisoRnNoiseSuppressorTests
        PRIVATE
            SoundBoardFasaFisoRnNoise
    )

    if(MSVC)
        target_compile_options(
            SoundBoardFasaFisoRnNoiseSuppressorTests
            PRIVATE
                /W4
                /permissive-
                /utf-8
                /sdl
        )

        if(SOUNDBOARD_WARNINGS_AS_ERRORS)
            target_compile_options(
                SoundBoardFasaFisoRnNoiseSuppressorTests
                PRIVATE
                    /WX
            )
        endif()

        set_property(
            TARGET SoundBoardFasaFisoRnNoiseSuppressorTests
            PROPERTY MSVC_RUNTIME_LIBRARY
                "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    endif()

    add_test(
        NAME RnNoiseSuppressor
        COMMAND SoundBoardFasaFisoRnNoiseSuppressorTests
    )
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(SOUNDBOARD_PORTABLE_DEPENDENCIES SoundBoardFasaFiso)
    if(SOUNDBOARD_ENABLE_WEBRTC_AEC3)
        list(APPEND SOUNDBOARD_PORTABLE_DEPENDENCIES GenerateWebRtcThirdPartyNotices)
    endif()

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

        COMMAND ${CMAKE_COMMAND}
            "-DENABLED=${SOUNDBOARD_ENABLE_WEBRTC_AEC3}"
            "-DSOURCE_NOTICE=${SOUNDBOARD_WEBRTC_NOTICE_FILE}"
            "-DDEST_NOTICE=${PORTABLE_RELEASE_DIR}/WEBRTC_THIRD_PARTY_NOTICES.txt"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyOptionalFile.cmake"

        COMMAND ${CMAKE_COMMAND} -E rm -f
            "${PORTABLE_RELEASE_ZIP}"

        COMMAND ${CMAKE_COMMAND} -E chdir
            "${PORTABLE_RELEASE_ROOT}"
            ${CMAKE_COMMAND} -E tar cf
            "${PORTABLE_RELEASE_ZIP}"
            --format=zip
            --
            "SoundBoardFasaFiso"

        DEPENDS ${SOUNDBOARD_PORTABLE_DEPENDENCIES}
        COMMENT "Creating portable release folder and ZIP archive..."
        VERBATIM
    )
endif()
