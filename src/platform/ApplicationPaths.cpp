#include "platform/ApplicationPaths.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <ShlObj.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace
{
    constexpr wchar_t PortableMarkerName[] = L"portable.flag";
    constexpr wchar_t ApplicationFolderName[] = L"SoundBoardFasaFiso";

    std::optional<std::filesystem::path> ReadLocalAppDataPath()
    {
        PWSTR rawPath = nullptr;
        const HRESULT result = SHGetKnownFolderPath(
            FOLDERID_LocalAppData,
            KF_FLAG_CREATE,
            nullptr,
            &rawPath
        );

        if (FAILED(result) || rawPath == nullptr)
        {
            if (rawPath != nullptr)
            {
                CoTaskMemFree(rawPath);
            }
            return std::nullopt;
        }

        const std::filesystem::path path{rawPath};
        CoTaskMemFree(rawPath);
        return path;
    }

    bool ProbeWritableFolder(
        const std::filesystem::path& folder,
        std::wstring& errorMessage
    )
    {
        const auto nonce = std::chrono::steady_clock::now()
            .time_since_epoch()
            .count();
        const std::filesystem::path probePath = folder /
            (L".soundboard-write-test-" +
                std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(nonce) + L".tmp");

        const HANDLE probe = CreateFileW(
            probePath.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_DELETE,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
            nullptr
        );

        if (probe == INVALID_HANDLE_VALUE)
        {
            errorMessage = L"The application data folder is not writable: " +
                folder.wstring() + L"\n\nWindows error code: " +
                std::to_wstring(GetLastError());
            return false;
        }

        CloseHandle(probe);
        return true;
    }

    bool CopyFileIfMissing(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        std::wstring& errorMessage
    )
    {
        std::error_code error;
        if (std::filesystem::exists(destination, error))
        {
            return !error;
        }

        if (error)
        {
            errorMessage = L"The destination path could not be inspected: " +
                destination.wstring();
            return false;
        }

        error.clear();
        if (!std::filesystem::is_regular_file(source, error) || error)
        {
            errorMessage = L"A required packaged file is missing: " +
                source.wstring();
            return false;
        }

        error.clear();
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error)
        {
            errorMessage = L"The destination folder could not be created: " +
                destination.parent_path().wstring();
            return false;
        }

        error.clear();
        std::filesystem::copy_file(
            source,
            destination,
            std::filesystem::copy_options::none,
            error
        );
        if (error)
        {
            errorMessage = L"A packaged default file could not be copied: " +
                source.wstring() + L"\n\nDestination: " +
                destination.wstring();
            return false;
        }

        return true;
    }

    bool CopyDirectoryDefaults(
        const std::filesystem::path& sourceRoot,
        const std::filesystem::path& destinationRoot,
        std::wstring& errorMessage
    )
    {
        std::error_code error;
        if (!std::filesystem::is_directory(sourceRoot, error) || error)
        {
            errorMessage = L"A required packaged folder is missing: " +
                sourceRoot.wstring();
            return false;
        }

        std::filesystem::recursive_directory_iterator iterator{
            sourceRoot,
            std::filesystem::directory_options::skip_permission_denied,
            error
        };
        const std::filesystem::recursive_directory_iterator end;

        if (error)
        {
            errorMessage = L"The packaged sounds folder could not be opened: " +
                sourceRoot.wstring();
            return false;
        }

        while (iterator != end)
        {
            const std::filesystem::directory_entry entry = *iterator;
            const std::filesystem::path relative =
                entry.path().lexically_relative(sourceRoot);
            const std::filesystem::path destination =
                destinationRoot / relative;

            error.clear();
            if (entry.is_directory(error) && !error)
            {
                std::filesystem::create_directories(destination, error);
            }
            else
            {
                error.clear();
                if (entry.is_regular_file(error) && !error)
                {
                    error.clear();
                    if (!std::filesystem::exists(destination, error) && !error)
                    {
                        std::filesystem::create_directories(
                            destination.parent_path(),
                            error
                        );

                        if (!error)
                        {
                            std::filesystem::copy_file(
                                entry.path(),
                                destination,
                                std::filesystem::copy_options::none,
                                error
                            );
                        }
                    }
                }
            }

            if (error)
            {
                errorMessage = L"A packaged sound could not be copied: " +
                    entry.path().wstring() + L"\n\nDestination: " +
                    destination.wstring();
                return false;
            }

            iterator.increment(error);
            if (error)
            {
                errorMessage = L"The packaged sounds folder could not be scanned: " +
                    sourceRoot.wstring();
                return false;
            }
        }

        return true;
    }
}

ApplicationPathResolution ResolveApplicationPaths(
    const std::filesystem::path& executablePath,
    const std::optional<std::filesystem::path>& localAppDataOverride
)
{
    ApplicationPathResolution resolution;

    if (executablePath.empty() || !executablePath.has_parent_path())
    {
        resolution.errorMessage =
            L"The executable folder could not be determined.";
        return resolution;
    }

    ApplicationPaths paths;
    paths.executablePath = executablePath.lexically_normal();
    paths.programFolder =
        paths.executablePath.parent_path().lexically_normal();
    paths.bundledToolsFolder = paths.programFolder / L"tools";

    std::error_code error;
    const bool portableMarkerExists = std::filesystem::exists(
        paths.programFolder / PortableMarkerName,
        error
    );

    if (error)
    {
        resolution.errorMessage =
            L"The application folder could not be inspected: " +
            paths.programFolder.wstring();
        return resolution;
    }

    if (portableMarkerExists)
    {
        paths.storageMode = ApplicationStorageMode::Portable;
        paths.dataFolder = paths.programFolder;
    }
    else
    {
        const std::optional<std::filesystem::path> localAppData =
            localAppDataOverride.has_value()
                ? localAppDataOverride
                : ReadLocalAppDataPath();

        if (!localAppData.has_value() || localAppData->empty())
        {
            resolution.errorMessage =
                L"The LocalAppData folder could not be determined.";
            return resolution;
        }

        paths.storageMode = ApplicationStorageMode::LocalAppData;
        paths.dataFolder =
            (*localAppData / ApplicationFolderName).lexically_normal();
    }

    paths.configPath = paths.dataFolder / L"config.txt";
    paths.soundsFolder = paths.dataFolder / L"sounds";
    paths.logsFolder = paths.dataFolder / L"logs";
    paths.userToolsFolder = paths.dataFolder / L"tools";

    resolution.paths = std::move(paths);
    return resolution;
}

bool PrepareApplicationStorage(
    const ApplicationPaths& paths,
    std::wstring& errorMessage
)
{
    errorMessage.clear();

    std::error_code error;
    std::filesystem::create_directories(paths.dataFolder, error);
    if (error)
    {
        errorMessage = L"The application data folder could not be created: " +
            paths.dataFolder.wstring();
        return false;
    }

    if (!ProbeWritableFolder(paths.dataFolder, errorMessage))
    {
        if (paths.storageMode == ApplicationStorageMode::Portable)
        {
            errorMessage +=
                L"\n\nPortable mode stores configuration, logs and imported sounds "
                L"beside the executable. Move the complete application folder "
                L"to a writable location such as Downloads or Documents and "
                L"launch it normally, not as an administrator.";
        }
        return false;
    }

    for (const auto& folder : {
            paths.soundsFolder,
            paths.soundsFolder / L"Imported",
            paths.logsFolder,
            paths.userToolsFolder
        })
    {
        error.clear();
        std::filesystem::create_directories(folder, error);
        if (error)
        {
            errorMessage = L"A required application folder could not be created: " +
                folder.wstring();
            return false;
        }
    }

    if (paths.storageMode == ApplicationStorageMode::Portable)
    {
        error.clear();
        if (!std::filesystem::is_regular_file(paths.configPath, error) || error)
        {
            errorMessage =
                L"Portable mode requires config.txt beside the executable. "
                L"Extract the complete release archive instead of copying only "
                L"SoundBoardFasaFiso.exe.";
            return false;
        }

        return true;
    }

    if (!CopyFileIfMissing(
            paths.programFolder / L"config.txt",
            paths.configPath,
            errorMessage
        ))
    {
        return false;
    }

    return CopyDirectoryDefaults(
        paths.programFolder / L"sounds",
        paths.soundsFolder,
        errorMessage
    );
}

bool IsCurrentProcessElevated() noexcept
{
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == FALSE)
    {
        return false;
    }

    TOKEN_ELEVATION elevation{};
    DWORD bytesWritten = 0;
    const BOOL result = GetTokenInformation(
        token,
        TokenElevation,
        &elevation,
        sizeof(elevation),
        &bytesWritten
    );

    CloseHandle(token);
    return result != FALSE && elevation.TokenIsElevated != 0;
}
