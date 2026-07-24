#include "import/SoundImporter.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
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
            const auto timestamp =
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count();
            path_ = std::filesystem::temp_directory_path() /
                ("SoundBoardFasaFiso-import-tests-" +
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
        const std::string_view contents = "fixture"
    )
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    void TestExternalFileCopiesIntoImported(
        const TemporaryDirectory& directory
    )
    {
        const auto source = directory.Path() / "incoming" / "clip.wav";
        const auto sounds = directory.Path() / "app" / "sounds";
        WriteFile(source);

        const SoundImportSummary result =
            SoundImporter{sounds}.Import({source});

        Expect(result.copiedCount == 1, "external file is copied");
        Expect(result.existingCount == 0, "external file is not existing");
        Expect(result.failedPaths.empty(), "external import has no failure");
        Expect(
            result.importedRelativePaths ==
                std::vector<std::filesystem::path>{
                    std::filesystem::path{L"Imported"} / L"clip.wav"
                },
            "external file returns a portable relative path"
        );
        Expect(
            std::filesystem::exists(sounds / L"Imported" / L"clip.wav"),
            "external file exists in Imported"
        );
    }

    void TestNameCollisionsUseStableSuffixes(
        const TemporaryDirectory& directory
    )
    {
        const auto first = directory.Path() / "one" / "same.mp3";
        const auto second = directory.Path() / "two" / "same.mp3";
        const auto sounds = directory.Path() / "collision" / "sounds";
        WriteFile(first, "one");
        WriteFile(second, "two");

        const SoundImportSummary result =
            SoundImporter{sounds}.Import({first, second});

        Expect(result.copiedCount == 2, "both colliding files are copied");
        Expect(
            std::filesystem::exists(sounds / L"Imported" / L"same.mp3"),
            "first colliding name is preserved"
        );
        Expect(
            std::filesystem::exists(sounds / L"Imported" / L"same_2.mp3"),
            "second colliding name receives a suffix"
        );
    }

    void TestFilesAlreadyInsideSoundsStayInPlace(
        const TemporaryDirectory& directory
    )
    {
        const auto sounds = directory.Path() / "existing" / "sounds";
        const auto source = sounds / "Effects" / "airhorn.flac";
        WriteFile(source);

        const SoundImportSummary result =
            SoundImporter{sounds}.Import({source});

        Expect(result.copiedCount == 0, "in-tree file is not copied");
        Expect(result.existingCount == 1, "in-tree file is reported existing");
        Expect(
            result.importedRelativePaths ==
                std::vector<std::filesystem::path>{
                    std::filesystem::path{L"Effects"} / L"airhorn.flac"
                },
            "in-tree file keeps its relative path"
        );
    }

    void TestDirectoryImportIsRecursiveAndFiltersFormats(
        const TemporaryDirectory& directory
    )
    {
        const auto input = directory.Path() / "folder";
        const auto sounds = directory.Path() / "recursive" / "sounds";
        WriteFile(input / "top.wav");
        WriteFile(input / "nested" / "deep.mp3");
        WriteFile(input / "nested" / "notes.txt");

        const SoundImportSummary result =
            SoundImporter{sounds}.Import({input});

        Expect(result.copiedCount == 2, "directory import finds nested audio");
        Expect(result.unsupportedCount == 1, "unsupported file is counted");
        Expect(result.failedPaths.empty(), "directory import has no failure");
    }

    void TestDuplicateInputsAreImportedOnce(
        const TemporaryDirectory& directory
    )
    {
        const auto source = directory.Path() / "duplicate" / "clip.wav";
        const auto sounds = directory.Path() / "dedupe" / "sounds";
        WriteFile(source);

        const SoundImportSummary result =
            SoundImporter{sounds}.Import({source, source});

        Expect(result.copiedCount == 1, "duplicate input is copied once");
        Expect(
            result.importedRelativePaths.size() == 1,
            "duplicate input yields one library entry"
        );
    }
}

int main()
{
    const TemporaryDirectory directory;

    TestExternalFileCopiesIntoImported(directory);
    TestNameCollisionsUseStableSuffixes(directory);
    TestFilesAlreadyInsideSoundsStayInPlace(directory);
    TestDirectoryImportIsRecursiveAndFiltersFormats(directory);
    TestDuplicateInputsAreImportedOnce(directory);

    if (failureCount != 0)
    {
        std::cerr << failureCount << " test assertion(s) failed.\n";
        return 1;
    }

    std::cout << "Sound importer tests passed.\n";
    return 0;
}
