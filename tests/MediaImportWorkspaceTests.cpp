#include "import/MediaImportWorkspace.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

namespace
{
    bool Expect(const bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
        }

        return condition;
    }

    std::filesystem::path TestBaseDirectory()
    {
        return std::filesystem::temp_directory_path() /
            (
                L"SoundBoardFasaFiso-WorkspaceTests-" +
                std::to_wstring(GetCurrentProcessId()) +
                L"-" +
                std::to_wstring(GetTickCount64())
            );
    }
}

int main()
{
    const std::filesystem::path baseDirectory =
        TestBaseDirectory();

    std::error_code ignoredError;
    std::filesystem::remove_all(
        baseDirectory,
        ignoredError
    );

    std::string errorMessage;
    std::filesystem::path firstRoot;

    {
        std::optional<MediaImportWorkspace> workspace =
            MediaImportWorkspace::Create(
                baseDirectory,
                errorMessage
            );

        if (!Expect(
                workspace.has_value(),
                "The media import workspace was not created."
            ) ||
            !Expect(
                workspace->IsValid(),
                "The created workspace was not valid."
            ))
        {
            std::cerr << errorMessage << '\n';
            return 1;
        }

        firstRoot = workspace->RootFolder();

        const std::filesystem::path expectedBase =
            std::filesystem::absolute(
                baseDirectory
            ).lexically_normal();

        if (!Expect(
                std::filesystem::is_directory(firstRoot),
                "The workspace directory does not exist."
            ) ||
            !Expect(
                firstRoot.parent_path() == expectedBase,
                "The workspace was created outside its base directory."
            ) ||
            !Expect(
                workspace->SourceDownloadTemplate() ==
                    firstRoot / L"source.%(ext)s",
                "The source download template was incorrect."
            ) ||
            !Expect(
                workspace->MetadataPath() ==
                    firstRoot / L"metadata.json",
                "The metadata path was incorrect."
            ) ||
            !Expect(
                workspace->ConvertedWavPath() ==
                    firstRoot / L"converted.wav",
                "The converted WAV path was incorrect."
            ))
        {
            return 1;
        }

        const std::optional<std::filesystem::path> safePath =
            workspace->SafeChildPath(L"safe name.wav");

        if (!Expect(
                safePath.has_value(),
                "A safe child file name was rejected."
            ) ||
            !Expect(
                safePath->parent_path() == firstRoot,
                "A safe child path escaped the workspace."
            ))
        {
            return 1;
        }

        const std::wstring invalidNames[]{
            L"",
            L".",
            L"..",
            L"../escape.wav",
            L"..\\escape.wav",
            L"folder/file.wav",
            L"folder\\file.wav",
            L"C:escape.wav",
            L"bad?.wav",
            L"bad*.wav",
            L"bad\nname.wav",
            L"trailing-space ",
            L"trailing-dot.",
            L"CON",
            L"con.txt",
            L"NUL.wav",
            L"COM1",
            L"LPT9.txt"
        };

        for (const std::wstring& name : invalidNames)
        {
            if (!Expect(
                    !MediaImportWorkspace::IsSafeLeafName(name),
                    "An unsafe leaf file name was accepted."
                ) ||
                !Expect(
                    !workspace->SafeChildPath(name).has_value(),
                    "An unsafe child path was created."
                ))
            {
                std::wcerr << L"Rejected test name: " << name << L'\n';
                return 1;
            }
        }
    }

    if (!Expect(
            !std::filesystem::exists(firstRoot),
            "The workspace was not removed by its destructor."
        ))
    {
        return 1;
    }

    std::filesystem::path replacedRoot;
    std::filesystem::path retainedRoot;

    {
        std::optional<MediaImportWorkspace> first =
            MediaImportWorkspace::Create(
                baseDirectory,
                errorMessage
            );

        std::optional<MediaImportWorkspace> second =
            MediaImportWorkspace::Create(
                baseDirectory,
                errorMessage
            );

        if (!Expect(
                first.has_value() && second.has_value(),
                "Move-assignment workspaces could not be created."
            ))
        {
            return 1;
        }

        replacedRoot = first->RootFolder();
        retainedRoot = second->RootFolder();

        *first = std::move(*second);

        if (!Expect(
                !std::filesystem::exists(replacedRoot),
                "Move assignment did not clean the replaced workspace."
            ) ||
            !Expect(
                first->RootFolder() == retainedRoot,
                "Move assignment did not retain the source workspace."
            ) ||
            !Expect(
                !second->IsValid(),
                "The moved-from workspace remained valid."
            ))
        {
            return 1;
        }
    }

    if (!Expect(
            !std::filesystem::exists(retainedRoot),
            "The moved workspace was not cleaned."
        ))
    {
        return 1;
    }

    const std::filesystem::path blockingFile =
        baseDirectory / L"blocking-file";

    std::filesystem::create_directories(baseDirectory);

    {
        std::ofstream stream{
            blockingFile,
            std::ios::binary
        };

        stream << "not a directory";
    }

    const std::optional<MediaImportWorkspace> blocked =
        MediaImportWorkspace::Create(
            blockingFile,
            errorMessage
        );

    if (!Expect(
            !blocked.has_value(),
            "A regular file was accepted as a workspace base."
        ) ||
        !Expect(
            !errorMessage.empty(),
            "Workspace creation failure did not return an error."
        ))
    {
        return 1;
    }

    std::filesystem::remove_all(
        baseDirectory,
        ignoredError
    );

    std::cout << "MediaImportWorkspace tests passed.\n";
    return 0;
}
