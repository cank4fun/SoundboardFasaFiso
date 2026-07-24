#include "platform/MediaToolCommandBuilder.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace
{
    MediaToolCommandBuildResult Failure(std::string message)
    {
        MediaToolCommandBuildResult result;
        result.errorMessage = std::move(message);
        return result;
    }

    MediaToolCommandBuildResult Success(
        const MediaToolKind tool,
        std::vector<std::wstring> arguments
    )
    {
        MediaToolCommandBuildResult result;
        result.command = MediaToolCommand{
            tool,
            std::move(arguments)
        };
        return result;
    }

    bool ContainsControlCharacter(const std::wstring& value)
    {
        return std::any_of(
            value.begin(),
            value.end(),
            [](const wchar_t character)
            {
                return character < 0x20 || character == 0x7f;
            }
        );
    }

    wchar_t LowerAscii(const wchar_t character)
    {
        if (character >= L'A' && character <= L'Z')
        {
            return character - L'A' + L'a';
        }

        return character;
    }

    bool StartsWithAsciiCaseInsensitive(
        const std::wstring& value,
        const std::wstring& prefix
    )
    {
        if (value.size() < prefix.size())
        {
            return false;
        }

        for (std::size_t index = 0; index < prefix.size(); ++index)
        {
            if (LowerAscii(value[index]) != LowerAscii(prefix[index]))
            {
                return false;
            }
        }

        return true;
    }

    bool IsSupportedUrl(const std::wstring& url)
    {
        if (url.empty() || ContainsControlCharacter(url))
        {
            return false;
        }

        return StartsWithAsciiCaseInsensitive(url, L"https://") ||
            StartsWithAsciiCaseInsensitive(url, L"http://");
    }

    bool IsUsablePath(const std::filesystem::path& path)
    {
        if (path.empty())
        {
            return false;
        }

        const std::wstring value = path.wstring();
        return !value.empty() && !ContainsControlCharacter(value);
    }

    std::wstring BuildDenoRuntimeArgument(
        const std::filesystem::path& denoExecutablePath
    )
    {
        return L"deno:" + denoExecutablePath.wstring();
    }
}

bool MediaToolCommandBuildResult::Succeeded() const noexcept
{
    return command.has_value();
}

MediaToolCommandBuildResult MediaToolCommandBuilder::ProbeAudio(
    const std::filesystem::path& inputPath
)
{
    if (!IsUsablePath(inputPath))
    {
        return Failure("The ffprobe input path is invalid.");
    }

    return Success(
        MediaToolKind::Ffprobe,
        {
            L"-hide_banner",
            L"-v",
            L"error",
            L"-select_streams",
            L"a:0",
            L"-show_entries",
            L"stream=codec_name,sample_rate,channels,channel_layout:"
                L"format=duration",
            L"-of",
            L"json",
            L"-i",
            inputPath.wstring()
        }
    );
}

MediaToolCommandBuildResult
MediaToolCommandBuilder::ConvertToPcm16Wav(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const std::uint32_t sampleRate,
    const std::uint16_t channelCount
)
{
    if (!IsUsablePath(inputPath))
    {
        return Failure("The FFmpeg input path is invalid.");
    }

    if (!IsUsablePath(outputPath))
    {
        return Failure("The FFmpeg output path is invalid.");
    }

    if (inputPath.lexically_normal() == outputPath.lexically_normal())
    {
        return Failure(
            "The FFmpeg input and output paths must be different."
        );
    }

    if (sampleRate < 8000U || sampleRate > 192000U)
    {
        return Failure("The requested sample rate is invalid.");
    }

    if (channelCount != 1U && channelCount != 2U)
    {
        return Failure("The requested channel count is invalid.");
    }

    return Success(
        MediaToolKind::Ffmpeg,
        {
            L"-hide_banner",
            L"-loglevel",
            L"error",
            L"-nostdin",
            L"-y",
            L"-i",
            inputPath.wstring(),
            L"-map",
            L"0:a:0",
            L"-vn",
            L"-sn",
            L"-dn",
            L"-map_metadata",
            L"-1",
            L"-acodec",
            L"pcm_s16le",
            L"-ar",
            std::to_wstring(sampleRate),
            L"-ac",
            std::to_wstring(channelCount),
            L"-f",
            L"wav",
            outputPath.wstring()
        }
    );
}

MediaToolCommandBuildResult MediaToolCommandBuilder::FetchMetadata(
    const std::wstring& url,
    const std::filesystem::path& denoExecutablePath
)
{
    if (!IsSupportedUrl(url))
    {
        return Failure("The media URL is invalid.");
    }

    if (!IsUsablePath(denoExecutablePath))
    {
        return Failure("The Deno executable path is invalid.");
    }

    return Success(
        MediaToolKind::YtDlp,
        {
            L"--no-playlist",
            L"--skip-download",
            L"--dump-single-json",
            L"--no-warnings",
            L"--no-progress",
            L"--js-runtimes",
            BuildDenoRuntimeArgument(denoExecutablePath),
            L"--",
            url
        }
    );
}

MediaToolCommandBuildResult
MediaToolCommandBuilder::DownloadBestAudio(
    const std::wstring& url,
    const std::filesystem::path& outputTemplate,
    const std::filesystem::path& denoExecutablePath
)
{
    if (!IsSupportedUrl(url))
    {
        return Failure("The media URL is invalid.");
    }

    if (!IsUsablePath(outputTemplate))
    {
        return Failure("The download output template is invalid.");
    }

    if (outputTemplate.wstring().find(L"%(ext)s") ==
        std::wstring::npos)
    {
        return Failure(
            "The download output template must contain %(ext)s."
        );
    }

    if (!IsUsablePath(denoExecutablePath))
    {
        return Failure("The Deno executable path is invalid.");
    }

    return Success(
        MediaToolKind::YtDlp,
        {
            L"--no-playlist",
            L"--no-progress",
            L"--newline",
            L"--no-part",
            L"--format",
            L"bestaudio/best",
            L"--output",
            outputTemplate.wstring(),
            L"--print",
            L"after_move:filepath",
            L"--js-runtimes",
            BuildDenoRuntimeArgument(denoExecutablePath),
            L"--",
            url
        }
    );
}