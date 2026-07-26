#include "import/UrlImportService.hpp"

#include "import/MediaImportWorkspace.hpp"
#include "platform/MediaToolCommandBuilder.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
    UrlImportResult Failure(
        const UrlImportError error,
        const UrlImportStage stage,
        std::string message
    )
    {
        UrlImportResult result;
        result.error = error;
        result.stage = stage;
        result.errorMessage = std::move(message);
        return result;
    }

    wchar_t LowerAscii(const wchar_t character)
    {
        if (character >= L'A' && character <= L'Z')
        {
            return character - L'A' + L'a';
        }

        return character;
    }

    bool HasWavExtension(const std::wstring_view fileName)
    {
        const std::filesystem::path path{
            std::wstring{fileName}
        };

        std::wstring extension = path.extension().wstring();

        for (wchar_t& character : extension)
        {
            character = LowerAscii(character);
        }

        return extension == L".wav";
    }

    bool WriteBinaryText(
        const std::filesystem::path& path,
        const std::string& content
    )
    {
        std::ofstream stream{
            path,
            std::ios::binary | std::ios::trunc
        };

        if (!stream)
        {
            return false;
        }

        stream.write(
            content.data(),
            static_cast<std::streamsize>(content.size())
        );

        return static_cast<bool>(stream);
    }

    std::optional<std::filesystem::path>
    FindDownloadedSource(
        const std::filesystem::path& workspaceRoot,
        std::string& errorMessage
    )
    {
        std::error_code filesystemError;
        std::optional<std::filesystem::path> result;

        std::filesystem::directory_iterator iterator{
            workspaceRoot,
            filesystemError
        };

        const std::filesystem::directory_iterator end;

        while (!filesystemError && iterator != end)
        {
            const std::filesystem::directory_entry& entry =
                *iterator;

            if (entry.is_regular_file(filesystemError))
            {
                const std::filesystem::path path =
                    entry.path();

                std::wstring extension =
                    path.extension().wstring();

                for (wchar_t& character : extension)
                {
                    character = LowerAscii(character);
                }

                if (path.stem() == L"source" &&
                    extension != L".part")
                {
                    if (result.has_value())
                    {
                        errorMessage =
                            "Multiple downloaded source files were found.";
                        return std::nullopt;
                    }

                    result = path.lexically_normal();
                }
            }

            iterator.increment(filesystemError);
        }

        if (filesystemError)
        {
            errorMessage =
                "The downloaded source directory could not be inspected.";
            return std::nullopt;
        }

        if (!result.has_value())
        {
            errorMessage =
                "The downloaded source file was not found.";
            return std::nullopt;
        }

        const std::uintmax_t size =
            std::filesystem::file_size(
                *result,
                filesystemError
            );

        if (filesystemError || size == 0U)
        {
            errorMessage =
                "The downloaded source file is empty or unreadable.";
            return std::nullopt;
        }

        return result;
    }

    bool RunCommand(
        const MediaToolCommandBuildResult& buildResult,
        const UrlImportRequest& request,
        const std::filesystem::path& workingDirectory,
        const std::chrono::milliseconds timeout,
        const UrlImportStage stage,
        const UrlImportToolRunner& runner,
        MediaToolRunResult& runResult,
        UrlImportResult& failureResult
    )
    {
        if (!buildResult.Succeeded())
        {
            failureResult = Failure(
                UrlImportError::CommandBuildFailed,
                stage,
                buildResult.errorMessage
            );

            return false;
        }

        MediaToolRunOptions options;
        options.arguments =
            buildResult.command->arguments;
        options.workingDirectory =
            workingDirectory;
        options.timeout = timeout;
        options.cancellationRequested =
            request.cancellationRequested;
        options.maximumOutputBytes =
            request.maximumOutputBytes;

        runResult = runner(
            buildResult.command->tool,
            options
        );

        if (!runResult.WasLaunched())
        {
            failureResult = Failure(
                UrlImportError::ToolExecutionFailed,
                stage,
                runResult.errorMessage.empty()
                    ? "The media tool was not launched."
                    : runResult.errorMessage
            );

            return false;
        }

        if (runResult.process.reason ==
            ProcessExitReason::Cancelled)
        {
            failureResult = Failure(
                UrlImportError::Cancelled,
                stage,
                "The media import was cancelled."
            );

            return false;
        }

        if (runResult.process.reason ==
            ProcessExitReason::TimedOut)
        {
            failureResult = Failure(
                UrlImportError::TimedOut,
                stage,
                "The media tool timed out."
            );

            return false;
        }

        if (runResult.process.reason !=
                ProcessExitReason::Exited ||
            runResult.process.exitCode != 0U)
        {
            failureResult = Failure(
                UrlImportError::ToolExecutionFailed,
                stage,
                runResult.process.errorMessage.empty()
                    ? "The media tool returned an error."
                    : runResult.process.errorMessage
            );

            return false;
        }

        return true;
    }
}

bool UrlImportResult::Succeeded() const noexcept
{
    return error == UrlImportError::None &&
        !destinationPath.empty();
}

UrlImportResult UrlImportService::Import(
    const UrlImportRequest& request,
    const MediaToolBundleStatus& bundle
)
{
    if (!bundle.IsReady())
    {
        return Failure(
            UrlImportError::ToolUnavailable,
            UrlImportStage::ValidateRequest,
            "The verified media-tool bundle is unavailable."
        );
    }

    const MediaToolStatus* deno =
        bundle.Find("deno");

    if (deno == nullptr ||
        deno->state != MediaToolState::Ready ||
        deno->executablePath.empty())
    {
        return Failure(
            UrlImportError::ToolUnavailable,
            UrlImportStage::ValidateRequest,
            "The verified Deno executable is unavailable."
        );
    }

    const UrlImportToolRunner runner =
        [&bundle](
            const MediaToolKind tool,
            const MediaToolRunOptions& options
        )
        {
            return MediaToolExecutor::Run(
                bundle,
                tool,
                options
            );
        };

    return ImportWithRunner(
        request,
        deno->executablePath,
        runner
    );
}

UrlImportResult UrlImportService::ImportWithRunner(
    const UrlImportRequest& request,
    const std::filesystem::path& denoExecutablePath,
    const UrlImportToolRunner& runner
)
{
    if (!runner ||
        request.url.empty() ||
        request.workspaceBaseDirectory.empty() ||
        request.destinationDirectory.empty() ||
        !MediaImportWorkspace::IsSafeLeafName(
            request.destinationFileName
        ) ||
        !HasWavExtension(request.destinationFileName))
    {
        return Failure(
            UrlImportError::InvalidRequest,
            UrlImportStage::ValidateRequest,
            "The URL import request is invalid."
        );
    }

    std::string workspaceError;

    std::optional<MediaImportWorkspace> workspace =
        MediaImportWorkspace::Create(
            request.workspaceBaseDirectory,
            workspaceError
        );

    if (!workspace.has_value())
    {
        return Failure(
            UrlImportError::WorkspaceCreationFailed,
            UrlImportStage::CreateWorkspace,
            workspaceError
        );
    }

    UrlImportResult failureResult;
    MediaToolRunResult metadataRun;

    const MediaToolCommandBuildResult metadataCommand =
        MediaToolCommandBuilder::FetchMetadata(
            request.url,
            denoExecutablePath
        );

    if (!RunCommand(
            metadataCommand,
            request,
            workspace->RootFolder(),
            request.metadataTimeout,
            UrlImportStage::FetchMetadata,
            runner,
            metadataRun,
            failureResult
        ))
    {
        return failureResult;
    }

    if (metadataRun.process.standardOutput.empty())
    {
        return Failure(
            UrlImportError::ToolOutputInvalid,
            UrlImportStage::FetchMetadata,
            "yt-dlp returned empty metadata."
        );
    }

    if (!WriteBinaryText(
            workspace->MetadataPath(),
            metadataRun.process.standardOutput
        ))
    {
        return Failure(
            UrlImportError::ToolOutputInvalid,
            UrlImportStage::FetchMetadata,
            "The metadata output could not be stored."
        );
    }

    MediaToolRunResult downloadRun;

    const MediaToolCommandBuildResult downloadCommand =
        MediaToolCommandBuilder::DownloadBestAudio(
            request.url,
            workspace->SourceDownloadTemplate(),
            denoExecutablePath
        );

    if (!RunCommand(
            downloadCommand,
            request,
            workspace->RootFolder(),
            request.downloadTimeout,
            UrlImportStage::Download,
            runner,
            downloadRun,
            failureResult
        ))
    {
        return failureResult;
    }

    std::string sourceError;

    const std::optional<std::filesystem::path> sourcePath =
        FindDownloadedSource(
            workspace->RootFolder(),
            sourceError
        );

    if (!sourcePath.has_value())
    {
        return Failure(
            UrlImportError::DownloadedFileInvalid,
            UrlImportStage::Download,
            sourceError
        );
    }

    MediaToolRunResult probeRun;

    const MediaToolCommandBuildResult probeCommand =
        MediaToolCommandBuilder::ProbeAudio(*sourcePath);

    if (!RunCommand(
            probeCommand,
            request,
            workspace->RootFolder(),
            request.probeTimeout,
            UrlImportStage::Probe,
            runner,
            probeRun,
            failureResult
        ))
    {
        return failureResult;
    }

    if (probeRun.process.standardOutput.empty())
    {
        return Failure(
            UrlImportError::ToolOutputInvalid,
            UrlImportStage::Probe,
            "ffprobe returned empty output."
        );
    }

    MediaToolRunResult conversionRun;

    const std::filesystem::path convertedPath =
        workspace->ConvertedWavPath();

    const MediaToolCommandBuildResult conversionCommand =
        MediaToolCommandBuilder::ConvertToPcm16Wav(
            *sourcePath,
            convertedPath,
            request.sampleRate,
            request.channelCount
        );

    if (!RunCommand(
            conversionCommand,
            request,
            workspace->RootFolder(),
            request.conversionTimeout,
            UrlImportStage::Convert,
            runner,
            conversionRun,
            failureResult
        ))
    {
        return failureResult;
    }

    std::error_code filesystemError;

    if (!std::filesystem::is_regular_file(
            convertedPath,
            filesystemError
        ))
    {
        return Failure(
            UrlImportError::ConversionOutputInvalid,
            UrlImportStage::Convert,
            "The converted WAV file was not created."
        );
    }

    const std::uintmax_t convertedSize =
        std::filesystem::file_size(
            convertedPath,
            filesystemError
        );

    if (filesystemError || convertedSize == 0U)
    {
        return Failure(
            UrlImportError::ConversionOutputInvalid,
            UrlImportStage::Convert,
            "The converted WAV file is empty or unreadable."
        );
    }

    const std::filesystem::path destinationDirectory =
        std::filesystem::absolute(
            request.destinationDirectory,
            filesystemError
        ).lexically_normal();

    if (filesystemError)
    {
        return Failure(
            UrlImportError::DestinationError,
            UrlImportStage::Publish,
            "The destination directory path is invalid."
        );
    }

    std::filesystem::create_directories(
        destinationDirectory,
        filesystemError
    );

    if (filesystemError)
    {
        return Failure(
            UrlImportError::DestinationError,
            UrlImportStage::Publish,
            "The destination directory could not be created."
        );
    }

    const std::filesystem::path destinationPath =
        (
            destinationDirectory /
            std::filesystem::path{
                request.destinationFileName
            }
        ).lexically_normal();

    if (destinationPath.parent_path() !=
        destinationDirectory)
    {
        return Failure(
            UrlImportError::DestinationError,
            UrlImportStage::Publish,
            "The destination file escaped its directory."
        );
    }

    if (std::filesystem::exists(
            destinationPath,
            filesystemError
        ))
    {
        return Failure(
            UrlImportError::DestinationError,
            UrlImportStage::Publish,
            "The destination WAV file already exists."
        );
    }

    if (!std::filesystem::copy_file(
            convertedPath,
            destinationPath,
            std::filesystem::copy_options::none,
            filesystemError
        ))
    {
        return Failure(
            UrlImportError::DestinationError,
            UrlImportStage::Publish,
            "The converted WAV file could not be published."
        );
    }

    UrlImportResult result;
    result.destinationPath = destinationPath;
    result.metadataJson =
        std::move(metadataRun.process.standardOutput);
    result.probeJson =
        std::move(probeRun.process.standardOutput);

    return result;
}
