#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

class MediaImportWorkspace final
{
public:
    static std::optional<MediaImportWorkspace> Create(
        const std::filesystem::path& baseDirectory,
        std::string& errorMessage
    );

    ~MediaImportWorkspace();

    MediaImportWorkspace(const MediaImportWorkspace&) = delete;
    MediaImportWorkspace& operator=(
        const MediaImportWorkspace&
    ) = delete;

    MediaImportWorkspace(MediaImportWorkspace&& other) noexcept;

    MediaImportWorkspace& operator=(
        MediaImportWorkspace&& other
    ) noexcept;

    bool IsValid() const noexcept;

    const std::filesystem::path& RootFolder() const noexcept;

    std::filesystem::path SourceDownloadTemplate() const;
    std::filesystem::path MetadataPath() const;
    std::filesystem::path ConvertedWavPath() const;

    std::optional<std::filesystem::path> SafeChildPath(
        std::wstring_view leafName
    ) const;

    static bool IsSafeLeafName(
        std::wstring_view leafName
    );

private:
    explicit MediaImportWorkspace(
        std::filesystem::path rootFolder
    );

    void Cleanup() noexcept;

    std::filesystem::path rootFolder_;
    bool cleanupEnabled_ = false;
};
