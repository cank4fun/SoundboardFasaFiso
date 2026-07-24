#include "platform/MediaToolManager.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    class TemporaryDirectory final
    {
    public:
        TemporaryDirectory()
        {
            const auto nonce = std::chrono::steady_clock::now()
                .time_since_epoch()
                .count();
            path_ = std::filesystem::temp_directory_path() /
                ("SoundBoardFasaFiso-MediaToolManagerTests-" +
                    std::to_string(nonce));
            std::filesystem::create_directories(path_);
        }

        ~TemporaryDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        const std::filesystem::path& Path() const noexcept
        {
            return path_;
        }

    private:
        std::filesystem::path path_;
    };

    bool WriteFile(
        const std::filesystem::path& path,
        const std::string& content
    )
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        return output.good();
    }

    bool CreateCompleteBundle(
        const std::filesystem::path& root,
        const std::string& suffix = {}
    )
    {
        std::filesystem::create_directories(root);
        const std::vector<std::pair<std::string, std::string>> tools{
            {"yt-dlp", "yt-dlp.exe"},
            {"deno", "deno.exe"},
            {"ffmpeg", "ffmpeg.exe"},
            {"ffprobe", "ffprobe.exe"}
        };

        std::string manifest =
            "manifest_version=1\n"
            "bundle_version=test-bundle" + suffix + "\n";

        for (const auto& [id, fileName] : tools)
        {
            const std::filesystem::path path = root / fileName;
            if (!WriteFile(path, id + " test payload " + suffix))
            {
                return false;
            }

            std::string error;
            const auto hash = MediaToolManager::ComputeFileSha256(path, error);
            if (!hash.has_value())
            {
                std::cerr << error << '\n';
                return false;
            }

            manifest += id + ".version=test\n";
            manifest += id + ".file=" + fileName + "\n";
            manifest += id + ".sha256=" + *hash + "\n";
        }

        return WriteFile(root / "media-tools.manifest", manifest);
    }

    bool Expect(const bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
        }
        return condition;
    }
}

int main()
{
    TemporaryDirectory temporary;
    const std::filesystem::path missing = temporary.Path() / "missing";
    const MediaToolBundleStatus missingStatus =
        MediaToolManager::InspectBundle(missing);
    if (!Expect(!missingStatus.IsReady(), "Missing bundle was accepted."))
    {
        return 1;
    }

    const std::filesystem::path valid = temporary.Path() / "valid";
    if (!CreateCompleteBundle(valid))
    {
        return 1;
    }

    const MediaToolBundleStatus validStatus =
        MediaToolManager::InspectBundle(valid);
    if (!Expect(validStatus.IsReady(), "Valid bundle was rejected.") ||
        !Expect(validStatus.tools.size() == 4, "Unexpected tool count.") ||
        !Expect(validStatus.Find("deno") != nullptr, "Deno was not indexed."))
    {
        return 1;
    }

    if (!WriteFile(valid / "ffmpeg.exe", "tampered"))
    {
        return 1;
    }
    const MediaToolBundleStatus tamperedStatus =
        MediaToolManager::InspectBundle(valid);
    const MediaToolStatus* ffmpeg = tamperedStatus.Find("ffmpeg");
    if (!Expect(!tamperedStatus.IsReady(), "Tampered bundle was accepted.") ||
        !Expect(
            ffmpeg != nullptr && ffmpeg->state == MediaToolState::HashMismatch,
            "Tampered FFmpeg was not reported as a hash mismatch."
        ))
    {
        return 1;
    }

    const std::filesystem::path preferred = temporary.Path() / "preferred";
    const std::filesystem::path fallback = temporary.Path() / "fallback";
    if (!CreateCompleteBundle(preferred, "-preferred") ||
        !CreateCompleteBundle(fallback, "-fallback"))
    {
        return 1;
    }

    const auto selected = MediaToolManager::FindUsableBundle(
        {missing, preferred, fallback}
    );
    if (!Expect(selected.has_value(), "No usable bundle was selected.") ||
        !Expect(
            selected->rootFolder == preferred.lexically_normal(),
            "The first usable bundle was not preferred."
        ))
    {
        return 1;
    }

    std::cout << "MediaToolManager tests passed.\n";
    return 0;
}
