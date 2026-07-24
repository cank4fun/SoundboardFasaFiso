#pragma once

#include "platform/MediaToolManager.hpp"
#include "platform/ProcessRunner.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

enum class MediaToolKind
{
    YtDlp,
    Deno,
    Ffmpeg,
    Ffprobe
};

enum class MediaToolExecutionError
{
    None,
    BundleNotReady,
    ToolNotFound,
    InvalidToolPath,
    IntegrityCheckFailed,
    IntegrityCheckError
};

struct MediaToolRunOptions
{
    std::vector<std::wstring> arguments;
    std::filesystem::path workingDirectory;
    std::chrono::milliseconds timeout{0};
    const std::atomic_bool* cancellationRequested = nullptr;
    std::size_t maximumOutputBytes = 4U * 1024U * 1024U;
};

struct MediaToolRunResult
{
    MediaToolExecutionError error = MediaToolExecutionError::None;
    ProcessRunResult process;
    std::string errorMessage;

    bool WasLaunched() const noexcept;
    bool Succeeded() const noexcept;
};

class MediaToolExecutor
{
public:
    static MediaToolRunResult Run(
        const MediaToolBundleStatus& bundle,
        MediaToolKind tool,
        const MediaToolRunOptions& options
    );

    static std::string_view ToolId(MediaToolKind tool) noexcept;
};
