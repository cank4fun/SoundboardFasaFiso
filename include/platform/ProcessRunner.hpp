#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

enum class ProcessExitReason
{
    Exited,
    LaunchFailed,
    TimedOut,
    Cancelled,
    IoError
};

struct ProcessRunOptions
{
    std::filesystem::path executablePath;
    std::vector<std::wstring> arguments;
    std::filesystem::path workingDirectory;
    std::chrono::milliseconds timeout{0};
    const std::atomic_bool* cancellationRequested = nullptr;
    std::size_t maximumOutputBytes = 4U * 1024U * 1024U;
};

struct ProcessRunResult
{
    ProcessExitReason reason = ProcessExitReason::LaunchFailed;
    std::uint32_t exitCode = UINT32_MAX;
    std::string standardOutput;
    std::string standardError;
    std::string errorMessage;
    bool standardOutputTruncated = false;
    bool standardErrorTruncated = false;

    bool Succeeded() const noexcept;
};

class ProcessRunner
{
public:
    static ProcessRunResult Run(const ProcessRunOptions& options);

    static std::wstring QuoteWindowsArgument(std::wstring_view argument);
};
