#include "gui/ControlWindow.hpp"

#include "import/UrlImportService.hpp"
#include "localization/Localization.hpp"

#include <cwctype>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>

namespace
{
    std::wstring TrimUrlText(std::wstring value)
    {
        while (!value.empty() &&
            std::iswspace(
                static_cast<wint_t>(value.front())
            ) != 0)
        {
            value.erase(value.begin());
        }

        while (!value.empty() &&
            std::iswspace(
                static_cast<wint_t>(value.back())
            ) != 0)
        {
            value.pop_back();
        }

        return value;
    }
}

void ControlWindow::ToggleUrlImport()
{
    if (window_ == nullptr)
    {
        return;
    }

    if (urlImportRunning_.load())
    {
        if (!urlImportCancellationRequested_.exchange(true))
        {
            SetStatus(Localization::Text(
                L"URL i\u00e7e aktarma iptal ediliyor...",
                L"Cancelling URL import..."
            ));
        }

        UpdateUrlImportControls();
        return;
    }

    if (!mediaToolBundle_.has_value() ||
        !mediaToolBundle_->IsReady())
    {
        SetStatus(Localization::Text(
            L"URL i\u00e7e aktarma kullan\u0131lam\u0131yor.",
            L"URL import is unavailable."
        ));

        MessageBoxW(
            window_,
            Localization::Text(
                L"Do\u011frulanm\u0131\u015f yt-dlp, Deno ve FFmpeg ara\u00e7lar\u0131 bulunamad\u0131. Portable paketi veya kullan\u0131c\u0131 ara\u00e7 klas\u00f6r\u00fcn\u00fc kontrol et.",
                L"Verified yt-dlp, Deno, and FFmpeg tools were not found. Check the portable package or the user tools folder."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );

        return;
    }

    const std::wstring url = TrimUrlText(
        GetControlText(bindingFileEdit_)
    );

    if (url.empty())
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Ses alan\u0131na http:// veya https:// ile ba\u015flayan bir URL yap\u0131\u015ft\u0131r.",
                L"Paste an http:// or https:// URL into the Sound field."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONINFORMATION
        );

        SetFocus(bindingFileEdit_);
        return;
    }

    if (urlImportThread_.joinable())
    {
        urlImportThread_.join();
    }

    {
        const std::scoped_lock lock{urlImportMutex_};
        pendingUrlImportResult_.reset();
    }

    UrlImportRequest request;
    request.url = url;
    request.workspaceBaseDirectory =
        soundsFolder_.parent_path() /
        L"temp" /
        L"url-import";
    request.destinationDirectory = soundsFolder_;
    request.destinationFileName =
        L"url-import-" +
        std::to_wstring(
            static_cast<unsigned long long>(
                GetCurrentProcessId()
            )
        ) +
        L"-" +
        std::to_wstring(
            static_cast<unsigned long long>(
                GetTickCount64()
            )
        ) +
        L".wav";

    const unsigned int configuredSampleRate =
        currentConfig_.GetAudioSampleRate();

    request.sampleRate =
        configuredSampleRate >= 8000U &&
        configuredSampleRate <= 192000U
            ? static_cast<std::uint32_t>(
                configuredSampleRate
            )
            : 48000U;

    request.channelCount = 2U;
    request.cancellationRequested =
        &urlImportCancellationRequested_;

    MediaToolBundleStatus bundle =
        *mediaToolBundle_;

    const HWND targetWindow = window_;

    urlImportCancellationRequested_.store(false);
    urlImportRunning_.store(true);

    UpdateUrlImportControls();

    SetStatus(Localization::Text(
        L"URL indiriliyor ve WAV'a d\u00f6n\u00fc\u015ft\u00fcr\u00fcl\u00fcyor...",
        L"Downloading URL and converting it to WAV..."
    ));

    try
    {
        urlImportThread_ = std::jthread(
            [
                this,
                targetWindow,
                request = std::move(request),
                bundle = std::move(bundle)
            ]() mutable
            {
                UrlImportResult result =
                    UrlImportService::Import(
                        request,
                        bundle
                    );

                {
                    const std::scoped_lock lock{
                        urlImportMutex_
                    };

                    pendingUrlImportResult_ =
                        std::move(result);
                }

                if (PostMessageW(
                        targetWindow,
                        UrlImportCompletedMessage,
                        0,
                        0
                    ) == FALSE)
                {
                    const std::scoped_lock lock{
                        urlImportMutex_
                    };

                    pendingUrlImportResult_.reset();
                    urlImportRunning_.store(false);
                    urlImportCancellationRequested_.
                        store(false);
                }
            }
        );
    }
    catch (const std::system_error& error)
    {
        urlImportRunning_.store(false);
        urlImportCancellationRequested_.store(false);
        UpdateUrlImportControls();

        std::cerr
            << "URL import worker could not start: "
            << error.what()
            << '\n';

        SetStatus(Localization::Text(
            L"URL i\u00e7e aktarma ba\u015flat\u0131lamad\u0131.",
            L"URL import could not be started."
        ));

        MessageBoxW(
            window_,
            Localization::Text(
                L"URL i\u00e7e aktarma i\u015f par\u00e7ac\u0131\u011f\u0131 ba\u015flat\u0131lamad\u0131.",
                L"The URL import worker thread could not be started."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONERROR
        );
    }
}

void ControlWindow::HandleUrlImportCompleted()
{
    std::optional<UrlImportResult> result;

    {
        const std::scoped_lock lock{urlImportMutex_};
        result = std::move(pendingUrlImportResult_);
        pendingUrlImportResult_.reset();
    }

    if (urlImportThread_.joinable())
    {
        urlImportThread_.join();
    }

    urlImportRunning_.store(false);
    urlImportCancellationRequested_.store(false);
    UpdateUrlImportControls();

    if (!result.has_value())
    {
        return;
    }

    if (result->Succeeded())
    {
        const std::wstring fileName =
            result->destinationPath.
                filename().
                wstring();

        SetControlText(
            bindingFileEdit_,
            fileName
        );

        SetStatus(
            std::wstring{
                Localization::Text(
                    L"URL i\u00e7e aktar\u0131ld\u0131: ",
                    L"URL imported: "
                )
            } +
            fileName
        );

        SetFocus(bindingHotkeyEdit_);

        std::cout
            << "URL import completed: "
            << WideToUtf8(fileName)
            << '\n';

        return;
    }

    if (result->error == UrlImportError::Cancelled)
    {
        SetStatus(Localization::Text(
            L"URL i\u00e7e aktarma iptal edildi.",
            L"URL import cancelled."
        ));

        return;
    }

    std::cerr
        << "URL import failed at stage "
        << static_cast<int>(result->stage)
        << ": "
        << result->errorMessage
        << '\n';

    SetStatus(Localization::Text(
        L"URL i\u00e7e aktarma ba\u015far\u0131s\u0131z.",
        L"URL import failed."
    ));

    std::wstring details{
        Localization::Text(
            L"URL indirilemedi veya WAV'a d\u00f6n\u00fc\u015ft\u00fcr\u00fclemedi.",
            L"The URL could not be downloaded or converted to WAV."
        )
    };

    if (!result->errorMessage.empty())
    {
        details += L"\n\n";
        details += Utf8ToWide(
            result->errorMessage
        );
    }

    MessageBoxW(
        window_,
        details.c_str(),
        L"SoundBoardFasaFiso",
        MB_OK | MB_ICONERROR
    );
}

void ControlWindow::UpdateUrlImportControls()
{
    if (importUrlButton_ == nullptr)
    {
        return;
    }

    const bool running =
        urlImportRunning_.load();

    SetControlText(
        importUrlButton_,
        running
            ? Localization::Text(
                L"\u0130ptal",
                L"Cancel"
            )
            : L"URL"
    );

    if (browseSoundButton_ != nullptr)
    {
        EnableWindow(
            browseSoundButton_,
            running ? FALSE : TRUE
        );
    }

    if (bindingFileEdit_ != nullptr)
    {
        EnableWindow(
            bindingFileEdit_,
            running ? FALSE : TRUE
        );
    }

    RedrawWindow(
        importUrlButton_,
        nullptr,
        nullptr,
        RDW_INVALIDATE |
            RDW_ERASE |
            RDW_UPDATENOW
    );
}
