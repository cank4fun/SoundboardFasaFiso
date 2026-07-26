#pragma once

#include <filesystem>
#include <optional>
#include <string>

enum class ApplicationStorageMode
{
    Portable,
    LocalAppData
};

struct ApplicationPaths
{
    std::filesystem::path executablePath;
    std::filesystem::path programFolder;
    std::filesystem::path dataFolder;
    std::filesystem::path configPath;
    std::filesystem::path soundsFolder;
    std::filesystem::path logsFolder;
    std::filesystem::path bundledToolsFolder;
    std::filesystem::path userToolsFolder;
    ApplicationStorageMode storageMode = ApplicationStorageMode::Portable;
};

struct ApplicationPathResolution
{
    std::optional<ApplicationPaths> paths;
    std::wstring errorMessage;
};

ApplicationPathResolution ResolveApplicationPaths(
    const std::filesystem::path& executablePath,
    const std::optional<std::filesystem::path>& localAppDataOverride =
        std::nullopt
);

bool PrepareApplicationStorage(
    const ApplicationPaths& paths,
    std::wstring& errorMessage
);

bool IsCurrentProcessElevated() noexcept;
