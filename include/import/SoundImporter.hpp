#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

struct SoundImportSummary
{
    std::vector<std::filesystem::path> importedRelativePaths;
    std::vector<std::filesystem::path> failedPaths;
    std::size_t copiedCount = 0;
    std::size_t existingCount = 0;
    std::size_t unsupportedCount = 0;
    bool itemLimitReached = false;
};

class SoundImporter
{
public:
    explicit SoundImporter(std::filesystem::path soundsFolder);

    [[nodiscard]] SoundImportSummary Import(
        const std::vector<std::filesystem::path>& inputs
    ) const;

private:
    std::filesystem::path soundsFolder_;
};
