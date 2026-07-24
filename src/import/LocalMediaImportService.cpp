#include "import/LocalMediaImportService.hpp"

#include "import/MediaImportWorkspace.hpp"
#include "platform/MediaToolCommandBuilder.hpp"
#include "sound/SoundFileFormat.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    constexpr std::size_t MaximumImportItems = 4096U;

    struct CandidateScan
    {
        std::vector<std::filesystem::path> directFiles;
        std::vector<std::filesystem::path> convertibleFiles;
        std::vector<std::filesystem::path> failedPaths;
        std::size_t unsupportedCount = 0U;
        bool itemLimitReached = false;
    };

    std::wstring CanonicalKey(const std::filesystem::path& path)
    {
        std::wstring key = path.lexically_normal().wstring();

        std::transform(
            key.begin(),
            key.end(),
            key.begin(),
            [](const wchar_t character)
            {
                return static_cast<wchar_t>(
                    std::towlower(character)
                );
            }
        );

        return key;
    }

    bool CancellationRequested(
        const LocalMediaImportRequest& request
    )
    {
        return request.cancellationRequested != nullptr &&
            request.cancellationRequested->load();
    }

    void StoreFirstError(
        LocalMediaImportResult& result,
        std::string message
    )
    {
        if (result.errorMessage.empty() && !message.empty())
        {
            result.errorMessage = std::move(message);
        }
    }

    CandidateScan ScanCandidates(
        const LocalMediaImportRequest& request
    )
    {
        CandidateScan scan;
        std::set<std::wstring> candidateKeys;
        std::size_t scannedItemCount = 0U;

        const auto addCandidate =
            [&](const std::filesystem::path& candidate)
            {
                if (scannedItemCount >= MaximumImportItems)
                {
                    scan.itemLimitReached = true;
                    return;
                }

                ++scannedItemCount;

                std::error_code candidateError;
                const std::filesystem::path canonical =
                    std::filesystem::weakly_canonical(
                        candidate,
                        candidateError
                    );

                if (candidateError ||
                    !std::filesystem::is_regular_file(
                        canonical,
                        candidateError
                    ) ||
                    candidateError)
                {
                    scan.failedPaths.push_back(candidate);
                    return;
                }

                if (!candidateKeys.insert(
                        CanonicalKey(canonical)
                    ).second)
                {
                    return;
                }

                if (SoundFileFormat::IsSupported(canonical))
                {
                    scan.directFiles.push_back(canonical);
                    return;
                }

                if (LocalMediaImportService::
                        IsConvertibleMediaFile(canonical))
                {
                    scan.convertibleFiles.push_back(canonical);
                    return;
                }

                ++scan.unsupportedCount;
            };

        for (const std::filesystem::path& input : request.inputs)
        {
            if (scan.itemLimitReached ||
                CancellationRequested(request))
            {
                break;
            }

            std::error_code inputError;
            const std::filesystem::path canonical =
                std::filesystem::weakly_canonical(
                    input,
                    inputError
                );

            if (inputError)
            {
                scan.failedPaths.push_back(input);
                continue;
            }

            inputError.clear();

            if (std::filesystem::is_regular_file(
                    canonical,
                    inputError
                ) &&
                !inputError)
            {
                addCandidate(canonical);
                continue;
            }

            inputError.clear();

            if (!std::filesystem::is_directory(
                    canonical,
                    inputError
                ) ||
                inputError)
            {
                scan.failedPaths.push_back(input);
                continue;
            }

            std::filesystem::recursive_directory_iterator iterator{
                canonical,
                std::filesystem::directory_options::
                    skip_permission_denied,
                inputError
            };

            const std::filesystem::recursive_directory_iterator end;

            while (!inputError && iterator != end)
            {
                if (scan.itemLimitReached ||
                    CancellationRequested(request))
                {
                    break;
                }

                std::error_code entryError;

                if (iterator->is_regular_file(entryError) &&
                    !entryError)
                {
                    addCandidate(iterator->path());
                }

                iterator.increment(inputError);
            }

            if (inputError)
            {
                scan.failedPaths.push_back(input);
            }
        }

        const auto sortPaths =
            [](std::vector<std::filesystem::path>& paths)
            {
                std::sort(
                    paths.begin(),
                    paths.end(),
                    [](const std::filesystem::path& left,
                       const std::filesystem::path& right)
                    {
                        return CanonicalKey(left) <
                            CanonicalKey(right);
                    }
                );
            };

        sortPaths(scan.directFiles);
        sortPaths(scan.convertibleFiles);

        return scan;
    }

    bool IsReservedWindowsName(
        const std::wstring_view value
    )
    {
        std::wstring baseName{
            value.substr(0, value.find(L'.'))
        };

        while (!baseName.empty() &&
            (baseName.back() == L' ' ||
             baseName.back() == L'.'))
        {
            baseName.pop_back();
        }

        for (wchar_t& character : baseName)
        {
            if (character >= L'a' && character <= L'z')
            {
                character = character - L'a' + L'A';
            }
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

        return baseName.size() == 4U &&
            (baseName.starts_with(L"COM") ||
             baseName.starts_with(L"LPT")) &&
            baseName[3] >= L'1' &&
            baseName[3] <= L'9';
    }

    std::wstring SafeConvertedLeafName(
        const std::filesystem::path& source
    )
    {
        std::wstring stem = source.stem().wstring();

        constexpr std::wstring_view InvalidCharacters =
            L"<>:\"/\\|?*";

        for (wchar_t& character : stem)
        {
            if (character < 0x20 ||
                character == 0x7f ||
                InvalidCharacters.find(character) !=
                    std::wstring_view::npos)
            {
                character = L'_';
            }
        }

        while (!stem.empty() &&
            (stem.back() == L' ' || stem.back() == L'.'))
        {
            stem.pop_back();
        }

        if (stem.empty() || IsReservedWindowsName(stem))
        {
            stem = L"converted-audio";
        }

        constexpr std::size_t MaximumStemLength = 180U;

        if (stem.size() > MaximumStemLength)
        {
            stem.resize(MaximumStemLength);
        }

        std::wstring result = stem + L".wav";

        if (!MediaImportWorkspace::IsSafeLeafName(result))
        {
            result = L"converted-audio.wav";
        }

        return result;
    }

    std::filesystem::path UniqueDestination(
        const std::filesystem::path& directory,
        const std::filesystem::path& source,
        std::error_code& error
    )
    {
        const std::wstring leafName =
            SafeConvertedLeafName(source);
        const std::filesystem::path leafPath{leafName};
        const std::filesystem::path stem = leafPath.stem();
        const std::filesystem::path extension =
            leafPath.extension();

        std::filesystem::path destination =
            directory / leafPath;

        error.clear();

        if (!std::filesystem::exists(destination, error) &&
            !error)
        {
            return destination;
        }

        for (unsigned int suffix = 2U;
             suffix < 10000U;
             ++suffix)
        {
            destination = directory /
                (
                    stem.wstring() + L"_" +
                    std::to_wstring(suffix) +
                    extension.wstring()
                );

            error.clear();

            if (!std::filesystem::exists(destination, error) &&
                !error)
            {
                return destination;
            }
        }

        error = std::make_error_code(std::errc::file_exists);
        return {};
    }

    bool CopyFileAtomically(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        std::error_code& error
    )
    {
        const auto nonce =
            std::chrono::steady_clock::now().
                time_since_epoch().
                count();

        std::filesystem::path temporary = destination;
        temporary += L".importing-" +
            std::to_wstring(nonce) +
            L".tmp";

        error.clear();

        std::filesystem::copy_file(
            source,
            temporary,
            std::filesystem::copy_options::none,
            error
        );

        if (error)
        {
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            return false;
        }

        error.clear();
        std::filesystem::rename(temporary, destination, error);

        if (error)
        {
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            return false;
        }

        return true;
    }

    bool RunCommand(
        const MediaToolCommandBuildResult& buildResult,
        const LocalMediaImportRequest& request,
        const std::filesystem::path& workingDirectory,
        const std::chrono::milliseconds timeout,
        const LocalMediaImportToolRunner& runner,
        LocalMediaImportResult& result
    )
    {
        if (!buildResult.Succeeded())
        {
            StoreFirstError(result, buildResult.errorMessage);
            return false;
        }

        MediaToolRunOptions options;
        options.arguments = buildResult.command->arguments;
        options.workingDirectory = workingDirectory;
        options.timeout = timeout;
        options.cancellationRequested =
            request.cancellationRequested;
        options.maximumOutputBytes =
            request.maximumOutputBytes;

        const MediaToolRunResult runResult = runner(
            buildResult.command->tool,
            options
        );

        if (!runResult.WasLaunched())
        {
            StoreFirstError(
                result,
                runResult.errorMessage.empty()
                    ? "The media tool was not launched."
                    : runResult.errorMessage
            );

            return false;
        }

        if (runResult.process.reason ==
            ProcessExitReason::Cancelled)
        {
            result.cancelled = true;
            StoreFirstError(
                result,
                "The local media import was cancelled."
            );
            return false;
        }

        if (runResult.process.reason ==
            ProcessExitReason::TimedOut)
        {
            StoreFirstError(
                result,
                "A local media conversion timed out."
            );
            return false;
        }

        if (runResult.process.reason !=
                ProcessExitReason::Exited ||
            runResult.process.exitCode != 0U)
        {
            std::string message =
                runResult.process.errorMessage;

            if (message.empty())
            {
                message = runResult.process.standardError;
            }

            StoreFirstError(
                result,
                message.empty()
                    ? "A media tool returned an error."
                    : std::move(message)
            );

            return false;
        }

        return true;
    }

    void MergeDirectImport(
        LocalMediaImportResult& result,
        SoundImportSummary direct
    )
    {
        result.summary.importedRelativePaths.insert(
            result.summary.importedRelativePaths.end(),
            std::make_move_iterator(
                direct.importedRelativePaths.begin()
            ),
            std::make_move_iterator(
                direct.importedRelativePaths.end()
            )
        );

        result.summary.failedPaths.insert(
            result.summary.failedPaths.end(),
            std::make_move_iterator(direct.failedPaths.begin()),
            std::make_move_iterator(direct.failedPaths.end())
        );

        result.summary.copiedCount += direct.copiedCount;
        result.summary.existingCount += direct.existingCount;
        result.summary.unsupportedCount +=
            direct.unsupportedCount;
        result.summary.itemLimitReached =
            result.summary.itemLimitReached ||
            direct.itemLimitReached;
    }
}

bool LocalMediaImportResult::ImportedAnything() const noexcept
{
    return !summary.importedRelativePaths.empty();
}

bool LocalMediaImportService::IsConvertibleMediaFile(
    const std::filesystem::path& path
)
{
    std::wstring extension = path.extension().wstring();

    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](const wchar_t character)
        {
            return static_cast<wchar_t>(
                std::towlower(character)
            );
        }
    );

    constexpr std::array<std::wstring_view, 20>
        ConvertibleExtensions{
            L".aac",
            L".ac3",
            L".aif",
            L".aiff",
            L".alac",
            L".ape",
            L".caf",
            L".m4a",
            L".mka",
            L".mkv",
            L".mov",
            L".mp4",
            L".oga",
            L".ogg",
            L".opus",
            L".webm",
            L".wma",
            L".flv",
            L".mpeg",
            L".mpg"
        };

    return std::find(
        ConvertibleExtensions.begin(),
        ConvertibleExtensions.end(),
        extension
    ) != ConvertibleExtensions.end();
}

LocalMediaImportResult LocalMediaImportService::Import(
    const LocalMediaImportRequest& request,
    const std::optional<MediaToolBundleStatus>& bundle
)
{
    const bool conversionAvailable =
        bundle.has_value() && bundle->IsReady();

    LocalMediaImportToolRunner runner;

    if (conversionAvailable)
    {
        runner = [&bundle](
            const MediaToolKind tool,
            const MediaToolRunOptions& options
        )
        {
            return MediaToolExecutor::Run(
                *bundle,
                tool,
                options
            );
        };
    }

    return ImportWithRunner(
        request,
        conversionAvailable,
        runner
    );
}

LocalMediaImportResult
LocalMediaImportService::ImportWithRunner(
    const LocalMediaImportRequest& request,
    const bool conversionAvailable,
    const LocalMediaImportToolRunner& runner
)
{
    LocalMediaImportResult result;

    if (request.inputs.empty() ||
        request.soundsFolder.empty() ||
        request.workspaceBaseDirectory.empty() ||
        request.sampleRate < 8000U ||
        request.sampleRate > 192000U ||
        (request.channelCount != 1U &&
         request.channelCount != 2U))
    {
        result.summary.failedPaths = request.inputs;
        result.errorMessage =
            "The local media import request is invalid.";
        return result;
    }

    const CandidateScan scan = ScanCandidates(request);

    result.summary.failedPaths = scan.failedPaths;
    result.summary.unsupportedCount = scan.unsupportedCount;
    result.summary.itemLimitReached = scan.itemLimitReached;

    if (CancellationRequested(request))
    {
        result.cancelled = true;
        return result;
    }

    if (!scan.directFiles.empty())
    {
        MergeDirectImport(
            result,
            SoundImporter{request.soundsFolder}.Import(
                scan.directFiles
            )
        );
    }

    if (CancellationRequested(request))
    {
        result.cancelled = true;
        return result;
    }

    if (scan.convertibleFiles.empty())
    {
        return result;
    }

    if (!conversionAvailable || !runner)
    {
        result.conversionToolsUnavailable = true;
        result.summary.failedPaths.insert(
            result.summary.failedPaths.end(),
            scan.convertibleFiles.begin(),
            scan.convertibleFiles.end()
        );
        StoreFirstError(
            result,
            "Verified FFmpeg and ffprobe tools are unavailable."
        );
        return result;
    }

    std::error_code filesystemError;
    const std::filesystem::path importedFolder =
        request.soundsFolder / L"Imported";

    std::filesystem::create_directories(
        importedFolder,
        filesystemError
    );

    if (filesystemError)
    {
        result.summary.failedPaths.insert(
            result.summary.failedPaths.end(),
            scan.convertibleFiles.begin(),
            scan.convertibleFiles.end()
        );
        result.errorMessage =
            "The Imported sound directory could not be created.";
        return result;
    }

    for (std::size_t index = 0U;
         index < scan.convertibleFiles.size();
         ++index)
    {
        const std::filesystem::path& source =
            scan.convertibleFiles[index];

        if (CancellationRequested(request))
        {
            result.cancelled = true;

            result.summary.failedPaths.insert(
                result.summary.failedPaths.end(),
                scan.convertibleFiles.begin() +
                    static_cast<std::ptrdiff_t>(index),
                scan.convertibleFiles.end()
            );

            break;
        }

        std::string workspaceError;
        std::optional<MediaImportWorkspace> workspace =
            MediaImportWorkspace::Create(
                request.workspaceBaseDirectory,
                workspaceError
            );

        if (!workspace.has_value())
        {
            result.summary.failedPaths.push_back(source);
            StoreFirstError(result, std::move(workspaceError));
            continue;
        }

        const MediaToolCommandBuildResult probeCommand =
            MediaToolCommandBuilder::ProbeAudio(source);

        if (!RunCommand(
                probeCommand,
                request,
                workspace->RootFolder(),
                request.probeTimeout,
                runner,
                result
            ))
        {
            result.summary.failedPaths.push_back(source);

            if (result.cancelled)
            {
                result.summary.failedPaths.insert(
                    result.summary.failedPaths.end(),
                    scan.convertibleFiles.begin() +
                        static_cast<std::ptrdiff_t>(index + 1U),
                    scan.convertibleFiles.end()
                );
                break;
            }

            continue;
        }

        const std::filesystem::path convertedPath =
            workspace->ConvertedWavPath();

        const MediaToolCommandBuildResult conversionCommand =
            MediaToolCommandBuilder::ConvertToPcm16Wav(
                source,
                convertedPath,
                request.sampleRate,
                request.channelCount
            );

        if (!RunCommand(
                conversionCommand,
                request,
                workspace->RootFolder(),
                request.conversionTimeout,
                runner,
                result
            ))
        {
            result.summary.failedPaths.push_back(source);

            if (result.cancelled)
            {
                result.summary.failedPaths.insert(
                    result.summary.failedPaths.end(),
                    scan.convertibleFiles.begin() +
                        static_cast<std::ptrdiff_t>(index + 1U),
                    scan.convertibleFiles.end()
                );
                break;
            }

            continue;
        }

        filesystemError.clear();

        if (!std::filesystem::is_regular_file(
                convertedPath,
                filesystemError
            ) ||
            filesystemError)
        {
            result.summary.failedPaths.push_back(source);
            StoreFirstError(
                result,
                "FFmpeg did not create a converted WAV file."
            );
            continue;
        }

        const std::uintmax_t convertedSize =
            std::filesystem::file_size(
                convertedPath,
                filesystemError
            );

        if (filesystemError || convertedSize == 0U)
        {
            result.summary.failedPaths.push_back(source);
            StoreFirstError(
                result,
                "The converted WAV file is empty or unreadable."
            );
            continue;
        }

        filesystemError.clear();
        const std::filesystem::path destination =
            UniqueDestination(
                importedFolder,
                source,
                filesystemError
            );

        if (filesystemError || destination.empty() ||
            !CopyFileAtomically(
                convertedPath,
                destination,
                filesystemError
            ))
        {
            result.summary.failedPaths.push_back(source);
            StoreFirstError(
                result,
                "The converted WAV file could not be published."
            );
            continue;
        }

        result.summary.importedRelativePaths.push_back(
            std::filesystem::path{L"Imported"} /
                destination.filename()
        );
        ++result.convertedCount;
    }

    return result;
}
