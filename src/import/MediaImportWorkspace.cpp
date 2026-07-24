#include "import/MediaImportWorkspace.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
    constexpr std::wstring_view WorkspacePrefix =
        L"SoundBoardFasaFiso-Import-";

    wchar_t UpperAscii(const wchar_t character)
    {
        if (character >= L'a' && character <= L'z')
        {
            return character - L'a' + L'A';
        }

        return character;
    }

    bool IsReservedDeviceName(
        const std::wstring_view leafName
    )
    {
        const std::size_t extensionPosition =
            leafName.find(L'.');

        std::wstring baseName{
            leafName.substr(0, extensionPosition)
        };

        while (!baseName.empty() &&
               (baseName.back() == L' ' ||
                baseName.back() == L'.'))
        {
            baseName.pop_back();
        }

        for (wchar_t& character : baseName)
        {
            character = UpperAscii(character);
        }

        if (baseName == L"CON" ||
            baseName == L"PRN" ||
            baseName == L"AUX" ||
            baseName == L"NUL" ||
            baseName == L"CLOCK$" ||
            baseName == L"CONIN$" ||
            baseName == L"CONOUT$")
        {
            return true;
        }

        if (baseName.size() == 4U &&
            (baseName.starts_with(L"COM") ||
             baseName.starts_with(L"LPT")) &&
            baseName[3] >= L'1' &&
            baseName[3] <= L'9')
        {
            return true;
        }

        return false;
    }

    std::wstring RandomHexName(
        std::string& errorMessage
    )
    {
        std::array<unsigned char, 16> randomBytes{};

        const NTSTATUS status = BCryptGenRandom(
            nullptr,
            randomBytes.data(),
            static_cast<ULONG>(randomBytes.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG
        );

        if (status != 0)
        {
            errorMessage =
                "Secure workspace name generation failed. Status " +
                std::to_string(static_cast<long>(status)) + ".";
            return {};
        }

        constexpr std::wstring_view HexDigits =
            L"0123456789abcdef";

        std::wstring result;
        result.reserve(
            WorkspacePrefix.size() +
            randomBytes.size() * 2U
        );

        result.append(WorkspacePrefix);

        for (const unsigned char byte : randomBytes)
        {
            result.push_back(HexDigits[(byte >> 4U) & 0x0fU]);
            result.push_back(HexDigits[byte & 0x0fU]);
        }

        return result;
    }
}

MediaImportWorkspace::MediaImportWorkspace(
    std::filesystem::path rootFolder
)
    : rootFolder_(std::move(rootFolder)),
      cleanupEnabled_(true)
{
}

MediaImportWorkspace::~MediaImportWorkspace()
{
    Cleanup();
}

MediaImportWorkspace::MediaImportWorkspace(
    MediaImportWorkspace&& other
) noexcept
    : rootFolder_(std::move(other.rootFolder_)),
      cleanupEnabled_(
          std::exchange(other.cleanupEnabled_, false)
      )
{
    other.rootFolder_.clear();
}

MediaImportWorkspace& MediaImportWorkspace::operator=(
    MediaImportWorkspace&& other
) noexcept
{
    if (this != &other)
    {
        Cleanup();

        rootFolder_ = std::move(other.rootFolder_);
        cleanupEnabled_ =
            std::exchange(other.cleanupEnabled_, false);

        other.rootFolder_.clear();
    }

    return *this;
}

std::optional<MediaImportWorkspace>
MediaImportWorkspace::Create(
    const std::filesystem::path& baseDirectory,
    std::string& errorMessage
)
{
    errorMessage.clear();

    if (baseDirectory.empty())
    {
        errorMessage =
            "The media import workspace base directory is empty.";
        return std::nullopt;
    }

    std::error_code filesystemError;

    const std::filesystem::path normalizedBase =
        std::filesystem::absolute(
            baseDirectory,
            filesystemError
        ).lexically_normal();

    if (filesystemError)
    {
        errorMessage =
            "The media import workspace base path is invalid.";
        return std::nullopt;
    }

    std::filesystem::create_directories(
        normalizedBase,
        filesystemError
    );

    if (filesystemError ||
        !std::filesystem::is_directory(
            normalizedBase,
            filesystemError
        ))
    {
        errorMessage =
            "The media import workspace base directory "
            "could not be created.";
        return std::nullopt;
    }

    for (std::uint32_t attempt = 0; attempt < 32U; ++attempt)
    {
        const std::wstring folderName =
            RandomHexName(errorMessage);

        if (folderName.empty())
        {
            return std::nullopt;
        }

        const std::filesystem::path candidate =
            normalizedBase / folderName;

        if (CreateDirectoryW(candidate.c_str(), nullptr) != FALSE)
        {
            return MediaImportWorkspace{
                candidate.lexically_normal()
            };
        }

        const DWORD error = GetLastError();

        if (error != ERROR_ALREADY_EXISTS &&
            error != ERROR_FILE_EXISTS)
        {
            errorMessage =
                "The media import workspace could not be "
                "created. Windows error " +
                std::to_string(error) + ".";
            return std::nullopt;
        }
    }

    errorMessage =
        "A unique media import workspace could not be created.";
    return std::nullopt;
}

bool MediaImportWorkspace::IsValid() const noexcept
{
    return cleanupEnabled_ && !rootFolder_.empty();
}

const std::filesystem::path&
MediaImportWorkspace::RootFolder() const noexcept
{
    return rootFolder_;
}

std::filesystem::path
MediaImportWorkspace::SourceDownloadTemplate() const
{
    const std::optional<std::filesystem::path> path =
        SafeChildPath(L"source.%(ext)s");

    return path.value_or(std::filesystem::path{});
}

std::filesystem::path
MediaImportWorkspace::MetadataPath() const
{
    const std::optional<std::filesystem::path> path =
        SafeChildPath(L"metadata.json");

    return path.value_or(std::filesystem::path{});
}

std::filesystem::path
MediaImportWorkspace::ConvertedWavPath() const
{
    const std::optional<std::filesystem::path> path =
        SafeChildPath(L"converted.wav");

    return path.value_or(std::filesystem::path{});
}

std::optional<std::filesystem::path>
MediaImportWorkspace::SafeChildPath(
    const std::wstring_view leafName
) const
{
    if (!IsValid() || !IsSafeLeafName(leafName))
    {
        return std::nullopt;
    }

    const std::filesystem::path candidate =
        (
            rootFolder_ /
            std::filesystem::path{std::wstring{leafName}}
        ).lexically_normal();

    if (candidate.parent_path() != rootFolder_)
    {
        return std::nullopt;
    }

    return candidate;
}

bool MediaImportWorkspace::IsSafeLeafName(
    const std::wstring_view leafName
)
{
    if (leafName.empty() ||
        leafName == L"." ||
        leafName == L".." ||
        leafName.size() > 240U)
    {
        return false;
    }

    constexpr std::wstring_view InvalidCharacters =
        L"<>:\"/\\|?*";

    for (const wchar_t character : leafName)
    {
        if (character < 0x20 ||
            character == 0x7f ||
            InvalidCharacters.find(character) !=
                std::wstring_view::npos)
        {
            return false;
        }
    }

    if (leafName.back() == L' ' ||
        leafName.back() == L'.' ||
        IsReservedDeviceName(leafName))
    {
        return false;
    }

    const std::filesystem::path path{
        std::wstring{leafName}
    };

    return !path.is_absolute() &&
        !path.has_parent_path() &&
        path.filename() == path;
}

void MediaImportWorkspace::Cleanup() noexcept
{
    if (!cleanupEnabled_ || rootFolder_.empty())
    {
        return;
    }

    std::error_code ignoredError;
    std::filesystem::remove_all(
        rootFolder_,
        ignoredError
    );

    cleanupEnabled_ = false;
    rootFolder_.clear();
}
