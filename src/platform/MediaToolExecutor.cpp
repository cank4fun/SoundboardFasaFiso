#include "platform/MediaToolExecutor.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
    bool IsSha256(const std::string_view value)
    {
        return value.size() == 64U &&
            std::all_of(
                value.begin(),
                value.end(),
                [](const unsigned char character)
                {
                    return std::isxdigit(character) != 0;
                }
            );
    }

    std::string NormalizeSha256(std::string value)
    {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](const unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            }
        );

        return value;
    }

    bool IsSafeFileName(const std::string& fileName)
    {
        if (fileName.empty() || fileName == "." || fileName == "..")
        {
            return false;
        }

        const std::filesystem::path path{fileName};

        return !path.has_parent_path() &&
            path.filename() == path;
    }

    MediaToolRunResult Failure(
        const MediaToolExecutionError error,
        std::string message
    )
    {
        MediaToolRunResult result;
        result.error = error;
        result.errorMessage = std::move(message);
        return result;
    }
}

bool MediaToolRunResult::WasLaunched() const noexcept
{
    return error == MediaToolExecutionError::None;
}

bool MediaToolRunResult::Succeeded() const noexcept
{
    return WasLaunched() && process.Succeeded();
}

std::string_view MediaToolExecutor::ToolId(
    const MediaToolKind tool
) noexcept
{
    switch (tool)
    {
        case MediaToolKind::YtDlp:
            return "yt-dlp";

        case MediaToolKind::Deno:
            return "deno";

        case MediaToolKind::Ffmpeg:
            return "ffmpeg";

        case MediaToolKind::Ffprobe:
            return "ffprobe";
    }

    return {};
}

MediaToolRunResult MediaToolExecutor::Run(
    const MediaToolBundleStatus& bundle,
    const MediaToolKind toolKind,
    const MediaToolRunOptions& options
)
{
    if (!bundle.IsReady())
    {
        return Failure(
            MediaToolExecutionError::BundleNotReady,
            "The media-tool bundle is not ready."
        );
    }

    const std::string_view toolId = ToolId(toolKind);

    if (toolId.empty())
    {
        return Failure(
            MediaToolExecutionError::ToolNotFound,
            "The requested media-tool identifier is invalid."
        );
    }

    const MediaToolStatus* tool =
        bundle.Find(std::string{toolId});

    if (tool == nullptr ||
        tool->state != MediaToolState::Ready)
    {
        return Failure(
            MediaToolExecutionError::ToolNotFound,
            "The requested media tool is not available."
        );
    }

    if (!IsSafeFileName(tool->fileName) ||
        tool->executablePath.empty() ||
        bundle.rootFolder.empty())
    {
        return Failure(
            MediaToolExecutionError::InvalidToolPath,
            "The verified media-tool path is invalid."
        );
    }

    const std::filesystem::path expectedPath =
        (
            bundle.rootFolder /
            std::filesystem::path{tool->fileName}
        ).lexically_normal();

    const std::filesystem::path executablePath =
        tool->executablePath.lexically_normal();

    if (executablePath != expectedPath)
    {
        return Failure(
            MediaToolExecutionError::InvalidToolPath,
            "The media-tool executable path does not match its manifest."
        );
    }

    std::error_code filesystemError;

    if (!std::filesystem::is_regular_file(
            executablePath,
            filesystemError
        ))
    {
        return Failure(
            MediaToolExecutionError::IntegrityCheckError,
            filesystemError
                ? "The media-tool executable could not be inspected."
                : "The media-tool executable is missing."
        );
    }

    const std::string expectedSha256 =
        NormalizeSha256(tool->expectedSha256);

    if (!IsSha256(expectedSha256))
    {
        return Failure(
            MediaToolExecutionError::IntegrityCheckError,
            "The expected media-tool SHA-256 is invalid."
        );
    }

    std::string hashError;

    const std::optional<std::string> actualSha256 =
        MediaToolManager::ComputeFileSha256(
            executablePath,
            hashError
        );

    if (!actualSha256.has_value())
    {
        return Failure(
            MediaToolExecutionError::IntegrityCheckError,
            hashError.empty()
                ? "The media-tool SHA-256 could not be calculated."
                : std::move(hashError)
        );
    }

    if (*actualSha256 != expectedSha256)
    {
        return Failure(
            MediaToolExecutionError::IntegrityCheckFailed,
            "The media-tool executable changed after bundle verification."
        );
    }

    ProcessRunOptions processOptions;
    processOptions.executablePath = executablePath;
    processOptions.arguments = options.arguments;
    processOptions.workingDirectory =
        options.workingDirectory.empty()
            ? bundle.rootFolder
            : options.workingDirectory;
    processOptions.timeout = options.timeout;
    processOptions.cancellationRequested =
        options.cancellationRequested;
    processOptions.maximumOutputBytes =
        options.maximumOutputBytes;

    MediaToolRunResult result;
    result.process = ProcessRunner::Run(processOptions);
    return result;
}
