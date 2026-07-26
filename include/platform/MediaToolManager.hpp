#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

enum class MediaToolState
{
    Ready,
    ManifestMissing,
    ManifestInvalid,
    FileMissing,
    HashMismatch,
    IoError
};

struct MediaToolStatus
{
    std::string id;
    std::string version;
    std::string fileName;
    std::string expectedSha256;
    std::string actualSha256;
    std::filesystem::path executablePath;
    MediaToolState state = MediaToolState::ManifestInvalid;
};

struct MediaToolBundleStatus
{
    std::filesystem::path rootFolder;
    std::string bundleVersion;
    std::vector<MediaToolStatus> tools;
    std::string errorMessage;

    bool IsReady() const noexcept;
    const MediaToolStatus* Find(const std::string& id) const noexcept;
};

class MediaToolManager
{
public:
    static MediaToolBundleStatus InspectBundle(
        const std::filesystem::path& rootFolder
    );

    static std::optional<MediaToolBundleStatus> FindUsableBundle(
        const std::vector<std::filesystem::path>& candidateFolders
    );

    static std::optional<std::string> ComputeFileSha256(
        const std::filesystem::path& path,
        std::string& errorMessage
    );
};
