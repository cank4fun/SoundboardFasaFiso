#include "platform/MediaToolManager.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <vector>

namespace
{
    constexpr wchar_t ManifestFileName[] = L"media-tools.manifest";
    constexpr std::array<std::string_view, 4> RequiredToolIds{
        "yt-dlp",
        "deno",
        "ffmpeg",
        "ffprobe"
    };

    std::string Trim(std::string value)
    {
        const auto notSpace = [](const unsigned char character)
        {
            return std::isspace(character) == 0;
        };

        value.erase(
            value.begin(),
            std::find_if(value.begin(), value.end(), notSpace)
        );
        value.erase(
            std::find_if(value.rbegin(), value.rend(), notSpace).base(),
            value.end()
        );
        return value;
    }

    std::string ToLowerAscii(std::string value)
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

    bool IsSha256(const std::string& value)
    {
        return value.size() == 64 && std::all_of(
            value.begin(),
            value.end(),
            [](const unsigned char character)
            {
                return std::isxdigit(character) != 0;
            }
        );
    }

    bool IsSafeFileName(const std::string& fileName)
    {
        if (fileName.empty() || fileName == "." || fileName == "..")
        {
            return false;
        }

        const std::filesystem::path path{fileName};
        return !path.has_parent_path() && path.filename() == path;
    }

    std::map<std::string, std::string> ReadManifest(
        const std::filesystem::path& manifestPath,
        std::string& errorMessage
    )
    {
        std::ifstream input(manifestPath, std::ios::binary);
        if (!input.is_open())
        {
            errorMessage = "The media-tool manifest could not be opened.";
            return {};
        }

        std::map<std::string, std::string> values;
        std::string line;
        std::size_t lineNumber = 0;

        while (std::getline(input, line))
        {
            ++lineNumber;
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }

            line = Trim(std::move(line));
            if (line.empty() || line.front() == '#')
            {
                continue;
            }

            const std::size_t separator = line.find('=');
            if (separator == std::string::npos)
            {
                errorMessage = "Invalid media-tool manifest line " +
                    std::to_string(lineNumber) + ".";
                return {};
            }

            std::string key = Trim(line.substr(0, separator));
            std::string value = Trim(line.substr(separator + 1));
            if (key.empty() || value.empty() || values.contains(key))
            {
                errorMessage = "Invalid or duplicate media-tool manifest key on line " +
                    std::to_string(lineNumber) + ".";
                return {};
            }

            values.emplace(std::move(key), std::move(value));
        }

        if (!input.eof())
        {
            errorMessage = "The media-tool manifest could not be read completely.";
            return {};
        }

        return values;
    }

    class BCryptAlgorithm final
    {
    public:
        BCryptAlgorithm()
        {
            const NTSTATUS status = BCryptOpenAlgorithmProvider(
                &handle_,
                BCRYPT_SHA256_ALGORITHM,
                nullptr,
                0
            );
            if (status < 0)
            {
                handle_ = nullptr;
            }
        }

        ~BCryptAlgorithm()
        {
            if (handle_ != nullptr)
            {
                BCryptCloseAlgorithmProvider(handle_, 0);
            }
        }

        BCRYPT_ALG_HANDLE Get() const noexcept
        {
            return handle_;
        }

    private:
        BCRYPT_ALG_HANDLE handle_ = nullptr;
    };

    class BCryptHashHandle final
    {
    public:
        BCryptHashHandle(
            const BCRYPT_ALG_HANDLE algorithm,
            std::vector<unsigned char>& object
        )
        {
            const NTSTATUS status = BCryptCreateHash(
                algorithm,
                &handle_,
                object.data(),
                static_cast<ULONG>(object.size()),
                nullptr,
                0,
                0
            );
            if (status < 0)
            {
                handle_ = nullptr;
            }
        }

        ~BCryptHashHandle()
        {
            if (handle_ != nullptr)
            {
                BCryptDestroyHash(handle_);
            }
        }

        BCRYPT_HASH_HANDLE Get() const noexcept
        {
            return handle_;
        }

    private:
        BCRYPT_HASH_HANDLE handle_ = nullptr;
    };

    bool ReadProperty(
        const BCRYPT_ALG_HANDLE algorithm,
        const wchar_t* property,
        ULONG& value
    )
    {
        ULONG resultSize = 0;
        return BCryptGetProperty(
            algorithm,
            property,
            reinterpret_cast<PUCHAR>(&value),
            sizeof(value),
            &resultSize,
            0
        ) >= 0 && resultSize == sizeof(value);
    }
}

bool MediaToolBundleStatus::IsReady() const noexcept
{
    return errorMessage.empty() && tools.size() == RequiredToolIds.size() &&
        std::all_of(
            tools.begin(),
            tools.end(),
            [](const MediaToolStatus& tool)
            {
                return tool.state == MediaToolState::Ready;
            }
        );
}

const MediaToolStatus* MediaToolBundleStatus::Find(
    const std::string& id
) const noexcept
{
    const auto iterator = std::find_if(
        tools.begin(),
        tools.end(),
        [&id](const MediaToolStatus& tool)
        {
            return tool.id == id;
        }
    );
    return iterator == tools.end() ? nullptr : &*iterator;
}

MediaToolBundleStatus MediaToolManager::InspectBundle(
    const std::filesystem::path& rootFolder
)
{
    MediaToolBundleStatus bundle;
    bundle.rootFolder = rootFolder.lexically_normal();

    std::error_code filesystemError;
    const std::filesystem::path manifestPath =
        bundle.rootFolder / ManifestFileName;
    if (!std::filesystem::is_regular_file(manifestPath, filesystemError))
    {
        bundle.errorMessage = filesystemError
            ? "The media-tool folder could not be inspected."
            : "The media-tool manifest is missing.";
        return bundle;
    }

    std::string manifestError;
    const std::map<std::string, std::string> values =
        ReadManifest(manifestPath, manifestError);
    if (!manifestError.empty())
    {
        bundle.errorMessage = std::move(manifestError);
        return bundle;
    }

    const auto manifestVersion = values.find("manifest_version");
    const auto bundleVersion = values.find("bundle_version");
    if (manifestVersion == values.end() || manifestVersion->second != "1" ||
        bundleVersion == values.end() || bundleVersion->second.empty())
    {
        bundle.errorMessage = "The media-tool manifest version is unsupported.";
        return bundle;
    }
    bundle.bundleVersion = bundleVersion->second;

    for (const std::string_view idView : RequiredToolIds)
    {
        const std::string id{idView};
        MediaToolStatus tool;
        tool.id = id;

        const auto version = values.find(id + ".version");
        const auto file = values.find(id + ".file");
        const auto hash = values.find(id + ".sha256");
        if (version == values.end() || file == values.end() ||
            hash == values.end() || version->second.empty() ||
            !IsSafeFileName(file->second) || !IsSha256(hash->second))
        {
            tool.state = MediaToolState::ManifestInvalid;
            bundle.tools.push_back(std::move(tool));
            continue;
        }

        tool.version = version->second;
        tool.fileName = file->second;
        tool.expectedSha256 = ToLowerAscii(hash->second);
        tool.executablePath = bundle.rootFolder /
            std::filesystem::path{tool.fileName};

        filesystemError.clear();
        if (!std::filesystem::is_regular_file(
                tool.executablePath,
                filesystemError
            ))
        {
            tool.state = filesystemError
                ? MediaToolState::IoError
                : MediaToolState::FileMissing;
            bundle.tools.push_back(std::move(tool));
            continue;
        }

        std::string hashError;
        const std::optional<std::string> actualHash =
            ComputeFileSha256(tool.executablePath, hashError);
        if (!actualHash.has_value())
        {
            tool.state = MediaToolState::IoError;
            bundle.tools.push_back(std::move(tool));
            continue;
        }

        tool.actualSha256 = *actualHash;
        tool.state = tool.actualSha256 == tool.expectedSha256
            ? MediaToolState::Ready
            : MediaToolState::HashMismatch;
        bundle.tools.push_back(std::move(tool));
    }

    if (!bundle.IsReady())
    {
        bundle.errorMessage = "One or more bundled media tools are missing or failed integrity verification.";
    }

    return bundle;
}

std::optional<MediaToolBundleStatus> MediaToolManager::FindUsableBundle(
    const std::vector<std::filesystem::path>& candidateFolders
)
{
    std::set<std::filesystem::path> inspected;
    for (const std::filesystem::path& candidate : candidateFolders)
    {
        if (candidate.empty())
        {
            continue;
        }

        const std::filesystem::path normalized = candidate.lexically_normal();
        if (!inspected.insert(normalized).second)
        {
            continue;
        }

        MediaToolBundleStatus status = InspectBundle(normalized);
        if (status.IsReady())
        {
            return status;
        }
    }

    return std::nullopt;
}

std::optional<std::string> MediaToolManager::ComputeFileSha256(
    const std::filesystem::path& path,
    std::string& errorMessage
)
{
    errorMessage.clear();
    BCryptAlgorithm algorithm;
    if (algorithm.Get() == nullptr)
    {
        errorMessage = "Windows SHA-256 provider initialization failed.";
        return std::nullopt;
    }

    ULONG objectLength = 0;
    ULONG hashLength = 0;
    if (!ReadProperty(algorithm.Get(), BCRYPT_OBJECT_LENGTH, objectLength) ||
        !ReadProperty(algorithm.Get(), BCRYPT_HASH_LENGTH, hashLength) ||
        objectLength == 0 || hashLength == 0)
    {
        errorMessage = "Windows SHA-256 provider properties could not be read.";
        return std::nullopt;
    }

    std::vector<unsigned char> hashObject(objectLength);
    BCryptHashHandle hash(algorithm.Get(), hashObject);
    if (hash.Get() == nullptr)
    {
        errorMessage = "Windows SHA-256 hash initialization failed.";
        return std::nullopt;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
    {
        errorMessage = "The file could not be opened for SHA-256 verification.";
        return std::nullopt;
    }

    std::array<char, 64 * 1024> buffer{};
    while (input)
    {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0 && BCryptHashData(
                hash.Get(),
                reinterpret_cast<PUCHAR>(buffer.data()),
                static_cast<ULONG>(count),
                0
            ) < 0)
        {
            errorMessage = "Windows SHA-256 hashing failed.";
            return std::nullopt;
        }
    }

    if (!input.eof())
    {
        errorMessage = "The file could not be read completely for SHA-256 verification.";
        return std::nullopt;
    }

    std::vector<unsigned char> digest(hashLength);
    if (BCryptFinishHash(
            hash.Get(),
            digest.data(),
            static_cast<ULONG>(digest.size()),
            0
        ) < 0)
    {
        errorMessage = "Windows SHA-256 finalization failed.";
        return std::nullopt;
    }

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const unsigned char byte : digest)
    {
        output << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return output.str();
}
