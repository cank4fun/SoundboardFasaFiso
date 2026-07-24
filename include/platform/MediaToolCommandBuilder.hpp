#pragma once

#include "platform/MediaToolExecutor.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct MediaToolCommand
{
    MediaToolKind tool = MediaToolKind::Ffprobe;
    std::vector<std::wstring> arguments;
};

struct MediaToolCommandBuildResult
{
    std::optional<MediaToolCommand> command;
    std::string errorMessage;

    bool Succeeded() const noexcept;
};

class MediaToolCommandBuilder
{
public:
    static MediaToolCommandBuildResult ProbeAudio(
        const std::filesystem::path& inputPath
    );

    static MediaToolCommandBuildResult ConvertToPcm16Wav(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        std::uint32_t sampleRate,
        std::uint16_t channelCount
    );

    static MediaToolCommandBuildResult FetchMetadata(
        const std::wstring& url,
        const std::filesystem::path& denoExecutablePath
    );

    static MediaToolCommandBuildResult DownloadBestAudio(
        const std::wstring& url,
        const std::filesystem::path& outputTemplate,
        const std::filesystem::path& denoExecutablePath
    );
};