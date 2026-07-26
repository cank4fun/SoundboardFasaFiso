#pragma once

#include "platform/MediaToolExecutor.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

enum class UrlImportStage
{
    None,
    ValidateRequest,
    CreateWorkspace,
    FetchMetadata,
    Download,
    Probe,
    Convert,
    Publish
};

enum class UrlImportError
{
    None,
    InvalidRequest,
    ToolUnavailable,
    WorkspaceCreationFailed,
    CommandBuildFailed,
    ToolExecutionFailed,
    ToolOutputInvalid,
    DownloadedFileInvalid,
    ConversionOutputInvalid,
    DestinationError,
    Cancelled,
    TimedOut
};

struct UrlImportRequest
{
    std::wstring url;
    std::filesystem::path workspaceBaseDirectory;
    std::filesystem::path destinationDirectory;
    std::wstring destinationFileName;

    std::uint32_t sampleRate = 48000U;
    std::uint16_t channelCount = 2U;

    std::chrono::milliseconds metadataTimeout{
        std::chrono::seconds{30}
    };

    std::chrono::milliseconds downloadTimeout{
        std::chrono::minutes{15}
    };

    std::chrono::milliseconds probeTimeout{
        std::chrono::seconds{30}
    };

    std::chrono::milliseconds conversionTimeout{
        std::chrono::minutes{5}
    };

    const std::atomic_bool* cancellationRequested = nullptr;
    std::size_t maximumOutputBytes = 4U * 1024U * 1024U;
};

struct UrlImportResult
{
    UrlImportError error = UrlImportError::None;
    UrlImportStage stage = UrlImportStage::None;

    std::filesystem::path destinationPath;
    std::string metadataJson;
    std::string probeJson;
    std::string errorMessage;

    bool Succeeded() const noexcept;
};

using UrlImportToolRunner = std::function<
    MediaToolRunResult(
        MediaToolKind,
        const MediaToolRunOptions&
    )
>;

class UrlImportService
{
public:
    static UrlImportResult Import(
        const UrlImportRequest& request,
        const MediaToolBundleStatus& bundle
    );

    static UrlImportResult ImportWithRunner(
        const UrlImportRequest& request,
        const std::filesystem::path& denoExecutablePath,
        const UrlImportToolRunner& runner
    );
};
