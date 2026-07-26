#pragma once

#include "import/SoundImporter.hpp"
#include "platform/MediaToolExecutor.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct LocalMediaImportRequest
{
    std::vector<std::filesystem::path> inputs;
    std::filesystem::path soundsFolder;
    std::filesystem::path workspaceBaseDirectory;

    std::uint32_t sampleRate = 48000U;
    std::uint16_t channelCount = 2U;

    std::chrono::milliseconds probeTimeout{
        std::chrono::seconds{30}
    };

    std::chrono::milliseconds conversionTimeout{
        std::chrono::minutes{5}
    };

    const std::atomic_bool* cancellationRequested = nullptr;
    std::size_t maximumOutputBytes = 4U * 1024U * 1024U;
};

struct LocalMediaImportResult
{
    SoundImportSummary summary;
    std::size_t convertedCount = 0;
    bool conversionToolsUnavailable = false;
    bool cancelled = false;
    std::string errorMessage;

    bool ImportedAnything() const noexcept;
};

using LocalMediaImportToolRunner = std::function<
    MediaToolRunResult(
        MediaToolKind,
        const MediaToolRunOptions&
    )
>;

class LocalMediaImportService
{
public:
    static LocalMediaImportResult Import(
        const LocalMediaImportRequest& request,
        const std::optional<MediaToolBundleStatus>& bundle
    );

    static LocalMediaImportResult ImportWithRunner(
        const LocalMediaImportRequest& request,
        bool conversionAvailable,
        const LocalMediaImportToolRunner& runner
    );

    static bool IsConvertibleMediaFile(
        const std::filesystem::path& path
    );
};
