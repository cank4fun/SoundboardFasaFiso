#include "platform/ApplicationPaths.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string_view>

namespace
{
    int failureCount = 0;

    void Expect(const bool condition, const std::string_view message)
    {
        if (condition)
        {
            return;
        }

        ++failureCount;
        std::cerr << "FAILED: " << message << '\n';
    }

    class TemporaryDirectory
    {
    public:
        TemporaryDirectory()
        {
            const auto timestamp = std::chrono::steady_clock::now()
                .time_since_epoch()
                .count();
            path_ = std::filesystem::temp_directory_path() /
                ("SoundBoardFasaFiso-path-tests-" +
                    std::to_string(timestamp));
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

    void WriteFile(
        const std::filesystem::path& path,
        const std::string_view contents
    )
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return {
            std::istreambuf_iterator<char>{file},
            std::istreambuf_iterator<char>{}
        };
    }

    void TestPortableModeStaysBesideExecutable(
        const TemporaryDirectory& directory
    )
    {
        const auto program = directory.Path() / "portable";
        const auto executable = program / "SoundBoardFasaFiso.exe";
        WriteFile(executable, "exe");
        WriteFile(program / "portable.flag", "portable\n");
        WriteFile(program / "config.txt", "language=en\n");
        WriteFile(program / "sounds" / "example.wav", "sound");

        const ApplicationPathResolution resolution = ResolveApplicationPaths(
            executable,
            directory.Path() / "unused-local-app-data"
        );

        Expect(resolution.paths.has_value(), "portable paths resolve");
        if (!resolution.paths.has_value())
        {
            return;
        }

        Expect(
            resolution.paths->storageMode == ApplicationStorageMode::Portable,
            "portable marker selects portable storage"
        );
        Expect(
            resolution.paths->dataFolder == program,
            "portable data stays beside executable"
        );

        std::wstring errorMessage;
        Expect(
            PrepareApplicationStorage(*resolution.paths, errorMessage),
            "portable storage prepares successfully"
        );
        Expect(
            std::filesystem::is_directory(program / "logs"),
            "portable logs directory is created"
        );
        Expect(
            std::filesystem::is_directory(program / "sounds" / "Imported"),
            "portable Imported directory is created"
        );
        Expect(
            std::filesystem::is_directory(program / "tools"),
            "portable tools directory is created"
        );
    }

    void TestLocalAppDataModeBootstrapsPackagedDefaults(
        const TemporaryDirectory& directory
    )
    {
        const auto program = directory.Path() / "installed";
        const auto localAppData = directory.Path() / "local-app-data";
        const auto executable = program / "SoundBoardFasaFiso.exe";
        WriteFile(executable, "exe");
        WriteFile(program / "config.txt", "language=tr\n");
        WriteFile(program / "sounds" / "example.wav", "sound");
        WriteFile(program / "sounds" / "Nested" / "example.mp3", "sound2");

        const ApplicationPathResolution resolution = ResolveApplicationPaths(
            executable,
            localAppData
        );

        Expect(resolution.paths.has_value(), "LocalAppData paths resolve");
        if (!resolution.paths.has_value())
        {
            return;
        }

        const auto expectedData = localAppData / "SoundBoardFasaFiso";
        Expect(
            resolution.paths->storageMode ==
                ApplicationStorageMode::LocalAppData,
            "missing portable marker selects LocalAppData"
        );
        Expect(
            resolution.paths->dataFolder == expectedData,
            "LocalAppData folder is application-scoped"
        );

        std::wstring errorMessage;
        Expect(
            PrepareApplicationStorage(*resolution.paths, errorMessage),
            "LocalAppData storage prepares successfully"
        );
        Expect(
            ReadFile(expectedData / "config.txt") == "language=tr\n",
            "default config is copied once"
        );
        Expect(
            std::filesystem::exists(expectedData / "sounds" / "example.wav"),
            "top-level example sound is copied"
        );
        Expect(
            std::filesystem::exists(
                expectedData / "sounds" / "Nested" / "example.mp3"
            ),
            "nested example sound is copied"
        );

        WriteFile(expectedData / "config.txt", "language=en\n");
        Expect(
            PrepareApplicationStorage(*resolution.paths, errorMessage),
            "LocalAppData preparation is repeatable"
        );
        Expect(
            ReadFile(expectedData / "config.txt") == "language=en\n",
            "existing user config is not overwritten"
        );
    }

    void TestLocalAppDataRejectsConfigDirectoryCollision(
        const TemporaryDirectory& directory
    )
    {
        const auto program =
            directory.Path() / "installed-config-collision";
        const auto localAppData =
            directory.Path() / "local-app-data-config-collision";
        const auto executable = program / "SoundBoardFasaFiso.exe";
        WriteFile(executable, "exe");
        WriteFile(program / "config.txt", "language=tr\n");
        WriteFile(program / "sounds" / "example.wav", "sound");

        const ApplicationPathResolution resolution =
            ResolveApplicationPaths(executable, localAppData);

        Expect(
            resolution.paths.has_value(),
            "config-collision paths resolve"
        );

        if (!resolution.paths.has_value())
        {
            return;
        }

        std::filesystem::create_directories(
            resolution.paths->configPath
        );

        std::wstring errorMessage;
        Expect(
            !PrepareApplicationStorage(
                *resolution.paths,
                errorMessage
            ),
            "a config directory collision is rejected"
        );
        Expect(
            errorMessage.find(L"config.txt") != std::wstring::npos,
            "the config collision error identifies config.txt"
        );
    }

    void TestLocalAppDataRejectsSoundDirectoryCollision(
        const TemporaryDirectory& directory
    )
    {
        const auto program =
            directory.Path() / "installed-sound-collision";
        const auto localAppData =
            directory.Path() / "local-app-data-sound-collision";
        const auto executable = program / "SoundBoardFasaFiso.exe";
        WriteFile(executable, "exe");
        WriteFile(program / "config.txt", "language=tr\n");
        WriteFile(program / "sounds" / "example.wav", "sound");

        const ApplicationPathResolution resolution =
            ResolveApplicationPaths(executable, localAppData);

        Expect(
            resolution.paths.has_value(),
            "sound-collision paths resolve"
        );

        if (!resolution.paths.has_value())
        {
            return;
        }

        std::filesystem::create_directories(
            resolution.paths->soundsFolder / "example.wav"
        );

        std::wstring errorMessage;
        Expect(
            !PrepareApplicationStorage(
                *resolution.paths,
                errorMessage
            ),
            "a packaged-sound directory collision is rejected"
        );
        Expect(
            errorMessage.find(L"example.wav") != std::wstring::npos,
            "the sound collision error identifies the destination"
        );
    }

    void TestIncompletePortableFolderFailsClearly(
        const TemporaryDirectory& directory
    )
    {
        const auto program = directory.Path() / "incomplete-portable";
        const auto executable = program / "SoundBoardFasaFiso.exe";
        WriteFile(executable, "exe");
        WriteFile(program / "portable.flag", "portable\n");

        const ApplicationPathResolution resolution = ResolveApplicationPaths(
            executable,
            directory.Path() / "unused-local-app-data-2"
        );

        Expect(resolution.paths.has_value(), "incomplete portable paths resolve");
        if (!resolution.paths.has_value())
        {
            return;
        }

        std::wstring errorMessage;
        Expect(
            !PrepareApplicationStorage(*resolution.paths, errorMessage),
            "portable folder without config is rejected"
        );
        Expect(
            errorMessage.find(L"config.txt") != std::wstring::npos,
            "portable error identifies missing config"
        );
    }
}

int main()
{
    const TemporaryDirectory directory;

    TestPortableModeStaysBesideExecutable(directory);
    TestLocalAppDataModeBootstrapsPackagedDefaults(directory);
    TestLocalAppDataRejectsConfigDirectoryCollision(directory);
    TestLocalAppDataRejectsSoundDirectoryCollision(directory);
    TestIncompletePortableFolderFailsClearly(directory);

    if (failureCount != 0)
    {
        std::cerr << failureCount << " test assertion(s) failed.\n";
        return 1;
    }

    std::cout << "Application path tests passed.\n";
    return 0;
}
