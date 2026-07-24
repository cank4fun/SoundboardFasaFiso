#include "import/LocalMediaImportService.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
                std::chrono::steady_clock::now().
                    time_since_epoch().
                    count();

            path_ = std::filesystem::temp_directory_path() /
                (
                    "SoundBoardFasaFiso-local-media-tests-" +
                    std::to_string(timestamp)
                );

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
        const std::string_view contents = "fixture"
    )
    {
        std::filesystem::create_directories(
            path.parent_path()
        );

        std::ofstream file{
            path,
            std::ios::binary | std::ios::trunc
        };

        file.write(
            contents.data(),
            static_cast<std::streamsize>(contents.size())
        );

        return static_cast<bool>(file);
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

    LocalMediaImportRequest RequestFor(
        const TemporaryDirectory& directory,
        std::vector<std::filesystem::path> inputs,
        const std::wstring_view name
    )
    {
        LocalMediaImportRequest request;
        request.inputs = std::move(inputs);
        request.soundsFolder =
            directory.Path() /
            std::filesystem::path{name} /
            L"sounds";
        request.workspaceBaseDirectory =
            directory.Path() /
            std::filesystem::path{name} /
            L"temp";
        request.sampleRate = 48000U;
        request.channelCount = 2U;
        return request;
    }

    void TestDirectAndConvertibleFiles(
        const TemporaryDirectory& directory
    )
    {
        const std::filesystem::path inputFolder =
            directory.Path() / L"mixed-input";
        const std::filesystem::path direct =
            inputFolder / L"direct.wav";
        const std::filesystem::path convertible =
            inputFolder / L"music.m4a";
        const std::filesystem::path unsupported =
            inputFolder / L"notes.txt";

        WriteFile(direct, "direct-wave");
        WriteFile(convertible, "fake-m4a");
        WriteFile(unsupported, "notes");

        LocalMediaImportRequest request = RequestFor(
            directory,
            {inputFolder},
            L"mixed"
        );

        std::size_t callIndex = 0U;

        const LocalMediaImportToolRunner runner =
            [&callIndex](
                const MediaToolKind tool,
                const MediaToolRunOptions& options
            ) -> MediaToolRunResult
            {
                const std::size_t current = callIndex++;

                if (current == 0U)
                {
                    if (tool != MediaToolKind::Ffprobe)
                    {
                        return Exited(91U);
                    }

                    return Exited(
                        0U,
                        R"({"streams":[{"codec_name":"aac"}]})"
                    );
                }

                if (current == 1U)
                {
                    if (tool != MediaToolKind::Ffmpeg ||
                        options.arguments.empty())
                    {
                        return Exited(92U);
                    }

                    const std::filesystem::path output{
                        options.arguments.back()
                    };

                    if (!WriteFile(output, "RIFF-local-wave"))
                    {
                        return Exited(93U);
                    }

                    return Exited(0U);
                }

                return Exited(94U);
            };

        const LocalMediaImportResult result =
            LocalMediaImportService::ImportWithRunner(
                request,
                true,
                runner
            );

        Expect(callIndex == 2U, "probe and conversion both run");
        Expect(result.summary.copiedCount == 1U,
            "direct audio is copied through the fast path");
        Expect(result.convertedCount == 1U,
            "convertible audio is converted");
        Expect(result.summary.unsupportedCount == 1U,
            "unknown files remain unsupported");
        Expect(result.summary.failedPaths.empty(),
            "successful mixed import has no failed paths");
        Expect(result.summary.importedRelativePaths.size() == 2U,
            "mixed import returns two sound paths");
        Expect(
            std::filesystem::is_regular_file(
                request.soundsFolder /
                L"Imported" /
                L"direct.wav"
            ),
            "direct audio is published"
        );
        Expect(
            std::filesystem::is_regular_file(
                request.soundsFolder /
                L"Imported" /
                L"music.wav"
            ),
            "converted audio uses the source stem"
        );
        Expect(
            std::filesystem::is_empty(
                request.workspaceBaseDirectory
            ),
            "temporary conversion workspaces are cleaned"
        );
    }

    void TestConversionNameCollisions(
        const TemporaryDirectory& directory
    )
    {
        const std::filesystem::path first =
            directory.Path() / L"collision-one" / L"same.ogg";
        const std::filesystem::path second =
            directory.Path() / L"collision-two" / L"same.webm";

        WriteFile(first, "first");
        WriteFile(second, "second");

        LocalMediaImportRequest request = RequestFor(
            directory,
            {first, second},
            L"collision"
        );

        const LocalMediaImportToolRunner runner =
            [](
                const MediaToolKind tool,
                const MediaToolRunOptions& options
            ) -> MediaToolRunResult
            {
                if (tool == MediaToolKind::Ffprobe)
                {
                    return Exited(0U, R"({"streams":[{}]})");
                }

                if (tool == MediaToolKind::Ffmpeg &&
                    !options.arguments.empty() &&
                    WriteFile(
                        std::filesystem::path{
                            options.arguments.back()
                        },
                        "RIFF-collision"
                    ))
                {
                    return Exited(0U);
                }

                return Exited(95U);
            };

        const LocalMediaImportResult result =
            LocalMediaImportService::ImportWithRunner(
                request,
                true,
                runner
            );

        Expect(result.convertedCount == 2U,
            "both colliding sources are converted");
        Expect(
            std::filesystem::is_regular_file(
                request.soundsFolder / L"Imported" / L"same.wav"
            ),
            "first converted name is preserved"
        );
        Expect(
            std::filesystem::is_regular_file(
                request.soundsFolder / L"Imported" / L"same_2.wav"
            ),
            "second converted name receives a suffix"
        );
    }

    void TestUnavailableToolsStillImportDirectAudio(
        const TemporaryDirectory& directory
    )
    {
        const std::filesystem::path direct =
            directory.Path() / L"unavailable" / L"direct.mp3";
        const std::filesystem::path convertible =
            directory.Path() / L"unavailable" / L"convert.aac";

        WriteFile(direct, "direct");
        WriteFile(convertible, "convertible");

        LocalMediaImportRequest request = RequestFor(
            directory,
            {direct, convertible},
            L"unavailable-result"
        );

        std::size_t callCount = 0U;

        const LocalMediaImportResult result =
            LocalMediaImportService::ImportWithRunner(
                request,
                false,
                [&callCount](
                    const MediaToolKind,
                    const MediaToolRunOptions&
                )
                {
                    ++callCount;
                    return Exited(0U);
                }
            );

        Expect(callCount == 0U,
            "tools do not run when conversion is unavailable");
        Expect(result.summary.copiedCount == 1U,
            "direct audio still imports without tools");
        Expect(result.convertedCount == 0U,
            "no conversion is reported without tools");
        Expect(result.conversionToolsUnavailable,
            "tool unavailability is reported");
        Expect(result.summary.failedPaths.size() == 1U,
            "convertible source is reported failed without tools");
    }

    void TestFailedConversionDoesNotPublishOutput(
        const TemporaryDirectory& directory
    )
    {
        const std::filesystem::path source =
            directory.Path() / L"failure" / L"broken.webm";
        WriteFile(source, "broken");

        LocalMediaImportRequest request = RequestFor(
            directory,
            {source},
            L"failure-result"
        );

        std::size_t callIndex = 0U;

        const LocalMediaImportResult result =
            LocalMediaImportService::ImportWithRunner(
                request,
                true,
                [&callIndex](
                    const MediaToolKind,
                    const MediaToolRunOptions&
                )
                {
                    if (callIndex++ == 0U)
                    {
                        return Exited(0U, R"({"streams":[{}]})");
                    }

                    return Exited(
                        7U,
                        {},
                        "simulated conversion failure"
                    );
                }
            );

        Expect(result.convertedCount == 0U,
            "failed conversion is not counted");
        Expect(result.summary.failedPaths.size() == 1U,
            "failed conversion reports its source");
        Expect(
            !std::filesystem::exists(
                request.soundsFolder /
                L"Imported" /
                L"broken.wav"
            ),
            "failed conversion does not publish a WAV"
        );
        Expect(
            result.errorMessage.find("simulated") !=
                std::string::npos,
            "conversion error detail is preserved"
        );
    }

    void TestCancellationStopsBeforeToolsRun(
        const TemporaryDirectory& directory
    )
    {
        const std::filesystem::path source =
            directory.Path() / L"cancelled" / L"clip.m4a";
        WriteFile(source, "cancelled");

        std::atomic_bool cancellationRequested{true};
        LocalMediaImportRequest request = RequestFor(
            directory,
            {source},
            L"cancelled-result"
        );
        request.cancellationRequested = &cancellationRequested;

        std::size_t callCount = 0U;

        const LocalMediaImportResult result =
            LocalMediaImportService::ImportWithRunner(
                request,
                true,
                [&callCount](
                    const MediaToolKind,
                    const MediaToolRunOptions&
                )
                {
                    ++callCount;
                    return Exited(0U);
                }
            );

        Expect(result.cancelled,
            "pre-requested cancellation is reported");
        Expect(callCount == 0U,
            "cancelled import does not launch tools");
        Expect(!result.ImportedAnything(),
            "cancelled import does not publish sounds");
    }

    void TestConvertibleExtensionDetection()
    {
        Expect(
            LocalMediaImportService::IsConvertibleMediaFile(
                L"clip.WEBM"
            ),
            "WebM extension is case insensitive"
        );
        Expect(
            LocalMediaImportService::IsConvertibleMediaFile(
                L"clip.m4a"
            ),
            "M4A is convertible"
        );
        Expect(
            !LocalMediaImportService::IsConvertibleMediaFile(
                L"clip.txt"
            ),
            "text files are not convertible"
        );
    }
}

int main()
{
    const TemporaryDirectory directory;

    TestDirectAndConvertibleFiles(directory);
    TestConversionNameCollisions(directory);
    TestUnavailableToolsStillImportDirectAudio(directory);
    TestFailedConversionDoesNotPublishOutput(directory);
    TestCancellationStopsBeforeToolsRun(directory);
    TestConvertibleExtensionDetection();

    if (failureCount != 0)
    {
        std::cerr
            << failureCount
            << " test assertion(s) failed.\n";
        return 1;
    }

    std::cout << "Local media import service tests passed.\n";
    return 0;
}
