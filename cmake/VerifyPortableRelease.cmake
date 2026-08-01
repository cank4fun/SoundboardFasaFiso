cmake_minimum_required(VERSION 3.25)

foreach(required_argument IN ITEMS
    ARCHIVE_PATH
    VERIFY_ROOT
    CHECKSUM_PATH
    EXPECTED_VERSION
)
    if(NOT DEFINED ${required_argument} OR "${${required_argument}}" STREQUAL "")
        message(FATAL_ERROR "Missing required argument: ${required_argument}")
    endif()
endforeach()

if(NOT DEFINED EXPECT_WEBRTC_NOTICES)
    set(EXPECT_WEBRTC_NOTICES OFF)
endif()
if(NOT DEFINED EXPECT_MEDIA_TOOLS)
    set(EXPECT_MEDIA_TOOLS OFF)
endif()

get_filename_component(ARCHIVE_PATH "${ARCHIVE_PATH}" ABSOLUTE)
get_filename_component(VERIFY_ROOT "${VERIFY_ROOT}" ABSOLUTE)
get_filename_component(CHECKSUM_PATH "${CHECKSUM_PATH}" ABSOLUTE)

get_filename_component(archive_name "${ARCHIVE_PATH}" NAME)
set(
    expected_archive_name
    "SoundBoardFasaFiso-v${EXPECTED_VERSION}-windows-x64-portable.zip"
)
if(NOT archive_name STREQUAL expected_archive_name)
    message(FATAL_ERROR
        "Portable archive name must be ${expected_archive_name}, got ${archive_name}."
    )
endif()

get_filename_component(checksum_name "${CHECKSUM_PATH}" NAME)
set(expected_checksum_name "${expected_archive_name}.sha256")
if(NOT checksum_name STREQUAL expected_checksum_name)
    message(FATAL_ERROR
        "Portable checksum name must be ${expected_checksum_name}, got ${checksum_name}."
    )
endif()

function(require_directory relative_path)
    set(path "${package_root}/${relative_path}")
    if(NOT IS_DIRECTORY "${path}")
        message(FATAL_ERROR "Portable package is missing directory: ${relative_path}")
    endif()
endfunction()

function(require_file relative_path minimum_size)
    set(path "${package_root}/${relative_path}")
    if(NOT EXISTS "${path}" OR IS_DIRECTORY "${path}")
        message(FATAL_ERROR "Portable package is missing file: ${relative_path}")
    endif()
    if(IS_SYMLINK "${path}")
        message(FATAL_ERROR "Portable package contains a symbolic link: ${relative_path}")
    endif()

    file(SIZE "${path}" size)
    if(size LESS minimum_size)
        message(FATAL_ERROR
            "Portable package file is unexpectedly small: ${relative_path} (${size} bytes)"
        )
    endif()
endfunction()

function(read_file_hex relative_path offset length output_variable)
    file(READ
        "${package_root}/${relative_path}"
        value
        OFFSET ${offset}
        LIMIT ${length}
        HEX
    )
    string(TOLOWER "${value}" value)
    set(${output_variable} "${value}" PARENT_SCOPE)
endfunction()

function(little_endian_u32 hex_value output_variable)
    string(LENGTH "${hex_value}" hex_length)
    if(NOT hex_length EQUAL 8 OR NOT hex_value MATCHES "^[0-9a-fA-F]+$")
        message(FATAL_ERROR "Invalid 32-bit little-endian value: ${hex_value}")
    endif()

    string(SUBSTRING "${hex_value}" 0 2 byte_0)
    string(SUBSTRING "${hex_value}" 2 2 byte_1)
    string(SUBSTRING "${hex_value}" 4 2 byte_2)
    string(SUBSTRING "${hex_value}" 6 2 byte_3)
    math(EXPR value "0x${byte_3}${byte_2}${byte_1}${byte_0}")
    set(${output_variable} "${value}" PARENT_SCOPE)
endfunction()

function(verify_windows_x64_gui_executable relative_path)
    set(path "${package_root}/${relative_path}")
    file(SIZE "${path}" executable_size)
    if(executable_size LESS 160)
        message(FATAL_ERROR "Portable executable is too small to contain a valid PE header.")
    endif()

    read_file_hex("${relative_path}" 0 2 dos_signature)
    if(NOT dos_signature STREQUAL "4d5a")
        message(FATAL_ERROR "Portable executable is not a Windows MZ executable.")
    endif()

    read_file_hex("${relative_path}" 60 4 pe_offset_hex)
    little_endian_u32("${pe_offset_hex}" pe_offset)
    math(EXPR maximum_pe_offset "${executable_size} - 96")
    if(pe_offset LESS 64 OR pe_offset GREATER maximum_pe_offset)
        message(FATAL_ERROR
            "Portable executable contains an invalid PE header offset: ${pe_offset}."
        )
    endif()

    read_file_hex("${relative_path}" ${pe_offset} 6 pe_header)
    if(NOT pe_header STREQUAL "504500006486")
        message(FATAL_ERROR
            "Portable executable is not a Windows x64 PE image. Header: ${pe_header}"
        )
    endif()

    math(EXPR optional_header_offset "${pe_offset} + 24")
    read_file_hex("${relative_path}" ${optional_header_offset} 2 optional_header_magic)
    if(NOT optional_header_magic STREQUAL "0b02")
        message(FATAL_ERROR
            "Portable executable does not use the PE32+ optional header."
        )
    endif()

    math(EXPR subsystem_offset "${optional_header_offset} + 68")
    read_file_hex("${relative_path}" ${subsystem_offset} 2 subsystem)
    if(NOT subsystem STREQUAL "0200")
        message(FATAL_ERROR
            "Portable executable is not linked for the Windows GUI subsystem."
        )
    endif()
endfunction()

function(require_exact_children directory expected_children_variable label)
    file(GLOB actual_children
        LIST_DIRECTORIES true
        RELATIVE "${directory}"
        "${directory}/*"
    )
    list(SORT actual_children)

    set(expected_children "${${expected_children_variable}}")
    list(SORT expected_children)

    if(NOT "${actual_children}" STREQUAL "${expected_children}")
        string(REPLACE ";" ", " actual_text "${actual_children}")
        string(REPLACE ";" ", " expected_text "${expected_children}")
        message(FATAL_ERROR
            "${label} contents differ from the release allowlist. "
            "Expected: [${expected_text}] Actual: [${actual_text}]"
        )
    endif()
endfunction()

function(require_config_line line)
    set(padded_config "\n${config_text}\n")
    string(FIND "${padded_config}" "\n${line}\n" line_index)
    if(line_index EQUAL -1)
        message(FATAL_ERROR "Portable config is missing required safe default: ${line}")
    endif()
endfunction()

function(read_manifest_value key output_variable)
    set(found_count 0)
    set(found_value "")

    foreach(line IN LISTS manifest_lines)
        string(FIND "${line}" "${key}=" prefix_index)
        if(prefix_index EQUAL 0)
            math(EXPR found_count "${found_count} + 1")
            string(LENGTH "${key}=" value_start)
            string(SUBSTRING "${line}" ${value_start} -1 found_value)
        endif()
    endforeach()

    if(NOT found_count EQUAL 1 OR "${found_value}" STREQUAL "")
        message(FATAL_ERROR
            "Media-tool manifest must contain exactly one non-empty ${key} entry."
        )
    endif()

    set(${output_variable} "${found_value}" PARENT_SCOPE)
endfunction()

function(verify_manifest_tool key expected_file)
    read_manifest_value("${key}.file" manifest_file)
    if(NOT manifest_file STREQUAL expected_file)
        message(FATAL_ERROR
            "Media-tool manifest ${key}.file must be ${expected_file}, got ${manifest_file}."
        )
    endif()

    read_manifest_value("${key}.sha256" expected_hash)
    string(LENGTH "${expected_hash}" hash_length)
    if(NOT hash_length EQUAL 64 OR NOT expected_hash MATCHES "^[0-9A-Fa-f]+$")
        message(FATAL_ERROR "Media-tool manifest contains an invalid ${key}.sha256 value.")
    endif()

    set(tool_path "${package_root}/tools/${expected_file}")
    file(SHA256 "${tool_path}" actual_hash)
    string(TOLOWER "${expected_hash}" expected_hash)
    string(TOLOWER "${actual_hash}" actual_hash)
    if(NOT actual_hash STREQUAL expected_hash)
        message(FATAL_ERROR
            "Bundled ${expected_file} does not match media-tools.manifest."
        )
    endif()
endfunction()

if(NOT EXISTS "${ARCHIVE_PATH}" OR IS_DIRECTORY "${ARCHIVE_PATH}")
    message(FATAL_ERROR "Portable ZIP does not exist: ${ARCHIVE_PATH}")
endif()
file(SIZE "${ARCHIVE_PATH}" archive_size)
if(archive_size LESS 1024)
    message(FATAL_ERROR "Portable ZIP is unexpectedly small: ${archive_size} bytes")
endif()

file(REMOVE "${CHECKSUM_PATH}")
file(REMOVE_RECURSE "${VERIFY_ROOT}")
file(MAKE_DIRECTORY "${VERIFY_ROOT}")
file(ARCHIVE_EXTRACT INPUT "${ARCHIVE_PATH}" DESTINATION "${VERIFY_ROOT}")

set(archive_root_children "SoundBoardFasaFiso")
require_exact_children("${VERIFY_ROOT}" archive_root_children "Portable ZIP root")

set(package_root "${VERIFY_ROOT}/SoundBoardFasaFiso")
require_directory("sounds")
require_directory("tools")
require_directory("voice-presets")

set(package_children
    LICENSE
    README.txt
    SoundBoardFasaFiso.exe
    THIRD_PARTY_NOTICES.txt
    config.txt
    portable.flag
    sounds
    tools
    voice-presets
)
if(EXPECT_WEBRTC_NOTICES)
    list(APPEND package_children WEBRTC_THIRD_PARTY_NOTICES.txt)
endif()
require_exact_children("${package_root}" package_children "Portable package root")

set(voice_preset_children)
require_exact_children(
    "${package_root}/voice-presets"
    voice_preset_children
    "Portable voice-presets directory"
)

require_file("SoundBoardFasaFiso.exe" 1024)
verify_windows_x64_gui_executable("SoundBoardFasaFiso.exe")
require_file("portable.flag" 1)
require_file("config.txt" 1)
require_file("README.txt" 1)
require_file("LICENSE" 1)
require_file("THIRD_PARTY_NOTICES.txt" 1)

if(EXPECT_WEBRTC_NOTICES)
    require_file("WEBRTC_THIRD_PARTY_NOTICES.txt" 1024)
endif()

file(READ "${package_root}/config.txt" config_text)
string(REPLACE "\r\n" "\n" config_text "${config_text}")
string(REPLACE "\r" "\n" config_text "${config_text}")
require_config_line("output=default")
require_config_line("monitor=none")
require_config_line("microphone_processing_enabled=false")
require_config_line("microphone_echo_cancellation_enabled=false")
require_config_line("voice_effects_enabled=false")
require_config_line("voice_effects_parametric_eq_enabled=false")
require_config_line("voice_effects_eq_low_gain_db=0.0")
require_config_line("voice_effects_eq_low_frequency_hz=135.0")
require_config_line("voice_effects_eq_mid_gain_db=0.0")
require_config_line("voice_effects_eq_mid_frequency_hz=1450.0")
require_config_line("voice_effects_eq_mid_q=0.82")
require_config_line("voice_effects_eq_high_gain_db=0.0")
require_config_line("voice_effects_eq_high_frequency_hz=6800.0")
require_config_line("voice_effects_de_esser_enabled=false")
require_config_line("voice_effects_de_esser_amount=0.0")
require_config_line("voice_effects_gate_enabled=false")
require_config_line("voice_effects_gate_amount=0.0")
require_config_line("voice_effects_compressor_enabled=false")
require_config_line("voice_effects_compressor_amount=0.0")
require_config_line(
    "voice_effects_rack_order=parametric-eq,de-esser,gate,compressor"
)
require_config_line("show_console_on_start=false")

file(READ "${package_root}/README.txt" portable_readme)
string(FIND "${portable_readme}" "${EXPECTED_VERSION}" version_index)
if(version_index EQUAL -1)
    message(FATAL_ERROR
        "Portable README does not contain expected version ${EXPECTED_VERSION}."
    )
endif()
foreach(required_readme_text IN ITEMS "voice-presets" ".sbffvoice")
    string(FIND "${portable_readme}" "${required_readme_text}" text_index)
    if(text_index EQUAL -1)
        message(FATAL_ERROR
            "Portable README is missing v2.3 preset documentation: ${required_readme_text}."
        )
    endif()
endforeach()

file(GLOB_RECURSE sound_files
    LIST_DIRECTORIES false
    RELATIVE "${package_root}/sounds"
    "${package_root}/sounds/*"
)
list(LENGTH sound_files sound_count)
if(sound_count EQUAL 0)
    message(FATAL_ERROR "Portable package contains no example sounds.")
endif()

set(allowed_sound_extensions .flac .mp3 .wav)
foreach(sound_file IN LISTS sound_files)
    get_filename_component(sound_extension "${sound_file}" EXT)
    string(TOLOWER "${sound_extension}" sound_extension)
    if(NOT sound_extension IN_LIST allowed_sound_extensions)
        message(FATAL_ERROR "Portable package contains unsupported sound file: ${sound_file}")
    endif()

    set(sound_path "${package_root}/sounds/${sound_file}")
    if(IS_SYMLINK "${sound_path}")
        message(FATAL_ERROR "Portable package contains a symbolic-link sound: ${sound_file}")
    endif()
    file(SIZE "${sound_path}" sound_size)
    if(sound_size EQUAL 0)
        message(FATAL_ERROR "Portable package contains an empty sound file: ${sound_file}")
    endif()
endforeach()

if(EXPECT_MEDIA_TOOLS)
    set(tool_children
        README.txt
        deno.exe
        ffmpeg.exe
        ffprobe.exe
        licenses
        media-tools.manifest
        yt-dlp.exe
    )
else()
    set(tool_children README.txt)
endif()
require_exact_children("${package_root}/tools" tool_children "Portable tools directory")
require_file("tools/README.txt" 1)

if(EXPECT_MEDIA_TOOLS)
    require_directory("tools/licenses")
    require_file("tools/yt-dlp.exe" 1024)
    require_file("tools/deno.exe" 1024)
    require_file("tools/ffmpeg.exe" 1024)
    require_file("tools/ffprobe.exe" 1024)
    require_file("tools/media-tools.manifest" 1)

    set(license_children
        BTBN-FFMPEG-BUILDS-LICENSE.txt
        DENO-LICENSE.md
        FFMPEG-LGPL-2.1.txt
        SOURCES.txt
        YT-DLP-LICENSE.txt
        YT-DLP-THIRD-PARTY-LICENSES.txt
    )
    require_exact_children(
        "${package_root}/tools/licenses"
        license_children
        "Portable media-tool license directory"
    )
    foreach(license_file IN LISTS license_children)
        require_file("tools/licenses/${license_file}" 1)
    endforeach()

    file(READ "${package_root}/tools/media-tools.manifest" manifest_text)
    string(REPLACE "\r\n" "\n" manifest_text "${manifest_text}")
    string(REPLACE "\r" "\n" manifest_text "${manifest_text}")
    string(REPLACE "\n" ";" manifest_lines "${manifest_text}")

    read_manifest_value("manifest_version" manifest_version)
    if(NOT manifest_version STREQUAL "1")
        message(FATAL_ERROR
            "Unsupported media-tool manifest version: ${manifest_version}"
        )
    endif()
    read_manifest_value("bundle_version" bundle_version)

    verify_manifest_tool("yt-dlp" "yt-dlp.exe")
    verify_manifest_tool("deno" "deno.exe")
    verify_manifest_tool("ffmpeg" "ffmpeg.exe")
    verify_manifest_tool("ffprobe" "ffprobe.exe")
endif()

file(GLOB_RECURSE packaged_entries
    LIST_DIRECTORIES true
    RELATIVE "${package_root}"
    "${package_root}/*"
)
set(forbidden_extensions .exp .iobj .ilk .ipdb .lib .log .obj .partial .pch .pdb .tmp)
foreach(packaged_entry IN LISTS packaged_entries)
    set(packaged_path "${package_root}/${packaged_entry}")
    if(IS_SYMLINK "${packaged_path}")
        message(FATAL_ERROR
            "Portable package contains a symbolic link: ${packaged_entry}"
        )
    endif()
    if(packaged_entry MATCHES "(^|/)\\.")
        message(FATAL_ERROR
            "Portable package contains a hidden path: ${packaged_entry}"
        )
    endif()
    if(IS_DIRECTORY "${packaged_path}")
        continue()
    endif()

    get_filename_component(packaged_extension "${packaged_entry}" EXT)
    string(TOLOWER "${packaged_extension}" packaged_extension)
    if(packaged_extension IN_LIST forbidden_extensions)
        message(FATAL_ERROR
            "Portable package contains build/runtime residue: ${packaged_entry}"
        )
    endif()
endforeach()

file(SHA256 "${ARCHIVE_PATH}" archive_hash)
string(TOLOWER "${archive_hash}" archive_hash)
get_filename_component(archive_name "${ARCHIVE_PATH}" NAME)
get_filename_component(checksum_parent "${CHECKSUM_PATH}" DIRECTORY)
file(MAKE_DIRECTORY "${checksum_parent}")
file(WRITE "${CHECKSUM_PATH}" "${archive_hash}  ${archive_name}\n")

message(STATUS "Verified portable ZIP: ${ARCHIVE_PATH}")
message(STATUS "Portable ZIP size: ${archive_size} bytes")
message(STATUS "Portable ZIP SHA-256: ${archive_hash}")
message(STATUS "Portable checksum: ${CHECKSUM_PATH}")
