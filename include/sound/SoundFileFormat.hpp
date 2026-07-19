#pragma once

#include "platform/Utf8Path.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace SoundFileFormat
{
    inline std::string NormalizedExtension(
        const std::filesystem::path& filePath
    )
    {
        std::string extension =
            PathToUtf8(filePath.extension());

        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](const unsigned char character)
            {
                return static_cast<char>(
                    std::tolower(character)
                );
            }
        );

        return extension;
    }

    inline bool IsSupported(
        const std::filesystem::path& filePath
    )
    {
        const std::string extension =
            NormalizedExtension(filePath);

        return extension == ".wav" ||
            extension == ".mp3" ||
            extension == ".flac";
    }

    inline constexpr std::string_view SupportedExtensions()
    {
        return ".wav, .mp3, .flac";
    }
}
