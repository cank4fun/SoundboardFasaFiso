#include "gui/ControlWindow.hpp"

#include "import/LocalMediaImportService.hpp"
#include "localization/Localization.hpp"

#include <iostream>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>

void ControlWindow::ImportSoundItems(
    const std::vector<std::filesystem::path>& selectedPaths
)
{
    if (selectedPaths.empty() || window_ == nullptr)
    {
        return;
    }

    if (localMediaImportRunning_.load())
    {
        SetStatus(Localization::Text(
            L"Yerel medya içe aktarma zaten çalışıyor...",
            L"A local media import is already running..."
        ));
        return;
    }

    if (urlImportRunning_.load())
    {
        SetStatus(Localization::Text(
            L"URL içe aktarma tamamlanmadan yerel medya eklenemez.",
            L"Local media cannot be added until the URL import finishes."
        ));
        return;
    }

    if (localMediaImportThread_.joinable())
    {
        localMediaImportThread_.join();
    }

    {
        const std::scoped_lock lock{localMediaImportMutex_};
        pendingLocalMediaImportResult_.reset();
    }

    LocalMediaImportRequest request;
    request.inputs = selectedPaths;
    request.soundsFolder = soundsFolder_;
    request.workspaceBaseDirectory =
        soundsFolder_.parent_path() /
        L"temp" /
        L"local-import";

    const unsigned int configuredSampleRate =
        currentConfig_.GetAudioSampleRate();

    request.sampleRate =
        configuredSampleRate >= 8000U &&
        configuredSampleRate <= 192000U
            ? static_cast<std::uint32_t>(configuredSampleRate)
            : 48000U;

    request.channelCount = 2U;
    request.cancellationRequested =
        &localMediaImportCancellationRequested_;

    const std::optional<MediaToolBundleStatus> bundle =
        mediaToolBundle_;
    const HWND targetWindow = window_;

    localMediaImportCancellationRequested_.store(false);
    localMediaImportRunning_.store(true);
    UpdateLocalMediaImportControls();

    SetStatus(Localization::Text(
        L"Sesler içe aktarılıyor ve gerekirse WAV'a dönüştürülüyor...",
        L"Importing sounds and converting to WAV when needed..."
    ));

    try
    {
        localMediaImportThread_ = std::jthread(
            [
                this,
                targetWindow,
                request = std::move(request),
                bundle
            ]() mutable
            {
                LocalMediaImportResult result =
                    LocalMediaImportService::Import(
                        request,
                        bundle
                    );

                {
                    const std::scoped_lock lock{
                        localMediaImportMutex_
                    };
                    pendingLocalMediaImportResult_ =
                        std::move(result);
                }

                if (PostMessageW(
                        targetWindow,
                        LocalMediaImportCompletedMessage,
                        0,
                        0
                    ) == FALSE)
                {
                    const std::scoped_lock lock{
                        localMediaImportMutex_
                    };
                    pendingLocalMediaImportResult_.reset();
                    localMediaImportRunning_.store(false);
                    localMediaImportCancellationRequested_.store(false);
                }
            }
        );
    }
    catch (const std::system_error& error)
    {
        localMediaImportRunning_.store(false);
        localMediaImportCancellationRequested_.store(false);
        UpdateLocalMediaImportControls();

        std::cerr
            << "Local media import worker could not start: "
            << error.what()
            << '\n';

        SetStatus(Localization::Text(
            L"Yerel medya içe aktarma başlatılamadı.",
            L"Local media import could not be started."
        ));

        MessageBoxW(
            window_,
            Localization::Text(
                L"Yerel medya içe aktarma iş parçacığı başlatılamadı.",
                L"The local media import worker thread could not be started."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONERROR
        );
    }
}

void ControlWindow::HandleLocalMediaImportCompleted()
{
    std::optional<LocalMediaImportResult> result;

    {
        const std::scoped_lock lock{localMediaImportMutex_};
        result = std::move(pendingLocalMediaImportResult_);
        pendingLocalMediaImportResult_.reset();
    }

    if (localMediaImportThread_.joinable())
    {
        localMediaImportThread_.join();
    }

    localMediaImportRunning_.store(false);
    localMediaImportCancellationRequested_.store(false);
    UpdateLocalMediaImportControls();

    if (result.has_value())
    {
        PresentLocalMediaImportResult(*result);
    }
}

void ControlWindow::UpdateLocalMediaImportControls()
{
    UpdateUrlImportControls();
}

void ControlWindow::PresentLocalMediaImportResult(
    const LocalMediaImportResult& result
)
{
    const SoundImportSummary& summary = result.summary;

    if (!summary.importedRelativePaths.empty())
    {
        SetControlText(
            bindingFileEdit_,
            summary.importedRelativePaths.front().wstring()
        );
    }

    std::wostringstream status;
    status << Localization::Text(
        L"İçe aktarılan ses: ",
        L"Imported sounds: "
    ) << summary.importedRelativePaths.size();

    if (summary.copiedCount != 0U)
    {
        status << Localization::Text(
            L" · Kopyalanan: ",
            L" · Copied: "
        ) << summary.copiedCount;
    }

    if (result.convertedCount != 0U)
    {
        status << Localization::Text(
            L" · WAV'a dönüştürülen: ",
            L" · Converted to WAV: "
        ) << result.convertedCount;
    }

    if (summary.existingCount != 0U)
    {
        status << Localization::Text(
            L" · Zaten içeride: ",
            L" · Existing: "
        ) << summary.existingCount;
    }

    if (summary.unsupportedCount != 0U)
    {
        status << Localization::Text(
            L" · Desteklenmeyen: ",
            L" · Unsupported: "
        ) << summary.unsupportedCount;
    }

    if (!summary.failedPaths.empty())
    {
        status << Localization::Text(
            L" · Başarısız: ",
            L" · Failed: "
        ) << summary.failedPaths.size();
    }

    if (summary.itemLimitReached)
    {
        status << Localization::Text(
            L" · 4096 dosya sınırına ulaşıldı",
            L" · 4096-file limit reached"
        );
    }

    if (result.cancelled)
    {
        status << Localization::Text(
            L" · İptal edildi",
            L" · Cancelled"
        );
    }

    SetStatus(status.str());

    const bool showDetails =
        summary.importedRelativePaths.empty() ||
        summary.unsupportedCount != 0U ||
        !summary.failedPaths.empty() ||
        summary.itemLimitReached ||
        result.cancelled;

    if (!showDetails)
    {
        return;
    }

    std::wostringstream details;
    details << status.str();

    if (result.conversionToolsUnavailable)
    {
        details << L"\n\n" << Localization::Text(
            L"WAV, MP3 ve FLAC doğrudan içe aktarılabilir. Diğer medya biçimleri için doğrulanmış FFmpeg ve ffprobe araçları gerekiyor.",
            L"WAV, MP3, and FLAC can be imported directly. Other media formats require verified FFmpeg and ffprobe tools."
        );
    }
    else if (!result.errorMessage.empty())
    {
        details << L"\n\n" << Utf8ToWide(
            result.errorMessage
        );
    }
    else if (summary.unsupportedCount != 0U)
    {
        details << L"\n\n" << Localization::Text(
            L"Bilinmeyen dosya türleri atlandı.",
            L"Unknown file types were skipped."
        );
    }

    MessageBoxW(
        window_,
        details.str().c_str(),
        L"SoundBoardFasaFiso",
        MB_OK | (summary.importedRelativePaths.empty()
            ? MB_ICONWARNING
            : MB_ICONINFORMATION)
    );
}
