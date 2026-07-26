#include "import/UrlImportService.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

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

    MediaToolRunResult Exited(
        const std::uint32_t exitCode,
        std::string standardOutput = {},
        std::string standardError = {}
    )
    {
        MediaToolRunResult result;
        result.process.reason = ProcessExitReason::Exited;
        result.process.exitCode = exitCode;
        result.process.standardOutput =
            std::move(standardOutput);
        result.process.standardError =
            std::move(standardError);

        return result;
    }

    bool WriteFile(
        const std::filesystem::path& path,
        const std::string& data
    )
    {
        std::filesystem::create_directories(
            path.parent_path()
        );

        std::ofstream stream{
            path,
            std::ios::binary | std::ios::trunc
        };

        stream.write(
            data.data(),
            static_cast<std::streamsize>(data.size())
        );

        return static_cast<bool>(stream);
    }

    std::optional<std::wstring> ArgumentAfter(
        const std::vector<std::wstring>& arguments,
        const std::wstring& argument
    )
    {
        for (std::size_t index = 0;
             index + 1U < arguments.size();
             ++index)
        {
            if (arguments[index] == argument)
            {
                return arguments[index + 1U];
            }
        }

        return std::nullopt;
    }

    bool IsDirectoryEmpty(
        const std::filesystem::path& path
    )
    {
        std::error_code error;

        const std::filesystem::directory_iterator iterator{
            path,
            error
        };

        return !error &&
            iterator == std::filesystem::directory_iterator{};
    }

    std::filesystem::path TestRoot()
    {
        return std::filesystem::temp_directory_path() /
            (
                L"SoundBoardFasaFiso-UrlImportTests-" +
                std::to_wstring(GetCurrentProcessId()) +
                L"-" +
                std::to_wstring(GetTickCount64())
            );
    }
}

int main()
{
    const std::filesystem::path testRoot = TestRoot();
    const std::filesystem::path workspaceBase =
        testRoot / L"workspace";
    const std::filesystem::path destinationDirectory =
        testRoot / L"output";

    std::error_code ignoredError;
    std::filesystem::remove_all(testRoot, ignoredError);

    UrlImportRequest request;
    request.url =
        L"https://example.com/watch?v=test&value=1";
    request.workspaceBaseDirectory = workspaceBase;
    request.destinationDirectory = destinationDirectory;
    request.destinationFileName = L"imported.wav";

    std::size_t callIndex = 0;

    const UrlImportToolRunner successfulRunner =
        [&callIndex](
            const MediaToolKind tool,
            const MediaToolRunOptions& options
        ) -> MediaToolRunResult
        {
            const std::size_t current = callIndex++;

            if (current == 0U)
            {
                if (tool != MediaToolKind::YtDlp)
                {
                    return Exited(90U);
                }

                return Exited(
                    0U,
                    R"({"title":"fixture","duration":12.5})"
                );
            }

            if (current == 1U)
            {
                if (tool != MediaToolKind::YtDlp)
                {
                    return Exited(91U);
                }

                const std::optional<std::wstring> output =
                    ArgumentAfter(
                        options.arguments,
                        L"--output"
                    );

                if (!output.has_value())
                {
                    return Exited(92U);
                }

                std::wstring sourceName = *output;
                const std::wstring token = L"%(ext)s";
                const std::size_t tokenPosition =
                    sourceName.find(token);

                if (tokenPosition == std::wstring::npos)
                {
                    return Exited(93U);
                }

                sourceName.replace(
                    tokenPosition,
                    token.size(),
                    L"webm"
                );

                if (!WriteFile(
                        std::filesystem::path{sourceName},
                        "downloaded-audio"
                    ))
                {
                    return Exited(94U);
                }

                return Exited(0U);
            }

            if (current == 2U)
            {
                if (tool != MediaToolKind::Ffprobe)
                {
                    return Exited(95U);
                }

                return Exited(
                    0U,
                    R"({"streams":[{"sample_rate":"48000","channels":2}]})"
                );
            }

            if (current == 3U)
            {
                if (tool != MediaToolKind::Ffmpeg ||
                    options.arguments.empty())
                {
                    return Exited(96U);
                }

                const std::filesystem::path outputPath{
                    options.arguments.back()
                };

                if (!WriteFile(
                        outputPath,
                        "RIFF-fake-wave-data"
                    ))
                {
                    return Exited(97U);
                }

                return Exited(0U);
            }

            return Exited(98U);
        };

    const UrlImportResult success =
        UrlImportService::ImportWithRunner(
            request,
            LR"(C:\Fake Tools\deno.exe)",
            successfulRunner
        );

    if (!Expect(
            success.Succeeded(),
            "The deterministic URL import failed."
        ) ||
        !Expect(
            callIndex == 4U,
            "The URL import did not execute four tool stages."
        ) ||
        !Expect(
            std::filesystem::is_regular_file(
                success.destinationPath
            ),
            "The published WAV file does not exist."
        ) ||
        !Expect(
            success.destinationPath ==
                std::filesystem::absolute(
                    destinationDirectory
                ).lexically_normal() /
                L"imported.wav",
            "The published WAV path was incorrect."
        ) ||
        !Expect(
            success.metadataJson.find("fixture") !=
                std::string::npos,
            "The metadata output was not preserved."
        ) ||
        !Expect(
            success.probeJson.find("sample_rate") !=
                std::string::npos,
            "The ffprobe output was not preserved."
        ) ||
        !Expect(
            IsDirectoryEmpty(workspaceBase),
            "The temporary workspace was not cleaned."
        ))
    {
        std::cerr << success.errorMessage << '\n';
        return 1;
    }

    UrlImportRequest failedRequest = request;
    failedRequest.destinationFileName =
        L"failed.wav";

    std::size_t failureCall = 0;

    const UrlImportToolRunner failingRunner =
        [&failureCall](
            const MediaToolKind,
            const MediaToolRunOptions&
        ) -> MediaToolRunResult
        {
            ++failureCall;

            if (failureCall == 1U)
            {
                return Exited(
                    0U,
                    R"({"title":"failure fixture"})"
                );
            }

            return Exited(
                7U,
                {},
                "simulated download failure"
            );
        };

    const UrlImportResult failed =
        UrlImportService::ImportWithRunner(
            failedRequest,
            LR"(C:\Fake Tools\deno.exe)",
            failingRunner
        );

    if (!Expect(
            failed.error ==
                UrlImportError::ToolExecutionFailed,
            "A failed download was not reported."
        ) ||
        !Expect(
            failed.stage == UrlImportStage::Download,
            "The failed download stage was incorrect."
        ) ||
        !Expect(
            !std::filesystem::exists(
                destinationDirectory / L"failed.wav"
            ),
            "A failed import published a WAV file."
        ) ||
        !Expect(
            IsDirectoryEmpty(workspaceBase),
            "A failed import left its workspace behind."
        ))
    {
        return 1;
    }

    UrlImportRequest invalidRequest = request;
    invalidRequest.destinationFileName =
        L"..\\escape.wav";

    std::size_t invalidCallCount = 0;

    const UrlImportResult invalid =
        UrlImportService::ImportWithRunner(
            invalidRequest,
            LR"(C:\Fake Tools\deno.exe)",
            [&invalidCallCount](
                const MediaToolKind,
                const MediaToolRunOptions&
            )
            {
                ++invalidCallCount;
                return Exited(0U);
            }
        );

    if (!Expect(
            invalid.error == UrlImportError::InvalidRequest,
            "An unsafe destination file name was accepted."
        ) ||
        !Expect(
            invalidCallCount == 0U,
            "Tools ran for an invalid request."
        ))
    {
        return 1;
    }

    UrlImportRequest cancelledRequest = request;
    cancelledRequest.destinationFileName =
        L"cancelled.wav";

    const UrlImportResult cancelled =
        UrlImportService::ImportWithRunner(
            cancelledRequest,
            LR"(C:\Fake Tools\deno.exe)",
            [](
                const MediaToolKind,
                const MediaToolRunOptions&
            )
            {
                MediaToolRunResult result;
                result.process.reason =
                    ProcessExitReason::Cancelled;
                result.process.errorMessage =
                    "simulated cancellation";
                return result;
            }
        );

    if (!Expect(
            cancelled.error == UrlImportError::Cancelled,
            "Cancellation was not propagated."
        ) ||
        !Expect(
            cancelled.stage ==
                UrlImportStage::FetchMetadata,
            "The cancellation stage was incorrect."
        ) ||
        !Expect(
            IsDirectoryEmpty(workspaceBase),
            "Cancellation left its workspace behind."
        ))
    {
        return 1;
    }

    std::filesystem::remove_all(
        testRoot,
        ignoredError
    );

    std::cout << "UrlImportService tests passed.\n";
    return 0;
}
