#include "platform/MediaToolExecutor.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

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

    std::filesystem::path CurrentExecutablePath()
    {
        std::array<wchar_t, 32768> buffer{};

        const DWORD length = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size())
        );

        if (length == 0 ||
            length >= static_cast<DWORD>(buffer.size()))
        {
            return {};
        }

        return std::filesystem::path{
            std::wstring{
                buffer.data(),
                static_cast<std::size_t>(length)
            }
        };
    }

    std::string NarrowAscii(const std::wstring_view value)
    {
        std::string result;
        result.reserve(value.size());

        for (const wchar_t character : value)
        {
            result.push_back(
                character <= 0x7f
                    ? static_cast<char>(character)
                    : '?'
            );
        }

        return result;
    }

    int RunChildMode(
        const int argumentCount,
        wchar_t* arguments[]
    )
    {
        if (argumentCount < 2 ||
            std::wstring_view{arguments[1]} !=
                L"--media-tool-executor-child")
        {
            return -1;
        }

        for (int index = 2; index < argumentCount; ++index)
        {
            std::cout
                << "arg"
                << index - 2
                << '='
                << NarrowAscii(arguments[index])
                << '\n';
        }

        std::cerr << "executor-stderr-marker\n";
        return 29;
    }

    bool BuildVerifiedBundle(
        const std::filesystem::path& executable,
        MediaToolBundleStatus& bundle
    )
    {
        if (executable.empty())
        {
            return false;
        }

        std::string hashError;

        const std::optional<std::string> hash =
            MediaToolManager::ComputeFileSha256(
                executable,
                hashError
            );

        if (!hash.has_value())
        {
            std::cerr << hashError << '\n';
            return false;
        }

        constexpr std::array<std::string_view, 4> ids{
            "yt-dlp",
            "deno",
            "ffmpeg",
            "ffprobe"
        };

        bundle.rootFolder =
            executable.parent_path().lexically_normal();
        bundle.bundleVersion = "executor-test-bundle";
        bundle.errorMessage.clear();
        bundle.tools.clear();

        const std::string fileName =
            executable.filename().string();

        for (const std::string_view id : ids)
        {
            MediaToolStatus status;
            status.id = std::string{id};
            status.version = "test";
            status.fileName = fileName;
            status.expectedSha256 = *hash;
            status.actualSha256 = *hash;
            status.executablePath =
                executable.lexically_normal();
            status.state = MediaToolState::Ready;

            bundle.tools.push_back(std::move(status));
        }

        return bundle.IsReady();
    }

    MediaToolStatus* FindMutable(
        MediaToolBundleStatus& bundle,
        const std::string_view id
    )
    {
        for (MediaToolStatus& tool : bundle.tools)
        {
            if (tool.id == id)
            {
                return &tool;
            }
        }

        return nullptr;
    }
}

int wmain(
    const int argumentCount,
    wchar_t* arguments[]
)
{
    const int childResult =
        RunChildMode(argumentCount, arguments);

    if (childResult >= 0)
    {
        return childResult;
    }

    MediaToolBundleStatus bundle;

    if (!BuildVerifiedBundle(
            CurrentExecutablePath(),
            bundle
        ))
    {
        std::cerr << "The test bundle could not be created.\n";
        return 1;
    }

    MediaToolRunOptions runOptions;
    runOptions.arguments = {
        L"--media-tool-executor-child",
        L"plain",
        L"two words",
        L"quote\"inside"
    };
    runOptions.timeout = std::chrono::seconds{5};

    const MediaToolRunResult runResult =
        MediaToolExecutor::Run(
            bundle,
            MediaToolKind::Ffprobe,
            runOptions
        );

    if (!Expect(
            runResult.WasLaunched(),
            "The verified media tool was not launched."
        ) ||
        !Expect(
            runResult.process.reason ==
                ProcessExitReason::Exited,
            "The verified media tool did not exit normally."
        ) ||
        !Expect(
            runResult.process.exitCode == 29,
            "The media-tool exit code was not preserved."
        ) ||
        !Expect(
            runResult.process.standardOutput.find(
                "arg0=plain"
            ) != std::string::npos,
            "The plain media-tool argument was not preserved."
        ) ||
        !Expect(
            runResult.process.standardOutput.find(
                "arg1=two words"
            ) != std::string::npos,
            "The spaced media-tool argument was not preserved."
        ) ||
        !Expect(
            runResult.process.standardOutput.find(
                "arg2=quote\"inside"
            ) != std::string::npos,
            "The quoted media-tool argument was not preserved."
        ) ||
        !Expect(
            runResult.process.standardError.find(
                "executor-stderr-marker"
            ) != std::string::npos,
            "Media-tool standard error was not captured."
        ))
    {
        std::cerr
            << runResult.errorMessage
            << runResult.process.errorMessage
            << '\n';
        return 1;
    }

    MediaToolBundleStatus changedHashBundle = bundle;
    MediaToolStatus* changedHashTool =
        FindMutable(changedHashBundle, "ffprobe");

    if (changedHashTool == nullptr)
    {
        return 1;
    }

    changedHashTool->expectedSha256 =
        std::string(64, '0');

    const MediaToolRunResult changedHashResult =
        MediaToolExecutor::Run(
            changedHashBundle,
            MediaToolKind::Ffprobe,
            {}
        );

    if (!Expect(
            changedHashResult.error ==
                MediaToolExecutionError::IntegrityCheckFailed,
            "A changed executable hash was not rejected."
        ) ||
        !Expect(
            !changedHashResult.WasLaunched(),
            "A changed executable was launched."
        ))
    {
        return 1;
    }

    MediaToolBundleStatus invalidPathBundle = bundle;
    MediaToolStatus* invalidPathTool =
        FindMutable(invalidPathBundle, "ffmpeg");

    if (invalidPathTool == nullptr)
    {
        return 1;
    }

    invalidPathTool->executablePath =
        invalidPathBundle.rootFolder /
        L"not-the-manifest-file.exe";

    const MediaToolRunResult invalidPathResult =
        MediaToolExecutor::Run(
            invalidPathBundle,
            MediaToolKind::Ffmpeg,
            {}
        );

    if (!Expect(
            invalidPathResult.error ==
                MediaToolExecutionError::InvalidToolPath,
            "A mismatched executable path was not rejected."
        ))
    {
        return 1;
    }

    MediaToolBundleStatus unavailableBundle = bundle;
    unavailableBundle.errorMessage =
        "The test bundle is intentionally unavailable.";

    const MediaToolRunResult unavailableResult =
        MediaToolExecutor::Run(
            unavailableBundle,
            MediaToolKind::YtDlp,
            {}
        );

    if (!Expect(
            unavailableResult.error ==
                MediaToolExecutionError::BundleNotReady,
            "An unavailable bundle was not rejected."
        ))
    {
        return 1;
    }

    const MediaToolRunResult invalidKindResult =
        MediaToolExecutor::Run(
            bundle,
            static_cast<MediaToolKind>(999),
            {}
        );

    if (!Expect(
            invalidKindResult.error ==
                MediaToolExecutionError::ToolNotFound,
            "An invalid media-tool kind was not rejected."
        ))
    {
        return 1;
    }

    std::cout << "MediaToolExecutor tests passed.\n";
    return 0;
}
